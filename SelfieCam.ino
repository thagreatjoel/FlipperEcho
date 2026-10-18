/*
 * ESP32-C3 SuperMini — DuckyMini
 * Flipper Zero theme.
 * Fix: poll ESP-NOW during WIFI_CONNECTING, disable modem sleep.
 */

#include <SPI.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Adafruit_GFX.h>

#include "types.h"
#include "theme.h"
#include "src/image_passport_happy1_bits.h"
#include "src/Org_01.h"
#include "src/Petme8x8.h"
#include "src/Picopixel.h"
#include "src/icons8_loading.h"
#include "src/image_Ble_connected_bits.h"
#include "src/image_download__2__bits.h"
#include "src/image_External_ant_1_bits.h"
#include "src/image_folder_file_bits.h"
#include "src/image_menu_settings_gear_bits.h"
#include "src/image_usb_cable_connected_bits.h"
#include "src/image_SDQuestion_bits.h"
#include "src/image_Alert_bits.h"
#include "src/image_download__1__bits.h"

#include "menu.h"
#include "espnow_link.h"
#include "ntp.h"
#include "keyboard.h"

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clockSprite = TFT_eSprite(&tft);

#define BTN_SCREEN    3
#define BTN_SELECT    5
#define BACKLIGHT_PIN 2
#define VIBE_PIN      1
#define VIBE_MS       35
#define LED_PIN       9
#define LED_ON        HIGH
#define LED_OFF       LOW
#define LONG_PRESS_MS 500
#define DOUBLE_CLICK_MS 180
#define DEBOUNCE_MS   15

#define AP_SSID       "DuckyMini"
#define AP_PASS       "12345678"

#define FRAME_W        160
#define FRAME_H        120
#define ROW_STRETCH    128

uint16_t* streamBuf = nullptr;
uint16_t* tftBuf    = nullptr;

Mode currentMode = MODE_BOOT;
LoadPurpose loadPurpose = LOAD_FOR_PREVIEW;

int  menuTile      = 0;
int  wifiOptSel    = 0;
int  g_netSelected = 0;
int  camModeSelected = 0;

bool g_wifiEnabled  = false;
const char* g_wifiModeStr = "AP+STA";
bool g_wifiOnline   = false;
bool g_wifiApActive = false;

bool popupYes              = false;
uint32_t bootStart         = 0;
int      bootPhase         = 0;
uint32_t lastFrameTime     = 0;
uint32_t framesThisSec     = 0;
uint32_t lastFpsCalc       = 0;
uint32_t notifyUntil       = 0;
uint32_t diagRefresh       = 0;
Mode     notifyReturnMode  = MODE_MENU;
int      notifyReturnTile  = 0;
int      lastLoaderFrame   = -1;

bool sdPresent       = false;
uint32_t alertShowUntil = 0;
Mode     alertReturnMode = MODE_MENU;
int      alertReturnTile = 0;

bool     flashMode    = false;
uint32_t captureStart = 0;

String   wifi_ssid        = "";
String   wifi_psk         = "";
uint32_t wifiConnectStart = 0;
bool     wifiConnecting   = false;

String   camIP            = "";
bool     miniJoinedWifi   = false;
bool     camConfirmed     = false;

uint32_t lastWhereMs      = 0;

#define MAX_WIFI_NETWORKS   16
#define WIFI_LIST_VISIBLE   5
String   wifiScanSSIDs[MAX_WIFI_NETWORKS];
int      wifiScanCount    = 0;
int      wifiScanSelected = 0;
int      wifiScanTop      = 0;

static uint32_t scanLastShortMs = 0;
static bool     scanHadShort    = false;

bool btn3LastReading = HIGH;
uint32_t btn3PressStart = 0;
bool btn3Held = false;
bool btn3LongPressTriggered = false;
uint32_t btn3LastReleaseMs = 0;
bool btn3PendingDouble = false;

bool btn5LastReading = HIGH;
uint32_t btn5PressStart = 0;
bool btn5Held = false;
bool btn5LongPressTriggered = false;
uint32_t btn5LastReleaseMs = 0;
bool btn5PendingDouble = false;

uint32_t vibeOffAt   = 0;
uint32_t ledPulseEnd = 0;
bool fatalError = false;
uint32_t fatalBlinkNext = 0;
bool fatalBlinkState = false;

static volatile uint32_t netActivityUntil = 0;

static inline void vibeKick() {
    digitalWrite(VIBE_PIN, HIGH);
    vibeOffAt = millis() + VIBE_MS;
}
static inline void vibeService() {
    if (vibeOffAt && (int32_t)(millis() - vibeOffAt) >= 0) {
        digitalWrite(VIBE_PIN, LOW);
        vibeOffAt = 0;
    }
}
static inline void ledPulseKick(uint16_t ms) {
    digitalWrite(LED_PIN, LED_ON);
    ledPulseEnd = millis() + ms;
}
static inline void beepBeep(int n) {
    vibeOffAt = 0;
    digitalWrite(VIBE_PIN, LOW);
    for (int i = 0; i < n; i++) {
        digitalWrite(VIBE_PIN, HIGH); delay(35);
        digitalWrite(VIBE_PIN, LOW);  delay(55);
    }
    vibeOffAt = 0;
}
static inline void disableWifiSleep() {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void led_update() {
    uint32_t now = millis();
    if (fatalError) {
        if ((int32_t)(now - fatalBlinkNext) >= 0) {
            fatalBlinkState = !fatalBlinkState;
            digitalWrite(LED_PIN, fatalBlinkState ? LED_ON : LED_OFF);
            fatalBlinkNext = now + 80;
        }
        return;
    }
    if (ledPulseEnd) {
        if ((int32_t)(now - ledPulseEnd) < 0) {
            digitalWrite(LED_PIN, LED_ON);
            return;
        }
        ledPulseEnd = 0;
    }
    switch (currentMode) {
        case MODE_BOOT: {
            if (bootPhase == 0) digitalWrite(LED_PIN, ((now / 400) & 1) ? LED_ON : LED_OFF);
            else if (bootPhase == 1) digitalWrite(LED_PIN, ((now / 150) & 1) ? LED_ON : LED_OFF);
            else digitalWrite(LED_PIN, LED_OFF);
            break;
        }
        case MODE_WIFI_OPTIONS:
        case MODE_NETWORK:
        case MODE_WIFI_SCAN_LIST:
            digitalWrite(LED_PIN, ((now / 500) & 1) ? LED_ON : LED_OFF);
            break;
        case MODE_KB_SSID:
        case MODE_KB_PSK:
            digitalWrite(LED_PIN, LED_OFF);
            break;
        case MODE_WIFI_CONNECTING:
            digitalWrite(LED_PIN, ((now / 100) & 1) ? LED_ON : LED_OFF);
            break;
        case MODE_ALERT: {
            uint32_t p = now % 1000;
            bool on = (p < 60) || (p >= 120 && p < 180) || (p >= 240 && p < 300);
            digitalWrite(LED_PIN, on ? LED_ON : LED_OFF);
            break;
        }
        case MODE_SD_MISSING: {
            uint32_t p = now % 500;
            digitalWrite(LED_PIN, (p < 80) ? LED_ON : LED_OFF);
            break;
        }
        default: {
            if (g_wifiOnline) {
                if ((int32_t)(now - netActivityUntil) < 0) {
                    digitalWrite(LED_PIN, LED_ON);
                } else {
                    digitalWrite(LED_PIN, ((now / 1000) & 1) ? LED_ON : LED_OFF);
                }
            } else {
                digitalWrite(LED_PIN, LED_OFF);
            }
            break;
        }
    }
}

static void led_powerOnBlip() {
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LED_ON); delay(80);
        digitalWrite(LED_PIN, LED_OFF); delay(80);
    }
}

void updateWifiState() {
    wifi_mode_t m = WiFi.getMode();
    bool nowOnline = (WiFi.status() == WL_CONNECTED) ||
                     (WiFi.softAPgetStationNum() > 0);
    bool nowAp     = (m == WIFI_AP) || (m == WIFI_AP_STA);
    if (nowOnline != g_wifiOnline || nowAp != g_wifiApActive) {
        g_wifiOnline   = nowOnline;
        g_wifiApActive = nowAp;
        if (currentMode == MODE_MENU) {
            menu_statusPatch(g_wifiOnline, g_wifiApActive);
        }
    }
}

void drawFlashIndicator() {
    uint16_t col  = flashMode ? T_ORANGE : T_DIM;
    uint16_t bord = flashMode ? T_ORANGE : T_BORDER;
    tft.fillRect(146, 2, 13, 14, T_BG);
    tft.drawRect(146, 2, 13, 14, bord);
    tft.drawBitmap(148, 4, image_Charging_lightning_bits, 9, 10, col);
}

bool jpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= FRAME_H || x >= FRAME_W) return 1;
    if (x + w > FRAME_W) w = FRAME_W - x;
    if (y + h > FRAME_H) h = FRAME_H - y;
    for (int row = 0; row < h; row++)
        memcpy(streamBuf + (y + row) * FRAME_W + x, bitmap + row * w, w * 2);
    return 1;
}

void blitFrame() {
    for (int dstY = 0; dstY < ROW_STRETCH; dstY++) {
        int srcY = (dstY * FRAME_H) / ROW_STRETCH;
        if (srcY >= FRAME_H) srcY = FRAME_H - 1;
        memcpy(tftBuf + dstY * FRAME_W, streamBuf + srcY * FRAME_W, FRAME_W * 2);
    }
    tft.setSwapBytes(true);
    tft.pushImage(0, 0, FRAME_W, ROW_STRETCH, tftBuf);
    if (currentMode == MODE_PREVIEW) drawFlashIndicator();
}

void showAlert(const char* msg, Mode returnTo, int returnTile) {
    menu_drawAlert(msg);
    currentMode = MODE_ALERT;
    alertShowUntil = millis() + 2000;
    alertReturnMode = returnTo;
    alertReturnTile = returnTile;
}

void applyApVisibility(bool visible) {
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_CHANNEL, !visible);
    disableWifiSleep();
}
void network_applyWifiEnable(bool on) {
    g_wifiEnabled = on;
    applyApVisibility(on);
    updateWifiState();
}
void network_actionTest() {
    Serial.printf("[NET] AP stations = %d\n", WiFi.softAPgetStationNum());
}

void network_scanWifiAndShow() {
    menu_drawLoader();
    tft.startWrite();
    tft.setTextColor(T_ORANGE, T_BG);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("Scanning...", 80, 90, 1);
    tft.endWrite();
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
    int lastFrame     = -1;
    uint32_t lastBeep = 0;
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        menu_pumpLoader(&lastFrame);
        if (millis() - lastBeep > 700) { lastBeep = millis(); beepBeep(2); }
        delay(2);
    }
    int n = WiFi.scanComplete();
    wifiScanCount = 0; wifiScanSelected = 0; wifiScanTop = 0;
    if (n > 0) {
        for (int i = 0; i < n && wifiScanCount < MAX_WIFI_NETWORKS; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            bool dup = false;
            for (int j = 0; j < wifiScanCount; j++)
                if (wifiScanSSIDs[j] == ssid) { dup = true; break; }
            if (dup) continue;
            wifiScanSSIDs[wifiScanCount++] = ssid;
        }
    }
    WiFi.scanDelete();
    Serial.printf("[WIFI] scan found %d networks\n", wifiScanCount);
    currentMode = MODE_WIFI_SCAN_LIST;
    scanHadShort = false;
    beepBeep(1);
    menu_drawWifiScanList(wifiScanSSIDs, wifiScanCount, wifiScanSelected, wifiScanTop);
}

void wifiScanMoveSelection(int delta) {
    if (wifiScanCount == 0) return;
    int old = wifiScanSelected;
    wifiScanSelected += delta;
    if (wifiScanSelected < 0)              wifiScanSelected = wifiScanCount - 1;
    if (wifiScanSelected >= wifiScanCount) wifiScanSelected = 0;
    if (wifiScanSelected == old) return;
    if (wifiScanSelected < wifiScanTop) wifiScanTop = wifiScanSelected;
    if (wifiScanSelected >= wifiScanTop + WIFI_LIST_VISIBLE)
        wifiScanTop = wifiScanSelected - WIFI_LIST_VISIBLE + 1;
    menu_drawWifiScanList(wifiScanSSIDs, wifiScanCount, wifiScanSelected, wifiScanTop);
}

void wifiScanSelectCurrent() {
    if (wifiScanCount == 0) return;
    wifi_ssid = wifiScanSSIDs[wifiScanSelected];
    wifi_psk = "";
    Serial.printf("[WIFI] selected SSID='%s'\n", wifi_ssid.c_str());
    currentMode = MODE_KB_PSK;
    kb_init("PSK:");
    beepBeep(2);
}

void network_sendWifiCreds() {
    wifi_psk = kb_getText();
    Serial.printf("[WIFI] PSK='%s'\n", wifi_psk.c_str());
    tft.startWrite();
    tft.fillScreen(T_BG);
    tft.fillRect(0, 0, 160, 18, T_SURFACE);
    tft.drawFastHLine(0, 18, 160, T_BORDER);
    tft.setTextColor(T_ORANGE, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("Connecting", 80, 5, 1);
    tft.setTextColor(T_ORANGE, T_BG);
    tft.drawCentreString("Please wait...", 80, 55, 1);
    String s = wifi_ssid;
    if (s.length() > 18) s = s.substring(0, 18);
    tft.setTextColor(T_TEXT, T_BG);
    tft.drawCentreString(s.c_str(), 80, 75, 1);
    tft.endWrite();
    camConfirmed = false;
    miniJoinedWifi = false;
    String cmd = "WIFI_SSID=" + wifi_ssid + " WIFI_PSK=" + wifi_psk;
    for (int i = 0; i < 2; i++) {
        Serial.printf("[MINI] sending creds #%d\n", i + 1);
        espnow_sendCommand(cmd.c_str());
        delay(300);
    }
    delay(200);
    Serial.println("[MINI] starting parallel STA join...");
    WiFi.begin(wifi_ssid.c_str(), wifi_psk.c_str());
    wifiConnecting = true;
    wifiConnectStart = millis();
    lastWhereMs = 0;
    currentMode = MODE_WIFI_CONNECTING;
    vibeKick();
}

static char kb_buttonGesture3() {
    bool reading = digitalRead(BTN_SCREEN);
    uint32_t now = millis();
    char result = '-';
    if (reading == LOW && btn3LastReading == HIGH) {
        btn3PressStart = now; btn3Held = true; btn3LongPressTriggered = false;
    } else if (reading == HIGH && btn3LastReading == LOW) {
        btn3Held = false;
        uint32_t held = now - btn3PressStart;
        if (held < DEBOUNCE_MS) { btn3LastReading = reading; return result; }
        if (!btn3LongPressTriggered && held < LONG_PRESS_MS) {
            if (btn3PendingDouble) { btn3PendingDouble = false; result = 'D'; }
            else { btn3PendingDouble = true; btn3LastReleaseMs = now; }
        }
    } else if (btn3Held && !btn3LongPressTriggered &&
               (now - btn3PressStart >= LONG_PRESS_MS)) {
        btn3LongPressTriggered = true; result = 'H';
    }
    if (btn3PendingDouble && (now - btn3LastReleaseMs > DOUBLE_CLICK_MS)) {
        btn3PendingDouble = false; result = 'S';
    }
    btn3LastReading = reading;
    return result;
}

static char kb_buttonGesture5() {
    bool reading = digitalRead(BTN_SELECT);
    uint32_t now = millis();
    char result = '-';
    if (reading == LOW && btn5LastReading == HIGH) {
        btn5PressStart = now; btn5Held = true; btn5LongPressTriggered = false;
    } else if (reading == HIGH && btn5LastReading == LOW) {
        btn5Held = false;
        uint32_t held = now - btn5PressStart;
        if (held < DEBOUNCE_MS) { btn5LastReading = reading; return result; }
        if (!btn5LongPressTriggered && held < LONG_PRESS_MS) {
            if (btn5PendingDouble) { btn5PendingDouble = false; result = 'D'; }
            else { btn5PendingDouble = true; btn5LastReleaseMs = now; }
        }
    } else if (btn5Held && !btn5LongPressTriggered &&
               (now - btn5PressStart >= LONG_PRESS_MS)) {
        btn5LongPressTriggered = true; result = 'H';
    }
    if (btn5PendingDouble && (now - btn5LastReleaseMs > DOUBLE_CLICK_MS)) {
        btn5PendingDouble = false; result = 'S';
    }
    btn5LastReading = reading;
    return result;
}

static void kb_handleBtns() {
    char g3 = kb_buttonGesture3();
    char g5 = kb_buttonGesture5();
    if (g3 == 'S') { vibeKick(); kb_moveRight(); }
    if (g3 == 'D') { vibeKick(); kb_moveLeft(); }
    if (g3 == 'H') { vibeKick(); kb_cancel(); }
    if (g5 == 'S') { vibeKick(); kb_moveDown(); }
    if (g5 == 'D') { vibeKick(); kb_moveUp(); }
    if (g5 == 'H') {
        vibeKick();
        int r = kb_select();
        if (r == 1) {
            if (currentMode == MODE_KB_PSK) network_sendWifiCreds();
            else if (currentMode == MODE_KB_SSID) { currentMode = MODE_KB_PSK; kb_init("PSK:"); }
        }
    }
}

void processEspNowText(const String& cmd) {
    if (cmd == "WIFI_OK" || cmd.startsWith("WIFI_OK ")) {
        String ip = (cmd.length() > 8) ? cmd.substring(8) : "?";
        ip.trim();
        if (ip.length() > 0 && ip != "?" && ip != "0.0.0.0") {
            camIP = ip;
            camConfirmed = true;
            Serial.printf("[CAM IP confirmed] %s\n", camIP.c_str());
        }
    }
    else if (cmd == "WIFI_FAIL") {
        wifiConnecting = false;
        for (int i = 0; i < 3; i++) { vibeKick(); delay(120); }
        menu_showNotification("FAILED", "check credentials", T_ORANGE, 2000);
        currentMode = MODE_NOTIFY;
        notifyUntil = millis() + 2000;
        notifyReturnMode = MODE_NETWORK;
        notifyReturnTile = -1;
    }
    else if (cmd.startsWith("PING")) {
        int sp = cmd.indexOf(' ');
        if (sp > 0) {
            String ip = cmd.substring(sp + 1);
            ip.trim();
            if (ip.length() > 0 && ip != "0.0.0.0" && ip != camIP) {
                camIP = ip;
                camConfirmed = true;
                Serial.printf("[CAM IP from PING] %s\n", camIP.c_str());
            }
        }
    }
    else if (cmd == "PHOTO SAVED") {
        if (currentMode == MODE_CAPTURE) {
            espnow_sendCommand("START_STREAM");
            currentMode = MODE_PREVIEW;
            lastFrameTime = millis();
        } else {
            menu_showNotification("SAVED!", "check gallery", T_ORANGE, 1500, MODE_PREVIEW);
        }
    }
    else if (cmd == "CAPTURE FAILED") {
        if (currentMode == MODE_CAPTURE) {
            espnow_sendCommand("START_STREAM");
            currentMode = MODE_PREVIEW;
            lastFrameTime = millis();
        } else {
            showAlert("Error 103", MODE_MENU, menuTile);
        }
    }
}

void handleBtn3ShortPress() {
    vibeKick();
    if (currentMode == MODE_MENU) {
        int old = menuTile;
        menuTile = (menuTile + 1) % 6;
        menu_redrawPatch(old, menuTile);
    }
    else if (currentMode == MODE_WIFI_OPTIONS) {
        int old = wifiOptSel;
        wifiOptSel = (wifiOptSel + 1) % 2;
        menu_wifiOptionsRedraw(old, wifiOptSel);
        ledPulseKick(40);
    }
    else if (currentMode == MODE_NETWORK) {
        int old = g_netSelected;
        g_netSelected = (g_netSelected + 1) % NET_ROW_COUNT;
        menu_networkPatch(old, g_netSelected);
        ledPulseKick(40);
    }
    else if (currentMode == MODE_CAMERA_MODES) {
        int old = camModeSelected;
        camModeSelected = (camModeSelected + 1) % CAM_MODE_COUNT;
        menu_cameraModesPatch(old, camModeSelected, flashMode);
        ledPulseKick(40);
    }
    else if (currentMode == MODE_WIFI_SCAN_LIST) {
        wifiScanMoveSelection(+1);
    }
    else if (currentMode == MODE_POPUP) {
        popupYes = !popupYes;
        menu_drawPopupPatch(popupYes);
    }
    else if (currentMode == MODE_PREVIEW) {
        flashMode = !flashMode;
        espnow_sendCommand(flashMode ? "FLASH_ON" : "FLASH_OFF");
        drawFlashIndicator();
    }
    else if (currentMode == MODE_DIAG) {
        currentMode = MODE_MENU;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_SD_MISSING) {
        currentMode = MODE_MENU;
        menuTile = 2;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_ALERT) {
        currentMode = alertReturnMode;
        if (currentMode == MODE_MENU) {
            menuTile = alertReturnTile;
            menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
            clock_startTick();
        }
    }
}

void handleBtn3Hold() {
    vibeKick();
    scanHadShort = false;
    if (currentMode == MODE_WIFI_SCAN_LIST) {
        currentMode = MODE_NETWORK;
        menu_drawNetwork(g_netSelected);
    }
    else if (currentMode == MODE_CAMERA_MODES) {
        currentMode = MODE_MENU;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_NETWORK) {
        currentMode = MODE_WIFI_OPTIONS;
        menu_drawWifiOptions(wifiOptSel);
    }
    else if (currentMode == MODE_WIFI_OPTIONS) {
        currentMode = MODE_MENU;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_POPUP) {
        espnow_sendCommand("STOP_STREAM");
        currentMode = MODE_MENU;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_PREVIEW) {
        espnow_sendCommand("STOP_STREAM");
        currentMode = MODE_MENU;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_DIAG) {
        currentMode = MODE_MENU;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_SD_MISSING) {
        currentMode = MODE_MENU;
        menuTile = 2;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_ALERT) {
        currentMode = alertReturnMode;
        if (currentMode == MODE_MENU) {
            menuTile = alertReturnTile;
            menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
            clock_startTick();
        }
    }
}

void handleBtn5Select() {
    vibeKick();
    if (currentMode == MODE_PREVIEW) {
        espnow_sendCommand("CAPTURE");
        captureStart = millis();
        menu_drawCaptureWait();
        currentMode = MODE_CAPTURE;
        return;
    }
    if (currentMode == MODE_MENU) {
        if (menuTile == 0) {
            loadPurpose = LOAD_FOR_PREVIEW;
            menu_drawLoader();
            currentMode = MODE_LOADING;
            lastLoaderFrame = -1;
            lastFrameTime = millis();
        } else if (menuTile == 1) {
            wifiOptSel = 0;
            currentMode = MODE_WIFI_OPTIONS;
            menu_drawWifiOptions(wifiOptSel);
        } else if (menuTile == 2) {
            loadPurpose = LOAD_FOR_LOCAL_SD_MISSING;
            menu_drawLoader();
            currentMode = MODE_LOADING;
            lastLoaderFrame = -1;
            lastFrameTime = millis();
        } else if (menuTile == 4) {
            currentMode = MODE_DIAG;
            menu_drawDiag(espnow_isConnected(), sdPresent);
        } else {
            menu_showNotification("SOON", "not impl", T_ORANGE, 2000, MODE_MENU, menuTile);
            currentMode = MODE_NOTIFY;
            notifyUntil = millis() + 2000;
            notifyReturnMode = MODE_MENU;
            notifyReturnTile = menuTile;
        }
    }
    else if (currentMode == MODE_WIFI_OPTIONS) {
        if (wifiOptSel == 0) {
            g_netSelected = 0;
            currentMode = MODE_NETWORK;
            menu_drawNetwork(g_netSelected);
        } else {
            menu_showNotification("SOON", "SCAN", T_ORANGE, 1500);
            currentMode = MODE_NOTIFY;
            notifyUntil = millis() + 1500;
            notifyReturnMode = MODE_WIFI_OPTIONS;
            notifyReturnTile = -1;
        }
    }
    else if (currentMode == MODE_NETWORK) {
        switch (g_netSelected) {
            case 0: network_applyWifiEnable(!g_wifiEnabled); menu_networkRefreshRow(0); break;
            case 1: network_scanWifiAndShow(); break;
            case 2: break;
            case 3: network_actionTest(); break;
        }
    }
    else if (currentMode == MODE_CAMERA_MODES) {
        if (camModeSelected == 0) {
            flashMode = !flashMode;
            espnow_sendCommand(flashMode ? "FLASH_ON" : "FLASH_OFF");
            menu_cameraModesRefreshRow(0, flashMode);
        } else {
            menu_showNotification("SOON", "not impl", T_ORANGE, 1200, MODE_CAMERA_MODES);
            currentMode = MODE_NOTIFY;
            notifyUntil = millis() + 1200;
            notifyReturnMode = MODE_CAMERA_MODES;
            notifyReturnTile = -1;
        }
    }
    else if (currentMode == MODE_WIFI_SCAN_LIST) {
        wifiScanSelectCurrent();
    }
    else if (currentMode == MODE_POPUP) {
        if (popupYes) {
            espnow_sendCommand("START_STREAM");
            currentMode = MODE_PREVIEW;
            tft.fillScreen(T_BG);
            lastFrameTime = millis();
        } else {
            camModeSelected = 0;
            currentMode = MODE_CAMERA_MODES;
            menu_drawCameraModes(camModeSelected, flashMode);
        }
    }
    else if (currentMode == MODE_DIAG) {
        menu_drawDiag(espnow_isConnected(), sdPresent);
    }
    else if (currentMode == MODE_SD_MISSING) {
        currentMode = MODE_MENU;
        menuTile = 2;
        menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
        clock_startTick();
    }
    else if (currentMode == MODE_ALERT) {
        currentMode = alertReturnMode;
        if (currentMode == MODE_MENU) {
            menuTile = alertReturnTile;
            menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
            clock_startTick();
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n==== DuckyMini ====");
    camIP = "";
    camConfirmed = false;
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, LOW);
    pinMode(VIBE_PIN, OUTPUT); digitalWrite(VIBE_PIN, LOW);
    pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LED_OFF);
    led_powerOnBlip();
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.invertDisplay(false);
    tft.fillScreen(T_BG);
    pinMode(BTN_SCREEN, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    streamBuf = (uint16_t*)malloc(FRAME_W * FRAME_H * 2);
    tftBuf    = (uint16_t*)malloc(FRAME_W * ROW_STRETCH * 2);
    if (!streamBuf || !tftBuf) { fatalError = true; while (true) { led_update(); delay(5); } }
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpg_output);
    digitalWrite(BACKLIGHT_PIN, HIGH);
    bootStart = millis();
    bootPhase = 0;
    menu_drawLogoScreen();
    if (!espnow_init()) { fatalError = true; while (true) { led_update(); delay(5); } }
    disableWifiSleep();
    WiFi.mode(WIFI_AP_STA);
    applyApVisibility(false);
    g_wifiEnabled = false;
    disableWifiSleep();
    clock_init();
    updateWifiState();
    Serial.println("[SYS] boot complete");
}

void loop() {
    vibeService();
    led_update();
    if (currentMode == MODE_BOOT) {
        uint32_t elapsed = millis() - bootStart;
        if (bootPhase == 0 && elapsed >= 2000) { bootPhase = 1; menu_drawLoader(); lastLoaderFrame = -1; }
        else if (bootPhase == 1 && elapsed >= 4500) { bootPhase = 2; menu_drawBlackScreen(); }
        else if (bootPhase == 2 && elapsed >= 5500) {
            bootPhase = 3;
            currentMode = MODE_MENU;
            updateWifiState();
            menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
            clock_startTick();
        }
        if (bootPhase == 1) menu_pumpLoader(&lastLoaderFrame);
        delay(1);
        return;
    }
    if (currentMode == MODE_KB_SSID || currentMode == MODE_KB_PSK) {
        Mode kbMode = currentMode;
        kb_serviceBlink();
        kb_handleBtns();
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (kb_pushSerial(c)) {
                while (Serial.available() &&
                       (Serial.peek() == '\r' || Serial.peek() == '\n')) Serial.read();
                Serial.printf("[KB/serial] submit '%s'\n", kb_getText().c_str());
                if (kbMode == MODE_KB_PSK) network_sendWifiCreds();
                else { currentMode = MODE_KB_PSK; kb_init("PSK:"); }
                delay(1);
                return;
            }
        }
        if (kb_isCancelled()) {
            if (kbMode == MODE_KB_PSK && wifiScanCount > 0) {
                currentMode = MODE_WIFI_SCAN_LIST;
                scanHadShort = false;
                menu_drawWifiScanList(wifiScanSSIDs, wifiScanCount,
                                      wifiScanSelected, wifiScanTop);
            } else {
                currentMode = MODE_NETWORK;
                menu_drawNetwork(g_netSelected);
            }
        }
        delay(1);
        return;
    }
    if (currentMode == MODE_WIFI_CONNECTING) {
        static uint32_t lastVibe = 0;
        if (millis() - lastVibe > 800) { lastVibe = millis(); vibeKick(); }
        EspNowMsg m;
        while (espnow_poll(&m)) {
            netActivityUntil = millis() + 150;
            if (m.isText) {
                String cmd((char*)m.data, m.len);
                cmd.trim();
                Serial.printf("[MINI<-CAM] %s\n", cmd.c_str());
                processEspNowText(cmd);
            }
            free(m.data);
        }
        if (WiFi.status() == WL_CONNECTED && !miniJoinedWifi) {
            miniJoinedWifi = true;
            Serial.printf("[MINI] router connected, ip=%s ch=%d\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.channel());
            WiFi.softAPdisconnect(true);
            delay(300);
            WiFi.mode(WIFI_STA);
            delay(800);
            WiFi.disconnect(false);
            delay(300);
            WiFi.begin(wifi_ssid.c_str(), wifi_psk.c_str());
            unsigned long t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
                delay(200);
            }
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[MINI] STA re-connect FAILED");
            } else {
                Serial.printf("[MINI] STA up, ip=%s ch=%d\n",
                              WiFi.localIP().toString().c_str(),
                              WiFi.channel());
            }
            delay(800);
            espnow_reconnect(WiFi.channel());
            delay(500);
            disableWifiSleep();
            espnow_sendCommand("WHERE?");
            lastWhereMs = millis();
            Serial.println("[MINI] sent WHERE? to CAM");
            delay(1);
            return;
        }
        if (miniJoinedWifi && !camConfirmed) {
            if (millis() - lastWhereMs > 2000) {
                lastWhereMs = millis();
                espnow_sendCommand("WHERE?");
                Serial.println("[MINI] retry WHERE?");
            }
        }
        if (camConfirmed) {
            Serial.println("[MINI] CAM confirmed — showing CONNECTED");
            vibeOffAt = millis() + 250;
            digitalWrite(VIBE_PIN, HIGH);
            menu_showNotification("CONNECTED", camIP.c_str(), T_ORANGE, 3000);
            currentMode = MODE_NOTIFY;
            notifyUntil = millis() + 3000;
            notifyReturnMode = MODE_NETWORK;
            notifyReturnTile = -1;
            lastVibe = 0;
            delay(1);
            return;
        }
        if (millis() - wifiConnectStart > 60000) {
            Serial.println("[MINI] CAM confirm timeout");
            menu_showNotification("FAILED", "no CAM reply", T_ORANGE, 2500);
            currentMode = MODE_NOTIFY;
            notifyUntil = millis() + 2500;
            notifyReturnMode = MODE_NETWORK;
            notifyReturnTile = -1;
            wifiConnecting = false;
            lastVibe = 0;
        }
        delay(5);
        return;
    }
    bool short3 = false;
    bool long3  = false;
    {
        bool reading = digitalRead(BTN_SCREEN);
        uint32_t now = millis();
        if (reading == LOW && btn3LastReading == HIGH) {
            btn3PressStart = now; btn3Held = true; btn3LongPressTriggered = false;
        } else if (reading == HIGH && btn3LastReading == LOW) {
            btn3Held = false;
            uint32_t held = now - btn3PressStart;
            if (!btn3LongPressTriggered && held < LONG_PRESS_MS && held >= DEBOUNCE_MS) short3 = true;
        } else if (btn3Held && !btn3LongPressTriggered &&
                   (now - btn3PressStart >= LONG_PRESS_MS)) {
            btn3LongPressTriggered = true;
            long3 = true;
        }
        btn3LastReading = reading;
    }
    bool short5 = false;
    {
        bool reading = digitalRead(BTN_SELECT);
        uint32_t now = millis();
        if (reading == LOW && btn5LastReading == HIGH) {
            btn5PressStart = now; btn5Held = true; btn5LongPressTriggered = false;
        } else if (reading == HIGH && btn5LastReading == LOW) {
            btn5Held = false;
            uint32_t held = now - btn5PressStart;
            if (!btn5LongPressTriggered && held < LONG_PRESS_MS && held >= DEBOUNCE_MS) short5 = true;
        }
        btn5LastReading = reading;
    }
    if (short3) {
        if (currentMode == MODE_WIFI_SCAN_LIST) {
            uint32_t now = millis();
            if (scanHadShort && (now - scanLastShortMs) <= DOUBLE_CLICK_MS) {
                wifiScanMoveSelection(-2);
                scanHadShort = false;
            } else {
                wifiScanMoveSelection(+1);
                scanHadShort = true;
            }
            scanLastShortMs = now;
        } else {
            handleBtn3ShortPress();
        }
    }
    if (long3)  handleBtn3Hold();
    if (short5) handleBtn5Select();
    if (currentMode == MODE_LOADING) {
        menu_pumpLoader(&lastLoaderFrame);
        if (loadPurpose == LOAD_FOR_PREVIEW) {
            if (millis() - lastFrameTime > 200) {
                currentMode = MODE_POPUP;
                popupYes = false;
                menu_drawPopupFull(popupYes);
            }
        } else if (loadPurpose == LOAD_FOR_LOCAL_SD_MISSING) {
            if (millis() - lastFrameTime > 500) {
                menu_drawSdMissing();
                currentMode = MODE_SD_MISSING;
            }
        }
    }
    if (currentMode == MODE_MENU) {
        clock_tick(&tft, &clockSprite);
        updateWifiState();
    }
    if (currentMode == MODE_NOTIFY && millis() > notifyUntil) {
        currentMode = notifyReturnMode;
        if (currentMode == MODE_MENU) {
            menuTile = notifyReturnTile;
            menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
            clock_startTick();
        } else if (currentMode == MODE_WIFI_OPTIONS) {
            menu_drawWifiOptions(wifiOptSel);
        } else if (currentMode == MODE_NETWORK) {
            menu_drawNetwork(g_netSelected);
        } else if (currentMode == MODE_CAMERA_MODES) {
            menu_drawCameraModes(camModeSelected, flashMode);
        }
    }
    if (currentMode == MODE_ALERT && millis() > alertShowUntil) {
        currentMode = alertReturnMode;
        if (currentMode == MODE_MENU) {
            menuTile = alertReturnTile;
            menu_drawFull(menuTile, g_wifiOnline, g_wifiApActive);
            clock_startTick();
        }
    }
    if (currentMode == MODE_PREVIEW && millis() - lastFrameTime > 8000) {
        espnow_sendCommand("STOP_STREAM");
        showAlert("Error 102", MODE_MENU, menuTile);
    }
    if (currentMode == MODE_CAPTURE && millis() - captureStart > 30000) {
        espnow_sendCommand("START_STREAM");
        currentMode = MODE_PREVIEW;
        lastFrameTime = millis();
    }
    if (currentMode == MODE_DIAG && millis() - diagRefresh > 2000) {
        diagRefresh = millis();
        menu_drawDiag(espnow_isConnected(), sdPresent);
    }
    EspNowMsg m;
    if (espnow_poll(&m)) {
        netActivityUntil = millis() + 150;
        if (m.isText) {
            String cmd((char*)m.data, m.len);
            cmd.trim();
            Serial.printf("[MINI<-CAM] %s\n", cmd.c_str());
            processEspNowText(cmd);
            free(m.data);
        } else {
            if (currentMode == MODE_PREVIEW &&
                m.len >= 4 && m.data[0] == 0xFF && m.data[1] == 0xD8) {
                lastFrameTime = millis();
                TJpgDec.drawJpg(0, 0, m.data, m.len);
                blitFrame();
                framesThisSec++;
            }
            free(m.data);
        }
    }
    delay(1);
}