// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance - Wardriving+ (#46): export KML + session stats from the
// WigleWifi-1.6 CSVs produced by wardriving. It's all file post-processing
// (SD read/write), so it works without a GPS plugged in.
//
// WigleWifi-1.6 columns (header row #2):
//   MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,
//   CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type
// Note: naive comma split, so an SSID containing a comma shifts the fields
// (rare); known limitation. The fields that matter for the KML (lat/lon/SSID)
// are correct in the common case.
#include "wardriving_plus.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <globals.h>
#include <set>
#include <vector>

// idx-th comma-separated field (0-based), "" if absent.
static String csvField(const String &line, int idx) {
    int start = 0;
    for (int i = 0; i < idx; i++) {
        int c = line.indexOf(',', start);
        if (c < 0) return "";
        start = c + 1;
    }
    int c = line.indexOf(',', start);
    return (c < 0) ? line.substring(start) : line.substring(start, c);
}

static const char *authClass(const String &a) {
    String u = a;
    u.toUpperCase();
    if (u.indexOf("WPA3") >= 0) return "WPA3";
    if (u.indexOf("WPA2") >= 0) return "WPA2";
    if (u.indexOf("WPA") >= 0) return "WPA";
    if (u.indexOf("WEP") >= 0) return "WEP";
    return "OPEN"; // OPEN/ESS/empty
}

static int listCsvs(FS *fs, String *out, int maxN) {
    int n = 0;
    if (!fs->exists("/VigilanceWardriving")) return 0;
    File dir = fs->open("/VigilanceWardriving");
    if (!dir) return 0;
    File e = dir.openNextFile();
    while (e && n < maxN) {
        String nm = String(e.name());
        int slash = nm.lastIndexOf('/');
        if (slash >= 0) nm = nm.substring(slash + 1);
        if (nm.endsWith(".csv")) out[n++] = nm;
        e = dir.openNextFile();
    }
    dir.close();
    return n;
}

static void exportOne(FS *fs, const String &csvName) {
    String inPath = "/VigilanceWardriving/" + csvName;
    String base = csvName.substring(0, csvName.lastIndexOf('.'));
    String kmlPath = "/VigilanceWardriving/" + base + ".kml";

    File in = (*fs).open(inPath, FILE_READ);
    if (!in) {
        displayError("Cannot open CSV", true);
        return;
    }
    File kml = (*fs).open(kmlPath, FILE_WRITE);
    if (!kml) {
        in.close();
        displayError("Cannot create KML", true);
        return;
    }

    kml.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    kml.println("<kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document>");
    kml.println("<name>Vigilance Wardriving - " + base + "</name>");

    long total = 0, placed = 0, wifi = 0, ble = 0;
    long cOpen = 0, cWep = 0, cWpa = 0, cWpa2 = 0, cWpa3 = 0;
    std::set<String> uniq;
    int lineNo = 0;

    while (in.available()) {
        String line = in.readStringUntil('\n');
        line.trim();
        lineNo++;
        if (lineNo <= 2) continue; // WigleWifi header + column names
        if (line.length() < 5) continue;

        String mac = csvField(line, 0);
        String ssid = csvField(line, 1);
        String auth = csvField(line, 2);
        String chan = csvField(line, 4);
        String rssi = csvField(line, 6);
        String lat = csvField(line, 7);
        String lon = csvField(line, 8);
        String alt = csvField(line, 9);
        String type = csvField(line, 13);

        total++;
        if (mac.length()) uniq.insert(mac);
        if (type.indexOf("BLE") >= 0 || type.indexOf("BT") >= 0) ble++;
        else wifi++;
        const char *ac = authClass(auth);
        if (!strcmp(ac, "OPEN")) cOpen++;
        else if (!strcmp(ac, "WEP")) cWep++;
        else if (!strcmp(ac, "WPA3")) cWpa3++;
        else if (!strcmp(ac, "WPA2")) cWpa2++;
        else cWpa++;

        double dlat = lat.toDouble(), dlon = lon.toDouble();
        if (dlat == 0.0 && dlon == 0.0) continue; // no fix: no KML point
        placed++;
        String nm = ssid.length() ? ssid : mac;
        nm.replace("<", "(");
        nm.replace(">", ")");
        nm.replace("&", "+");
        kml.println("<Placemark><name>" + nm + "</name>");
        kml.println(
            "<description>" + mac + " | ch" + chan + " | " + rssi + "dBm | " + String(ac) + "</description>"
        );
        kml.println(
            "<Point><coordinates>" + lon + "," + lat + "," + (alt.length() ? alt : "0") +
            "</coordinates></Point></Placemark>"
        );
    }
    kml.println("</Document></kml>");
    in.close();
    kml.close();

    // Show the stats.
    const uint16_t ac = bruceConfig.priColor, dimc = bruceConfig.secColor, bg = bruceConfig.bgColor;
    drawMainBorderWithTitle("WARDRIVING+");
    int y = 32;
    tft.setTextColor(ac, bg);
    tft.drawString("KML: " + base + ".kml", 10, y, 1);
    y += 16;
    tft.setTextColor(dimc, bg);
    auto line2 = [&](const String &s) {
        tft.setTextColor(ac, bg);
        tft.drawString(s, 10, y, 1);
        y += 14;
    };
    line2("Entries: " + String(total) + "  points: " + String(placed));
    line2("Unique (MAC): " + String((int)uniq.size()));
    line2("WiFi: " + String(wifi) + "   BLE: " + String(ble));
    line2("Open:" + String(cOpen) + " WEP:" + String(cWep) + " WPA:" + String(cWpa));
    line2("WPA2:" + String(cWpa2) + "  WPA3:" + String(cWpa3));
    tft.setTextColor(dimc, bg);
    tft.drawString("SEL/ESC to return", 10, tftHeight - 14, 1);
    while (!check(SelPress) && !check(EscPress)) delay(20);
}

void wardriving_plus_menu() {
    FS *fs;
    if (!getFsStorage(fs)) {
        displayError("SD unavailable", true);
        return;
    }
    String csvs[24];
    int n = listCsvs(fs, csvs, 24);
    if (n == 0) {
        displayError("No wardriving CSV", true);
        return;
    }
    std::vector<Option> o;
    for (int i = 0; i < n; i++) {
        String name = csvs[i];
        o.push_back({name, [fs, name]() { exportOne(fs, name); }});
    }
    o.push_back({"Back", []() {}});
    loopOptions(o, MENU_TYPE_SUBMENU, "Export KML + stats");
}
