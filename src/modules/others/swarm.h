#ifndef __VIGILANCE_SWARM_H__
#define __VIGILANCE_SWARM_H__

#include <stdint.h>

// ============================================================================
//  Vigilance Swarm - ESP-NOW protocol (T-Embed master <-> ESP32 satellites)
//  IMPORTANT: this protocol section must stay IDENTICAL on the satellite side
//  (Vigilance-Satellite project, include/swarm_protocol.h). Any change here must
//  be mirrored there, otherwise the master and satellites no longer understand
//  each other.
// ============================================================================

#define VIG_SWARM_MAGIC 0x5647   // "VG"
#define VIG_SWARM_VER 2          // V2: roles, offloaded tasks, alerts, OTA
#define VIG_SWARM_ID 0x2F        // default swarm id (cyan nod #2F..)
#define VIG_SWARM_CHANNEL 1      // fixed WiFi control channel (ESP-NOW peers)
#define VIG_SWARM_MAX_SATS 16

enum SwarmMsgType : uint8_t {
    SW_DISCOVER = 1,  // master -> broadcast: pairing beacon
    SW_HELLO = 2,     // sat -> master: presence + capabilities + battery
    SW_ENROLL = 3,    // master -> sat: enrolled, here is your satId
    SW_HEARTBEAT = 4, // sat -> master: alive + battery + task
    SW_ASSIGN = 5,    // master -> sat: task (channel, duration) [V2]
    SW_REPORT = 6,    // sat -> master: results [V2]
    SW_STOP = 7,       // master -> sat: stop the task (back to idle)
    SW_ALERT = 8,      // sat -> master: watch alert (#21) -> master journal
    SW_OTA_BEGIN = 9,  // master -> sat: OTA start (payload = size + md5) (#23)
    SW_OTA_DATA = 10,  // master -> sat: firmware block (seq = block index)
    SW_OTA_END = 11,   // master -> sat: end of transfer, validate + reboot
    SW_OTA_ACK = 12,   // sat -> master: OTA ACK (payload[0]=status, seq=expected block)
    SW_CHAT = 13,      // master <-> master: text message (Mesh Whisper #22, payload=text)
};

// Tasks (SwarmMsg.task field, for SW_ASSIGN)
#define SW_TASK_IDLE 0    // passive idle according to role
#define SW_TASK_SCAN 1    // WiFi scan -> one SW_REPORT per network
#define SW_TASK_VEILLE 2  // active watch: reports SW_ALERT (#21)
#define SW_TASK_MEASURE 3 // measure RSSI of a target BSSID (#20 triangulation)
#define SW_TASK_SNIFF 4   // watch EAPOL of a target AP (#19 WIDS capture)

// Roles (SwarmMsg.role field, #24): steer the satellite's default watch.
#define SW_ROLE_ANY 0
#define SW_ROLE_WIFI 1
#define SW_ROLE_BLE 2
#define SW_ROLE_SUBGHZ 3 // no sub-GHz radio on a bare C3: treated as WIFI
#define SW_ROLE_LORA 4   // LoRa relay (#22): needs a LoRa module (stub)

// Alert types (SW_ALERT, payload[0])
#define SW_AL_DEAUTH 0  // deauth flood detected
#define SW_AL_DEVICE 1  // notable new device
#define SW_AL_TRACKER 2 // BLE tracker (BLE role)

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

// Payload sized for OTA; the whole struct stays < 250 (ESP-NOW limit).
struct __attribute__((packed)) SwarmMsg {
    uint16_t magic;   // VIG_SWARM_MAGIC
    uint8_t ver;      // VIG_SWARM_VER
    uint8_t type;     // SwarmMsgType
    uint8_t swarmId;  // VIG_SWARM_ID
    uint8_t satId;    // id assigned to the satellite
    uint8_t battery;  // %
    uint8_t task;     // current task (SW_TASK_*)
    uint8_t role;     // role (SW_ROLE_*)
    uint8_t channel;  // channel for the task
    uint16_t seq;     // counter / OTA block index
    char name[16];    // satellite name / capabilities
    uint8_t payload[SW_OTA_CHUNK];
};

// An ESP-NOW frame cannot exceed 250 bytes: compile-time guard.
static_assert(sizeof(SwarmMsg) <= 250, "SwarmMsg > 250 B: too big for ESP-NOW");

// Master (Vigilance firmware side): opens the Swarm screen, discovers and lists
// live satellites. ESC to quit.
void swarm_setup();

#endif // __VIGILANCE_SWARM_H__
