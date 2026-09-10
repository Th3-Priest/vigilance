#include "core/main_menu.h"
#include <globals.h>

#include "core/bus_HAL.h"
#include "core/module_detect.h"
#include "core/powerSave.h"
#include "core/ram_profile.h"
#include "core/serial_commands/cli.h"
#include "core/utils.h"
#include "core/vig_hud.h"     // HUD palette + vigMix for the boot animation
#include "core/vig_standby.h" // Vigilance standby scene
#include "current_year.h"
#include "esp32-hal-psram.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <functional>
#include <string>
#include <vector>
io_expander ioExpander;
BruceConfig bruceConfig;
BruceConfigPins bruceConfigPins;

SerialCli serialCli;
USBSerial USBserial;
SerialDevice *serialDevice = &USBserial;

StartupApp startupApp;
String startupAppJSInterpreterFile = "";

MainMenu mainMenu;
SPIClass sdcardSPI;
#ifdef USE_HSPI_PORT
#ifndef VSPI
#define VSPI FSPI
#endif
SPIClass AUX_SPI(VSPI);
#else
SPIClass AUX_SPI(HSPI);
#endif

// Navigation Variables
volatile bool NextPress = false;
volatile bool PrevPress = false;
volatile bool UpPress = false;
volatile bool DownPress = false;
volatile bool SelPress = false;
volatile bool EscPress = false;
volatile bool AnyKeyPress = false;
volatile bool NextPagePress = false;
volatile bool PrevPagePress = false;
volatile bool LongPress = false;
volatile bool SerialCmdPress = false;
volatile int forceMenuOption = -1;
volatile uint8_t menuOptionType = 0;
String menuOptionLabel = "";
#ifdef HAS_ENCODER_LED
volatile int EncoderLedChange = 0;
#endif

TouchPoint touchPoint;

keyStroke KeyStroke;

volatile int32_t RotaryNetSteps = 0;

#ifdef HAS_ENCODER
// Default no-op: boards that define HAS_ENCODER but don't implement
// pollEncoder() (shouldn't happen, but keeps the linker happy either way).
void __attribute__((weak)) pollEncoder(void) {}

// Dedicated, high-priority, tight-cadence task that does nothing but sample
// the rotary encoder A/B lines -- mirrors the Flipper port's input_srv,
// which runs encoder_poll() on its own thread every 4ms, decoupled from
// GUI/app work so the raw quadrature read is never delayed by rendering
// or by whether the previous input event has been consumed yet. Only
// exists on HAS_ENCODER boards; other boards pay zero cost for this.
static void taskEncoderPoll(void *parameter) {
    while (true) {
        pollEncoder();
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}
#endif

TaskHandle_t xHandle;
void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        checkPowerSaveTime();
        // Sometimes this task run 2 or more times before looptask,
        // and navigation gets stuck, the idea here is run the input detection
        // if AnyKeyPress is false, or rerun if it was not renewed within 75ms (arbitrary)
        // because AnyKeyPress will be true if didn´t passed through a check(bool var)
        if (!AnyKeyPress || millis() - timer > 75) {
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            // EscPress is intentionally NOT cleared here: it latches until a
            // check(EscPress) consumes it. Otherwise a short back-press during a
            // multi-second blocking scan (BLE Watch, WiFi scan...) is wiped by
            // this 75 ms reset before the module ever gets to test it, and "back"
            // silently fails. loopOptions clears any stale latch on entry and
            // after running a submodule, so this can't cascade into an extra back.
            AnyKeyPress = false;
            SerialCmdPress = false;
            NextPagePress = false;
            PrevPagePress = false;
            touchPoint.pressed = false;
            touchPoint.Clear();
            checkAndRecoverSysI2CBus();
#ifndef USE_TFT_eSPI_TOUCH
            InputHandler();
#endif
            timer = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
// Public Globals Variables
unsigned long previousMillis = millis();
int prog_handler; // 0 - Flash, 1 - LittleFS, 3 - Download
String cachedPassword = "";
int8_t interpreter_state = -1;
bool sdcardMounted = false;
bool gpsConnected = false;

// wifi globals
// TODO put in a namespace
bool wifiConnected = false;
bool isWebUIActive = false;
String wifiIP;

bool BLEConnected = false;
bool returnToMenu;
bool isSleeping = false;
bool isScreenOff = false;
bool dimmer = false;
char timeStr[16];
time_t localTime;
struct tm *timeInfo;
#if defined(HAS_RTC)
#if defined(HAS_RTC_PCF85063A)
pcf85063_RTC _rtc;
#else
cplus_RTC _rtc;
#endif
RTC_TimeTypeDef _time;
RTC_DateTypeDef _date;
bool clock_set = true;
#else
ESP32Time rtc;
bool clock_set = false;
#endif

std::vector<Option> options;
// Protected global variables
#if defined(HAS_SCREEN)
tft_logger tft = tft_logger(); // Invoke custom library
tft_sprite sprite = tft_sprite(&tft);
tft_sprite draw = tft_sprite(&tft);
volatile int tftWidth = TFT_HEIGHT;
#ifdef HAS_TOUCH
volatile int tftHeight =
    TFT_WIDTH - 20; // 20px to draw the TouchFooter(), were the btns are being read in touch devices.
#else
volatile int tftHeight = TFT_WIDTH;
#endif
#else
tft_logger tft;
SerialDisplayClass &sprite = tft;
SerialDisplayClass &draw = tft;
volatile int tftWidth = VECTOR_DISPLAY_DEFAULT_HEIGHT;
volatile int tftHeight = VECTOR_DISPLAY_DEFAULT_WIDTH;
#endif

#include "core/bus_HAL.h"
#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "modules/bjs_interpreter/interpreter.h" // for JavaScript interpreter
#include "modules/others/audio.h"                // for playAudioFile
#include "modules/rf/rf_utils.h"                 // for initCC1101once
#include <Wire.h>

/*********************************************************************
 **  Function: begin_storage
 **  Config LittleFS and SD storage
 *********************************************************************/
void begin_storage() {
    if (!setupLittleFS()) {
        LittleFS.format();
        setupLittleFS();
    }
    RAM_LOG("after LittleFS");
    bool checkFS = setupSdCard();
    bruceConfig.fromFile(checkFS);
    bruceConfigPins.fromFile(checkFS);
}

/*********************************************************************
 **  Function: _setup_gpio()
 **  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
 *********************************************************************/
void _setup_gpio() __attribute__((weak));
void _setup_gpio() {}

/*********************************************************************
 **  Function: _post_setup_gpio()
 **  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
 *********************************************************************/
void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
 **  Function: _pre_storage_gpio()
 **  Sets up a weak (empty) function for board fixes that must run
 **  after the first TFT access and before storage is mounted.
 *********************************************************************/
void _pre_storage_gpio() __attribute__((weak));
void _pre_storage_gpio() {}

/*********************************************************************
 **  Function: setup_gpio
 **  Setup GPIO pins
 *********************************************************************/
void setup_gpio() {

    // init setup from /ports/*/interface.h
    _setup_gpio();

    // Smoochiee v2 uses a AW9325 tro control GPS, MIC, Vibro and CC1101 RX/TX powerlines
    ioExpander.init(IO_EXPANDER_ADDRESS, &Wire);

    initCC1101once(acquireSPIBus(
        bruceConfigPins.CC1101_bus.sck, bruceConfigPins.CC1101_bus.miso, bruceConfigPins.CC1101_bus.mosi
    ));
    // acquireSPIBus() returns nullptr when these pins have no hardware controller left (e.g.
    // ARDUINO_M5STICK_C_PLUS and others that don't share SPI with the display/SD/aux bus);
    // initCC1101once(NULL) lets the driver fall back to managing the default SPI object itself.
}

/*********************************************************************
 **  Function: begin_tft
 **  Config tft
 *********************************************************************/
void begin_tft() {
    tft.setRotation(bruceConfigPins.rotation); // sometimes it misses the first command
    tft.invertDisplay(bruceConfig.colorInverted);
    tft.setRotation(bruceConfigPins.rotation);
    tftWidth = tft.width();
#ifdef HAS_TOUCH
    tftHeight = tft.height() - 20;
#else
    tftHeight = tft.height();
#endif
    resetTftDisplay();
    setBrightness(bruceConfig.bright, false);
}

/*********************************************************************
 **  Function: vigilanceBoot
 **  Vigilance Watch boot: radar sweep + red/blue glitch name + particles
 *********************************************************************/
static void vigilanceBoot() {
    const uint16_t C_CYAN = 0x2DBF; // #2FB7FF
    const uint16_t C_HI = 0x9FFF;   // bright cyan-white
    const uint16_t C_TEAL = 0x0739; // #00E6C8
    const uint16_t C_RING = 0x10AA; // faint cyan
    const uint16_t C_AXIS = 0x0A28; // very faint axes
    const uint16_t C_GRID = 0x08A8; // working grid
    const uint16_t C_BG = bruceConfig.bgColor;

    int W = tftWidth, H = tftHeight;
    float cx = W / 2.0f, cy = H / 2.0f;

    TFT_eSprite spr = TFT_eSprite(tft.native());
    spr.setColorDepth(16);
    bool ok = (spr.createSprite(W, H) != nullptr);
    if (!ok) { // fallback sans sprite (peu de RAM) : ecran statique
        tft.fillScreen(C_BG);
        tft.setTextSize(3);
        tft.setTextColor(C_CYAN, C_BG);
        tft.drawCentreString("VIGILANCE", W / 2, H / 2 - 12, 1);
        tft.setTextSize(1);
        tft.setTextColor(C_TEAL, C_BG);
        tft.drawCentreString("SUB-GHZ  NFC  IR  WIFI  BLE", W / 2, H / 2 + 22, 1);
        delay(1600);
        tft.fillScreen(C_BG);
        return;
    }

    // Lock-on sequence: rings settle, the sweep finds a target and brackets it,
    // then the name resolves cleanly over the radar. Skippable, ~2.6 s.
    const int cix = (int)cx, ciy = (int)cy;
    const float R = H * 0.42f;
    const float tgtA = -38.0f * DEG_TO_RAD;
    const int tx = cix + (int)(cosf(tgtA) * R * 0.6f), ty2 = ciy + (int)(sinf(tgtA) * R * 0.6f);
    const int halfName = 9 * 18 / 2; // "VIGILANCE" at text size 3

    uint32_t t0 = millis();
    while (true) {
        uint32_t e = millis() - t0;
        if (e > 2600) break;
        float sec = e / 1000.0f;
        spr.fillSprite(C_BG);

        for (int x = 0; x < W; x += 16) spr.drawFastVLine(x, 0, H, C_GRID);
        for (int y = 0; y < H; y += 16) spr.drawFastHLine(0, y, W, C_GRID);

        // rings settle in
        for (int r = 0; r < 3; r++) {
            float pop = constrain((sec - r * 0.10f) / 0.42f, 0.0f, 1.0f);
            if (pop <= 0.0f) continue;
            int rad = (int)(R * (0.45f + 0.275f * r) * pop);
            if (rad > 1) {
                spr.drawSmoothArc(cix, ciy, rad, rad - 1, 0, 180, C_RING, C_BG, false);
                spr.drawSmoothArc(cix, ciy, rad, rad - 1, 180, 360, C_RING, C_BG, false);
            }
        }
        spr.drawFastHLine(cix - (int)R, ciy, (int)(2 * R), C_AXIS);
        spr.drawFastVLine(cix, ciy - (int)R, (int)(2 * R), C_AXIS);

        // sweep runs ~1.25 turns and eases onto the target bearing
        if (sec < 1.5f) {
            float prog = constrain((sec - 0.2f) / 1.25f, 0.0f, 1.0f);
            float ang = -1.4f + prog * (1.25f * TWO_PI + (tgtA + 1.4f));
            for (int j = 0; j < 16; j++) {
                float aa = ang - j * 0.055f;
                uint8_t al = (uint8_t)((1.0f - j / 16.0f) * 0.55f * 255);
                spr.drawLine(cix, ciy, cix + (int)(cosf(aa) * R), ciy + (int)(sinf(aa) * R), vigMix(C_TEAL, C_BG, al));
            }
        }

        // target blip + lock brackets once the sweep reaches it
        if (sec > 1.15f) {
            float pl = 0.6f + 0.4f * sinf(sec * 12.0f);
            spr.drawSpot((float)tx, (float)ty2, 2.2f + pl, C_HI);
            uint8_t ba = (uint8_t)(constrain((sec - 1.15f) / 0.3f, 0.0f, 1.0f) * 255);
            uint16_t bc = vigMix(C_CYAN, C_BG, ba);
            const int b = 9;
            spr.drawFastHLine(tx - b, ty2 - b, 4, bc);
            spr.drawFastVLine(tx - b, ty2 - b, 4, bc);
            spr.drawFastHLine(tx + b - 3, ty2 - b, 4, bc);
            spr.drawFastVLine(tx + b, ty2 - b, 4, bc);
            spr.drawFastHLine(tx - b, ty2 + b, 4, bc);
            spr.drawFastVLine(tx - b, ty2 + b - 3, 4, bc);
            spr.drawFastHLine(tx + b - 3, ty2 + b, 4, bc);
            spr.drawFastVLine(tx + b, ty2 + b - 3, 4, bc);
        }

        // name resolves (fade in) with a bright scan line passing once
        if (sec > 1.2f) {
            float g = constrain((sec - 1.2f) / 0.5f, 0.0f, 1.0f);
            spr.setTextSize(3);
            spr.setTextColor(vigMix(C_CYAN, C_BG, (uint8_t)(g * 255)));
            spr.drawCentreString("VIGILANCE", W / 2, ciy - 12, 1);
            if (g < 1.0f)
                spr.drawFastVLine(W / 2 - halfName + (int)(2 * halfName * g), ciy - 14, 26, C_HI);
        }

        // subtitle
        if (sec > 1.7f) {
            uint8_t sa = (uint8_t)(constrain((sec - 1.7f) / 0.5f, 0.0f, 1.0f) * 255);
            spr.setTextSize(1);
            spr.setTextColor(vigMix(C_TEAL, C_BG, sa));
            spr.drawCentreString("DEFENSIVE SURVEILLANCE PLATFORM", W / 2, ciy + 16, 1);
        }

        spr.pushSprite(0, 0);
        if (check(AnyKeyPress)) break;
        delay(16);
    }
    spr.deleteSprite();
    tft.fillScreen(C_BG);
}

/*********************************************************************
 **  Function: boot_screen
 **  Draw boot screen
 *********************************************************************/
void boot_screen() {
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.drawPixel(0, 0, bruceConfig.bgColor);
    tft.drawCentreString("VIGILANCE", tftWidth / 2, 10, 1);
    tft.setTextSize(FP);
    tft.drawCentreString(BRUCE_VERSION, tftWidth / 2, 25, 1);
    tft.setTextSize(FM);
    tft.drawCentreString(
        "VIGILANCE FIRMWARE", tftWidth / 2, tftHeight + 2, 1
    ); // will draw outside the screen on non touch devices
}

/*********************************************************************
 **  Function: boot_screen_anim
 **  Draw boot screen
 *********************************************************************/
void boot_screen_anim() {
    // Vigilance: boot anime par defaut, sauf si une image de boot custom existe
    {
        int _bimg = 0;
        if (sdcardMounted && (SD.exists("/boot.jpg") || SD.exists("/boot.gif"))) _bimg = 1;
        if (_bimg == 0 && (LittleFS.exists("/boot.jpg") || LittleFS.exists("/boot.gif"))) _bimg = 1;
        if (bruceConfig.theme.boot_img) _bimg = 1;
        if (_bimg == 0) {
            vigilanceBoot();
            return;
        }
    }
    boot_screen();
    int i = millis();
    // checks for boot.jpg in SD and LittleFS for customization
    int boot_img = 0;
    bool drawn = false;
    if (sdcardMounted) {
        if (SD.exists("/boot.jpg")) boot_img = 1;
        else if (SD.exists("/boot.gif")) boot_img = 3;
    }
    if (boot_img == 0 && LittleFS.exists("/boot.jpg")) boot_img = 2;
    else if (boot_img == 0 && LittleFS.exists("/boot.gif")) boot_img = 4;
    if (bruceConfig.theme.boot_img) boot_img = 5; // override others

    tft.drawPixel(0, 0, 0);       // Forces back communication with TFT, to avoid ghosting
                                  // Start image loop
    while (millis() < i + 7000) { // boot image lasts for 5 secs
        if ((millis() - i > 2000) && !drawn) {
            tft.fillRect(0, 45, tftWidth, tftHeight - 45, bruceConfig.bgColor);
            if (boot_img > 0 && !drawn) {
                tft.fillScreen(bruceConfig.bgColor);
                if (boot_img == 5) {
                    drawImg(
                        *bruceConfig.themeFS(),
                        bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_img),
                        0,
                        0,
                        true,
                        3600
                    );
                    Serial.println("Image from SD theme");
                } else if (boot_img == 1) {
                    drawImg(SD, "/boot.jpg", 0, 0, true);
                    Serial.println("Image from SD");
                } else if (boot_img == 2) {
                    drawImg(LittleFS, "/boot.jpg", 0, 0, true);
                    Serial.println("Image from LittleFS");
                } else if (boot_img == 3) {
                    drawImg(SD, "/boot.gif", 0, 0, true, 3600);
                    Serial.println("Image from SD");
                } else if (boot_img == 4) {
                    drawImg(LittleFS, "/boot.gif", 0, 0, true, 3600);
                    Serial.println("Image from LittleFS");
                }
                tft.drawPixel(0, 0, 0); // Forces back communication with TFT, to avoid ghosting
            }
            drawn = true;
        }
#if !defined(LITE_VERSION)
        if (!boot_img && (millis() - i > 2200) && (millis() - i) < 2700)
            tft.drawRect(2 * tftWidth / 3, tftHeight / 2, 2, 2, bruceConfig.priColor);
        if (!boot_img && (millis() - i > 2700) && (millis() - i) < 2900)
            tft.fillRect(0, 45, tftWidth, tftHeight - 45, bruceConfig.bgColor);
        if (!boot_img && (millis() - i > 2900) && (millis() - i) < 3400)
            tft.drawXBitmap(
                2 * tftWidth / 3 - 30,
                5 + tftHeight / 2,
                bruce_small_bits,
                bruce_small_width,
                bruce_small_height,
                bruceConfig.bgColor,
                bruceConfig.priColor
            );
        if (!boot_img && (millis() - i > 3400) && (millis() - i) < 3600) tft.fillScreen(bruceConfig.bgColor);
        if (!boot_img && (millis() - i > 3600))
            tft.drawXBitmap(
                (tftWidth - 238) / 2,
                (tftHeight - 133) / 2,
                bits,
                bits_width,
                bits_height,
                bruceConfig.bgColor,
                bruceConfig.priColor
            );
#endif
        if (check(AnyKeyPress)) // If any key or M5 key is pressed, it'll jump the boot screen
        {
            tft.fillScreen(bruceConfig.bgColor);
            delay(10);
            return;
        }
    }

    // Clear splashscreen
    tft.fillScreen(bruceConfig.bgColor);
}

/*********************************************************************
 **  Function: init_clock
 **  Clock initialisation for propper display in menu
 *********************************************************************/
void init_clock() {
#if defined(HAS_RTC)
    _rtc.begin();
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
    _rtc.GetTime(&_time);
    _rtc.GetDate(&_date);

    struct tm timeinfo = {};
    timeinfo.tm_sec = _time.Seconds;
    timeinfo.tm_min = _time.Minutes;
    timeinfo.tm_hour = _time.Hours;
    timeinfo.tm_mday = _date.Date;
    timeinfo.tm_mon = _date.Month > 0 ? _date.Month - 1 : 0;
    timeinfo.tm_year = _date.Year >= 1900 ? _date.Year - 1900 : 0;
    time_t epoch = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, nullptr);
#else
    struct tm timeinfo = {};
    timeinfo.tm_year = CURRENT_YEAR - 1900;
    timeinfo.tm_mon = 0x05;
    timeinfo.tm_mday = 0x14;
    time_t epoch = mktime(&timeinfo);
    rtc.setTime(epoch);
    clock_set = true;
    struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, nullptr);
    restorePersistedClock(); // override the default with the last-saved time (NVS) + start periodic save
#endif
}

/*********************************************************************
 **  Function: init_led
 **  Led initialisation
 *********************************************************************/
void init_led() {
#ifdef HAS_RGB_LED
    beginLed();
#endif
}

/*********************************************************************
 **  Function: startup_sound
 **  Play sound or tone depending on device hardware
 *********************************************************************/
void startup_sound() {
    if (bruceConfig.soundEnabled == 0) return; // if sound is disabled, do not play sound
#if !defined(LITE_VERSION)
#if defined(BUZZ_PIN)
    // Bip M5 just because it can. Does not bip if splashscreen is bypassed
    _tone(5000, 50);
    delay(200);
    _tone(5000, 50);
    /*  2fix: menu infinite loop */
#elif defined(HAS_NS4168_SPKR)
    // play a boot sound
    if (bruceConfig.theme.boot_sound) {
        playAudioFile(bruceConfig.themeFS(), bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_sound));
    } else if (SD.exists("/boot.wav")) {
        playAudioFile(&SD, "/boot.wav");
    } else if (LittleFS.exists("/boot.wav")) {
        playAudioFile(&LittleFS, "/boot.wav");
    }
#endif
#endif
}

/*********************************************************************
 **  Function: setup
 **  Where the devices are started and variables set
 *********************************************************************/
void setup() {
    Serial.setRxBufferSize(
        SAFE_STACK_BUFFER_SIZE / 4
    ); // Must be invoked before Serial.begin(). Default is 256 chars
    Serial.begin(115200);

    log_d("Total heap: %d", ESP.getHeapSize());
    log_d("Free heap: %d", ESP.getFreeHeap());
    bool psramStarted = psramInit();
    // Printed unconditionally (boards force CORE_DEBUG_LEVEL=1, so log_d is invisible).
    // If PSRAM fails to init, a PSRAM board effectively becomes a no-PSRAM board and
    // Wi-Fi + BLE cannot coexist. This one boot line makes that failure mode observable.
    Serial.printf(
        "[PSRAM] init=%d found=%d total=%u free=%u | internal free=%u largest=%u\n",
        psramStarted,
        psramFound(),
        (unsigned)ESP.getPsramSize(),
        (unsigned)ESP.getFreePsram(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
    );
    Serial.flush();

    RAM_LOG("setup-start");

    // declare variables
    prog_handler = 0;
    sdcardMounted = false;
    wifiConnected = false;
    BLEConnected = false;
    bruceConfig.bright = 100; // theres is no value yet
    bruceConfigPins.rotation = ROTATION;
    setup_gpio();
#if defined(HAS_SCREEN)
    tft.init();
    tft.setRotation(bruceConfigPins.rotation);
    tft.fillScreen(TFT_BLACK);
    // bruceConfig is not read yet.. just to show something on screen due to long boot time
    tft.setTextColor(TFT_PURPLE, TFT_BLACK);
    tft.drawCentreString("Booting", tft.width() / 2, tft.height() / 2, 1);
    RAM_LOG("first-display-elem"); // first element drawn on screen
#else
    tft.begin();
#endif
    _pre_storage_gpio();
    begin_storage();
    RAM_LOG("after-storage"); // bruceConfig/bruceConfigPins loaded from FS
    begin_tft();
    init_clock();
    init_led();
    RAM_LOG("after-tft-clock-led");

    // Vigilance: detect plug-in modules (I2C) at boot
    detectModulesAtBoot();

    options.reserve(20); // preallocate some options space to avoid fragmentation

    RAM_LOG("before-wifi-init"); // largest contiguous internal block here gates Wi-Fi/BLE

    // Set WiFi country to avoid warnings and ensure max power
    const wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 14,
#ifdef CONFIG_ESP_PHY_MAX_TX_POWER
        .max_tx_power = CONFIG_ESP_PHY_MAX_TX_POWER, // 20
#endif
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };

    esp_wifi_set_max_tx_power(80); // 80 translates to 20dBm
    esp_wifi_set_country(&country);

    // Some GPIO Settings (such as CYD's brightness control must be set after tft and sdcard)
    _post_setup_gpio();
    // Some board interfaces initialize or reset the backlight in post-setup,
    // so re-apply the stored brightness after that stage completes.
    setBrightness(bruceConfig.bright, false);
    // end of post gpio begin

    // #ifndef USE_TFT_eSPI_TOUCH
    // This task keeps running all the time, will never stop
    xTaskCreate(
        taskInputHandler,              // Task function
        "InputHandler",                // Task Name
        INPUT_HANDLER_TASK_STACK_SIZE, // Stack size
        NULL,                          // Task parameters
        2,                             // Task priority (0 to 3), loopTask has priority 2.
        &xHandle                       // Task handle (not used)
    );
#ifdef HAS_ENCODER
    // Dedicated encoder sampling task, higher priority than loopTask so a
    // busy render/redraw pass can never delay reading the A/B lines.
    // Only created on boards with a rotary encoder.
    xTaskCreate(
        taskEncoderPoll, // Task function
        "EncoderPoll",   // Task Name
        2048,            // Stack size
        NULL,            // Task parameters
        3,               // Task priority (0 to 3), higher than loopTask's 2
        NULL             // Task handle (not used)
    );
#endif
    // #endif
#if defined(HAS_SCREEN)
    bruceConfig.openThemeFile(bruceConfig.themeFS(), bruceConfig.themePath, false);
    if (!bruceConfig.instantBoot) {
        boot_screen_anim();
        startup_sound();
    }
    if (bruceConfig.wifiAtStartup) {
        log_i("Loading Wifi at Startup");
        xTaskCreate(
            wifiConnectTask,   // Task function
            "wifiConnectTask", // Task Name
            4096,              // Stack size
            NULL,              // Task parameters
            2,                 // Task priority (0 to 3), loopTask has priority 2.
            NULL               // Task handle (not used)
        );
    }
#endif
    //  start a task to handle serial commands while the webui is running
    startSerialCommandsHandlerTask(true);

    wakeUpScreen();
    if (bruceConfig.startupApp != "" && !startupApp.startApp(bruceConfig.startupApp)) {
        bruceConfig.setStartupApp("");
    }

    RAM_LOG("setup-end");
}

/**********************************************************************
 **  Function: loop
 **  Main loop
 **********************************************************************/
#if defined(HAS_SCREEN)
void loop() {
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    if (interpreter_state > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        interpreter_state = 2;
        Serial.println("Entering interpreter...");
        while (interpreter_state > 0) { vTaskDelay(pdMS_TO_TICKS(500)); }
        if (interpreter_state == 0) {
            Serial.println("Interpreter put to background.");
        } else {
            Serial.println("Exiting interpreter...");
        }
        if (interpreter_state == -1) { interpreterTaskHandler = NULL; }
        previousMillis = millis(); // ensure that will not dim screen when get back to menu
    }
#endif
    tft.fillScreen(bruceConfig.bgColor);

#if defined(ENABLE_RAM_LOGGING)
    static bool ramLoggedFirstMenu = false;
    if (!ramLoggedFirstMenu) {
        RAM_LOG("first-mainMenu");
        ramLoggedFirstMenu = true;
    }
#endif

    mainMenu.begin();
    delay(1);
}
#else

void loop() {
    tft.setLogging();
    Serial.println(
        "\n"
        "██████  ██████  ██    ██  ██████ ███████ \n"
        "██   ██ ██   ██ ██    ██ ██      ██      \n"
        "██████  ██████  ██    ██ ██      █████   \n"
        "██   ██ ██   ██ ██    ██ ██      ██      \n"
        "██████  ██   ██  ██████   ██████ ███████ \n"
        "                                         \n"
        "         PREDATORY FIRMWARE\n\n"
        "Tips: Connect to the WebUI for better experience\n"
        "      Add your network by sending: wifi add ssid password\n\n"
        "At your command:"
    );

    // Enable navigation through webUI
    tft.fillScreen(bruceConfig.bgColor);
    mainMenu.begin();
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
#endif
