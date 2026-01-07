#ifndef CONTEXT_H
#define CONTEXT_H

#include "Types.h"
#include <SparkFun_u-blox_GNSS_Arduino_Library.h> // v2 Header

// Declare externs so other files can use them
extern GPSData gpsData;
extern SFE_UBLOX_GNSS myGNSS;
class Storage; // Forward declaration
extern Storage storage;

// OTA Update Status
extern volatile bool otaInProgress;

#endif
