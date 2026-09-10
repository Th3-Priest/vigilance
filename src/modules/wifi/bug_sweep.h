#ifndef __VIGILANCE_BUG_SWEEP_H__
#define __VIGILANCE_BUG_SWEEP_H__

// Vigilance - Bug Sweep (#5). WiFi scan that flags likely cameras/DVRs nearby
// (by SSID pattern and vendor), to spot a hidden camera in an Airbnb/hotel
// before settling in. Passive detection, no transmission.
void bug_sweep_setup();

#endif // __VIGILANCE_BUG_SWEEP_H__
