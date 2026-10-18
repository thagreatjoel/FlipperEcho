/*
 * keyboard.h — Flipper Zero-style on-screen keyboard
 */

#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "src/Org_01.h"
#include "theme.h"

extern TFT_eSPI tft;

#define KB_ROWS 4
#define KB_COLS 12

#define KB_MODE_LOWER  0
#define KB_MODE_UPPER  1
#define KB_MODE_NUM    2

static const char KB_LOWER[3][KB_COLS] = {
    { 'q','w','e','r','t','y','u','i','o','p','\0','\0' },
    { 'a','s','d','f','g','h','j','k','l','.','\0','\0' },
    { 'z','x','c','v','b','n','m','-','_','?','\0','\0' },
};
static const char KB_UPPER[3][KB_COLS] = {
    { 'Q','W','E','R','T','Y','U','I','O','P','\0','\0' },
    { 'A','S','D','F','G','H','J','K','L','.','\0','\0' },
    { 'Z','X','C','V','B','N','M','-','_','?','\0','\0' },
};
static const char KB_NUM[3][KB_COLS] = {
    { '1','2','3','4','5','6','7','8','9','0','\0','\0' },
    { '!','@','#','$','%','^','&','*','(',')','\0','\0' },
    { '-','_','=','+','[',']','{','}','/','\\','\0','\0' },
};

#define KB_SPACE_COL_FIRST  0
#define KB_SPACE_COL_LAST   5
#define KB_MODE_COL         6
#define KB_DEL_COL          7
#define KB_OK_COL           8

#define KB_KEY_W      12
#define KB_KEY_H      11
#define KB_COL_GAP    2
#define KB_ROW_GAP    2
#define KB_ORIGIN_X   10
#define KB_ORIGIN_Y   61
#define KB_ROW_STEP   (KB_KEY_H + KB_ROW_GAP)
#define KB_COL_STEP   (KB_KEY_W + KB_COL_GAP)

#define KB_FIELD_X    4
#define KB_FIELD_Y    23
#define KB_FIELD_W    152
#define KB_FIELD_H    16

#define KB_KEY_MODE   0x01

static int   kb_cursor_row = 0;
static int   kb_cursor_col = 0;
static String kb_text = "";
static String kb_title = "SSID:";
static uint32_t kb_last_blink_ms = 0;
static bool  kb_cursor_visible = true;
static bool  kb_done = false;
static bool  kb_cancelled = false;
static int   kb_mode = KB_MODE_LOWER;

static inline int kb_keyX(int col) { return KB_ORIGIN_X + col * KB_COL_STEP; }
static inline int kb_keyY(int row) { return KB_ORIGIN_Y + row * KB_ROW_STEP; }

static const char* kb_activeLayoutRow(int row) {
    if (row < 0 || row > 2) return KB_LOWER[0];
    if (kb_mode == KB_MODE_UPPER) return KB_UPPER[row];
    if (kb_mode == KB_MODE_NUM)   return KB_NUM[row];
    return KB_LOWER[row];
}

static char kb_currentChar() {
    if (kb_cursor_row == 3) {
        if (kb_cursor_col >= KB_SPACE_COL_FIRST &&
            kb_cursor_col <= KB_SPACE_COL_LAST) return ' ';
        if (kb_cursor_col == KB_MODE_COL) return KB_KEY_MODE;
        if (kb_cursor_col == KB_DEL_COL)  return '\b';
        if (kb_cursor_col == KB_OK_COL)   return '\n';
        return '\0';
    }
    if (kb_cursor_row < 0 || kb_cursor_row >= 3) return '\0';
    if (kb_cursor_col < 0 || kb_cursor_col >= KB_COLS) return '\0';
    return kb_activeLayoutRow(kb_cursor_row)[kb_cursor_col];
}

static bool kb_cellValid(int row, int col) {
    if (row == 3) return (col >= 0 && col <= KB_OK_COL);
    if (row < 0 || row > 2) return false;
    if (col < 0 || col >= KB_COLS) return false;
    return kb_activeLayoutRow(row)[col] != '\0';
}

static void kb_skipEmpty() {
    for (int i = 0; i < 200; i++) {
        if (kb_cellValid(kb_cursor_row, kb_cursor_col)) return;
        kb_cursor_col++;
        if (kb_cursor_col >= KB_COLS) {
            kb_cursor_col = 0;
            kb_cursor_row++;
            if (kb_cursor_row >= KB_ROWS) kb_cursor_row = 0;
        }
    }
}

static void kb_drawKey(int row, int col, char label, bool selected) {
    int x = kb_keyX(col);
    int y = kb_keyY(row);
    uint16_t bg, fg, bd;
    if (selected) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
    else          { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
    tft.fillRect(x, y, KB_KEY_W, KB_KEY_H, bg);
    tft.drawRect(x, y, KB_KEY_W, KB_KEY_H, bd);
    tft.setTextColor(fg, bg);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setFreeFont(&Org_01);
    char buf[2] = { label, 0 };
    if (label == '\b')      strcpy(buf, "<");
    else if (label == '\n') strcpy(buf, ">");
    else if (label == ' ')  strcpy(buf, "_");
    tft.drawString(buf, x + 3, y + 2);
}

static void kb_drawField() {
    tft.fillRect(0, 0, 160, 18, T_SURFACE);
    tft.drawFastHLine(0, 18, 160, T_BORDER);
    tft.setTextColor(T_ORANGE, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Petme8x8);
    tft.drawString(kb_title, 6, 5);
    tft.fillRect(KB_FIELD_X, KB_FIELD_Y, KB_FIELD_W, KB_FIELD_H, T_SURFACE);
    tft.drawRect(KB_FIELD_X, KB_FIELD_Y, KB_FIELD_W, KB_FIELD_H, T_BORDER);
    tft.setTextColor(T_TEXT, T_SURFACE);
    tft.setTextSize(1);
    tft.setFreeFont(&Org_01);
    String line = kb_text;
    if (kb_cursor_visible) line += "_";
    const int maxChars = 26;
    if ((int)line.length() > maxChars) {
        line = line.substring(line.length() - maxChars);
    }
    tft.drawString(line, KB_FIELD_X + 4, KB_FIELD_Y + 6);
}

static void kb_drawSpaceBar(bool selected) {
    int x = kb_keyX(KB_SPACE_COL_FIRST);
    int y = kb_keyY(3);
    int w = (KB_SPACE_COL_LAST - KB_SPACE_COL_FIRST + 1) * KB_COL_STEP - KB_COL_GAP;
    uint16_t bg, fg, bd;
    if (selected) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
    else          { bg = T_SURFACE; fg = T_TEXT; bd = T_BORDER; }
    tft.fillRect(x, y, w, KB_KEY_H, bg);
    tft.drawRect(x, y, w, KB_KEY_H, bd);
    tft.setTextColor(fg, bg);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setFreeFont(&Org_01);
    int tw = tft.textWidth("SPACE");
    tft.drawString("SPACE", x + (w - tw) / 2, y + 2);
}

static void kb_drawModeKey(bool selected) {
    int x = kb_keyX(KB_MODE_COL);
    int y = kb_keyY(3);
    int w = KB_KEY_W;
    int h = KB_KEY_H;
    uint16_t bg, fg, bd;
    if (selected) { bg = T_ORANGE; fg = T_BLACK; bd = T_ORANGE; }
    else          { bg = T_SURFACE; fg = T_ORANGE; bd = T_ORANGE; }
    tft.fillRect(x, y, w, h, bg);
    tft.drawRect(x, y, w, h, bd);
    tft.setTextColor(fg, bg);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setFreeFont(&Org_01);
    const char* label = "abc";
    if (kb_mode == KB_MODE_UPPER) label = "ABC";
    else if (kb_mode == KB_MODE_NUM) label = "123";
    int tw = tft.textWidth(label);
    tft.drawString(label, x + (w - tw) / 2, y + 2);
}

void kb_draw() {
    tft.startWrite();
    tft.fillScreen(T_BG);
    kb_drawField();
    for (int r = 0; r < 3; r++) {
        const char* row = kb_activeLayoutRow(r);
        for (int c = 0; c < KB_COLS; c++) {
            char ch = row[c];
            if (ch == '\0') continue;
            bool sel = (r == kb_cursor_row && c == kb_cursor_col);
            kb_drawKey(r, c, ch, sel);
        }
    }
    bool spaceSel = (kb_cursor_row == 3 &&
                     kb_cursor_col >= KB_SPACE_COL_FIRST &&
                     kb_cursor_col <= KB_SPACE_COL_LAST);
    bool modeSel = (kb_cursor_row == 3 && kb_cursor_col == KB_MODE_COL);
    bool delSel  = (kb_cursor_row == 3 && kb_cursor_col == KB_DEL_COL);
    bool okSel   = (kb_cursor_row == 3 && kb_cursor_col == KB_OK_COL);
    kb_drawSpaceBar(spaceSel);
    kb_drawModeKey(modeSel);
    kb_drawKey(3, KB_DEL_COL, '\b', delSel);
    kb_drawKey(3, KB_OK_COL, '\n', okSel);
    tft.endWrite();
}

static void kb_redrawCursor(int oldRow, int oldCol, int newRow, int newCol) {
    tft.startWrite();
    if (oldRow == 3) {
        if (oldCol >= KB_SPACE_COL_FIRST && oldCol <= KB_SPACE_COL_LAST) kb_drawSpaceBar(false);
        else if (oldCol == KB_MODE_COL) kb_drawModeKey(false);
        else if (oldCol == KB_DEL_COL)  kb_drawKey(3, KB_DEL_COL, '\b', false);
        else if (oldCol == KB_OK_COL)   kb_drawKey(3, KB_OK_COL, '\n', false);
    } else if (oldRow >= 0 && oldRow < 3 && oldCol >= 0 && oldCol < KB_COLS) {
        char c = kb_activeLayoutRow(oldRow)[oldCol];
        if (c != '\0') kb_drawKey(oldRow, oldCol, c, false);
    }
    if (newRow == 3) {
        if (newCol >= KB_SPACE_COL_FIRST && newCol <= KB_SPACE_COL_LAST) kb_drawSpaceBar(true);
        else if (newCol == KB_MODE_COL) kb_drawModeKey(true);
        else if (newCol == KB_DEL_COL)  kb_drawKey(3, KB_DEL_COL, '\b', true);
        else if (newCol == KB_OK_COL)   kb_drawKey(3, KB_OK_COL, '\n', true);
    } else if (newRow >= 0 && newRow < 3 && newCol >= 0 && newCol < KB_COLS) {
        char c = kb_activeLayoutRow(newRow)[newCol];
        if (c != '\0') kb_drawKey(newRow, newCol, c, true);
    }
    tft.endWrite();
}

void kb_init(const char* title) {
    while (Serial.available()) Serial.read();
    kb_title = title;
    kb_text = "";
    kb_cursor_row = 0;
    kb_cursor_col = 0;
    kb_mode = KB_MODE_LOWER;
    kb_skipEmpty();
    kb_last_blink_ms = millis();
    kb_cursor_visible = true;
    kb_done = false;
    kb_cancelled = false;
    kb_draw();
}

void kb_serviceBlink() {
    if (millis() - kb_last_blink_ms > 600) {
        kb_last_blink_ms = millis();
        kb_cursor_visible = !kb_cursor_visible;
        kb_drawField();
    }
}

static void kb_move(int dRow, int dCol) {
    int oldRow = kb_cursor_row;
    int oldCol = kb_cursor_col;
    kb_cursor_col += dCol;
    kb_cursor_row += dRow;
    if (kb_cursor_col < 0) {
        kb_cursor_col = KB_COLS - 1;
        kb_cursor_row--;
        if (kb_cursor_row < 0) kb_cursor_row = KB_ROWS - 1;
    }
    if (kb_cursor_col >= KB_COLS) {
        kb_cursor_col = 0;
        kb_cursor_row++;
        if (kb_cursor_row >= KB_ROWS) kb_cursor_row = 0;
    }
    if (kb_cursor_row < 0) kb_cursor_row = KB_ROWS - 1;
    if (kb_cursor_row >= KB_ROWS) kb_cursor_row = 0;
    kb_skipEmpty();
    if (oldRow == kb_cursor_row && oldCol == kb_cursor_col) return;
    kb_redrawCursor(oldRow, oldCol, kb_cursor_row, kb_cursor_col);
}

void kb_moveLeft()  { kb_move(0, -1); }
void kb_moveRight() { kb_move(0, +1); }
void kb_moveDown()  { kb_move(+1, 0); }
void kb_moveUp()    { kb_move(-1, 0); }

int kb_select() {
    char c = kb_currentChar();
    if (c == '\0') return 0;
    if (c == KB_KEY_MODE) {
        kb_mode = (kb_mode + 1) % 3;
        kb_draw();
        return 0;
    }
    if (c == '\b') {
        if (kb_text.length() > 0) {
            kb_text.remove(kb_text.length() - 1);
            kb_cursor_visible = true;
            kb_last_blink_ms = millis();
            kb_drawField();
        }
        return 0;
    }
    if (c == '\n') { kb_done = true; return 1; }
    if (kb_text.length() < 32) {
        kb_text += c;
        kb_cursor_visible = true;
        kb_last_blink_ms = millis();
        kb_drawField();
    }
    return 0;
}

void kb_cancel() { kb_cancelled = true; }

const String& kb_getText() { return kb_text; }
bool kb_isDone()           { return kb_done; }
bool kb_isCancelled()      { return kb_cancelled; }

bool kb_pushSerial(char c) {
    if (c == '\r' || c == '\n') { kb_done = true; return true; }
    if (c == 8 || c == 127) {
        if (kb_text.length() > 0) {
            kb_text.remove(kb_text.length() - 1);
            kb_cursor_visible = true;
            kb_last_blink_ms = millis();
            kb_drawField();
        }
        return false;
    }
    if (c >= 32 && c < 127) {
        if (kb_text.length() < 32) {
            kb_text += c;
            kb_cursor_visible = true;
            kb_last_blink_ms = millis();
            kb_drawField();
        }
    }
    return false;
}