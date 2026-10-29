//================================================================
// ESP32S2_HWTest_r12.ino
// CoreBoard ESP32-S2 mini 硬件测试（r12：菜单版，灯控制）
//
// ---- 菜单结构 ----
//   首页（颜色测试 + 状态区）
//     KEY4 -> 一级菜单：灯 / 空调 / 红外解码 / 关于 / 返回
//       灯：9 个 NEC 键码，KEY2/KEY3 选码，KEY1 发送，KEY4 回菜单
//       空调：开机 / 关机（R05D），KEY2/KEY3 选码，KEY1 发送，KEY4 回菜单
//       红外解码：显示最近一次接收的协议 / 数值 / 位数 / 原始项数，
//                 KEY1 清除，KEY4 回菜单
//       关于：版本与按键说明，KEY4 回菜单
//       返回：回首页
//     一级菜单中 KEY4 也可直接回首页。
//
// ---- 按键逻辑 ----
//   KEY4 = 菜单键（首页进菜单，子菜单回一级菜单，一级菜单回首页）
//   KEY2 = 加键 -> 下一项（序号 +1，循环）
//   KEY3 = 减键 -> 上一项（序号 -1，循环）
//   KEY1 = 确认键（菜单中进入子菜单，灯/空调中发送选中码，解码中清除显示）
//   任何按键都会更新 LED1/LED2（KEY1~KEY4 -> 0~3 -> 00/01/10/11）。
//   首页中 KEY1/KEY2/KEY3 不触发发送（发送只在灯/空调子菜单中）。
//
// ---- 灯子菜单底部 ----
//   底部三行：发送提示（上次发送的键名）/ 解码提示（最近一次接收）/ 按键提示。
//   解码提示优先显示“协议 + 数值”，太长时只显示数值（数值一定显示）。
//
// ---- 多文件 ----
//   本文件：状态机、按键、红外收发、主循环。
//   codes.h：灯（NEC）与空调（R05D）码表。
//   ui.h：字库混排与全部屏幕绘制。
//   cn6.h：16 点阵字库（与 r8/r9 相同）。
//
// ---- 引脚（来自原理图）----
//   ST7789: CS=IO5  DC=IO3  RST=IO1  MOSI=IO11  SCK=IO7  BL=IO2  TE=IO12(不用)
//   按键:   KEY1=IO8  KEY2=IO10  KEY3=IO13  KEY4=IO14（按下=低电平，内部上拉）
//   指示灯: LED1=IO40  LED2=IO38（拉低点亮）
//   红外:   接收 RX=IO16  发送 TX=IO17
//   发射电路：IO17 -> NPN 基极，NPN 集电极 -> 发射管 -> 电源，低边驱动，
//             GPIO 高 = 导通发光，mark 输出载波无需反相。
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
// ---- 其他说明 ----
//   背光：原理图里 Q2 是低边开关、R2(10k)下拉到 GND，BL 高 -> 背光亮。
//   如果你的板子背光逻辑相反（下载后常亮/常灭），把下面 BL_ON/BL_OFF
//   的 HIGH/LOW 对调即可。
//   屏幕尺寸：竖屏 240x320；换屏改 TFT_W/TFT_H 即可，界面分区自动跟随。
//   本文件需保存为 UTF-8 编码（Arduino IDE 默认即是）。
//================================================================

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "cn6.h"

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

#define BL_ON   HIGH
#define BL_OFF  LOW

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
// 接收缓存开到 512 项：默认 100 项装不下 199 项的长码，原始数组会被截断
IRrecv  irrecv(IR_RX_PIN, 512);
IRsend  irsend(IR_TX_PIN);
decode_results irResults;

const uint8_t keyPins[4] = {KEY1, KEY2, KEY3, KEY4};
const char *keyNames[4]  = {"KEY1", "KEY2", "KEY3", "KEY4"};

#include "codes.h"
#include "ui.h"

// ---------------- 菜单状态 ----------------
enum MenuState { ST_HOME, ST_MENU, ST_LAMP, ST_AC, ST_DECODE, ST_ABOUT };
MenuState state = ST_HOME;
uint8_t menuSel = 0;   // 一级菜单选择（0~4）
uint8_t lampSel = 0;   // 灯键码选择（0~8）
uint8_t acSel   = 0;   // 空调键码选择（0~1）

// ---------------- 运行状态 ----------------
uint8_t  colorIdx    = 0;
int8_t   lastKey     = -1;   // 上次按下的键 0..3；-1 = 还没按过
uint8_t  ledCode     = 0;
String   irRxProto   = "--";   // 接收到的协议名，如 NEC / COOLIX
String   irRxHex     = "--";   // 接收到的解码值，如 0x01FE807F
String   lastSent    = "--";   // 上次发送的键名（灯/空调子菜单底部显示）
String   lastRecv    = "--";   // 最近一次接收（灯/空调子菜单底部显示）
uint16_t recvBits    = 0;      // 接收到的位数
uint16_t recvLen     = 0;      // 接收到的原始项数
bool     screenDirty = true;

unsigned long colorMs = 0;

uint8_t       keyStable[4]  = {HIGH, HIGH, HIGH, HIGH};
uint8_t       keyReading[4] = {HIGH, HIGH, HIGH, HIGH};
unsigned long keyT[4]       = {0, 0, 0, 0};
const unsigned long DEBOUNCE_MS = 30;

// ---------------- 指示灯 ----------------
void applyLedCode(uint8_t v) {
  ledCode = v;
  digitalWrite(LED1, (v & 0x02) ? LED_ON : LED_OFF);  // LED1 = 高位
  digitalWrite(LED2, (v & 0x01) ? LED_ON : LED_OFF);  // LED2 = 低位
}

// ---------------- 红外发送 ----------------
// 发 R05D 一组：L + 帧 + S + L + 帧 + 结束码（共 199 项时序，与 r5 实测数组一致）
//   L  = 4400 + 4400
//   帧 = A ~A B ~B C ~C，每字节 MSB 先发；0 = 540+540，1 = 540+1620
//   S  = 540 + 5220（两帧间隔）
//   结束 = 540 + 持续无载波
void sendR05D(uint8_t a, uint8_t b, uint8_t c) {
  uint16_t buf[199];
  uint16_t n = 0;
  uint8_t frame[6] = {a, (uint8_t)~a, b, (uint8_t)~b, c, (uint8_t)~c};
  for (uint8_t rep = 0; rep < 2; rep++) {
    if (rep == 1) { buf[n++] = 540; buf[n++] = 5220; }   // 分隔码 S
    buf[n++] = 4400; buf[n++] = 4400;                    // 引导码 L
    for (uint8_t i = 0; i < 6; i++) {
      for (int8_t bit = 7; bit >= 0; bit--) {
        buf[n++] = 540;
        buf[n++] = (frame[i] & (1 << bit)) ? 1620 : 540;
      }
    }
  }
  buf[n++] = 540;   // 结束码 mark
  if (n != 199) {
    Serial.print("R05D 数组长度异常: ");
    Serial.println(n);
    return;
  }
  irsend.sendRaw(buf, n, 38);
  Serial.print("发送 R05D 帧=");
  for (uint8_t i = 0; i < 6; i++) {
    if (frame[i] < 0x10) Serial.print('0');
    Serial.print(frame[i], HEX);
    if (i < 5) Serial.print(' ');
  }
  Serial.println();
}

// 发送灯码（NEC 32 位）
void sendLamp(uint8_t idx) {
  irsend.sendNEC(LAMP_CODES[idx].nec, 32);
  lastSent = LAMP_CODES[idx].name;
  screenDirty = true;
  Serial.print("发送灯码: ");
  Serial.print(LAMP_CODES[idx].name);
  Serial.print(" NEC ");
  Serial.println(LAMP_CODES[idx].hex);
}

// 发送空调码（R05D 构造发送）
void sendAc(uint8_t idx) {
  sendR05D(AC_CODES[idx].a, AC_CODES[idx].b, AC_CODES[idx].c);
  lastSent = AC_CODES[idx].name;
  screenDirty = true;
  Serial.print("发送空调码: ");
  Serial.println(AC_CODES[idx].name);
}

// 清除解码显示（红外解码子菜单）
void clearDecode() {
  irRxProto = "--";
  irRxHex = "--";
  recvBits = 0;
  recvLen = 0;
  lastRecv = "--";
  screenDirty = true;
  Serial.println("解码显示已清除");
}

// ---------------- 屏幕刷新 ----------------
void drawScreen() {
  switch (state) {
    case ST_HOME: drawHomeScreen(colorIdx, lastKey, ledCode, irRxProto, irRxHex, lastSent); break;
    case ST_MENU: drawMenuScreen(menuSel); break;
    case ST_LAMP: drawLampScreen(lampSel, lastSent, lastRecv); break;
    case ST_AC: drawAcScreen(acSel, lastSent, lastRecv); break;
    case ST_DECODE: drawDecodeScreen(irRxProto, irRxHex, recvBits, recvLen); break;
    case ST_ABOUT: drawAboutScreen(); break;
  }
}

// ---------------- 按键 ----------------
void onKeyPressed(uint8_t i) {
  lastKey = i;
  applyLedCode(i);   // KEY1~KEY4 -> 0~3 -> LED 显示 00/01/10/11
  screenDirty = true;
  Serial.print("按键 ");
  Serial.print(keyNames[i]);
  Serial.print(" 按下 -> 值=");
  Serial.print(i);
  Serial.print(" -> LED1");
  Serial.print((i & 0x02) ? "亮" : "灭");
  Serial.print(" LED2");
  Serial.println((i & 0x01) ? "亮" : "灭");

  switch (state) {
    case ST_HOME:
      if (i == 3) state = ST_MENU;  // KEY4 -> 一级菜单
      break;
    case ST_MENU:
      if (i == 1) menuSel = (menuSel + 1) % MENU_COUNT;  // KEY2 加 -> 下一项
      else if (i == 2) menuSel = (menuSel + MENU_COUNT - 1) % MENU_COUNT;  // KEY3 减 -> 上一项
      else if (i == 0) {  // KEY1 确认 -> 进入子菜单
        switch (menuSel) {
          case 0: state = ST_LAMP; break;
          case 1: state = ST_AC; break;
          case 2: state = ST_DECODE; break;
          case 3: state = ST_ABOUT; break;
          default: state = ST_HOME; break;  // 返回
        }
      }
      else if (i == 3) state = ST_HOME;  // KEY4 -> 首页
      break;
    case ST_LAMP:
      if (i == 1) lampSel = (lampSel + 1) % LAMP_COUNT;  // KEY2 加 -> 下一项
      else if (i == 2) lampSel = (lampSel + LAMP_COUNT - 1) % LAMP_COUNT;  // KEY3 减 -> 上一项
      else if (i == 0) sendLamp(lampSel);  // KEY1 -> 发送选中码
      else if (i == 3) state = ST_MENU;   // KEY4 -> 一级菜单
      break;
    case ST_AC:
      if (i == 1) acSel = (acSel + 1) % AC_COUNT;
      else if (i == 2) acSel = (acSel + AC_COUNT - 1) % AC_COUNT;
      else if (i == 0) sendAc(acSel);
      else if (i == 3) state = ST_MENU;
      break;
    case ST_DECODE:
      if (i == 0) clearDecode();  // KEY1 -> 清除显示
      else if (i == 3) state = ST_MENU;
      break;
    case ST_ABOUT:
      if (i == 3) state = ST_MENU;
      break;
  }
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
  drawScreen();
  screenDirty = false;

  irrecv.enableIRIn();   // RMT 接收，IO16，缓存 512 项
  irsend.begin();        // RMT 发送，IO17

  Serial.println();
  Serial.println("===== ESP32-S2 mini 硬件测试启动（r12 菜单版）=====");
  Serial.println("首页：颜色测试 + 状态区，KEY4 进入一级菜单");
  Serial.println("一级菜单：灯 / 空调 / 红外解码 / 关于 / 返回");
  Serial.println("按键：KEY4 菜单，KEY2 加（下一项），KEY3 减（上一项），KEY1 确认");
  Serial.println("灯：KEY2/KEY3 选码，KEY1 发送 NEC 码");
  Serial.println("=====================================");
}

void loop() {
  unsigned long now = millis();

  // 1. 首页颜色切换（2s，仅首页有效）
  if (state == ST_HOME && (now - colorMs >= 2000)) {
    colorMs = now;
    colorIdx = (colorIdx + 1) % COLOR_N;
    screenDirty = true;
    Serial.println(String("屏幕颜色切换: ") + colorList[colorIdx].name);
  }

  // 2. 按键扫描（含 30ms 消抖）
  pollKeys(now);

  // 3. 红外接收：更新解码显示，串口打印完整解码（含原始数组）
  if (irrecv.decode(&irResults)) {
    irRxProto = typeToString(irResults.decode_type, irResults.repeat);
    irRxHex = hexPad((uint32_t)irResults.value, irResults.bits);
    if (irResults.repeat) irRxHex += "(R)";
    recvBits = irResults.bits;
    recvLen = irResults.rawlen - 1;
    // 底部解码提示：优先显示“协议 + 数值”，太长则只留数值（数值一定显示）
    String cand = irRxProto + " " + irRxHex;
    if (cnTextWidth(("接收: " + cand).c_str(), 1) + 8 > TFT_W) cand = irRxHex;
    lastRecv = cand;
    Serial.println("----- 红外接收 -----");
    Serial.print("协议: ");
    Serial.println(irRxProto);
    Serial.print("数值: ");
    Serial.println(irRxHex);
    Serial.print("位数: ");
    Serial.print(recvBits);
    Serial.println(" Bits");
    if (irResults.repeat) Serial.println("重复码: 是");
    // 原始数组：跳过 rawbuf[0] 的起始空闲，单位 50us，换算成微秒打印
    Serial.print("原始数组 (");
    Serial.print(recvLen);
    Serial.println("项):");
    Serial.print("uint16_t rawData[");
    Serial.print(recvLen);
    Serial.print("] = {");
    for (uint16_t k = 0; k < recvLen; k++) {
      Serial.print(irResults.rawbuf[k + 1] * 50);
      if (k + 1 < recvLen) Serial.print(", ");
    }
    Serial.println("};");
    irrecv.resume();
    // 只有显示接收信息的界面才重画（菜单/关于不显示接收，不重画）
    if (state == ST_HOME || state == ST_LAMP || state == ST_AC || state == ST_DECODE) {
      screenDirty = true;
    }
  }

  // 4. 屏幕刷新
  if (screenDirty) {
    drawScreen();
    screenDirty = false;
  }
}
