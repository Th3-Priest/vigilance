#ifndef __VIGILANCE_SWARM_PROTOCOL_H__
#define __VIGILANCE_SWARM_PROTOCOL_H__

#include <stdint.h>

// ============================================================================
//  Vigilance Swarm - ESP-NOW protocol (COPY of the master)
//  MUST stay IDENTICAL to src/modules/others/swarm.h of the Vigilance firmware
//  (same enum values, same fields, SAME struct size). Any change on one side must
//  be mirrored on the other, otherwise master and satellites no longer understand
//  each other.
// ============================================================================

#define VIG_SWARM_MAGIC 0x5647 // "VG"
#define VIG_SWARM_VER 2        // V2: roles, offloaded tasks, alerts, OTA
#define VIG_SWARM_ID 0x2F
#define VIG_SWARM_CHANNEL 1
#define VIG_SWARM_MAX_SATS 16

enum SwarmMsgType : uint8_t {
    SW_DISCOVER = 1,
    SW_HELLO = 2,
    SW_ENROLL = 3,
    SW_HEARTBEAT = 4,
    SW_ASSIGN = 5,
    SW_REPORT = 6,
    SW_STOP = 7,
    SW_ALERT = 8,     // sat -> master: watch alert (#21)
    SW_OTA_BEGIN = 9, // master -> sat: OTA start (#23)
    SW_OTA_DATA = 10, // master -> sat: firmware block (seq = index)
    SW_OTA_END = 11,  // master -> sat: end, validate + reboot
    SW_OTA_ACK = 12,  // sat -> master: OTA ACK (payload[0]=status, seq=expected block)
    SW_CHAT = 13,     // master <-> master: text message (Mesh Whisper #22); the sat ignores it
};

// Tasks (SwarmMsg.task)
#define SW_TASK_IDLE 0
#define SW_TASK_SCAN 1
#define SW_TASK_VEILLE 2  // active watch -> SW_ALERT (#21)
#define SW_TASK_MEASURE 3 // measure RSSI of a target BSSID (#20)
#define SW_TASK_SNIFF 4   // watch EAPOL of a target AP (#19)

// Roles (SwarmMsg.role, #24)
#define SW_ROLE_ANY 0
#define SW_ROLE_WIFI 1
#define SW_ROLE_BLE 2
#define SW_ROLE_SUBGHZ 3 // no sub-GHz on a bare C3: treated as WIFI
#define SW_ROLE_LORA 4   // LoRa relay (#22), needs a module (stub)

// Alert types (SW_ALERT, payload[0])
#define SW_AL_DEAUTH 0
#define SW_AL_DEVICE 1
#define SW_AL_TRACKER 2

#define SW_OTA_CHUNK 200 // firmware bytes per SW_OTA_DATA block

// Payload formats:
//   SW_REPORT/SCAN    : [0..5]=BSSID, [6]=RSSI(int8), [7]=channel, [8..23]=SSID(16)
//   SW_ASSIGN/MEASURE : [0..5]=target BSSID
//   SW_REPORT/MEASURE : [0..5]=BSSID, [6]=RSSI(int8), [7]=found(0/1)
//   SW_ASSIGN/SNIFF   : [0..5]=target BSSID, [6]=channel, [7]=duration(s)
//   SW_REPORT/SNIFF   : [0..5]=BSSID, [6]=EAPOL count, [7]=full handshake(0/1)
//   SW_ALERT          : [0]=type(SW_AL_*), [1]=RSSI(int8), [2..7]=MAC, [8..]=text
//   SW_OTA_BEGIN      : [0..3]=total size (LE), [4..19]=MD5
//   SW_OTA_DATA       : raw firmware, length = min(SW_OTA_CHUNK, remaining), seq=index
//   SW_OTA_ACK        : [0]=status(0=ok,1=err,2=done), seq=next expected block

struct __attribute__((packed)) SwarmMsg {
    uint16_t magic;
    uint8_t ver;
    uint8_t type;
    uint8_t swarmId;
    uint8_t satId;
    uint8_t battery;
    uint8_t task;
    uint8_t role;
    uint8_t channel;
    uint16_t seq;
    char name[16];
    uint8_t payload[SW_OTA_CHUNK];
};

// Guard: an ESP-NOW frame cannot exceed 250 bytes.
static_assert(sizeof(SwarmMsg) <= 250, "SwarmMsg > 250 B: too big for ESP-NOW");

#endif
