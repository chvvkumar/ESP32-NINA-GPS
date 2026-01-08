#ifndef WEB_SERVER_H
#define WEB_SERVER_H

void setupWeb();
void webLoop();
bool isOTAUpdating();

// Web Serial Logging
void webSerialLog(const String& message);
void webSerialBegin();

#endif
