//================================================================
// ui.h - 屏幕绘制（r12 菜单版）
// 只负责把给定内容画到屏幕上，不含业务逻辑。
//
// 依赖（主文件须按此顺序准备好再 include 本文件）：
//   1. Adafruit_ST7789.h 已 include，tft 对象已定义；
//   2. TFT_W 已定义；
//   3. cn6.h 已 include（字库 CN16_FONT）；
//   4. codes.h 已 include（LAMP_CODES / AC_CODES 码表）。
// 屏幕 240x320，字体 16 点阵（中文 16px，ASCII size2 为 12px 宽）。
//================================================================
#pragma once
#include <Arduino.h>

// ---------------- 首页布局 ----------------
#define TITLE_H   24
#define STATUS_H  110
#define COLOR_Y   (TITLE_H)
#define STATUS_Y  (TFT_H - STATUS_H)
#define COLOR_H   (STATUS_Y - COLOR_Y)

// ---------------- 16 进制串 ----------------
// 按位数补齐前导 0：32 位 -> 8 位，24 位 -> 6 位（显示与码表格式一致）
String hexPad(uint32_t v, uint16_t bits) {
  String s = String(v, HEX);
  s.toUpperCase();
  uint8_t digits = (bits + 3) / 4;
  if (digits > 8) digits = 8;
  if (digits < 1) digits = 1;
  while (s.length() < digits) s = "0" + s;
  return "0x" + s;
}

// ---------------- 中文 16 点阵混排 ----------------
// ASCII 走 Adafruit 内置字体（size2：12px 宽、16px 高），
// 中文走 cn6.h 的 16 点阵（scale=1 为 16px，scale=2 为 32px）。
static const uint8_t *findGlyph(uint16_t code) {
  for (unsigned int i = 0; i < CN16_FONT_COUNT; i++) {
    if (CN16_FONT[i].code == code) return CN16_FONT[i].bitmap;
  }
  return nullptr;
}

// 在 (x, y) 画一个中文字符
static void drawCNChar(int16_t x, int16_t y, uint16_t code,
                       uint16_t color, uint16_t bg, uint8_t scale) {
  const uint8_t *g = findGlyph(code);
  tft.fillRect(x, y, 16 * scale, 16 * scale, bg);
  if (!g) {  // 字库里没有这个字：画框代替
    tft.drawRect(x, y, 16 * scale, 16 * scale, color);
    return;
  }
  for (uint8_t row = 0; row < 16; row++) {
    uint16_t bits = (uint16_t(g[row * 2]) << 8) | g[row * 2 + 1];
    for (uint8_t col = 0; col < 16; col++) {
      if (bits & (0x8000 >> col)) {
        if (scale == 1) tft.drawPixel(x + col, y + row, color);
        else tft.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

// 混排字符串宽度（ASCII 12px/字，中文 16*scale px/字）
static int16_t cnTextWidth(const char *s, uint8_t scale) {
  int16_t w = 0;
  while (*s) {
    uint8_t c = (uint8_t)*s;
    if (c < 0x80) { w += 12; s++; }
    else if ((c & 0xF0) == 0xE0) { w += 16 * scale; s += 3; }
    else s++;
  }
  return w;
}

// 混排打印 UTF-8 字符串，返回结束 x 坐标
static int16_t printCN(int16_t x, int16_t y, const char *s,
                       uint16_t color, uint16_t bg, uint8_t scale = 1) {
  tft.setTextSize(2);
  tft.setTextColor(color, bg);
  while (*s) {
    uint8_t c = (uint8_t)*s;
    if (c < 0x80) {
      tft.setCursor(x, y);
      tft.write(c);
      x += 12;
      s++;
    } else if ((c & 0xF0) == 0xE0) {
      uint16_t code = ((uint16_t)(c & 0x0F) << 12)
                    | ((uint16_t)((uint8_t)s[1] & 0x3F) << 6)
                    | ((uint8_t)s[2] & 0x3F);
      drawCNChar(x, y, code, color, bg, scale);
      x += 16 * scale;
      s += 3;
    } else {
      s++;
    }
  }
  return x;
}

// ---------------- 通用绘制 ----------------
// 蓝色标题栏
void uiTitle(const char *title) {
  tft.fillRect(0, 0, TFT_W, TITLE_H, ST77XX_BLUE);
  printCN(8, 5, title, ST77XX_WHITE, ST77XX_BLUE, 1);
}

// 列表：col1 为名，col2 为码串（可为 NULL，只显示名）。
// 选中行反白（白底黑字）并加 "> " 标记；行高 rowH，首行 y0。
void uiList(const char *const *col1, const char *const *col2,
            uint8_t count, uint8_t sel, int16_t y0, int16_t rowH) {
  for (uint8_t i = 0; i < count; i++) {
    bool isSel = (i == sel);
    uint16_t bg = isSel ? ST77XX_WHITE : ST77XX_BLACK;
    uint16_t fg = isSel ? ST77XX_BLACK : ST77XX_WHITE;
    tft.fillRect(0, y0 + i * rowH, TFT_W, rowH, bg);
    String line = isSel ? "> " : "  ";
    line += col1[i];
    if (col2) { line += " "; line += col2[i]; }
    printCN(8, y0 + i * rowH + (rowH - 16) / 2, line.c_str(), fg, bg, 1);
  }
}

// ---------------- 首页（颜色测试 + 状态区）----------------
struct ColorItem { const char *name; const char *cn; uint16_t c; };
ColorItem colorList[] = {
  {"RED",     "红色", ST77XX_RED},
  {"GREEN",   "绿色", ST77XX_GREEN},
  {"BLUE",    "蓝色", ST77XX_BLUE},
  {"YELLOW",  "黄色", ST77XX_YELLOW},
  {"CYAN",    "青色", ST77XX_CYAN},
  {"MAGENTA", "品红", ST77XX_MAGENTA},
  {"WHITE",   "白色", ST77XX_WHITE},
  {"BLACK",   "黑色", ST77XX_BLACK},
};
const uint8_t COLOR_N = sizeof(colorList) / sizeof(colorList[0]);

void drawTitle() {
  tft.fillRect(0, 0, TFT_W, TITLE_H, ST77XX_BLUE);
  printCN(8, 5, "ESP32-S2 硬件测试", ST77XX_WHITE, ST77XX_BLUE, 1);
}

void showColor(uint8_t i) {
  uint16_t bg = colorList[i].c;
  tft.fillRect(0, COLOR_Y, TFT_W, COLOR_H, bg);
  bool bright = (bg == ST77XX_WHITE || bg == ST77XX_YELLOW ||
                 bg == ST77XX_CYAN  || bg == ST77XX_GREEN);
  uint16_t fg = bright ? ST77XX_BLACK : ST77XX_WHITE;
  // 中文色名 32px 居中，英文名放下方
  int16_t cy = COLOR_Y + (COLOR_H - 60) / 2;
  int16_t cw = cnTextWidth(colorList[i].cn, 2);
  printCN((TFT_W - cw) / 2, cy, colorList[i].cn, fg, bg, 2);
  tft.setTextSize(2);
  tft.setTextColor(fg, bg);
  String s = colorList[i].name;
  tft.setCursor((TFT_W - (int16_t)s.length() * 12) / 2, cy + 44);
  tft.print(s);
}

void drawHomeScreen(uint8_t colorIdx, int8_t lastKey, uint8_t ledCode,
                    const String &proto, const String &hex, const String &sent) {
  drawTitle();
  showColor(colorIdx);
  tft.fillRect(0, STATUS_Y, TFT_W, STATUS_H, ST77XX_BLACK);

  // 第 1 行：按键与指示灯
  if (lastKey < 0) {
    printCN(6, STATUS_Y + 8, "按键: --  指示灯: --",
            ST77XX_WHITE, ST77XX_BLACK, 1);
  } else {
    String line = "按键: KEY" + String(lastKey + 1) + "  指示灯: " +
                  String((ledCode >> 1) & 1) + String(ledCode & 1);
    printCN(6, STATUS_Y + 8, line.c_str(),
            ST77XX_WHITE, ST77XX_BLACK, 1);
  }

  // 第 2 行：接收到的协议名
  printCN(6, STATUS_Y + 32, ("接收: " + proto).c_str(),
          ST77XX_WHITE, ST77XX_BLACK, 1);

  // 第 3 行：接收到的解码值（缩进 4 格，与协议名对齐）
  printCN(6, STATUS_Y + 56, ("    " + hex).c_str(),
          ST77XX_WHITE, ST77XX_BLACK, 1);

  // 第 4 行：发送状态
  printCN(6, STATUS_Y + 80, ("发送: " + sent).c_str(),
          ST77XX_WHITE, ST77XX_BLACK, 1);
}

// ---------------- 一级菜单 ----------------
const char *MENU_ITEMS[] = {"灯", "空调", "红外解码", "关于", "返回"};
const uint8_t MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

void drawMenuScreen(uint8_t sel) {
  tft.fillScreen(ST77XX_BLACK);
  uiTitle("菜单");
  uiList(MENU_ITEMS, NULL, MENU_COUNT, sel, 40, 32);
  printCN(8, 264, "KEY2 下 KEY3 上", ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 288, "KEY1 进入 KEY4 返回", ST77XX_WHITE, ST77XX_BLACK, 1);
}

// ---------------- 灯子菜单 ----------------
// 列表 9 行（行高 24），底部三行：发送提示 / 解码提示 / 按键提示
void drawLampScreen(uint8_t sel, const String &sent, const String &recv) {
  tft.fillScreen(ST77XX_BLACK);
  uiTitle("灯");
  const char *names[16];
  const char *hexes[16];
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    names[i] = LAMP_CODES[i].name;
    hexes[i] = LAMP_CODES[i].hex;
  }
  uiList(names, hexes, LAMP_COUNT, sel, 28, 24);
  printCN(8, 248, ("发送: " + sent).c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 272, ("接收: " + recv).c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 296, "KEY1 发送 KEY4 返回", ST77XX_WHITE, ST77XX_BLACK, 1);
}

// ---------------- 空调子菜单 ----------------
void drawAcScreen(uint8_t sel, const String &sent, const String &recv) {
  tft.fillScreen(ST77XX_BLACK);
  uiTitle("空调");
  const char *names[16];
  const char *hexes[16];
  for (uint8_t i = 0; i < AC_COUNT; i++) {
    names[i] = AC_CODES[i].name;
    hexes[i] = AC_CODES[i].hex;
  }
  uiList(names, hexes, AC_COUNT, sel, 40, 32);
  printCN(8, 120, "注: 关机码目前无效", ST77XX_YELLOW, ST77XX_BLACK, 1);
  printCN(8, 248, ("发送: " + sent).c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 272, ("接收: " + recv).c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 296, "KEY1 发送 KEY4 返回", ST77XX_WHITE, ST77XX_BLACK, 1);
}

// ---------------- 红外解码子菜单 ----------------
void drawDecodeScreen(const String &proto, const String &hex,
                      uint16_t bits, uint16_t rawLen) {
  tft.fillScreen(ST77XX_BLACK);
  uiTitle("红外解码");
  printCN(8, 40, ("协议: " + proto).c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 68, ("数值: " + hex).c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  String bitsLine = "位数: ";
  if (bits == 0) bitsLine += "--";
  else { bitsLine += String(bits); bitsLine += " Bits"; }
  printCN(8, 96, bitsLine.c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  String rawLine = "原始: ";
  if (rawLen == 0) rawLine += "--";
  else { rawLine += String(rawLen); rawLine += "项"; }
  printCN(8, 124, rawLine.c_str(), ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 160, "对着板子按遥控器", ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 288, "KEY1 清除 KEY4 返回", ST77XX_WHITE, ST77XX_BLACK, 1);
}

// ---------------- 关于子菜单 ----------------
void drawAboutScreen() {
  tft.fillScreen(ST77XX_BLACK);
  uiTitle("关于");
  printCN(8, 40, "ESP32-S2 硬件测试 r12", ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 68, "红外接收 IO16 发送 IO17", ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 96, "KEY4 菜单 KEY1 确认", ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 124, "KEY2 加 KEY3 减", ST77XX_WHITE, ST77XX_BLACK, 1);
  printCN(8, 288, "KEY4 返回", ST77XX_WHITE, ST77XX_BLACK, 1);
}
