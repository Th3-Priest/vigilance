#ifndef __VIGILANCE_RF_CENSUS_H__
#define __VIGILANCE_RF_CENSUS_H__

// Vigilance - Ambient Sub-GHz Census (#46). A passive watch mode for the sub-GHz
// band: it listens on the configured frequency, decodes what it can (remotes,
// rolling codes, cheap weather sensors...) and keeps a live list of the distinct
// devices around you, feeding each into the shared event journal. The sub-GHz
// counterpart of the WiFi/BLE watch mode. Receive only, never transmits.
void rf_census_setup();

#endif // __VIGILANCE_RF_CENSUS_H__
