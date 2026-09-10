#pragma once

#if !defined(LITE_VERSION)

// Vigilance - Watch Mode. Continuous passive WiFi surveillance:
//  - detects deauth/disassoc attacks (WIDS) and alerts past a threshold;
//  - spots new devices/APs appearing (beacons + probe requests);
//  - alerts loudly if a watchlist target appears
//    (file /VigilanceVeille/watch.txt, one MAC per line, # = comment);
//  - logs detections to /VigilanceVeille/veille_log.csv.
// Alerts: cyan LED + beep + on-screen banner. ESC to quit.
void watch_mode_setup();

#endif
