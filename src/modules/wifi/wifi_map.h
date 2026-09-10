#ifndef __VIGILANCE_WIFI_MAP_H__
#define __VIGILANCE_WIFI_MAP_H__

// Vigilance - WiFi neighborhood map (#30). Scans APs in range, with two views:
//  - channel usage (histogram 1..13, busiest channel highlighted)
//  - AP list (SSID, channel, RSSI, encryption, vendor via the OUI database)
// Passive recon view, complements the Channel Analyzer (RF spectrum).
void wifi_map_setup();

#endif // __VIGILANCE_WIFI_MAP_H__
