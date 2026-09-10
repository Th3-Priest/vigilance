// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vigilance: embedded OUI database. Curated table of OUIs (24-bit MAC prefixes)
// for the most common consumer vendors, used to name devices seen by Watch Mode
// or wardriving. On ESP32 consts live in flash (.rodata, memory-mapped): direct
// access, no PROGMEM needed. The scan is linear but only runs when a *new* device
// appears (deduplicated), so it's negligible. Best-effort database: covers the
// essentials, not the whole IEEE registry.
#include "oui_vendor.h"

struct OuiEntry {
    uint32_t oui; // top 3 bytes: (mac[0]<<16)|(mac[1]<<8)|mac[2]
    const char *name;
};

// clang-format off
static const OuiEntry OUI_TABLE[] = {
    // --- Espressif (ESP32/ESP8266) - very relevant for this tool ---
    {0x18FE34, "Espressif"}, {0x240AC4, "Espressif"}, {0x2462AB, "Espressif"},
    {0x246F28, "Espressif"}, {0x24B2DE, "Espressif"}, {0x2C3AE8, "Espressif"},
    {0x30AEA4, "Espressif"}, {0x3C6105, "Espressif"}, {0x3C71BF, "Espressif"},
    {0x4C11AE, "Espressif"}, {0x545AA6, "Espressif"}, {0x5CCF7F, "Espressif"},
    {0x600194, "Espressif"}, {0x68C63A, "Espressif"}, {0x7C9EBD, "Espressif"},
    {0x7CDFA1, "Espressif"}, {0x840D8E, "Espressif"}, {0x84CCA8, "Espressif"},
    {0x84F3EB, "Espressif"}, {0x9097D5, "Espressif"}, {0xA020A6, "Espressif"},
    {0xA47B9D, "Espressif"}, {0xA4CF12, "Espressif"}, {0xAC67B2, "Espressif"},
    {0xB4E62D, "Espressif"}, {0xB8F009, "Espressif"}, {0xBCDDC2, "Espressif"},
    {0xC44F33, "Espressif"}, {0xC82B96, "Espressif"}, {0xCC50E3, "Espressif"},
    {0xD8A01D, "Espressif"}, {0xD8BFC0, "Espressif"}, {0xDC4F22, "Espressif"},
    {0xE09806, "Espressif"}, {0xECFABC, "Espressif"}, {0xF008D1, "Espressif"},
    {0xF4CFA2, "Espressif"}, {0xFCF5C4, "Espressif"},
    // --- Apple ---
    {0x000393, "Apple"}, {0x000A27, "Apple"}, {0x000A95, "Apple"}, {0x000D93, "Apple"},
    {0x001124, "Apple"}, {0x0016CB, "Apple"}, {0x0017F2, "Apple"}, {0x0019E3, "Apple"},
    {0x001B63, "Apple"}, {0x001EC2, "Apple"}, {0x001F5B, "Apple"}, {0x0021E9, "Apple"},
    {0x002332, "Apple"}, {0x00236C, "Apple"}, {0x0023DF, "Apple"}, {0x002500, "Apple"},
    {0x00254B, "Apple"}, {0x0025BC, "Apple"}, {0x002608, "Apple"}, {0x00264A, "Apple"},
    {0x0026B0, "Apple"}, {0x0026BB, "Apple"}, {0x003EE1, "Apple"}, {0x0050E4, "Apple"},
    {0x040CCE, "Apple"}, {0x041552, "Apple"}, {0x042665, "Apple"}, {0x04489A, "Apple"},
    {0x0452F3, "Apple"}, {0x0469F8, "Apple"}, {0x04D3CF, "Apple"}, {0x04E536, "Apple"},
    {0x04F13E, "Apple"}, {0x04F7E4, "Apple"}, {0x086D41, "Apple"}, {0x086698, "Apple"},
    {0x0C3021, "Apple"}, {0x0C4DE9, "Apple"}, {0x0C7466, "Apple"}, {0x101C0C, "Apple"},
    {0x109ADD, "Apple"}, {0x10DDB1, "Apple"}, {0x14109F, "Apple"}, {0x1499E2, "Apple"},
    {0x18AF61, "Apple"}, {0x18E7F4, "Apple"}, {0x1C9148, "Apple"}, {0x1CABA7, "Apple"},
    {0x203CAE, "Apple"}, {0x20768F, "Apple"}, {0x24A074, "Apple"}, {0x28CFE9, "Apple"},
    {0x28E02C, "Apple"}, {0x28E7CF, "Apple"}, {0x2C1F23, "Apple"}, {0x2CBE08, "Apple"},
    {0x2CF0EE, "Apple"}, {0x30636B, "Apple"}, {0x3090AB, "Apple"}, {0x30F7C5, "Apple"},
    {0x34159E, "Apple"}, {0x3871DE, "Apple"}, {0x38B54D, "Apple"}, {0x3C0754, "Apple"},
    {0x3C15C2, "Apple"}, {0x40331A, "Apple"}, {0x40A6D9, "Apple"}, {0x40D32D, "Apple"},
    {0x442A60, "Apple"}, {0x44D884, "Apple"}, {0x48437C, "Apple"}, {0x4C57CA, "Apple"},
    {0x4C8D79, "Apple"}, {0x50EAD6, "Apple"}, {0x542696, "Apple"}, {0x5855CA, "Apple"},
    {0x5C5948, "Apple"}, {0x5C95AE, "Apple"}, {0x606944, "Apple"}, {0x609217, "Apple"},
    {0x60FACD, "Apple"}, {0x64200C, "Apple"}, {0x64B9E8, "Apple"}, {0x68967B, "Apple"},
    {0x68A86D, "Apple"}, {0x6C4008, "Apple"}, {0x6C709F, "Apple"}, {0x7014A6, "Apple"},
    {0x7073CB, "Apple"}, {0x74E2F5, "Apple"}, {0x783A84, "Apple"}, {0x7CD1C3, "Apple"},
    {0x7CF05F, "Apple"}, {0x8866A5, "Apple"}, {0x8C2937, "Apple"}, {0x8C5877, "Apple"},
    {0x90B21F, "Apple"}, {0x90FD61, "Apple"}, {0x9803D8, "Apple"}, {0x98B8E3, "Apple"},
    {0x9C04EB, "Apple"}, {0x9C207B, "Apple"}, {0xA45E60, "Apple"}, {0xA8667F, "Apple"},
    {0xA886DD, "Apple"}, {0xAC3C0B, "Apple"}, {0xACBC32, "Apple"}, {0xB09FBA, "Apple"},
    {0xB418D1, "Apple"}, {0xB8098A, "Apple"}, {0xB8C111, "Apple"}, {0xBC926B, "Apple"},
    {0xC06394, "Apple"}, {0xC82A14, "Apple"}, {0xC8B5B7, "Apple"}, {0xCC088D, "Apple"},
    {0xCC29F5, "Apple"}, {0xD023DB, "Apple"}, {0xD0817A, "Apple"}, {0xD89695, "Apple"},
    {0xDC2B2A, "Apple"}, {0xDCA904, "Apple"}, {0xE0ACCB, "Apple"}, {0xE425E7, "Apple"},
    {0xE8802E, "Apple"}, {0xF0DBF8, "Apple"}, {0xF40F24, "Apple"}, {0xF80377, "Apple"},
    {0xFC253F, "Apple"},
    // --- Samsung ---
    {0x0000F0, "Samsung"}, {0x001632, "Samsung"}, {0x00166B, "Samsung"}, {0x0017C9, "Samsung"},
    {0x0018AF, "Samsung"}, {0x001A8A, "Samsung"}, {0x001B98, "Samsung"}, {0x001C43, "Samsung"},
    {0x001D25, "Samsung"}, {0x001E7D, "Samsung"}, {0x001FCC, "Samsung"}, {0x002119, "Samsung"},
    {0x002339, "Samsung"}, {0x002454, "Samsung"}, {0x002490, "Samsung"}, {0x002566, "Samsung"},
    {0x00265D, "Samsung"}, {0x08373D, "Samsung"}, {0x08D42B, "Samsung"}, {0x0C1420, "Samsung"},
    {0x0C715D, "Samsung"}, {0x101DC0, "Samsung"}, {0x103B59, "Samsung"}, {0x1449E0, "Samsung"},
    {0x183A2D, "Samsung"}, {0x1C5A3E, "Samsung"}, {0x1C66AA, "Samsung"}, {0x2013E0, "Samsung"},
    {0x244B03, "Samsung"}, {0x28395E, "Samsung"}, {0x28CC01, "Samsung"}, {0x2C4401, "Samsung"},
    {0x301966, "Samsung"}, {0x3423BA, "Samsung"}, {0x34BE00, "Samsung"}, {0x380A94, "Samsung"},
    {0x38AA3C, "Samsung"}, {0x3C5A37, "Samsung"}, {0x400E85, "Samsung"}, {0x444E1A, "Samsung"},
    {0x5C0A5B, "Samsung"}, {0x781FDB, "Samsung"}, {0x8C71F8, "Samsung"}, {0x8CBFA6, "Samsung"},
    {0x9401C2, "Samsung"}, {0xA00798, "Samsung"}, {0xB47443, "Samsung"}, {0xC81479, "Samsung"},
    {0xE8508B, "Samsung"}, {0xF008F1, "Samsung"},
    // --- Google / Nest ---
    {0x001A11, "Google"}, {0x3C5AB4, "Google"}, {0x546009, "Google"}, {0x20DFB9, "Google"},
    {0x48D6D5, "Google"}, {0x6CADF8, "Google"}, {0x944952, "Google"}, {0x94EB2C, "Google"},
    {0xA47733, "Google"}, {0xF4F5D8, "Google"}, {0xF4F5E8, "Google"},
    {0x641666, "Nest"},   {0x188B0E, "Nest"},   {0x18B430, "Nest"},
    // --- Amazon ---
    {0x00BB3A, "Amazon"}, {0x0C47C9, "Amazon"}, {0x34D270, "Amazon"}, {0x40B4CD, "Amazon"},
    {0x44650D, "Amazon"}, {0x50DCE7, "Amazon"}, {0x6837E9, "Amazon"}, {0x6854FD, "Amazon"},
    {0x6C5697, "Amazon"}, {0x74C246, "Amazon"}, {0x84D6D0, "Amazon"}, {0xA002DC, "Amazon"},
    {0xAC63BE, "Amazon"}, {0xB47C9C, "Amazon"}, {0xF0272D, "Amazon"}, {0xFC65DE, "Amazon"},
    {0xFCA183, "Amazon"}, {0x689A87, "Amazon"},
    // --- Intel (WiFi) ---
    {0x0002B3, "Intel"}, {0x000CF1, "Intel"}, {0x000E0C, "Intel"}, {0x001111, "Intel"},
    {0x0013CE, "Intel"}, {0x0013E8, "Intel"}, {0x001500, "Intel"}, {0x00166F, "Intel"},
    {0x001676, "Intel"}, {0x0018DE, "Intel"}, {0x0019D1, "Intel"}, {0x001B21, "Intel"},
    {0x001C24, "Intel"}, {0x001DE0, "Intel"}, {0x001E64, "Intel"}, {0x001F3B, "Intel"},
    {0x00215C, "Intel"}, {0x00216A, "Intel"}, {0x00216B, "Intel"}, {0x0022FA, "Intel"},
    {0x0022FB, "Intel"}, {0x0024D6, "Intel"}, {0x0024D7, "Intel"}, {0x0026C6, "Intel"},
    {0x0026C7, "Intel"}, {0x002710, "Intel"}, {0x3413E8, "Intel"}, {0x34DE1A, "Intel"},
    {0x3CA9F4, "Intel"}, {0x4025C2, "Intel"}, {0x5C514F, "Intel"}, {0x5CE0C5, "Intel"},
    {0x6036DD, "Intel"}, {0x7C5CF8, "Intel"}, {0x7CB0C2, "Intel"}, {0x8CA982, "Intel"},
    {0x90E2BA, "Intel"}, {0xA08869, "Intel"}, {0xAC7289, "Intel"}, {0xAC7BA1, "Intel"},
    // --- Xiaomi ---
    {0x009EC8, "Xiaomi"}, {0x102AB3, "Xiaomi"}, {0x14F65A, "Xiaomi"}, {0x185936, "Xiaomi"},
    {0x2082C0, "Xiaomi"}, {0x286C07, "Xiaomi"}, {0x28E31F, "Xiaomi"}, {0x3480B3, "Xiaomi"},
    {0x34CE00, "Xiaomi"}, {0x38A4ED, "Xiaomi"}, {0x3CBD3E, "Xiaomi"}, {0x50642B, "Xiaomi"},
    {0x508F4C, "Xiaomi"}, {0x584498, "Xiaomi"}, {0x5CE50C, "Xiaomi"}, {0x640980, "Xiaomi"},
    {0x64B473, "Xiaomi"}, {0x64CC2E, "Xiaomi"}, {0x68DFDD, "Xiaomi"}, {0x6C5C14, "Xiaomi"},
    {0x703A51, "Xiaomi"}, {0x742344, "Xiaomi"}, {0x7451BA, "Xiaomi"}, {0x7802F8, "Xiaomi"},
    {0x7811DC, "Xiaomi"}, {0x7C1DD9, "Xiaomi"}, {0x8CBEBE, "Xiaomi"}, {0x98FAE3, "Xiaomi"},
    {0x9C99A0, "Xiaomi"}, {0xA086C6, "Xiaomi"}, {0xACC1EE, "Xiaomi"}, {0xB0E235, "Xiaomi"},
    {0xC40BCB, "Xiaomi"}, {0xC46AB7, "Xiaomi"}, {0xD4970B, "Xiaomi"}, {0xF0B429, "Xiaomi"},
    {0xF8A45F, "Xiaomi"}, {0xFC64BA, "Xiaomi"},
    // --- Huawei ---
    {0x001882, "Huawei"}, {0x001E10, "Huawei"}, {0x0022A1, "Huawei"}, {0x002568, "Huawei"},
    {0x00259E, "Huawei"}, {0x0034FE, "Huawei"}, {0x00464B, "Huawei"}, {0x005A13, "Huawei"},
    {0x00E0FC, "Huawei"}, {0x04021F, "Huawei"}, {0x0425C5, "Huawei"}, {0x043389, "Huawei"},
    {0x047503, "Huawei"}, {0x049FCA, "Huawei"}, {0x04B0E7, "Huawei"}, {0x04C06F, "Huawei"},
    {0x04F938, "Huawei"}, {0x0819A6, "Huawei"}, {0x0831A4, "Huawei"}, {0x086361, "Huawei"},
    {0x087A4C, "Huawei"}, {0x08E84F, "Huawei"}, {0x0C37DC, "Huawei"}, {0x0C96BF, "Huawei"},
    {0x101B54, "Huawei"}, {0x104780, "Huawei"}, {0x105172, "Huawei"}, {0x200BC7, "Huawei"},
    {0x202BC1, "Huawei"}, {0x20F17C, "Huawei"}, {0x2469A5, "Huawei"}, {0x24DBAC, "Huawei"},
    {0x283152, "Huawei"}, {0x283CE4, "Huawei"}, {0x285FDB, "Huawei"}, {0x48435A, "Huawei"},
    // --- TP-Link ---
    {0x001D0F, "TP-Link"}, {0x14CC20, "TP-Link"}, {0x14CF92, "TP-Link"}, {0x18A6F7, "TP-Link"},
    {0x1CFA68, "TP-Link"}, {0x30B5C2, "TP-Link"}, {0x40169F, "TP-Link"}, {0x50C7BF, "TP-Link"},
    {0x54C80F, "TP-Link"}, {0x6032B1, "TP-Link"}, {0x647002, "TP-Link"}, {0x8416F9, "TP-Link"},
    {0x90F652, "TP-Link"}, {0xA0F3C1, "TP-Link"}, {0xB0487A, "TP-Link"}, {0xC025E9, "TP-Link"},
    {0xC46E1F, "TP-Link"}, {0xD80D17, "TP-Link"}, {0xE894F6, "TP-Link"}, {0xEC086B, "TP-Link"},
    {0xF4EC38, "TP-Link"}, {0xF81A67, "TP-Link"}, {0xF8D111, "TP-Link"},
    // --- Microsoft ---
    {0x0003FF, "Microsoft"}, {0x000D3A, "Microsoft"}, {0x00125A, "Microsoft"}, {0x00155D, "Microsoft"},
    {0x0017FA, "Microsoft"}, {0x002248, "Microsoft"}, {0x0025AE, "Microsoft"}, {0x0050F2, "Microsoft"},
    {0x281878, "Microsoft"}, {0x3059B7, "Microsoft"}, {0x485073, "Microsoft"}, {0x501AC5, "Microsoft"},
    {0x5882A8, "Microsoft"}, {0x6045BD, "Microsoft"}, {0x7CED8D, "Microsoft"}, {0x985FD3, "Microsoft"},
    {0x9CAA1B, "Microsoft"}, {0xA49A58, "Microsoft"}, {0xBC8385, "Microsoft"}, {0xC49DED, "Microsoft"},
    {0xDC9840, "Microsoft"}, {0xE498D6, "Microsoft"},
    // --- Raspberry Pi ---
    {0xB827EB, "RaspberryPi"}, {0xDCA632, "RaspberryPi"}, {0xE45F01, "RaspberryPi"},
    {0x28CDC1, "RaspberryPi"}, {0x2CCF67, "RaspberryPi"}, {0xD83ADD, "RaspberryPi"},
    // --- Network / misc IoT ---
    {0x0018E7, "Cisco"},   {0x001A2F, "Cisco"},   {0x00259C, "Cisco"},   {0x00A0C9, "Cisco"},
    {0x0CD0F8, "Cisco"},   {0x2C3F38, "Cisco"},   {0x00095B, "Netgear"}, {0x00146C, "Netgear"},
    {0x001E2A, "Netgear"}, {0x00224B, "Netgear"}, {0x204E7F, "Netgear"}, {0x28C68E, "Netgear"},
    {0x9C3DCF, "Netgear"}, {0xA040A0, "Netgear"}, {0x0016B6, "Cisco-Linksys"}, {0x001839, "Cisco-Linksys"},
    {0x00040E, "AVM-Fritz"}, {0x3810D5, "AVM-Fritz"}, {0x9CC7A6, "AVM-Fritz"}, {0xC0C1C0, "AVM-Fritz"},
    {0x001915, "SonyMobile"}, {0x0024BE, "Sony"}, {0x30F9ED, "Sony"}, {0xFCF152, "Sony"},
    {0x0005CD, "Denon"}, {0x001788, "Philips-Hue"}, {0xECB5FA, "Philips-Hue"}, {0x001374, "Amazon"},
    {0x00125F, "Asus"}, {0x001BFC, "Asus"}, {0x002618, "Asus"}, {0x04D9F5, "Asus"},
    {0x08606E, "Asus"}, {0x107B44, "Asus"}, {0x1C872C, "Asus"}, {0x2C56DC, "Asus"},
    {0x305A3A, "Asus"}, {0x38D547, "Asus"}, {0x50465D, "Asus"}, {0xBCEE7B, "Asus"},
    {0x00904C, "Epson"}, {0x001E8F, "Canon"}, {0x002673, "Brother"}, {0x0080C8, "D-Link"},
    {0x14D64D, "D-Link"}, {0x1C7EE5, "D-Link"}, {0x28107B, "D-Link"}, {0x340804, "D-Link"},
    {0xC8787D, "Realtek"}, {0x525400, "QEMU-Virt"}, {0x080027, "VirtualBox"}, {0x000C29, "VMware"},
    {0x005056, "VMware"},
};
// clang-format on

static const int OUI_N = sizeof(OUI_TABLE) / sizeof(OUI_TABLE[0]);

const char *ouiVendor(const uint8_t *mac) {
    if (!mac) return nullptr;
    uint32_t oui = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | (uint32_t)mac[2];
    for (int i = 0; i < OUI_N; i++)
        if (OUI_TABLE[i].oui == oui) return OUI_TABLE[i].name;
    return nullptr;
}

bool ouiIsRandom(const uint8_t *mac) {
    if (!mac) return false;
    // "Locally administered" bit (0x02) of the first byte. Multicast bit (0x01)
    // ignored (a legit station always has an even, unicast first byte).
    return (mac[0] & 0x02) != 0;
}

String ouiLabel(const uint8_t *mac) {
    const char *v = ouiVendor(mac);
    if (v) return String(v);
    if (ouiIsRandom(mac)) return String("random");
    return String("?");
}

const char *ouiCategory(const uint8_t *mac) {
    const char *v = ouiVendor(mac);
    if (!v) return "?";
    // Rough grouping by vendor. One maker spans several types (Apple =
    // phone/computer/watch): keep the most likely use.
    if (!strcmp(v, "Apple") || !strcmp(v, "Samsung") || !strcmp(v, "Xiaomi") ||
        !strcmp(v, "Huawei") || !strcmp(v, "Google") || !strcmp(v, "SonyMobile"))
        return "Mobile";
    if (!strcmp(v, "Intel") || !strcmp(v, "Microsoft") || !strcmp(v, "Realtek") ||
        !strcmp(v, "VMware") || !strcmp(v, "VirtualBox") || !strcmp(v, "QEMU-Virt"))
        return "PC";
    if (!strcmp(v, "TP-Link") || !strcmp(v, "Netgear") || !strcmp(v, "Cisco") ||
        !strcmp(v, "Cisco-Linksys") || !strcmp(v, "D-Link") || !strcmp(v, "Asus") ||
        !strcmp(v, "AVM-Fritz"))
        return "Net";
    if (!strcmp(v, "Amazon") || !strcmp(v, "Nest") || !strcmp(v, "Philips-Hue") ||
        !strcmp(v, "Denon") || !strcmp(v, "Sony"))
        return "IoT";
    if (!strcmp(v, "Epson") || !strcmp(v, "Canon") || !strcmp(v, "Brother")) return "Print";
    if (!strcmp(v, "Espressif") || !strcmp(v, "RaspberryPi")) return "Dev";
    return "?";
}
