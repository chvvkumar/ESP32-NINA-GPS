#include <Arduino.h>
#include <AsyncTCP.h>
#include <vector>
#include "TcpServer.h"
#include "Config.h"
#include "Context.h"
#include "WebServer.h"

AsyncServer tcpServer(TCP_PORT);

struct ClientContext {
  AsyncClient* client;
  bool isGpsd = false; 
};
std::vector<ClientContext> clients;
SemaphoreHandle_t clientsMutex = NULL;
volatile bool newClientConnected = false;
volatile bool pendingBroadcast = false;

// --- Helper Functions Local to this file ---
String getChecksum(String content) {
  int xorResult = 0;
  for (int i = 0; i < content.length(); i++) {
    xorResult ^= content.charAt(i);
  }
  char hex[3];
  sprintf(hex, "%02X", xorResult);
  return String(hex);
}

String toNMEA(double deg, bool isLon) {
  int d = (int)abs(deg);
  double m = (abs(deg) - d) * 60.0;
  char buf[20];
  if (isLon) sprintf(buf, "%03d%07.4f", d, m);
  else sprintf(buf, "%02d%07.4f", d, m); 
  return String(buf);
}

String generateTPV() {
  if (!gpsData.hasFix) return "";

  int mode = 1; // Default No Fix
  if (gpsData.fixType == 2) mode = 2; // 2D
  if (gpsData.fixType == 3) mode = 3; // 3D

  String tpv = "{\"class\":\"TPV\",\"device\":\"/dev/i2c\"";
  tpv += ",\"status\":1"; 
  tpv += ",\"mode\":" + String(mode);
  tpv += ",\"time\":\"" + gpsData.dateStr + "T" + gpsData.timeStr + "Z\"";
  tpv += ",\"lat\":" + String(gpsData.lat, 7);
  tpv += ",\"lon\":" + String(gpsData.lon, 7);
  tpv += ",\"alt\":" + String(gpsData.alt, 3);
  tpv += ",\"altHAE\":" + String(gpsData.alt, 3);
  tpv += ",\"altMSL\":" + String(gpsData.altMSL, 3);
  tpv += ",\"speed\":" + String(gpsData.speed, 3);
  tpv += ",\"track\":" + String(gpsData.heading, 2);
  tpv += ",\"epx\":" + String(gpsData.hAcc, 2);
  tpv += ",\"epy\":" + String(gpsData.hAcc, 2);
  tpv += ",\"epv\":" + String(gpsData.vAcc, 2); 
  tpv += "}\n";
  return tpv;
}

static void handleClientData(void* arg, AsyncClient* client, void* data, size_t len) {
  String cmd = String((char*)data).substring(0, len);
  if (cmd.indexOf("?WATCH") != -1) {
    webSerialLog("GPSD client registered: " + client->remoteIP().toString());
    if (xSemaphoreTake(clientsMutex, portMAX_DELAY)) {
      for (auto& ctx : clients) {
        if (ctx.client == client) {
          ctx.isGpsd = true;
          String ack = "{\"class\":\"VERSION\",\"release\":\"3.23\",\"rev\":\"ESP32\",\"proto_major\":3,\"proto_minor\":14}\\n";
          ack += "{\"class\":\"DEVICES\",\"devices\":[{\"class\":\"DEVICE\",\"path\":\"/dev/i2c\",\"driver\":\"u-blox\",\"activated\":\"" + gpsData.dateStr + "T" + gpsData.timeStr + "Z\"}]}\\n";
          ack += "{\"class\":\"WATCH\",\"enable\":true,\"json\":true}\\n";
          
          // IMMEDIATE UPDATE: Send current TPV if valid
          if (gpsData.hasFix) {
             ack += generateTPV();
          }

          client->write(ack.c_str());
          break;
        }
      }
      xSemaphoreGive(clientsMutex);
    }
  }
}

static void handleNewClient(void* arg, AsyncClient* client) {
  ClientContext ctx;
  ctx.client = client;
  ctx.isGpsd = false; 
  
  String clientIP = client->remoteIP().toString();
  webSerialLog("TCP client connected: " + clientIP);
  
  if (xSemaphoreTake(clientsMutex, portMAX_DELAY)) {
    clients.push_back(ctx);
    newClientConnected = true;
    xSemaphoreGive(clientsMutex);
  }

  client->onDisconnect([](void* arg, AsyncClient* c) {
    String clientIP = c->remoteIP().toString();
    webSerialLog("TCP client disconnected: " + clientIP);
    if (xSemaphoreTake(clientsMutex, portMAX_DELAY)) {
      for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->client == c) {
          clients.erase(it);
          break;
        }
      }
      xSemaphoreGive(clientsMutex);
    }
  }, NULL);
  client->onData(&handleClientData, NULL);
}

void setupTCP() {
  clientsMutex = xSemaphoreCreateMutex();
  tcpServer.onClient(&handleNewClient, NULL);
  tcpServer.begin();
  Serial.println("TCP server started on port " + String(TCP_PORT));
}

bool hasNewConnections() {
  if (newClientConnected) {
    newClientConnected = false; 
    return true;
  }
  return false;
}

void broadcastData() {
  // OPTIMIZATION: Check if anyone is actually listening before building strings
  bool hasActiveClients = false;
  if (xSemaphoreTake(clientsMutex, portMAX_DELAY)) {
    for (auto& ctx : clients) {
      if (ctx.client->connected()) {
        hasActiveClients = true;
        break;
      }
    }
    xSemaphoreGive(clientsMutex);
  }

  if (!hasActiveClients) return; // Exit immediately if no clients, saving CPU/RAM

  // Construct strings first to minimize lock time, relying on extern gpsData
  
  // --- PREPARE NMEA ---
  String rmc = "GPRMC,";
  char timeBuf[15];
  sprintf(timeBuf, "%02d%02d%02d.00,", gpsData.hour, gpsData.minute, gpsData.second);
  rmc += timeBuf;
  rmc += (gpsData.hasFix ? "A," : "V,");
  rmc += toNMEA(gpsData.lat, false) + (gpsData.lat >= 0 ? ",N," : ",S,");
  rmc += toNMEA(gpsData.lon, true) + (gpsData.lon >= 0 ? ",E," : ",W,");
  rmc += String(gpsData.speed * 1.94384) + ",";
  rmc += String(gpsData.heading) + ",";
  char dateBuf[10];
  sprintf(dateBuf, "%02d%02d%02d,", gpsData.day, gpsData.month, gpsData.year % 100);
  rmc += dateBuf;
  rmc += ",,";
  String rmcFull = "$" + rmc + "*" + getChecksum(rmc) + "\r\n";

  String gga = "GPGGA,";
  gga += String(timeBuf);
  gga += toNMEA(gpsData.lat, false) + (gpsData.lat >= 0 ? ",N," : ",S,");
  gga += toNMEA(gpsData.lon, true) + (gpsData.lon >= 0 ? ",E," : ",W,");
  gga += (gpsData.hasFix ? "1," : "0,");
  gga += String(gpsData.satellites) + ",";
  gga += String(gpsData.hdop) + ",";
  gga += String(gpsData.alt) + ",M,";
  gga += "0.0,M,,";
  String ggaFull = "$" + gga + "*" + getChecksum(gga) + "\r\n";
  
  int mode = 1; // Default No Fix
  if (gpsData.fixType == 2) mode = 2; // 2D
  if (gpsData.fixType == 3) mode = 3; // 3D
  
  String gsa = "GPGSA,A," + String(mode) + ",,,,,,,,,,,,,";
  gsa += String(gpsData.pdop) + "," + String(gpsData.hdop) + "," + String(gpsData.vdop);
  String gsaFull = "$" + gsa + "*" + getChecksum(gsa) + "\r\n";

  // --- PREPARE GPSD JSON ---
  String tpv = generateTPV();

  if (xSemaphoreTake(clientsMutex, portMAX_DELAY)) {
    for (auto& ctx : clients) {
      if (ctx.client->connected() && ctx.client->canSend()) {
        if (ctx.isGpsd && gpsData.hasFix) {
          ctx.client->write(tpv.c_str());
        } else {
          ctx.client->write(rmcFull.c_str());
          ctx.client->write(ggaFull.c_str());
          ctx.client->write(gsaFull.c_str());
        }
      }
    }
    xSemaphoreGive(clientsMutex);
  }
}