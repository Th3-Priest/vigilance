#ifndef __VIGILANCE_REPORT_H__
#define __VIGILANCE_REPORT_H__

#include <Arduino.h>

// Save a plain-text session report to /Vigilance/reports/<tag>_NNN.txt.
// Returns the path written, or "" on failure. Used by Bug Sweep, Watch Mode,
// and other modules to leave a shareable summary of what a run found.
String vigSaveReport(const String &tag, const String &body);

#endif // __VIGILANCE_REPORT_H__
