#ifndef __VIGILANCE_ENV_H__
#define __VIGILANCE_ENV_H__

#include <stdint.h>

// Vigilance - last known picture of the RF environment. The monitoring modules
// (Watch Mode, BLE Watch, Sub-GHz Census...) publish their counts here and the
// home screen radar shows them. The home screen itself never scans: it would
// cost battery and fight the module that owns the radio.
#define VIG_ENV_WIFI 0
#define VIG_ENV_BLE 1
#define VIG_ENV_SUB 2
#define VIG_ENV_N 3

void vigEnvSet(uint8_t kind, int n);
int vigEnvGet(uint8_t kind);       // -1 while nothing has been published yet
uint32_t vigEnvAgeS(uint8_t kind); // seconds since the last update, UINT32_MAX if never

// Light non-blocking WiFi scan used by the home radar and the standby scene:
// call vigScanTick() every frame (it self-throttles and starts/reaps async
// WiFi scans, publishing the AP count to VIG_ENV_WIFI); vigScanStop() turns the
// radio back off when leaving those screens.
void vigScanTick();
void vigScanStop();

#endif // __VIGILANCE_ENV_H__
