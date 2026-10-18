/*
 * espnow_link.h — ESP-NOW send/receive + JPEG reassembly
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define ESPNOW_CHANNEL     6
#define ESPNOW_MAX_JPEG    16384
#define PKT_SYNC1          0xAA
#define PKT_SYNC2          0x55
#define PKT_TYPE_JPEG      0
#define PKT_TYPE_TEXT      1
#define PKT_MAX_PAYLOAD    240
#define PKT_HEADER_LEN     8

static uint8_t camMAC[] = { 0xB4, 0xBF, 0xE9, 0x34, 0x8E, 0x0C };

struct EspNowMsg {
    uint8_t* data;
    size_t   len;
    bool     isText;
};

struct __attribute__((packed)) Packet {
    uint8_t sync1;
    uint8_t sync2;
    uint8_t frameID;
    uint8_t chunkIdx;
    uint8_t totalChunks;
    uint8_t type;
    uint8_t len;
    uint8_t _pad;
    uint8_t payload[PKT_MAX_PAYLOAD];
};

static Packet   sendPkt;
static Packet   recvPkt;

static uint8_t* reassemblyBuf   = nullptr;
static size_t   reassemblyLen   = 0;
static uint8_t  reassemblyID    = 0;
static uint8_t  reassemblyTotal = 0;
static uint8_t  reassemblyGot   = 0;
static uint16_t chunkReceived[256] = { 0 };

#define ESPQ_LEN 4
static EspNowMsg msgQueue[ESPQ_LEN];
static int       msgHead = 0;
static int       msgTail = 0;

static volatile bool espnow_connected = false;
static volatile uint32_t lastPacketMs = 0;

static void espnow_recv_cb(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < PKT_HEADER_LEN) return;
    const Packet* p = (const Packet*)data;
    if (p->sync1 != PKT_SYNC1 || p->sync2 != PKT_SYNC2) return;
    lastPacketMs = millis();
    espnow_connected = true;
    if (p->type == PKT_TYPE_TEXT) {
        if ((msgHead + 1) % ESPQ_LEN == msgTail) return;
        uint8_t* buf = (uint8_t*)malloc(p->len);
        if (!buf) return;
        memcpy(buf, p->payload, p->len);
        msgQueue[msgHead].data   = buf;
        msgQueue[msgHead].len    = p->len;
        msgQueue[msgHead].isText = true;
        msgHead = (msgHead + 1) % ESPQ_LEN;
        return;
    }
    if (p->type != PKT_TYPE_JPEG) return;
    if (p->chunkIdx == 0) {
        reassemblyID    = p->frameID;
        reassemblyTotal = p->totalChunks;
        reassemblyLen   = 0;
        reassemblyGot   = 0;
        memset(chunkReceived, 0, sizeof(chunkReceived));
    } else if (p->frameID != reassemblyID) {
        return;
    }
    if (p->chunkIdx >= 255) return;
    if (chunkReceived[p->chunkIdx]) return;
    chunkReceived[p->chunkIdx] = 1;
    reassemblyGot++;
    size_t off = p->chunkIdx * PKT_MAX_PAYLOAD;
    if (off + p->len <= ESPNOW_MAX_JPEG) {
        memcpy(reassemblyBuf + off, p->payload, p->len);
        if (off + p->len > reassemblyLen)
            reassemblyLen = off + p->len;
    }
    if (reassemblyGot == reassemblyTotal) {
        if ((msgHead + 1) % ESPQ_LEN == msgTail) return;
        uint8_t* buf = (uint8_t*)malloc(reassemblyLen);
        if (!buf) return;
        memcpy(buf, reassemblyBuf, reassemblyLen);
        msgQueue[msgHead].data   = buf;
        msgQueue[msgHead].len    = reassemblyLen;
        msgQueue[msgHead].isText = false;
        msgHead = (msgHead + 1) % ESPQ_LEN;
    }
}

bool espnow_init() {
    reassemblyBuf = (uint8_t*)malloc(ESPNOW_MAX_JPEG);
    if (!reassemblyBuf) {
        Serial.println("[ESP-NOW] reassembly buffer OOM");
        return false;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    delay(200);
    WiFi.softAP("__chlock__", NULL, ESPNOW_CHANNEL);
    delay(300);
    Serial.printf("[WiFi] after softAP, channel: %d\n", WiFi.channel());
    WiFi.softAPdisconnect(false);
    delay(200);
    WiFi.begin("__stastalock__", "0123456789");
    delay(500);
    WiFi.disconnect(false);
    delay(200);
    Serial.printf("[WiFi] Mini actual channel: %d\n", WiFi.channel());
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] init failed");
        return false;
    }
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, camMAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESP-NOW] add peer failed");
        return false;
    }
    Serial.printf("[ESP-NOW] peer CAM %02X:%02X:%02X:%02X:%02X:%02X ch %d\n",
                  camMAC[0], camMAC[1], camMAC[2],
                  camMAC[3], camMAC[4], camMAC[5], ESPNOW_CHANNEL);
    return true;
}

bool espnow_reconnect(uint8_t channel) {
    Serial.printf("[ESP-NOW] re-init on ch=%d\n", channel);
    esp_now_deinit();
    delay(100);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] re-init failed");
        return false;
    }
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, camMAC, 6);
    peer.channel = channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESP-NOW] re-add peer failed");
        return false;
    }
    Serial.printf("[ESP-NOW] peer CAM re-added on ch=%d\n", channel);
    return true;
}

bool espnow_sendCommand(const char* cmd) {
    memset(&sendPkt, 0, sizeof(sendPkt));
    sendPkt.sync1 = PKT_SYNC1;
    sendPkt.sync2 = PKT_SYNC2;
    sendPkt.type = PKT_TYPE_TEXT;
    sendPkt.totalChunks = 1;
    size_t n = strlen(cmd);
    if (n > PKT_MAX_PAYLOAD) n = PKT_MAX_PAYLOAD;
    sendPkt.len = n;
    memcpy(sendPkt.payload, cmd, n);
    esp_err_t r = esp_now_send(camMAC, (uint8_t*)&sendPkt, PKT_HEADER_LEN + n);
    if (r == ESP_OK) {
        Serial.printf("[MINI→CAM] %s\n", cmd);
        return true;
    }
    Serial.printf("[MINI→CAM] send failed: %d\n", r);
    return false;
}

bool espnow_poll(EspNowMsg* out) {
    if (msgHead == msgTail) return false;
    *out = msgQueue[msgTail];
    msgTail = (msgTail + 1) % ESPQ_LEN;
    return true;
}

bool espnow_isConnected() {
    if (millis() - lastPacketMs > 3000) espnow_connected = false;
    return espnow_connected;
}