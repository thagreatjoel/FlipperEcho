/*
 * menu.h — Flipper Zero-style UI
 */

#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
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

#include "src/image_download_bits.h"
#include "src/image_file_search_bits.h"
#include "src/image_camera_bits.h"
#include "src/image_Charging_lightning_bits.h"

extern TFT_eSPI tft;
extern TFT_eSprite clockSprite;

void clock_redraw(TFT_eSPI* tft, TFT_eSprite* spr);

extern bool        g_wifiEnabled;
extern const char* g_wifiModeStr;
extern int         g_netSelected;

struct Tile {
    int x, y;
    int iconX, iconY;
    int textX, textY;
    const char* label;
    const unsigned char* icon;
    int iconW, iconH;
};

static const Tile TILES[6] = {
    { 10, 54,  21, 56,  16, 76, "Camera",   image_download__2__bits, 21, 21 },
    { 58, 54,  69, 61,  72, 76, "Wifi",     image_External_ant_1_bits, 19, 11 },
    {106, 54, 119, 58, 118, 76, "Files",    image_folder_file_bits, 18, 16 },
    { 10, 88,  24, 92,  26,110, "BT",       image_Ble_connected_bits, 15, 15 },
    { 58, 88,  72, 90,  71,110, "HID",      image_usb_cable_connected_bits, 16, 16 },
    {106, 88, 120, 92, 108,110, "Settings", image_menu_settings_gear_bits, 16, 16 },
};

#define TILE_W 43
#define TILE_H 31

static inline void theme_topbar_impl(const char* title) {
    tft.drawFastHLine(0, 18, 160, T_BORDER);
    tft.setTextColor(T_ORANGE, T_BG);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString(title, 80, 5, 1);
}

void menu_drawLogoScreen() {
    tft.startWrite();
    tft.fillScreen(T_BG);
    tft.drawBitmap(57, 30, image_passport_happy1_bits, 46, 49, T_ORANGE);
    tft.setTextFont(1);
    tft.setTextSize(2);
    tft.setTextColor(T_ORANGE, T_BG);
    tft.drawCentreString("Welcome", 80, 92, 1);
    tft.endWrite();
}

void menu_drawBlackScreen() {
    tft.startWrite();
    tft.fillScreen(T_BG);
    tft.endWrite();
}

void menu_drawLoader() {
    tft.startWrite();
    tft.fillScreen(T_BG);
    tft.endWrite();
}

void menu_pumpLoader(int* lastFrame) {
    int frame = (millis() / 17) % 61;
    if (frame == *lastFrame) return;
    *lastFrame = frame;
    tft.setBitmapColor(T_ORANGE, T_BG);
    tft.pushImage(68, 52, 24, 24,
                  icons8_loading_frames[frame],
                  false, nullptr);
}

#define STATUS_ICON_X    6
#define STATUS_ICON_Y    4
#define STATUS_TEXT_X    17
#define STATUS_TEXT_Y    6
#define STATUS_BOX_W     60
#define STATUS_BOX_H     18

void menu_drawStatusStrip(bool online, bool apActive) {
    tft.fillRect(0, 0, STATUS_BOX_W, STATUS_BOX_H, T_SURFACE);
    tft.drawBitmap(STATUS_ICON_X, STATUS_ICON_Y,
                   image_download__1__bits, 8, 8,
                   online ? T_ORANGE : T_BORDER);
    tft.setTextColor(T_TEXT, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Picopixel);
    if (online) {
        tft.drawString("ONLINE", STATUS_TEXT_X, STATUS_TEXT_Y);
    } else if (apActive) {
        tft.drawString("AP", STATUS_TEXT_X, STATUS_TEXT_Y);
    } else {
        tft.drawString("OFFLINE", STATUS_TEXT_X, STATUS_TEXT_Y);
        tft.drawLine(6, 11, 13, 20, T_ORANGE);
    }
}

void menu_statusPatch(bool online, bool apActive) {
    tft.startWrite();
    menu_drawStatusStrip(online, apActive);
    tft.endWrite();
}

static void menu_drawTileOnly(TFT_eSPI& t, int idx, bool selected) {
    const Tile& tile = TILES[idx];
    if (selected) {
        t.fillRect(tile.x, tile.y, TILE_W, TILE_H, T_ORANGE);
        t.drawRect(tile.x, tile.y, TILE_W, TILE_H, T_ORANGE);
        t.drawBitmap(tile.iconX, tile.iconY, tile.icon, tile.iconW, tile.iconH, T_BLACK);
        t.setTextColor(T_BLACK, T_ORANGE);
        t.setTextSize(1);
        t.setFreeFont(&Org_01);
        t.drawString(tile.label, tile.textX, tile.textY);
    } else {
        t.fillRect(tile.x, tile.y, TILE_W, TILE_H, T_SURFACE);
        t.drawRect(tile.x, tile.y, TILE_W, TILE_H, T_BORDER);
        t.drawBitmap(tile.iconX, tile.iconY, tile.icon, tile.iconW, tile.iconH, T_ORANGE);
        t.setTextColor(T_TEXT, T_SURFACE);
        t.setTextSize(1);
        t.setFreeFont(&Org_01);
        t.drawString(tile.label, tile.textX, tile.textY);
    }
}

void menu_drawFull(int menuTile, bool online, bool apActive) {
    tft.startWrite();
    tft.fillScreen(T_BG);
    menu_drawStatusStrip(online, apActive);
    for (int i = 0; i < 6; i++) menu_drawTileOnly(tft, i, i == menuTile);
    tft.endWrite();
    clock_redraw(&tft, &clockSprite);
}

void menu_redrawPatch(int oldTile, int newTile) {
    tft.startWrite();
    menu_drawTileOnly(tft, oldTile, false);
    menu_drawTileOnly(tft, newTile, true);
    tft.endWrite();
}

#define WIFI_OPT_X_L   8
#define WIFI_OPT_X_R   83
#define WIFI_OPT_Y     39
#define WIFI_OPT_W     69
#define WIFI_OPT_H     74

static void menu_wifiDrawTile(int idx, bool selected) {
    int x = (idx == 0) ? WIFI_OPT_X_L : WIFI_OPT_X_R;
    const char* label = (idx == 0) ? "NETWORK" : "SCAN";
    const unsigned char* icon = (idx == 0) ? image_download_bits
                                            : image_file_search_bits;
    int iw = (idx == 0) ? 21 : 15;
    int ih = (idx == 0) ? 21 : 16;
    int ix = x + 25;
    int iy = (idx == 0) ? (WIFI_OPT_Y + 17) : (WIFI_OPT_Y + 20);
    int tx = (idx == 0) ? (x + 6)  : (x + 17);
    int ty = WIFI_OPT_Y + 47;
    uint16_t fg, bg, bd;
    if (selected) { bg = T_ORANGE; bd = T_ORANGE; fg = T_BLACK; }
    else          { bg = T_SURFACE; bd = T_BORDER; fg = T_TEXT; }
    tft.fillRect(x, WIFI_OPT_Y, WIFI_OPT_W, WIFI_OPT_H, bg);
    tft.drawRect(x, WIFI_OPT_Y, WIFI_OPT_W, WIFI_OPT_H, bd);
    tft.drawBitmap(ix, iy, icon, iw, ih, fg);
    tft.setTextColor(fg, bg);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawString(label, tx, ty);
}

void menu_drawWifiOptions(int selected) {
    tft.startWrite();
    tft.fillRect(0, 0, 160, 128, T_BG);
    theme_topbar_impl("WIFI OPTIONS");
    menu_wifiDrawTile(0, selected == 0);
    menu_wifiDrawTile(1, selected == 1);
    tft.endWrite();
}

void menu_wifiOptionsRedraw(int oldSel, int newSel) {
    tft.startWrite();
    if (oldSel != newSel) {
        menu_wifiDrawTile(oldSel, false);
        menu_wifiDrawTile(newSel, true);
    }
    tft.endWrite();
}

#define NET_ROW_X      8
#define NET_ROW_W      144
#define NET_ROW_H      17
#define NET_ROW_Y0     30
#define NET_ROW_STEP   19
#define NET_ROW_COUNT  4

static void network_drawRow(int idx, bool selected) {
    int y = NET_ROW_Y0 + idx * NET_ROW_STEP;
    uint16_t bg, fg, bd;
    if (selected) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
    else          { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
    tft.fillRect(NET_ROW_X, y, NET_ROW_W, NET_ROW_H, bg);
    tft.drawRect(NET_ROW_X, y, NET_ROW_W, NET_ROW_H, bd);
    const char* label = "";
    const char* value = "";
    switch (idx) {
        case 0: label = "WIFI";    value = g_wifiEnabled ? "ON" : "OFF"; break;
        case 1: label = "Connect"; value = ""; break;
        case 2: label = "Mode";    value = g_wifiModeStr; break;
        case 3: label = "TEST";    value = ""; break;
    }
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.setTextColor(fg, bg);
    tft.drawString(label, NET_ROW_X + 6, y + 5);
    int px = NET_ROW_X + NET_ROW_W - 40;
    int py = y + 2;
    if (idx == 0 || idx == 2) {
        tft.drawRect(px, py, 34, 13, fg);
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setFreeFont(&Picopixel);
        int vw = tft.textWidth(value);
        tft.drawString(value, px + (34 - vw) / 2, py + 4);
        tft.setFreeFont(&Petme8x8);
    } else {
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setFreeFont(&Picopixel);
        tft.drawString(">", NET_ROW_X + NET_ROW_W - 14, y + 5);
        tft.setFreeFont(&Petme8x8);
    }
}

void menu_drawNetwork(int selected) {
    tft.startWrite();
    tft.fillRect(0, 0, 160, 128, T_BG);
    theme_topbar_impl("Network");
    for (int i = 0; i < NET_ROW_COUNT; i++) network_drawRow(i, i == selected);
    tft.endWrite();
}

void menu_networkPatch(int oldSel, int newSel) {
    tft.startWrite();
    network_drawRow(oldSel, false);
    network_drawRow(newSel, true);
    tft.endWrite();
}

void menu_networkRefreshRow(int idx) {
    tft.startWrite();
    network_drawRow(idx, idx == g_netSelected);
    tft.endWrite();
}

#define WIFI_LIST_X       6
#define WIFI_LIST_Y       24
#define WIFI_LIST_W       148
#define WIFI_LIST_ROW_H   17
#define WIFI_LIST_ROWS    5

void menu_drawWifiScanList(const String* ssids, int count,
                           int selected, int top) {
    tft.startWrite();
    tft.fillScreen(T_BG);
    theme_topbar_impl("Select WiFi");
    if (count == 0) {
        tft.setTextColor(T_TEXT, T_BG);
        tft.setTextSize(1);
        tft.setFreeFont(&Petme8x8);
        tft.drawCentreString("No networks found", 80, 60, 1);
        tft.setTextColor(T_DIM, T_BG);
        tft.drawCentreString("hold GPIO3 to go back", 80, 76, 1);
        tft.endWrite();
        return;
    }
    for (int i = 0; i < WIFI_LIST_ROWS; i++) {
        int listIdx = top + i;
        if (listIdx >= count) break;
        int y = WIFI_LIST_Y + i * WIFI_LIST_ROW_H;
        bool sel = (listIdx == selected);
        uint16_t bg, fg, bd;
        if (sel) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
        else     { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
        tft.fillRect(WIFI_LIST_X, y, WIFI_LIST_W, WIFI_LIST_ROW_H - 2, bg);
        tft.drawRect(WIFI_LIST_X, y, WIFI_LIST_W, WIFI_LIST_ROW_H - 2, bd);
        if (sel) {
            tft.setTextColor(T_BLACK, T_ORANGE);
            tft.setFreeFont(&Petme8x8);
            tft.setTextSize(1);
            tft.drawString(">", WIFI_LIST_X + 3, y + 4);
        }
        tft.setTextSize(1);
        tft.setFreeFont(&Petme8x8);
        tft.setTextColor(fg, bg);
        String name = ssids[listIdx];
        if (name.length() > 15) name = name.substring(0, 15);
        tft.drawString(name, WIFI_LIST_X + 14, y + 4);
    }
    if (count > WIFI_LIST_ROWS) {
        int trackH = WIFI_LIST_ROWS * WIFI_LIST_ROW_H;
        int barH   = trackH / count;
        if (barH < 8) barH = 8;
        int barY   = WIFI_LIST_Y + (top * trackH) / count;
        tft.fillRect(156, WIFI_LIST_Y, 2, trackH, T_SURFACE);
        tft.fillRect(156, barY, 2, barH, T_ORANGE);
    }
    tft.endWrite();
}

void menu_drawQrFrame() {
    const uint16_t C = T_ORANGE;
    tft.drawFastVLine(41,  33, 15, C);
    tft.drawFastVLine(119, 33, 15, C);
    tft.drawFastVLine(41,  82, 15, C);
    tft.drawFastVLine(119, 82, 15, C);
    tft.drawFastHLine(41,  32, 16, C);
    tft.drawFastHLine(104, 32, 16, C);
    tft.drawFastHLine(41,  96, 16, C);
    tft.drawFastHLine(104, 96, 16, C);
}

void menu_drawScanQr() {
    tft.startWrite();
    menu_drawQrFrame();
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.setTextColor(T_ORANGE, T_BG);
    tft.drawCentreString("SCAN QR", 80, 8, 1);
    tft.endWrite();
}

void menu_drawQrConfirm(const char* ssid, const char* pass, bool yesSelected) {
    tft.startWrite();
    tft.fillScreen(T_BG);
    theme_topbar_impl("Confirm");
    tft.setTextColor(T_TEXT, T_BG);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("JOIN THIS NETWORK?", 80, 26, 1);
    tft.setTextColor(T_DIM, T_BG);
    tft.drawString("SSID", 8, 44);
    tft.setTextColor(T_TEXT, T_BG);
    tft.drawString(ssid, 8, 56);
    tft.setTextColor(T_DIM, T_BG);
    tft.drawString("PASS", 8, 72);
    tft.setTextColor(T_TEXT, T_BG);
    tft.drawString(pass, 8, 84);
    auto drawBtn = [](int x, int y, const char* label, bool highlighted) {
        uint16_t bg, fg, bd;
        if (highlighted) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
        else            { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
        tft.fillRect(x, y, 44, 20, bg);
        tft.drawRect(x, y, 44, 20, bd);
        tft.setTextColor(fg, bg);
        tft.setTextSize(1);
        tft.setFreeFont(&Petme8x8);
        tft.drawCentreString(label, x + 22, y + 6, 1);
    };
    drawBtn(15, 104, "Yes",  yesSelected);
    drawBtn(99, 104, "No",  !yesSelected);
    tft.endWrite();
}

void menu_qrConfirmPatch(bool yesSelected) {
    auto drawBtn = [](int x, int y, const char* label, bool highlighted) {
        uint16_t bg, fg, bd;
        if (highlighted) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
        else            { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
        tft.fillRect(x, y, 44, 20, bg);
        tft.drawRect(x, y, 44, 20, bd);
        tft.setTextColor(fg, bg);
        tft.setTextSize(1);
        tft.setFreeFont(&Petme8x8);
        tft.drawCentreString(label, x + 22, y + 6, 1);
    };
    tft.startWrite();
    drawBtn(15, 104, "Yes",  yesSelected);
    drawBtn(99, 104, "No",  !yesSelected);
    tft.endWrite();
}

static void menu_drawPopupButton(TFT_eSPI& t, int x, int y,
                                 const char* label, bool highlighted) {
    uint16_t bg, fg, bd;
    if (highlighted) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
    else            { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
    t.fillRect(x, y, 44, 20, bg);
    t.drawRect(x, y, 44, 20, bd);
    t.setTextColor(fg, bg);
    t.setTextSize(1);
    t.setFreeFont(&Petme8x8);
    t.drawCentreString(label, x + 22, y + 6, 1);
}

void menu_drawPopupFull(bool popupYes) {
    tft.startWrite();
    tft.fillScreen(T_BG);
    theme_topbar_impl("Preview?");
    tft.setTextColor(T_TEXT, T_BG);
    tft.setTextSize(1);
    tft.setFreeFont(&Org_01);
    tft.drawCentreString("Do you want to turn on", 80, 42, 1);
    tft.drawCentreString("view live preview?",    80, 58, 1);
    menu_drawPopupButton(tft, 15, 88, "Yes", popupYes);
    menu_drawPopupButton(tft, 99, 88, "No",  !popupYes);
    tft.endWrite();
}

void menu_drawPopupPatch(bool popupYes) {
    tft.startWrite();
    menu_drawPopupButton(tft, 15, 88, "Yes", popupYes);
    menu_drawPopupButton(tft, 99, 88, "No",  !popupYes);
    tft.endWrite();
}

void menu_drawCaptureWait() {
    tft.startWrite();
    tft.fillScreen(T_BG);
    tft.drawBitmap(67, 43, image_camera_bits, 26, 24, T_TEXT);
    tft.setTextColor(T_TEXT, T_BG);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("Please Wait...", 80, 77, 1);
    tft.endWrite();
}

#define CAM_MODE_X      8
#define CAM_MODE_W      144
#define CAM_MODE_H      17
#define CAM_MODE_Y0     30
#define CAM_MODE_STEP   19
#define CAM_MODE_COUNT  3

static void cameraMode_drawRow(int idx, bool selected, bool flashOn) {
    int y = CAM_MODE_Y0 + idx * CAM_MODE_STEP;
    uint16_t bg, fg, bd;
    if (selected) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
    else          { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
    tft.fillRect(CAM_MODE_X, y, CAM_MODE_W, CAM_MODE_H, bg);
    tft.drawRect(CAM_MODE_X, y, CAM_MODE_W, CAM_MODE_H, bd);
    const char* label = "";
    const char* value = "";
    bool showBox = false;
    switch (idx) {
        case 0: label = "FLASH";   value = flashOn ? "ON" : "OFF"; showBox = true; break;
        case 1: label = "Shutter"; value = ""; break;
        case 2: label = "Speed";   value = ""; break;
    }
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.setTextColor(fg, bg);
    tft.drawString(label, CAM_MODE_X + 6, y + 5);
    int px = CAM_MODE_X + CAM_MODE_W - 40;
    int py = y + 2;
    if (showBox) {
        tft.drawRect(px, py, 34, 13, fg);
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setFreeFont(&Picopixel);
        int vw = tft.textWidth(value);
        tft.drawString(value, px + (34 - vw) / 2, py + 4);
        tft.setFreeFont(&Petme8x8);
    } else {
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setFreeFont(&Picopixel);
        tft.drawString(">", CAM_MODE_X + CAM_MODE_W - 14, y + 5);
        tft.setFreeFont(&Petme8x8);
    }
}

void menu_drawCameraModes(int selected, bool flashOn) {
    tft.startWrite();
    tft.fillRect(0, 0, 160, 128, T_BG);
    theme_topbar_impl("Camera Modes");
    for (int i = 0; i < CAM_MODE_COUNT; i++) {
        cameraMode_drawRow(i, i == selected, flashOn);
    }
    tft.endWrite();
}

void menu_cameraModesPatch(int oldSel, int newSel, bool flashOn) {
    tft.startWrite();
    cameraMode_drawRow(oldSel, false, flashOn);
    cameraMode_drawRow(newSel, true,  flashOn);
    tft.endWrite();
}

void menu_cameraModesRefreshRow(int idx, bool flashOn) {
    tft.startWrite();
    cameraMode_drawRow(idx, idx == 0, flashOn);
    tft.endWrite();
}

void menu_drawSdMissing() {
    tft.startWrite();
    tft.fillScreen(T_BG);
    theme_topbar_impl("SD Error");
    tft.drawBitmap(63, 42, image_SDQuestion_bits, 35, 43, T_ORANGE);
    tft.setTextColor(T_TEXT, T_BG);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("SD card not found!", 80, 96, 1);
    tft.endWrite();
}

void menu_drawAlert(const char* errText) {
    tft.startWrite();
    tft.fillScreen(T_BG);
    theme_topbar_impl("Alert");
    tft.fillRect(15, 28, 130, 72, T_SURFACE);
    tft.drawRect(15, 28, 130, 72, T_BORDER);
    tft.setTextColor(T_ORANGE, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("ERROR", 80, 34, 1);
    tft.drawFastHLine(15, 47, 130, T_BORDER);
    tft.drawBitmap(66, 53, image_Alert_bits, 28, 24, T_ORANGE);
    tft.setTextColor(T_TEXT, T_SURFACE);
    tft.drawCentreString(errText, 80, 82, 1);
    tft.endWrite();
}

void menu_showNotification(const char* l1, const char* l2,
                           uint16_t color, uint32_t holdMs,
                           Mode returnTo = MODE_MENU,
                           int returnTile = -1) {
    (void)holdMs; (void)returnTo; (void)returnTile;
    tft.startWrite();
    tft.fillScreen(T_BG);
    tft.fillRect(0, 0, 160, 18, T_SURFACE);
    tft.drawFastHLine(0, 18, 160, T_BORDER);
    tft.setTextColor(T_ORANGE, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString("DuckyMini", 80, 5, 1);
    tft.fillRect(10, 38, 140, 60, T_SURFACE);
    tft.drawRect(10, 38, 140, 60, T_BORDER);
    tft.setTextColor(color, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawCentreString(l1, 80, 55, 1);
    if (l2) {
        tft.setTextColor(T_TEXT, T_SURFACE);
        tft.drawCentreString(l2, 80, 78, 1);
    }
    tft.endWrite();
}

void menu_drawDiag(bool espConnected, bool sdPresent) {
    tft.startWrite();
    tft.fillScreen(T_BG);
    theme_topbar_impl("Diagnostics");
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    char buf[48];
    int y = 30;
    auto row = [&](const char* k, const char* v, uint16_t vc) {
        tft.setTextColor(T_DIM, T_BG);
        tft.drawString(k, 8, y);
        tft.setTextColor(vc, T_BG);
        tft.drawString(v, 60, y);
        y += 14;
    };
    row("Link:",   espConnected ? "up"   : "down",  espConnected ? T_ORANGE : T_DIM);
    row("SD:",     sdPresent    ? "yes"  : "no",    sdPresent    ? T_ORANGE : T_DIM);
    snprintf(buf, sizeof(buf), "%u K",  ESP.getFreeHeap() / 1024);
    row("Heap:",   buf, T_TEXT);
    snprintf(buf, sizeof(buf), "%lu s", millis() / 1000);
    row("Uptime:", buf, T_TEXT);
    snprintf(buf, sizeof(buf), "%d",    esp_reset_reason());
    row("Reset:",  buf, T_TEXT);
    tft.endWrite();
}