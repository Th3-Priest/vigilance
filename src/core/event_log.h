#ifndef __VIGILANCE_EVENT_LOG_H__
#define __VIGILANCE_EVENT_LOG_H__

#include <Arduino.h>

// Vigilance: global event log (alerts, detections). Fed by the modules (Watch
// Mode, swarm...) and read by the web companion via the /events endpoint.
// Ring buffer protected by a spinlock (accessed from several tasks).

void vigLogEvent(const char *type, const String &msg);

// JSON of the recent events (most recent first) for the /events endpoint.
String vigEventsJson();

// Public view of an event (for the on-device Timeline screen).
struct VigEventView {
    uint32_t ts_s;   // timestamp (seconds since boot)
    char type[12];   // "TARGET", "DEAUTH", "RF", "NFC"...
    char msg[48];    // detail
};

// Copy the recent events (most recent first) into out (capacity maxN).
// Returns the number copied.
int vigEventsSnapshot(VigEventView *out, int maxN);

// Total events logged since boot (may exceed the ring size).
uint32_t vigEventsTotal();

#endif
