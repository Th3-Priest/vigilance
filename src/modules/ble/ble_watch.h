#ifndef __VIGILANCE_BLE_WATCH_H__
#define __VIGILANCE_BLE_WATCH_H__

// Vigilance - BLE Watch Mode / anti-tracker (#6). Continuous passive BLE scan:
// spots trackers (Apple FindMy/AirTag, Tile, Samsung SmartTag, Chipolo) and,
// above all, those that stay near you across several scan windows
// (anti-stalking heuristic). Alerts with red LED + beep + event log.
// Defensive (blue-team) feature, the BLE counterpart to WiFi Watch Mode.
void ble_watch_setup();

#endif // __VIGILANCE_BLE_WATCH_H__
