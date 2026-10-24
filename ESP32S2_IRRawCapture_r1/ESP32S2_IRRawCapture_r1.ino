//================================================================
// ESP32S2_IRRawCapture_r1.ino
// 红外原始数据抓取工具（配合美的空调 COOLIX 分析）
//
// 用法：
//   1. 烧进板子（红外接收头接 IO16，和测试程序同一块板）。
//   2. 打开串口监视器（115200），把遥控器对准接收头按键。
//   3. 每次按键，串口打印一帧的完整信息：时间戳、解码结果、
//      以及可直接用于 sendRaw 的 raw 数组（微秒单位）。
//   4. 美的遥控器一次按键发两帧，串口会连续打印两块，
//      第一块是 COOLIX，第二块是 UNKNOWN，两块都要复制下来，
//      时间戳相减就是两帧的间隔。
//
// 注意：
//   48 位帧约 100 个 entry，库默认缓存只有 100，刚好在边界上
//   容易截断，所以这里把缓存放大到 1024。
//   抓完数据后，把原来的测试程序（r8）烧回去继续测。
//
// 回放时用法（另写发送程序，不要和本程序混在一起）：
//   uint16_t rawData[...] = {...};  // 串口打印出来的数组，原样粘贴
//   irsend.sendRaw(rawData, sizeof(rawData) / sizeof(rawData[0]), 38);
//
// 依赖库：IRremoteESP8266（作者 David Conran，建议 2.8 以上）
// 开发板：ESP32S2 Dev Module（LOLIN_S2_MINI 也行）
//================================================================

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

#define IR_RX_PIN  16   // 红外接收头（HS0038B）

// 第二个参数是缓存 entry 数：1024，保证 48 位长帧不被截断
IRrecv irrecv(IR_RX_PIN, 1024);
decode_results results;

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) delay(10);  // 等 native USB，最多 2s

  irrecv.enableIRIn();   // RMT 接收，IO16

  Serial.println();
  Serial.println("===== 红外原始数据抓取启动 =====");
  Serial.println("遥控器对准接收头按键，每帧打印一块：");
  Serial.println("  时间戳 + 解码结果 + sendRaw 可用的 raw 数组");
  Serial.println("美的遥控器一次按键发两帧，会连续打印两块，都要复制");
  Serial.println("================================");
  Serial.println();
}

void loop() {
  if (irrecv.decode(&results)) {
    Serial.println("--------------------------------------------------");
    Serial.print("时间(ms): ");
    Serial.println(millis());
    Serial.print("帧长度(rawlen): ");
    Serial.println(results.rawlen);
    if (results.overflow) {
      Serial.println("警告：缓存溢出，本帧数据不完整！");
    }
    Serial.println(resultToHumanReadableBasic(&results));
    Serial.println("--- raw 数组（微秒，可直接用于 sendRaw）---");
    Serial.println(resultToSourceCode(&results));
    Serial.println();
    irrecv.resume();   // 继续接收下一帧
  }
}
