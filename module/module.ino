/*
 * ESP32-CAM — ESP-NOW + Soft-AP + DHCP
 * Requires: #include <esp_wifi.h>
 * Modem sleep disabled after every esp_now_init().
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#include "soc/rtc_cntl_reg.h"

#define ESPNOW_CHANNEL    6
#define MAX_PHOTOS        4

uint8_t miniMAC[] = { 0x14, 0x63, 0x93, 0xC5, 0xD6, 0xC8 };

#define PKT_SYNC1         0xAA
#define PKT_SYNC2         0x55
#define PKT_TYPE_JPEG     0
#define PKT_TYPE_TEXT     1
#define PKT_MAX_PAYLOAD   240
#define PKT_HEADER_LEN    8

struct __attribute__((packed)) EspNowPacket {
    uint8_t sync1, sync2, frameID, chunkIdx, totalChunks, type, len, _pad;
    uint8_t payload[PKT_MAX_PAYLOAD];
};

EspNowPacket sendPkt;
uint8_t streamFrameID = 0;
bool    flashMode     = false;

const char* AP_SSID = "DuckyCam";

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define BUTTON_PIN        13
#define FLASH_LED_PIN      4

#define STREAM_FRAMESIZE   FRAMESIZE_QQVGA
#define STREAM_QUALITY     15
#define STILL_QUALITY      10
#define STREAM_INTERVAL_MS 50
#define HTTP_PORT          81

volatile bool photoRequested = false;
volatile bool streamEnabled  = false;
bool sdAvailable = false;
uint32_t lastFrameMs = 0;
uint32_t frameCount = 0;
uint32_t lastFpsCalc = 0;
uint32_t espNowRxCount = 0;
uint32_t pingCount = 0;

uint32_t lastPingMs   = 0;
bool     sendPingNow  = false;

bool joinInProgress = false;
bool alreadyJoined  = false;

int  lastBtnState = HIGH;
uint32_t lastDebounce = 0;

WebServer httpServer(HTTP_PORT);
camera_config_t camConfig;

volatile bool pendingWifiJoin = false;
String        pendingSSID     = "";
String        pendingPSK      = "";

void sendTextPacket(const char* msg) {
    memset(&sendPkt, 0, sizeof(sendPkt));
    sendPkt.sync1 = PKT_SYNC1;
    sendPkt.sync2 = PKT_SYNC2;
    sendPkt.totalChunks = 1;
    sendPkt.type = PKT_TYPE_TEXT;
    size_t n = strlen(msg);
    if (n > PKT_MAX_PAYLOAD) n = PKT_MAX_PAYLOAD;
    sendPkt.len = n;
    memcpy(sendPkt.payload, msg, n);
    esp_err_t r = esp_now_send(miniMAC, (uint8_t*)&sendPkt, PKT_HEADER_LEN + n);
    Serial.printf("[CAM→MINI] '%s'  (err=%d)\n", msg, r);
}

void sendJpegFrame(const uint8_t* data, size_t len) {
    if (len == 0 || len > 255 * PKT_MAX_PAYLOAD) return;
    uint8_t frameID = streamFrameID++;
    uint8_t totalChunks = (len + PKT_MAX_PAYLOAD - 1) / PKT_MAX_PAYLOAD;
    if (totalChunks > 255) totalChunks = 255;
    const uint8_t* p = data;
    size_t remaining = len;
    for (uint8_t i = 0; i < totalChunks; i++) {
        size_t chunkLen = remaining > PKT_MAX_PAYLOAD ? PKT_MAX_PAYLOAD : remaining;
        memset(&sendPkt, 0, sizeof(sendPkt));
        sendPkt.sync1 = PKT_SYNC1;
        sendPkt.sync2 = PKT_SYNC2;
        sendPkt.frameID = frameID;
        sendPkt.chunkIdx = i;
        sendPkt.totalChunks = totalChunks;
        sendPkt.type = PKT_TYPE_JPEG;
        sendPkt.len = chunkLen;
        memcpy(sendPkt.payload, p, chunkLen);
        esp_now_send(miniMAC, (uint8_t*)&sendPkt, PKT_HEADER_LEN + chunkLen);
        delayMicroseconds(300);
        p += chunkLen;
        remaining -= chunkLen;
    }
}

void ensurePeer(uint8_t channel) {
    esp_now_del_peer(miniMAC);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, miniMAC, 6);
    peer.channel = channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_OK) {
        Serial.printf("[ESP-NOW] peer added on ch=%d\n", channel);
    } else {
        Serial.println("[ESP-NOW] peer add FAILED");
    }
}

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
    espNowRxCount++;
    Serial.printf("[ESP-NOW RX #%lu] len=%d\n",
                  (unsigned long)espNowRxCount, len);
    if (len < PKT_HEADER_LEN) return;
    const EspNowPacket* p = (const EspNowPacket*)data;
    if (p->sync1 != PKT_SYNC1 || p->sync2 != PKT_SYNC2) return;
    if (p->type != PKT_TYPE_TEXT) return;
    String cmd((char*)p->payload, p->len);
    cmd.trim();
    Serial.printf("[MINI→CAM] '%s'\n", cmd.c_str());

    if (cmd == "START_STREAM") {
        streamEnabled = true; lastFrameMs = millis();
        Serial.println("[CAM] stream ON");
    }
    else if (cmd == "STOP_STREAM") {
        streamEnabled = false;
        Serial.println("[CAM] stream OFF");
    }
    else if (cmd == "CAPTURE") {
        photoRequested = true;
        Serial.println("[CAM] capture requested");
    }
    else if (cmd == "SD_CHECK") {
        sendTextPacket(sdAvailable ? "SD_FOUND" : "SD_MISSING");
    }
    else if (cmd == "FLASH_ON") {
        flashMode = true; digitalWrite(FLASH_LED_PIN, HIGH);
        Serial.println("[CAM] flash ON");
    }
    else if (cmd == "FLASH_OFF") {
        flashMode = false; digitalWrite(FLASH_LED_PIN, LOW);
        Serial.println("[CAM] flash OFF");
    }
    else if (cmd == "WHERE?") {
        sendPingNow = true;
        Serial.println("[CAM] PING requested by Mini");
    }
    else if (cmd.startsWith("WIFI_SSID=")) {
        if (alreadyJoined) {
            Serial.println("[CAM] already joined — ignoring WIFI_SSID");
            return;
        }
        if (joinInProgress || pendingWifiJoin) {
            Serial.println("[CAM] join already in progress — ignoring");
            return;
        }
        int ssidStart = 10;
        int sp = cmd.indexOf(' ', ssidStart);
        if (sp < 0) { sendTextPacket("WIFI_FAIL"); return; }
        String ssid = cmd.substring(ssidStart, sp);
        String psk  = cmd.substring(sp + 1);
        if (psk.startsWith("WIFI_PSK=")) psk = psk.substring(9);
        ssid.trim(); psk.trim();
        pendingSSID     = ssid;
        pendingPSK      = psk;
        pendingWifiJoin = true;
        joinInProgress  = true;
        Serial.printf("[CAM] WiFi join queued: '%s'\n", ssid.c_str());
    }
}

void performWifiJoin(const String& ssid, const String& psk) {
    Serial.printf("[WIFI] === join SSID='%s' ===\n", ssid.c_str());
    streamEnabled = false;
    delay(300);
    esp_now_deinit();
    delay(500);
    WiFi.softAPdisconnect(true);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    delay(800);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    delay(200);
    Serial.println("[WIFI] switched to STA-only (low power)");
    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, true);
    int    targetCh = 0;
    int8_t rssi = 0;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid) {
            targetCh = WiFi.channel(i);
            rssi     = WiFi.RSSI(i);
            break;
        }
    }
    WiFi.scanDelete();
    Serial.printf("[WIFI] target ch=%d rssi=%d dBm\n", targetCh, rssi);
    delay(300);
    bool joinOK = false;
    if (targetCh > 0) {
        WiFi.begin(ssid.c_str(), psk.c_str());
        unsigned long t0 = millis();
        int lastStatus = -1;
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) {
            int s = WiFi.status();
            if (s != lastStatus) {
                Serial.printf("[WIFI] status=%d\n", s);
                lastStatus = s;
            }
            delay(200);
        }
        joinOK = (WiFi.status() == WL_CONNECTED);
    }
    if (joinOK) {
        alreadyJoined = true;
        Serial.printf("[WIFI] DHCP: ip=%s gw=%s ch=%d\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.channel());
        delay(1500);
        if (esp_now_init() != ESP_OK) {
            Serial.println("[ESP-NOW] reinit failed after join");
        } else {
            esp_now_register_recv_cb(onEspNowRecv);
            ensurePeer(WiFi.channel());
            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
            Serial.println("[WiFi] modem sleep disabled");
        }
        delay(500);
        String ip = WiFi.localIP().toString();
        Serial.printf("[CAM] ONLINE at %s ch=%d\n", ip.c_str(), WiFi.channel());
        String resp = "WIFI_OK " + ip;
        sendTextPacket(resp.c_str());
        delay(200);
        pingCount++;
        char buf[40];
        snprintf(buf, sizeof(buf), "PING%lu %s",
                 (unsigned long)pingCount, ip.c_str());
        sendTextPacket(buf);
        lastPingMs  = millis();
        sendPingNow = false;
    } else {
        Serial.println("[WIFI] join FAILED — restoring AP+STA");
        delay(500);
        WiFi.mode(WIFI_AP_STA);
        delay(500);
        WiFi.softAP(AP_SSID, NULL, ESPNOW_CHANNEL);
        delay(500);
        if (esp_now_init() != ESP_OK) {
            Serial.println("[ESP-NOW] reinit failed");
        } else {
            esp_now_register_recv_cb(onEspNowRecv);
            ensurePeer(ESPNOW_CHANNEL);
            WiFi.setSleep(false);
            esp_wifi_set_ps(WIFI_PS_NONE);
        }
        delay(300);
        sendTextPacket("WIFI_FAIL");
    }
    joinInProgress = false;
}

void buildCamConfig() {
    camConfig.ledc_channel = LEDC_CHANNEL_0;
    camConfig.ledc_timer   = LEDC_TIMER_0;
    camConfig.pin_d0       = Y2_GPIO_NUM;
    camConfig.pin_d1       = Y3_GPIO_NUM;
    camConfig.pin_d2       = Y4_GPIO_NUM;
    camConfig.pin_d3       = Y5_GPIO_NUM;
    camConfig.pin_d4       = Y6_GPIO_NUM;
    camConfig.pin_d5       = Y7_GPIO_NUM;
    camConfig.pin_d6       = Y8_GPIO_NUM;
    camConfig.pin_d7       = Y9_GPIO_NUM;
    camConfig.pin_xclk     = XCLK_GPIO_NUM;
    camConfig.pin_pclk     = PCLK_GPIO_NUM;
    camConfig.pin_vsync    = VSYNC_GPIO_NUM;
    camConfig.pin_href     = HREF_GPIO_NUM;
    camConfig.pin_sccb_sda = SIOD_GPIO_NUM;
    camConfig.pin_sccb_scl = SIOC_GPIO_NUM;
    camConfig.pin_pwdn     = PWDN_GPIO_NUM;
    camConfig.pin_reset    = RESET_GPIO_NUM;
    camConfig.xclk_freq_hz = 20000000;
    camConfig.pixel_format = PIXFORMAT_JPEG;
    camConfig.fb_location  = CAMERA_FB_IN_PSRAM;
    camConfig.fb_count     = 1;
    camConfig.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    camConfig.frame_size   = STREAM_FRAMESIZE;
    camConfig.jpeg_quality = STREAM_QUALITY;
}

void applySensorTuning() {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_brightness(s, 0); s->set_contrast(s, 0); s->set_saturation(s, 0);
    s->set_whitebal(s, 1); s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1); s->set_aec2(s, 1); s->set_gain_ctrl(s, 1);
    s->set_hmirror(s, 1); s->set_vflip(s, 0);
}

bool initCameraAt(framesize_t fs, int quality) {
    esp_camera_deinit();
    delay(100);
    camConfig.frame_size   = fs;
    camConfig.jpeg_quality = quality;
    esp_err_t err = esp_camera_init(&camConfig);
    if (err != ESP_OK) {
        Serial.printf("[CAM] init failed 0x%x at fs=%d\n", err, (int)fs);
        return false;
    }
    applySensorTuning();
    delay(300);
    return true;
}

bool setupCamera() {
    esp_log_level_set("*", ESP_LOG_NONE);
    buildCamConfig();
    if (esp_camera_init(&camConfig) != ESP_OK) return false;
    applySensorTuning();
    Serial.println("[CAM] OK");
    Serial.printf("[CAM] PSRAM total: %u KB, free: %u KB\n",
                  ESP.getPsramSize() / 1024, ESP.getFreePsram() / 1024);
    return true;
}

int findHighestPhotoNumber() {
    int maxNum = -1;
    File dir = sdAvailable ? SD_MMC.open("/photo") : LittleFS.open("/photo");
    if (!dir) return -1;
    File f = dir.openNextFile();
    while (f) {
        String name = String(f.name());
        int idx = name.indexOf("photo_");
        if (idx >= 0) {
            int num = name.substring(idx + 6, idx + 10).toInt();
            if (num > maxNum) maxNum = num;
        }
        f = dir.openNextFile();
    }
    return maxNum;
}

int findOldestPhotoNumber() {
    int minNum = 99999;
    File dir = sdAvailable ? SD_MMC.open("/photo") : LittleFS.open("/photo");
    if (!dir) return -1;
    File f = dir.openNextFile();
    while (f) {
        String name = String(f.name());
        int idx = name.indexOf("photo_");
        if (idx >= 0) {
            int num = name.substring(idx + 6, idx + 10).toInt();
            if (num < minNum) minNum = num;
        }
        f = dir.openNextFile();
    }
    return (minNum == 99999) ? -1 : minNum;
}

int countPhotos() {
    int count = 0;
    File dir = sdAvailable ? SD_MMC.open("/photo") : LittleFS.open("/photo");
    if (!dir) return 0;
    File f = dir.openNextFile();
    while (f) { count++; f = dir.openNextFile(); }
    return count;
}

bool deleteOldestPhoto() {
    int oldest = findOldestPhotoNumber();
    if (oldest < 0) return false;
    char path[40];
    snprintf(path, sizeof(path), "/photo/photo_%04d.jpg", oldest);
    bool ok = sdAvailable ? SD_MMC.remove(path) : LittleFS.remove(path);
    if (ok) Serial.printf("[STORAGE] deleted %s\n", path);
    return ok;
}

bool trySaveToStorage(const char* path, const uint8_t* data, size_t len) {
    if (sdAvailable) {
        File f = SD_MMC.open(path, FILE_WRITE);
        if (!f) return false;
        size_t written = f.write(data, len);
        f.close();
        return (written == len);
    } else {
        File f = LittleFS.open(path, FILE_WRITE);
        if (!f) return false;
        size_t written = f.write(data, len);
        f.close();
        return (written == len);
    }
}

bool saveFrameToStorage(const uint8_t* data, size_t len) {
    int count = countPhotos();
    if (count >= MAX_PHOTOS) { deleteOldestPhoto(); delay(50); }
    int highest = findHighestPhotoNumber();
    int nextNum = (highest < 0) ? 1 : highest + 1;
    char path[40];
    snprintf(path, sizeof(path), "/photo/photo_%04d.jpg", nextNum);
    if (trySaveToStorage(path, data, len)) {
        Serial.printf("[STORAGE] saved %s (%u bytes)\n", path, (unsigned)len);
        return true;
    }
    for (int attempt = 0; attempt < 10; attempt++) {
        if (!deleteOldestPhoto()) break;
        delay(50);
        if (trySaveToStorage(path, data, len)) return true;
    }
    Serial.println("[STORAGE] save FAILED");
    return false;
}

void setupStorage() {
    Serial.println("[SD] init...");
    if (SD_MMC.begin("/sdcard", true)) {
        uint8_t cardType = SD_MMC.cardType();
        if (cardType == CARD_NONE) sdAvailable = false;
        else {
            sdAvailable = true;
            Serial.printf("[SD] type=%d size=%llu MB\n",
                          cardType, SD_MMC.cardSize() / (1024 * 1024));
        }
    } else sdAvailable = false;
    if (!sdAvailable) {
        if (!LittleFS.begin(true)) Serial.println("[FS] LittleFS mount failed");
        else Serial.printf("[FS] LittleFS OK, %u/%u KB\n",
                           LittleFS.usedBytes() / 1024, LittleFS.totalBytes() / 1024);
    }
    if (sdAvailable) { if (!SD_MMC.exists("/photo")) SD_MMC.mkdir("/photo"); }
    else             { if (!LittleFS.exists("/photo")) LittleFS.mkdir("/photo"); }
    Serial.printf("[STORAGE] %d existing photos\n", countPhotos());
}

bool tryCaptureAt(framesize_t fs, int quality) {
    Serial.printf("[CAM] try fs=%d...\n", (int)fs);
    if (!initCameraAt(fs, quality)) return false;
    camera_fb_t* warm = esp_camera_fb_get();
    if (warm) esp_camera_fb_return(warm);
    delay(100);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { Serial.println("[CAM] no frame"); return false; }
    Serial.printf("[CAM] captured %u bytes\n", fb->len);
    bool saved = saveFrameToStorage(fb->buf, fb->len);
    sendTextPacket(saved ? "PHOTO SAVED" : "CAPTURE FAILED");
    esp_camera_fb_return(fb);
    return true;
}

void sendStill() {
    unsigned long tStart = millis();
    Serial.println("[CAM] === capture start ===");
    bool ok = false;
    if (!ok) ok = tryCaptureAt(FRAMESIZE_QXGA, STILL_QUALITY);
    if (!ok) ok = tryCaptureAt(FRAMESIZE_UXGA, STILL_QUALITY);
    if (!ok) ok = tryCaptureAt(FRAMESIZE_SVGA, STILL_QUALITY);
    if (!ok) ok = tryCaptureAt(FRAMESIZE_VGA,  STILL_QUALITY);
    if (!ok) {
        Serial.println("[CAM] ALL capture attempts FAILED");
        sendTextPacket("CAPTURE FAILED");
    }
    Serial.println("[CAM] restore stream (QQVGA)");
    initCameraAt(STREAM_FRAMESIZE, STREAM_QUALITY);
    Serial.printf("[CAM] === capture done in %lu ms ===\n", millis() - tStart);
}

void checkButton() {
    static int lastReading = HIGH;
    int reading = digitalRead(BUTTON_PIN);
    if (reading != lastReading) {
        if ((millis() - lastDebounce) > 50) {
            lastDebounce = millis();
            if (reading == LOW) photoRequested = true;
        }
        lastReading = reading;
    }
    lastBtnState = reading;
}

void handleRoot() {
    File dir = sdAvailable ? SD_MMC.open("/photo") : LittleFS.open("/photo");
    if (!dir) { httpServer.send(200, "text/html", "<h1>No storage</h1>"); return; }
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>DuckyCam</title>";
    html += "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:12px}";
    html += "h1{color:#F206}";
    html += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:8px}";
    html += ".card{background:#222;padding:6px;border-radius:6px;text-align:center}";
    html += "img{width:100%;height:auto;border-radius:4px;display:block}";
    html += "a{color:#F206;font-size:11px;text-decoration:none;margin:2px}";
    html += "</style></head><body>";
    html += "<h1>DuckyCam</h1>";
    html += "<p>Storage: " + String(sdAvailable ? "SD card" : "internal flash");
    html += " | Photos: " + String(countPhotos());
    html += " | IP: " + WiFi.localIP().toString() + "</p><div class='grid'>";
    File f = dir.openNextFile();
    while (f) {
        String name = String(f.name());
        if (!name.startsWith("/")) name = "/photo/" + name;
        html += "<div class='card'>";
        html += "<a href='/photo?p=" + name + "'>";
        html += "<img src='/photo?p=" + name + "' loading='lazy'>";
        html += "</a>";
        html += "<a href='/delete?p=" + name + "'>[delete]</a>";
        html += "</div>";
        f = dir.openNextFile();
    }
    html += "</div></body></html>";
    httpServer.send(200, "text/html", html);
}

void handlePhoto() {
    String p = httpServer.arg("p");
    if (p.length() == 0) { httpServer.send(404); return; }
    File f = sdAvailable ? SD_MMC.open(p) : LittleFS.open(p);
    if (!f) { httpServer.send(404, "text/plain", "not found"); return; }
    httpServer.streamFile(f, "image/jpeg");
    f.close();
}

void handleDelete() {
    String p = httpServer.arg("p");
    if (p.length() == 0) { httpServer.send(400); return; }
    bool ok = sdAvailable ? SD_MMC.remove(p) : LittleFS.remove(p);
    httpServer.send(200, "text/plain", ok ? "deleted" : "fail");
}

void handleInfo() {
    String s = "DuckyCam\n";
    s += "IP(STA): " + WiFi.localIP().toString() + "\n";
    s += "IP(AP):  " + WiFi.softAPIP().toString() + "\n";
    s += "Ch: " + String(WiFi.channel()) + "\n";
    s += "SD: " + String(sdAvailable ? "yes" : "no") + "\n";
    s += "Photos: " + String(countPhotos()) + "\n";
    s += "Stream: " + String(streamEnabled ? "on" : "off") + "\n";
    s += "Flash: " + String(flashMode ? "on" : "off") + "\n";
    s += "Heap: " + String(ESP.getFreeHeap() / 1024) + "K\n";
    s += "PSRAM free: " + String(ESP.getFreePsram() / 1024) + "K\n";
    s += "ESPNOW RX: " + String(espNowRxCount) + "\n";
    httpServer.send(200, "text/plain", s);
}

void httpTask(void* param) {
    for (;;) { httpServer.handleClient(); vTaskDelay(pdMS_TO_TICKS(5)); }
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    delay(500);
    RTC_NOINIT_ATTR static uint32_t bootCount;
    RTC_NOINIT_ATTR static uint32_t lastReason;
    uint32_t rr = esp_reset_reason();
    if (rr == ESP_RST_POWERON && lastReason == ESP_RST_POWERON) bootCount++;
    else bootCount = 0;
    lastReason = rr;
    Serial.println();
    Serial.println("==== ESP32-CAM ====");
    Serial.printf("[BOOT] reason=%lu bootCount=%lu\n",
                  (unsigned long)rr, (unsigned long)bootCount);
    if (bootCount > 4) {
        Serial.println("[BOOT] reset loop — sleeping 30s");
        delay(30000);
        bootCount = 0;
        ESP.restart();
    }
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);
    if (!setupCamera()) {
        Serial.println("[CAM] FAIL");
        while (true) delay(1000);
    }
    setupStorage();
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    delay(500);
    WiFi.softAP(AP_SSID, NULL, ESPNOW_CHANNEL);
    delay(800);
    Serial.printf("[WiFi] after softAP, ch=%d\n", WiFi.channel());
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin("__stastalock__", "0123456789");
    delay(800);
    WiFi.disconnect(false);
    delay(500);
    Serial.printf("[WiFi] CAM ch=%d\n", WiFi.channel());
    Serial.printf("[HTTP] AP URL: http://%s:%d/\n",
                  WiFi.softAPIP().toString().c_str(), HTTP_PORT);
    delay(300);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] init FAILED");
        while (true) delay(1000);
    }
    esp_now_register_recv_cb(onEspNowRecv);
    ensurePeer(ESPNOW_CHANNEL);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    Serial.println("[WiFi] modem sleep disabled");
    httpServer.on("/",       handleRoot);
    httpServer.on("/photo",  handlePhoto);
    httpServer.on("/delete", handleDelete);
    httpServer.on("/info",   handleInfo);
    httpServer.begin();
    Serial.printf("[HTTP] on :%d\n", HTTP_PORT);
    xTaskCreatePinnedToCore(httpTask, "HTTP", 4096, NULL, 1, NULL, 0);
    Serial.println("[CAM] ready");
    lastFpsCalc = millis();
    lastPingMs  = millis();
}

void loop() {
    if (pendingWifiJoin) {
        pendingWifiJoin = false;
        String ssid = pendingSSID;
        String psk  = pendingPSK;
        pendingSSID = "";
        pendingPSK  = "";
        performWifiJoin(ssid, psk);
        return;
    }
    uint32_t now = millis();
    if (sendPingNow || (now - lastPingMs > 5000)) {
        lastPingMs  = now;
        sendPingNow = false;
        pingCount++;
        char buf[40];
        snprintf(buf, sizeof(buf), "PING%lu %s",
                 (unsigned long)pingCount,
                 WiFi.localIP().toString().c_str());
        sendTextPacket(buf);
    }
    checkButton();
    if (photoRequested) {
        photoRequested = false;
        sendStill();
        lastFrameMs = millis();
        return;
    }
    if (!streamEnabled) { delay(20); return; }
    if (now - lastFrameMs < STREAM_INTERVAL_MS) { delay(1); return; }
    lastFrameMs = now;
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { delay(5); return; }
    sendJpegFrame(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    frameCount++;
    if (now - lastFpsCalc >= 1000) {
        Serial.printf("[CAM] FPS:%lu heap:%u\n",
                      (unsigned long)frameCount, ESP.getFreeHeap());
        frameCount = 0;
        lastFpsCalc = now;
    }
}