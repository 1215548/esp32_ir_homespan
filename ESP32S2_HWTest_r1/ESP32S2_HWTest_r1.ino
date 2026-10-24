//================================================================
// ESP32S2_HWTest_r1.ino
// CoreBoard ESP32-S2 mini 四项硬件测试
//
// 1. ST7789 屏幕：每 2 秒循环切换颜色（红/绿/蓝/黄/青/品红/白/黑）
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
// ---- 开发板设置 ----
//   开发板选 "ESP32S2 Dev Module"；若串口无输出，把
//   Tools > USB CDC On Boot 设为 Enabled 后重新上传
//
// ---- 说明 ----
//   背光：原理图里 Q2 是低边开关、R2(10k)下拉到 GND，BL 高 -> 背光亮。
//   如果你的板子背光逻辑相反（下载后常亮/常灭），把下面 BL_ON/BL_OFF
//   的 HIGH/LOW 对调即可。
//   屏幕尺寸：默认 240x240；若是 240x320 / 170x320 等，改 TFT_W/TFT_H。
//   屏幕只显示英文（Adafruit 默认字体无中文），中文说明走串口。
//================================================================

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

// ---------------- 引脚 ----------------
#define TFT_CS    5
#define TFT_DC    3
#define TFT_RST   1
#define TFT_MOSI  11
#define TFT_SCK   7
#define TFT_BL    2
#define TFT_TE    12

#define TFT_W   240
#define TFT_H   240

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
#define IR_TEST_CODE  0x20DF10EFUL   // 测试用 NEC 码（32 位）

#define BL_ON   HIGH
#define BL_OFF  LOW

// ---------------- 屏幕分区 ----------------
#define TITLE_H   24
#define STATUS_H  90
#define COLOR_Y   (TITLE_H)
#define STATUS_Y  (TFT_H - STATUS_H)
#define COLOR_H   (STATUS_Y - COLOR_Y)

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
IRrecv  irrecv(IR_RX_PIN);
IRsend  irsend(IR_TX_PIN);
decode_results irResults;

const uint8_t keyPins[4] = {KEY1, KEY2, KEY3, KEY4};
const char *keyNames[4]  = {"KEY1", "KEY2", "KEY3", "KEY4"};

struct ColorItem { const char *name; uint16_t c; };
ColorItem colorList[] = {
  {"RED",     ST77XX_RED},
  {"GREEN",   ST77XX_GREEN},
  {"BLUE",    ST77XX_BLUE},
  {"YELLOW",  ST77XX_YELLOW},
  {"CYAN",    ST77XX_CYAN},
  {"MAGENTA", ST77XX_MAGENTA},
  {"WHITE",   ST77XX_WHITE},
  {"BLACK",   ST77XX_BLACK},
};
const uint8_t COLOR_N = sizeof(colorList) / sizeof(colorList[0]);

// ---------------- 运行状态 ----------------
uint8_t  colorIdx    = 0;
int8_t   lastKey     = -1;   // 上次按下的键 0..3；-1 = 还没按过
uint8_t  ledCode     = 0;
String   irRxText    = "--";
bool     txActive    = false;
bool     statusDirty = true;

unsigned long colorMs = 0, lastTxMs = 0;

uint8_t       keyStable[4]  = {HIGH, HIGH, HIGH, HIGH};
uint8_t       keyReading[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long keyT[4]       = {0, 0, 0, 0};
const unsigned long DEBOUNCE_MS = 30;

// ---------------- 屏幕绘制 ----------------
void drawTitle() {
  tft.fillRect(0, 0, TFT_W, TITLE_H, ST77XX_BLUE);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 5);
  tft.print("ESP32-S2 HW TEST");
}

void showColor(uint8_t i) {
  uint16_t bg = colorList[i].c;
  tft.fillRect(0, COLOR_Y, TFT_W, COLOR_H, bg);
  bool bright = (bg == ST77XX_WHITE || bg == ST77XX_YELLOW ||
                 bg == ST77XX_CYAN  || bg == ST77XX_GREEN);
  tft.setTextSize(3);
  tft.setTextColor(bright ? ST77XX_BLACK : ST77XX_WHITE);
  String s = colorList[i].name;
  int16_t x = (TFT_W - (int16_t)s.length() * 18) / 2;
  tft.setCursor(x < 0 ? 0 : x, COLOR_Y + (COLOR_H - 24) / 2);
  tft.print(s);
}

void applyLedCode(uint8_t v) {
  ledCode = v;
  digitalWrite(LED1, (v & 0x02) ? LED_ON : LED_OFF);  // LED1 = 高位
  digitalWrite(LED2, (v & 0x01) ? LED_ON : LED_OFF);  // LED2 = 低位
}

void drawStatus() {
  tft.fillRect(0, STATUS_Y, TFT_W, STATUS_H, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(6, STATUS_Y + 8);
  if (lastKey < 0) {
    tft.print("KEY: --   LED: --");
  } else {
    tft.print("KEY: " + String(lastKey + 1) + "   LED: " +
              String((ledCode >> 1) & 1) + String(ledCode & 1));
  }

  String rxLine = "RX: " + irRxText;
  if (rxLine.length() > 19) rxLine = rxLine.substring(0, 19);
  tft.setCursor(6, STATUS_Y + 32);
  tft.print(rxLine);

  tft.setCursor(6, STATUS_Y + 56);
  tft.setTextColor(txActive ? ST77XX_YELLOW : ST77XX_GREEN);
  tft.print(txActive ? "TX: SENDING..." : "TX: IDLE");
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
  tft.setRotation(0);
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
    String proto = typeToString(irResults.decode_type, irResults.repeat);
    String hexv = String((uint32_t)irResults.value, HEX);
    hexv.toUpperCase();
    irRxText = proto + " 0x" + hexv;
    if (irResults.repeat) irRxText += "(R)";
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
      Serial.println("红外发送：开始发射 NEC 码（手机摄像头对准红外发射管可见闪烁）");
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
