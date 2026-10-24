//================================================================
// ESP32S2_IRRawCapture_r5.ino
// 红外原始数据抓取工具 v5（RMT 抓原始时间 + 按 R05D 时间规则判断）
//
// 相对 r4 的改动：
//   r4 的 GPIO 中断法在这块板子上会中途丢边沿（一次按键只抓到
//   8/7/8 个电平，远少于应有的约 199 个），数据没法用。
//   本版改回 RMT 硬件抓时间（r3 已证明能完整抓到 199 个电平，
//   1us 精度），判断部分保留 r4 的 R05D 规则：
//     引导码 L： LOW 约 4.4ms + HIGH 约 4.4ms
//     分隔码 S： LOW 约 0.54ms + HIGH 约 5.22ms
//     数据 0  ： LOW 约 0.54ms + HIGH 约 0.54ms
//     数据 1  ： LOW 约 0.54ms + HIGH 约 1.62ms
//   （接收端视角：LOW = 有红外 mark，HIGH = 无红外 space。）
//   解出 A/B/C 后按 R05D 表翻译：风速、温度、模式，并识别关机。
//   分隔码 S 的 HIGH 时间就是两帧间隔，实测值，不再估算。
//
// 用法：烧进板子（接收头 IO16），串口 115200，遥控器对准接收头按键。
// 一次按键打印一组：原始时间串 + 逐段判断 + 解出的 A/B/C 与语义。
//
// 注意：本程序用 RMT 接收驱动，不用 IRremoteESP8266，不要和它混用。
// 开发板：ESP32S2 Dev Module（LOLIN_S2_MINI 也行）
//================================================================

#include <Arduino.h>
#include "driver/rmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

#define IR_RX_PIN 16
#define RMT_CH RMT_CHANNEL_0
#define RMT_CLK_DIV 80          // APB 80MHz / 80 = 1MHz，1 tick = 1us
#define RMT_MEM_BLOCKS 4        // 4 x 64 = 256 个 RMT item
#define RMT_RX_BUF_SIZE 4096    // 环形缓冲（字节）
#define IDLE_THRESHOLD_US 10000 // 空闲 10ms 即判定一帧结束
#define MAX_LEVELS 512          // 一串内电平数上限（一次按键约 199 个）

// 时间门限（us，接收端视角）
#define L_LOW_MIN 4000
#define L_LOW_MAX 4800
#define L_HIGH_MIN 4000
#define L_HIGH_MAX 4800
#define BIT_LOW_MIN 300
#define BIT_LOW_MAX 800
#define BIT0_HIGH_MIN 300
#define BIT0_HIGH_MAX 800
#define BIT1_HIGH_MIN 1200
#define BIT1_HIGH_MAX 2000
#define S_HIGH_MIN 4800
#define S_HIGH_MAX 5600

RingbufHandle_t rb = NULL;

bool inRange(uint32_t v, uint32_t lo, uint32_t hi) {
  return v >= lo && v <= hi;
}

void printHexByte(uint8_t v) {
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
}

// 语义翻译：B 风速 / C 温度模式（R05D 表）
void printSemantics(uint8_t A, uint8_t B, uint8_t C) {
  Serial.print("  A=0x");
  printHexByte(A);
  Serial.print(" B=0x");
  printHexByte(B);
  Serial.print(" C=0x");
  printHexByte(C);
  if (A == 0xB2 && B == 0x7B && C == 0xE0) {
    Serial.println(" -> 关机");
    return;
  }
  // 风速 B7B6B5（另一篇文章把中风作 010，这里以 011 为准）
  const char *wind;
  switch (B >> 5) {
    case 0b101: wind = "自动风"; break;
    case 0b100: wind = "低风"; break;
    case 0b011: wind = "中风"; break;
    case 0b001: wind = "高风"; break;
    case 0b000: wind = "固定风"; break;
    default: wind = "未知风速"; break;
  }
  // 温度 C7C6C5C4
  int temp = -1;  // -1 未知，-2 无定义（送风）
  switch (C >> 4) {
    case 0b0000: temp = 17; break;
    case 0b0001: temp = 18; break;
    case 0b0011: temp = 19; break;
    case 0b0010: temp = 20; break;
    case 0b0110: temp = 21; break;
    case 0b0111: temp = 22; break;
    case 0b0101: temp = 23; break;
    case 0b0100: temp = 24; break;
    case 0b1100: temp = 25; break;
    case 0b1101: temp = 26; break;
    case 0b1001: temp = 27; break;
    case 0b1000: temp = 28; break;
    case 0b1010: temp = 29; break;
    case 0b1011: temp = 30; break;
    case 0b1110: temp = -2; break;
  }
  // 模式 C3C2：01 有温度码为抽湿、无为送风
  const char *mode;
  switch ((C >> 2) & 0x03) {
    case 0b10: mode = "自动"; break;
    case 0b00: mode = "制冷"; break;
    case 0b11: mode = "制热"; break;
    default: mode = (temp == -2) ? "送风" : "抽湿"; break;
  }
  Serial.print(" -> ");
  Serial.print(mode);
  Serial.print(' ');
  if (temp == -2) Serial.print("无温度码");
  else if (temp < 0) Serial.print("未知温度");
  else {
    Serial.print(temp);
    Serial.print("°C");
  }
  Serial.print(' ');
  Serial.println(wind);
}

// 引导码 L：LOW 4.4ms + HIGH 4.4ms，返回新位置，失败返回 -1
int parseLead(uint16_t *d, uint8_t *l, uint16_t nd, uint16_t pos) {
  if (pos + 1 < nd && l[pos] == LOW && inRange(d[pos], L_LOW_MIN, L_LOW_MAX)
      && l[pos + 1] == HIGH && inRange(d[pos + 1], L_HIGH_MIN, L_HIGH_MAX)) {
    Serial.print("  引导码 L: LOW ");
    Serial.print(d[pos]);
    Serial.print(" + HIGH ");
    Serial.println(d[pos + 1]);
    return pos + 2;
  }
  return -1;
}

// 一帧 48 位 -> 6 字节，打印字节、互补校验与语义
int parseFrame(uint16_t *d, uint8_t *l, uint16_t nd, uint16_t pos) {
  uint8_t bytes[6] = {0, 0, 0, 0, 0, 0};
  for (int b = 0; b < 48; b++) {
    if (pos + 1 >= nd) return -1;
    if (l[pos] != LOW || !inRange(d[pos], BIT_LOW_MIN, BIT_LOW_MAX)) return -1;
    if (l[pos + 1] != HIGH) return -1;
    int bit;
    if (inRange(d[pos + 1], BIT0_HIGH_MIN, BIT0_HIGH_MAX)) bit = 0;
    else if (inRange(d[pos + 1], BIT1_HIGH_MIN, BIT1_HIGH_MAX)) bit = 1;
    else return -1;
    bytes[b / 8] = (bytes[b / 8] << 1) | bit;
    pos += 2;
  }
  Serial.print("  帧: ");
  for (int i = 0; i < 6; i++) {
    if (i) Serial.print(' ');
    printHexByte(bytes[i]);
  }
  bool ok = true;
  for (int i = 0; i < 6; i += 2) {
    if (bytes[i + 1] != (uint8_t)~bytes[i]) ok = false;
  }
  Serial.print(ok ? "  互补校验通过" : "  互补校验不通过");
  Serial.println();
  printSemantics(bytes[0], bytes[2], bytes[4]);
  return pos;
}

// 分隔码 S：LOW 0.54ms + HIGH 5.22ms
int parseSep(uint16_t *d, uint8_t *l, uint16_t nd, uint16_t pos) {
  if (pos + 1 < nd && l[pos] == LOW && inRange(d[pos], BIT_LOW_MIN, BIT_LOW_MAX)
      && l[pos + 1] == HIGH && inRange(d[pos + 1], S_HIGH_MIN, S_HIGH_MAX)) {
    Serial.print("  分隔码 S: LOW ");
    Serial.print(d[pos]);
    Serial.print(" + HIGH ");
    Serial.print(d[pos + 1]);
    Serial.println("（该 HIGH 即两帧间隔，实测值）");
    return pos + 2;
  }
  return -1;
}

void processCapture(uint16_t *dIn, uint8_t *lIn, uint16_t nIn) {
  // 跳过开头的 HIGH，从第一个 LOW 开始
  uint16_t start = 0;
  while (start < nIn && lIn[start] == HIGH) start++;
  if (nIn - start < 2) {
    Serial.println("===== 抓到一串，但太短，忽略 =====");
    return;
  }
  static uint16_t d[MAX_LEVELS];
  static uint8_t l[MAX_LEVELS];
  uint16_t nd = 0;
  for (uint16_t i = start; i < nIn && nd < MAX_LEVELS; i++) {
    d[nd] = dIn[i];
    l[nd] = lIn[i];
    nd++;
  }

  Serial.println("--------------------------------------------------");
  Serial.print("电平数: ");
  Serial.print(nd);
  Serial.print("  总时长: ");
  uint32_t total = 0;
  for (uint16_t i = 0; i < nd; i++) total += d[i];
  Serial.print(total);
  Serial.println(" us（LOW=有红外，HIGH=无红外，从 LOW 开始交替）");

  Serial.println("--- 原始时间数组（微秒，可直接用于 sendRaw）---");
  Serial.print("uint16_t rawData[");
  Serial.print(nd);
  Serial.print("] = {");
  for (uint16_t i = 0; i < nd; i++) {
    if (i) Serial.print(", ");
    Serial.print(d[i]);
  }
  Serial.println("};");

  Serial.println("--- 按时间判断（R05D）---");
  int pos = parseLead(d, l, nd, 0);
  if (pos < 0) {
    Serial.println("  不是 R05D 引导码，可能是噪声或其它协议，原始时间已打印在上面");
    Serial.println();
    return;
  }
  pos = parseFrame(d, l, nd, pos);
  if (pos < 0) {
    Serial.println("  第 1 帧未解完，停止");
    Serial.println();
    return;
  }
  int p2 = parseSep(d, l, nd, pos);
  if (p2 < 0) {
    // 单帧情况：剩余应为一个 LOW（结束码）
    if (pos + 1 == nd && l[pos] == LOW) {
      Serial.print("  结束码: LOW ");
      Serial.print(d[pos]);
      Serial.println(" + 持续 HIGH（只抓到 1 帧）");
    } else {
      Serial.println("  第 1 帧后无分隔码，停止");
    }
    Serial.println();
    return;
  }
  pos = p2;
  pos = parseLead(d, l, nd, pos);
  if (pos < 0) {
    Serial.println("  第 2 帧引导码不匹配，停止");
    Serial.println();
    return;
  }
  pos = parseFrame(d, l, nd, pos);
  if (pos < 0) {
    Serial.println("  第 2 帧未解完，停止");
    Serial.println();
    return;
  }
  if (pos + 1 == nd && l[pos] == LOW) {
    Serial.print("  结束码: LOW ");
    Serial.print(d[pos]);
    Serial.println(" + 持续 HIGH");
  } else if (pos < nd) {
    Serial.print("  解析后还剩 ");
    Serial.print(nd - pos);
    Serial.println(" 个电平未处理");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) delay(10);  // 等 native USB，最多 2s

  rmt_config_t cfg = {};
  cfg.rmt_mode = RMT_MODE_RX;
  cfg.channel = RMT_CH;
  cfg.gpio_num = (gpio_num_t)IR_RX_PIN;
  cfg.clk_div = RMT_CLK_DIV;
  cfg.mem_block_num = RMT_MEM_BLOCKS;
  cfg.rx_config.idle_threshold = IDLE_THRESHOLD_US;  // 单位 tick = us
  cfg.rx_config.filter_ticks_thresh = 0;
  cfg.rx_config.filter_en = false;  // 不滤波，最原始
  ESP_ERROR_CHECK(rmt_config(&cfg));
  ESP_ERROR_CHECK(rmt_driver_install(RMT_CH, RMT_RX_BUF_SIZE, 0));
  ESP_ERROR_CHECK(rmt_get_ringbuf_handle(RMT_CH, &rb));
  ESP_ERROR_CHECK(rmt_rx_start(RMT_CH, true));

  Serial.println();
  Serial.println("===== 红外原始数据抓取启动（v5：RMT 抓时间 + R05D 判断）=====");
  Serial.println("遥控器对准接收头按键，每次按键打印一组。");
  Serial.println("==================================================");
  Serial.println();
}

void loop() {
  size_t size = 0;
  rmt_item32_t *items =
      (rmt_item32_t *)xRingbufferReceive(rb, &size, pdMS_TO_TICKS(100));
  if (!items) return;

  uint16_t nItems = size / sizeof(rmt_item32_t);
  static uint16_t durs[MAX_LEVELS];
  static uint8_t levels[MAX_LEVELS];
  uint16_t nlevels = 0;
  for (uint16_t i = 0; i < nItems && nlevels < MAX_LEVELS; i++) {
    if (items[i].duration0 > 0) {
      levels[nlevels] = items[i].level0;
      durs[nlevels] = items[i].duration0;
      nlevels++;
    }
    if (items[i].duration1 > 0 && nlevels < MAX_LEVELS) {
      levels[nlevels] = items[i].level1;
      durs[nlevels] = items[i].duration1;
      nlevels++;
    }
  }
  vRingbufferReturnItem(rb, items);
  if (nlevels == 0) return;
  processCapture(durs, levels, nlevels);
}
