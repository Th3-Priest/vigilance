#ifndef __VIGILANCE_WARDRIVING_PLUS_H__
#define __VIGILANCE_WARDRIVING_PLUS_H__

// Vigilance - Wardriving+ (#46). Post-processing of Wigle wardriving logs:
//  - export KML (Google Earth) from an existing WigleWifi CSV,
//  - session stats (networks, unique MACs, encryption breakdown, WiFi/BLE).
// Works on the CSVs already saved in /BruceWardriving/, no GPS needed at
// export time.
void wardriving_plus_menu();

#endif // __VIGILANCE_WARDRIVING_PLUS_H__
