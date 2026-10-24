//================================================================
// ESP32S2_HWTest_r5.ino
// CoreBoard ESP32-S2 mini 四项硬件测试（r5：屏幕中文显示）
//
// 1. ST7789 屏幕：每 2 秒循环切换颜色（中文色名 32px + 英文名）
// 2. 按键：KEY1~KEY4 按下 -> 值 0~3 -> LED1/LED2 按二进制显示 00/01/10/11
//           （LED1 为高位，LED2 为低位；按键松开后指示灯保持显示）
// 3. 红外接收（IO16）：解码结果打印到串口，同时显示在屏幕状态区
// 4. 红外发送（IO17）：每 3 秒为一周期，发射 1.5 秒、静默 1.5 秒；
//           发射的是 38kHz NEC 码，肉眼不可见，用手机摄像头对准
//           红外发射管可看到闪烁
//
// ---- 引脚（来自原理图）----
//   ST7789: CS=IO5  DC=IO3  RST=IO1  MOSI=IO11  SCK=IO7  BL=IO2  TE=IO12(不用)
//   按键:   KEY1=IO8  KEY2=IO10  KEY3=IO13  KEY4=IO14（按下=低电平，内部上拉）
//   指示灯: LED1=IO40  LED2=IO38（拉低点亮）
//   红外:   接收 RX=IO16  发送 TX=IO17
//
// ---- 需要的库（Arduino IDE -> 工具 -> 管理库，搜索安装）----
//   Adafruit GFX Library
//   Adafruit ST7789 Library
//   IRremoteESP8266（作者 David Conran，建议 2.8 以上）
//
// ---- 中文显示说明 ----
//   Adafruit 默认字体没有中文。本程序用 cn16.h（16 点阵字模，
//   只含界面用到的 25 个字：硬件测试按键指示灯接收发送中空闲
//   红绿蓝黄青品白黑色），不依赖任何字库芯片或文件系统。
//   cn16.h 必须和本文件放在同一个文件夹里。
//   本文件需保存为 UTF-8 编码（Arduino IDE 默认即是）。
//
// ---- 开发板设置 ----
//   开发板选 "ESP32S2 Dev Module"；若串口无输出，把
//   Tools > USB CDC On Boot 设为 Enabled 后重新上传
//
// ---- 其他说明 ----
//   背光：原理图里 Q2 是低边开关、R2(10k)下拉到 GND，BL 高 -> 背光亮。
//   如果你的板子背光逻辑相反（下载后常亮/常灭），把下面 BL_ON/BL_OFF
//   的 HIGH/LOW 对调即可。
//   屏幕尺寸：竖屏 240x320；换屏改 TFT_W/TFT_H 即可，界面分区自动跟随。
//================================================================

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "cn16.h"

// ---------------- 引脚 ----------------
#define TFT_CS    5
#define TFT_DC    3
#define TFT_RST   1
#define TFT_MOSI  11
#define TFT_SCK   7
#define TFT_BL    2
#define TFT_TE    12

#define TFT_W   240
#define TFT_H   320

#define KEY1  8
#define KEY2  10
#define KEY3  13
#define KEY4  14

#define LED1     40
#define LED2     38
#define LED_ON   LOW      // 拉低点亮
#define LED_OFF  HIGH

#define IR_RX_PIN     16
#define IR_TX_PIN     17
#define IR_TEST_CODE  0x20DF10EEUL   // 测试用 NEC 码（32 位）

#define BL_ON   HIGH
#define BL_OFF  LOW

// ---------------- 屏幕分区 ----------------
#define TITLE_H   24
#define STATUS_H  110
#define COLOR_Y   (TITLE_H)
#define STATUS_Y  (TFT_H - STATUS_H)
#define COLOR_H   (STATUS_Y - COLOR_Y)

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
IRrecv  irrecv(IR_RX_PIN);
IRsend  irsend(IR_TX_PIN);
decode_results irResults;

const uint8_t keyPins[4] = {KEY1, KEY2, KEY3, KEY4};
const char *keyNames[4]  = {"KEY1", "KEY2", "KEY3", "KEY4"};

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

// ---------------- 运行状态 ----------------
uint8_t  colorIdx    = 0;
int8_t   lastKey     = -1;   // 上次按下的键 0..3；-1 = 还没按过
uint8_t  ledCode     = 0;
String   irRxProto   = "--";   // 接收到的协议名，如 NEC / NEC_LIKE
String   irRxHex     = "--";   // 接收到的解码值，如 0x20DF10EE
bool     txActive    = false;
bool     statusDirty = true;

unsigned long colorMs = 0, lastTxMs = 0;

uint8_t       keyStable[4]  = {HIGH, HIGH, HIGH, HIGH};
uint8_t       keyReading[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long keyT[4]       = {0, 0, 0, 0};
const unsigned long DEBOUNCE_MS = 30;

// ---------------- 中文 16 点阵混排 ----------------
// ASCII 走 Adafruit 内置字体（size2：12px 宽、16px 高），
// 中文走 cn16.h 的 16 点阵（scale=1 为 16px，scale=2 为 32px）。
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

// ---------------- 屏幕绘制 ----------------
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

void applyLedCode(uint8_t v) {
  ledCode = v;
  digitalWrite(LED1, (v & 0x02) ? LED_ON : LED_OFF);  // LED1 = 高位
  digitalWrite(LED2, (v & 0x01) ? LED_ON : LED_OFF);  // LED2 = 低位
}

void drawStatus() {
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
  printCN(6, STATUS_Y + 32, ("接收: " + irRxProto).c_str(),
          ST77XX_WHITE, ST77XX_BLACK, 1);

  // 第 3 行：接收到的解码值（缩进 4 格，与协议名对齐）
  printCN(6, STATUS_Y + 56, ("    " + irRxHex).c_str(),
          ST77XX_WHITE, ST77XX_BLACK, 1);

  // 第 4 行：发送状态
  printCN(6, STATUS_Y + 80, txActive ? "发送: 发送中..." : "发送: 空闲",
          txActive ? ST77XX_YELLOW : ST77XX_GREEN, ST77XX_BLACK, 1);
}

// ---------------- 按键 ----------------
void onKeyPressed(uint8_t i) {
  lastKey = i;
  applyLedCode(i);   // KEY1~KEY4 -> 0~3 -> LED 显示 00/01/10/11
  statusDirty = true;
  Serial.print("按键 ");
  Serial.print(keyNames[i]);
  Serial.print(" 按下 -> 值=");
  Serial.print(i);
  Serial.print(" -> LED1");
  Serial.print((i & 0x02) ? "亮" : "灭");
  Serial.print(" LED2");
  Serial.println((i & 0x01) ? "亮" : "灭");
}

void pollKeys(unsigned long now) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t r = digitalRead(keyPins[i]);
    if (r != keyReading[i]) { keyReading[i] = r; keyT[i] = now; }
    if ((now - keyT[i] > DEBOUNCE_MS) && (keyReading[i] != keyStable[i])) {
      keyStable[i] = keyReading[i];
      if (keyStable[i] == LOW) onKeyPressed(i);   // 按下（低电平）才动作
    }
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) delay(10);  // 等 native USB，最多 2s

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, BL_ON);
  pinMode(TFT_TE, INPUT);

  for (uint8_t i = 0; i < 4; i++) pinMode(keyPins[i], INPUT_PULLUP);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  applyLedCode(0);   // 初始 LED 全灭

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.init(TFT_W, TFT_H);
  tft.setRotation(2);   // 旋转180度，适配当前视角
  drawTitle();
  showColor(0);
  drawStatus();
  statusDirty = false;

  irrecv.enableIRIn();   // RMT 接收，IO16
  irsend.begin();        // RMT 发送，IO17

  Serial.println();
  Serial.println("===== ESP32-S2 mini 硬件测试启动 =====");
  Serial.println("1. 屏幕每 2 秒切换一种颜色");
  Serial.println("2. KEY1~KEY4 -> 值 0~3 -> LED1/LED2 显示 00/01/10/11");
  Serial.println("3. 红外接收解码后打印到串口");
  Serial.println("4. 红外发送：每 3 秒发射 1.5 秒 NEC 码，手机摄像头对准发射管可见闪烁");
  Serial.println("=====================================");
}

void loop() {
  unsigned long now = millis();

  // 1. 屏幕颜色切换（2s）
  if (now - colorMs >= 2000) {
    colorMs = now;
    colorIdx = (colorIdx + 1) % COLOR_N;
    showColor(colorIdx);
    Serial.println(String("屏幕颜色切换: ") + colorList[colorIdx].name);
  }

  // 2. 按键扫描（含 30ms 消抖）
  pollKeys(now);

  // 3. 红外接收：解码成功就打印
  if (irrecv.decode(&irResults)) {
    irRxProto = typeToString(irResults.decode_type, irResults.repeat);
    String hexv = String((uint32_t)irResults.value, HEX);
    hexv.toUpperCase();
    irRxHex = "0x" + hexv;
    if (irResults.repeat) irRxHex += "(R)";
    statusDirty = true;
    Serial.println("红外接收: " + resultToHumanReadableBasic(&irResults));
    irrecv.resume();
  }

  // 4. 红外发送：3 秒周期 = 1.5 秒发射 / 1.5 秒静默
  bool sending = (now % 3000) < 1500;
  if (sending != txActive) {
    txActive = sending;
    statusDirty = true;
    if (txActive)
      Serial.println("红外发送：开始发射 NEC 0x20DF10EE（手机摄像头对准红外发射管可见闪烁）");
    else
      Serial.println("红外发送：停止发射");
  }
  if (txActive && (now - lastTxMs >= 150)) {
    lastTxMs = now;
    irsend.sendNEC(IR_TEST_CODE, 32);
  }

  if (statusDirty) {
    drawStatus();
    statusDirty = false;
  }
}
