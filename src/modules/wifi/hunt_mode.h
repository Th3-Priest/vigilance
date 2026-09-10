#ifndef __VIGILANCE_HUNT_MODE_H__
#define __VIGILANCE_HUNT_MODE_H__

// Vigilance - Hunt Mode (#8). "Hot/cold" direction finder: lock onto a WiFi
// target, then the device acts like a Geiger counter - RSSI bar on screen, an
// ever-greener LED and an ever-faster beep as you get closer. For physically
// tracking down a transmitter.
void hunt_mode_setup();

#endif // __VIGILANCE_HUNT_MODE_H__
