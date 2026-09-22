// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - session reports. A tiny helper that writes a plain-text summary
// of a monitoring run to the SD card, so what the device found can be reviewed
// or shared later.
#include "vig_report.h"

#include "core/sd_functions.h"
#include <globals.h>

String vigSaveReport(const String &tag, const String &body) {
    FS *fs;
    if (!getFsStorage(fs)) return "";
    if (!(*fs).exists("/Vigilance")) (*fs).mkdir("/Vigilance");
    if (!(*fs).exists("/Vigilance/reports")) (*fs).mkdir("/Vigilance/reports");

    char path[48];
    int n = 0;
    do {
        snprintf(path, sizeof(path), "/Vigilance/reports/%s_%03d.txt", tag.c_str(), n++);
    } while ((*fs).exists(path) && n < 1000);

    File f = (*fs).open(path, FILE_WRITE);
    if (!f) return "";
    f.print(body);
    f.close();
    return String(path);
}
