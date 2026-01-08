#ifndef ESP_NOW_SENDER_H
#define ESP_NOW_SENDER_H

#include "Types.h"

void setupEspNow();
void sendGpsDataViaEspNow();
void checkEspNowClientTimeouts();  // Check for client timeouts

#endif
