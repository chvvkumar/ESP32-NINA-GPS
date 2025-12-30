#ifndef STORAGE_H
#define STORAGE_H

#include <Preferences.h>
#include "Types.h"
#include "Context.h"

// Define a separate struct for persisted stats to allow easy serialization/deserialization
// and addition of new metrics without affecting the main GPSData struct layout logic too much.
// However, since we are mapping directly to variables in GPSData, we can just save/load individual keys.
// To meet the requirement "New metrics can be easily added", we will use key-value pairs in Preferences.

class Storage {
public:
    void begin() {
        prefs.begin("gps_stats", false); // Read-write mode
        loadStats();
    }

    void loadStats() {
        gpsData.altMin = prefs.getDouble("altMin", 99999.0);
        gpsData.altMax = prefs.getDouble("altMax", -99999.0);
        gpsData.speedMax = prefs.getFloat("speedMax", 0.0);
        gpsData.satellitesMax = prefs.getInt("satsMax", 0);
        gpsData.satellitesVisibleMax = prefs.getInt("visSatsMax", 0);
        gpsData.pdopMin = prefs.getFloat("pdopMin", 100.0);
        gpsData.hdopMin = prefs.getFloat("hdopMin", 100.0);
        gpsData.vdopMin = prefs.getFloat("vdopMin", 100.0);
        gpsData.hAccMin = prefs.getFloat("hAccMin", 99999.0);
        gpsData.vAccMin = prefs.getFloat("vAccMin", 99999.0);
    }

    // Call this whenever a new min/max is observed
    void saveIfChanged() {
        // We check against the stored values before writing to save flash cycles.
        // Note: Preferences.putX() internaly checks if value is different before writing, 
        // but explicit checks here can save function call overhead if needed, 
        // though the library does the heavy lifting for flash wear.
        
        // However, since the user explicitly asked to "only write to flash if a new min/max value is seen",
        // and we are updating the in-memory gpsData struct constantly in the loop,
        // we need a way to know if the *persisted* value is different from the *current* value in gpsData.
        // The most robust way is to rely on Preferences' built-in check or keep a "lastSaved" shadow copy.
        // Given Preferences.putX returns size_t bytes written (0 if no change), we can just call it.
        // But to be super explicit about the logic:
        
        // Altitude
        if (gpsData.altMin != prefs.getDouble("altMin", 99999.0)) prefs.putDouble("altMin", gpsData.altMin);
        if (gpsData.altMax != prefs.getDouble("altMax", -99999.0)) prefs.putDouble("altMax", gpsData.altMax);
        
        // Speed
        if (gpsData.speedMax != prefs.getFloat("speedMax", 0.0)) prefs.putFloat("speedMax", gpsData.speedMax);
        
        // Sats
        if (gpsData.satellitesMax != prefs.getInt("satsMax", 0)) prefs.putInt("satsMax", gpsData.satellitesMax);
        if (gpsData.satellitesVisibleMax != prefs.getInt("visSatsMax", 0)) prefs.putInt("visSatsMax", gpsData.satellitesVisibleMax);
        
        // DOP
        if (gpsData.pdopMin != prefs.getFloat("pdopMin", 100.0)) prefs.putFloat("pdopMin", gpsData.pdopMin);
        if (gpsData.hdopMin != prefs.getFloat("hdopMin", 100.0)) prefs.putFloat("hdopMin", gpsData.hdopMin);
        if (gpsData.vdopMin != prefs.getFloat("vdopMin", 100.0)) prefs.putFloat("vdopMin", gpsData.vdopMin);
        
        // Accuracy
        if (gpsData.hAccMin != prefs.getFloat("hAccMin", 99999.0)) prefs.putFloat("hAccMin", gpsData.hAccMin);
        if (gpsData.vAccMin != prefs.getFloat("vAccMin", 99999.0)) prefs.putFloat("vAccMin", gpsData.vAccMin);
    }
    
    // Helper to streamline updates in the main loop
    // Usage: storage.update(gpsData.altMin, currentAlt, [](double& min, double val){ return val < min; });
    // Actually, simpler to just let the main loop update the struct, and then call saveIfChanged().
    
    // Explicit setter helpers can ensure we write immediately when a record is broken
    // This avoids polling 'saveIfChanged' constantly or missing a save on power loss.
    
    void updateAlt(double alt) {
        if (alt < gpsData.altMin) { gpsData.altMin = alt; prefs.putDouble("altMin", alt); }
        if (alt > gpsData.altMax) { gpsData.altMax = alt; prefs.putDouble("altMax", alt); }
    }
    
    void updateSpeed(float speed) {
        if (speed > gpsData.speedMax) { gpsData.speedMax = speed; prefs.putFloat("speedMax", speed); }
    }
    
    void updateSats(int sats) {
        if (sats > gpsData.satellitesMax) { gpsData.satellitesMax = sats; prefs.putInt("satsMax", sats); }
    }
    
    void updateVisibleSats(int sats) {
        if (sats > gpsData.satellitesVisibleMax) { gpsData.satellitesVisibleMax = sats; prefs.putInt("visSatsMax", sats); }
    }
    
    void updateDOP(float pdop, float hdop, float vdop) {
        if (pdop > 0.01 && pdop < gpsData.pdopMin) { gpsData.pdopMin = pdop; prefs.putFloat("pdopMin", pdop); }
        if (hdop > 0.01 && hdop < gpsData.hdopMin) { gpsData.hdopMin = hdop; prefs.putFloat("hdopMin", hdop); }
        if (vdop > 0.01 && vdop < gpsData.vdopMin) { gpsData.vdopMin = vdop; prefs.putFloat("vdopMin", vdop); }
    }
    
    void updateAcc(float hAcc, float vAcc) {
        if (hAcc > 0 && hAcc < gpsData.hAccMin) { gpsData.hAccMin = hAcc; prefs.putFloat("hAccMin", hAcc); }
        if (vAcc > 0 && vAcc < gpsData.vAccMin) { gpsData.vAccMin = vAcc; prefs.putFloat("vAccMin", vAcc); }
    }

    void clearSession() {
        // Reset in-memory values to defaults
        // Note: This will likely cause immediate writes to flash on the next GPS poll
        // as the current values will set new "records" against these defaults.
        gpsData.altMin = 99999.0;
        gpsData.altMax = -99999.0;
        gpsData.speedMax = 0.0;
        gpsData.satellitesMax = 0;
        gpsData.satellitesVisibleMax = 0;
        gpsData.pdopMin = 100.0;
        gpsData.hdopMin = 100.0;
        gpsData.vdopMin = 100.0;
        gpsData.hAccMin = 99999.0;
        gpsData.vAccMin = 99999.0;
    }

    void clearStorage() {
        prefs.clear();
    }

private:
    Preferences prefs;
};

extern Storage storage;

#endif
