//================================================================
// ESP32S2_HWTest_r59.ino
// CoreBoard ESP32-S2 mini 硬件测试（r59：修 r56 编译错：菜单切换日志挪进分支内，if-else 链恢复）
//
// 相对 r42 的改动：
//   1. 温度推送取消 0.5°C 阈值，改每 1 秒推一次当前值（BMP 温度与芯片温度都是）。
//   2. HomeSpan 加配件 4：芯片内核温度（ESP32-S2 内置 TSENS，新版 IDF 驱动，
//      量程 -10~80°C；标准 TemperatureSensor，家庭显示“芯片温度”瓷砖）。
//      气压仍无标准 HomeKit 服务，进不了家庭，与 BMP 温度同一次读数、屏上同步刷新。
//   3. BMP085 读数周期由 30 秒改 1 秒（读一次约 10ms，1 秒分摊可忽略），
//      保证每秒推家庭的都是新鲜值。
//   2. 一级菜单加 HomeSpan 页：显示当前配对码（默认 466-37-726）；
//      配对码存 NVS（hk/pair），网页表单可改（8 位数字），重启后确保生效；
//      改码只影响下次重新添加，已配对的不受影响。
//   3. 红外复制换编号时：非学习状态下旧提示清掉（学习中不清除，
//      提示讲的是待存捕获，换编号后仍有效）。
//
// 相对 r40 的改动（纯编译修复）：Preferences 的 64 位无符号方法
//   不叫 getUInt64/putUInt64（不存在），叫 getULong64/putULong64
//   （getUInt 只是 32 位）。copyLoadAll 与 copySaveSlot 各改一处，
//   其余不动。
//
// 相对 r39 的改动（三点）：
//   ① 一级菜单滚动化：菜单到 9 项，改成空调页同款窗口（可见
//   7 行 + 右侧滚动条），行高恢复 32（40+7*32=264，正好贴
//   到提示行，r37 为挤 8 项改 28 的妥协撤销；再加项不用改
//   布局，窗口自动滚动）。
//   ② 一级菜单加“红外复制”：10 个编号（1-10），键码存 NVS
//   （命名空间 ircopy），重新上电不丢。KEY3 选编号（1-10
//   循环），KEY2 发射选中编号的键码，KEY1 切换学习/存储
//   （学习时接收并解析，显示协议与键码；再按 KEY1 存入
//   当前编号；学习中无捕获时按 KEY1 取消）。学习与发射互
//   斥：学习中按 KEY2 不发（显示“学习中无法发射”）。屏
//   显：编号（含网页命的名）、模式、状态、已存协议与键码、
//   学习到的协议与键码。学习中换编号不取消学习，存时以
//   当前编号为准；KEY4 回菜单会取消学习、清除未存捕获。
//   发射支持 NEC（sendNEC）与 COOLIX（美的空调，走现有
//   sendR05D 三字节码）；其它协议只显示，KEY2 提示不支持
//   发射。发射时关接收头（与灯/空调发送一致，防自激回波）。
//   ③ 网页命名：控制页（:8080）底部加“红外复制编号命名”
//   表单，10 个编号各一个输入框（最长 8 字，保证屏上完整
//   显示），保存后同时写 NVS 与内存，设备屏立即显示新名，
//   不用重启，不影响已存键码。
//
// 相对 r32 的改动（三点）：
//   ① 首屏时间更新不再全屏刷新：原来每秒 screenDirty → fillScreen
//      全重画，屏闪。改为只重画变化的行——时钟每秒变只刷时钟行
//      （y=40，2 号字），日期/天数/SSID 很少变、变了才刷对应行；
//      每行先 fillRect 擦行区再印新串（新串比旧串短也不留残影）。
//      同步中/未连是静态画面，不每秒刷；NTP 同步完成（模式变了）
//      才全屏重画切布局。进屏、联网/断网切换仍全屏重画。
//   ② WiFi 页按键对调 + 网页合页：KEY1 改为开关网页服务（原来
//      是热点开关，不管网页），KEY2 改为开关配置热点（热点状态
//      仍跨菜单保持）；网页的红外控制（“/”）与 WiFi 配置（“/wifi”）
//      合为一页（根页同时放按钮和表单），“/wifi”旧地址 302 回根页，
//      保存失败页的返回链接也改回根页。
//   ③ 家庭按钮连发优化：一次点按在串口上看是连发多次，原因是
//      家庭 App 一次点按可能触发多次 HAP 写入（重试），r32 的队列
//      又会把重复全发出来。本版排队时去重——同一动作已在队里
//      （还没发）就合并丢弃，不再排队；隔开点按（上一发已出队）
//      仍各发各的。排队/合并都打一行可读日志（动作名），便于
//      从串口确认一次点按只排一次。快速连点同一按钮（<500ms，
//      上一发还没出队）也会合并成一次，这是故意的。
//
// 相对 r28 的改动（两点）：
//   ① 网页拆分：原来根页混着 WiFi 表单和灯/空调按钮。本版拆成
//      两个页——根页“/”只放灯/空调控制（含上次发送），“/wifi”
//      只放 WiFi 配置表单，两页互链；网页服务不再只在热点开着
//      时跑，家里 WiFi 连上后也能用（控制页用家里 IP:8080 打开）。
//   ② 网页/家庭与按键的发送条件统一：原来按键、网页、家庭三路
//      的发码函数相同，但网页/家庭发出的信号与按键抓到的不一
//      致、设备不响应。两个修正：(a) 三个 Raw 发送函数在发码
//      前后开关接收头——接收头离发射管太近，发送时自己会收到
//      回波（串口里那些 UNKNOWN 就是回波），还会在单核 S2 上
//      跟发送抢 RMT/中断；发送时关接收、发完再开，各路条件
//      一致，也不再有回波干扰；(b) TFT 灯页按“开/关”拨动码后
//      把 lightIsOn 翻转一次，否则网页/家庭的开/关判断（依赖
//      lightIsOn）会与按键拨动后的实际灯态脱节。
//
// 相对 r25 的改动（两点）：
//   ① HomeKit 服务名：服务（LightBulb/Switch）的显示名不能用
//      Characteristic::Name（Name 只属于 AccessoryInformation，
//      是配件名），要用 Characteristic::ConfiguredName（HAP 服务
//      显示名）。r25 以前在服务里放 Name，HomeSpan 的服务表里
//      根本没有这一项，iOS 读不到就退回通用名“开关、开关2…”。
//      本版三处 DEV 的 Name 全改成 ConfiguredName，配件名
//      （AccessoryInformation 里的 Name）保持不动。
//   ② 网页/HomeKit 的红外改登记制：以前在 HTTP/HAP 回调里直接
//      发红外（按键是在 loop 里直接发），两路上下文不同。本版
//      网页与 HomeKit 只登记待发动作（requestIr），由 loop 在
//      同一位置统一执行（execPendingIr，与按键同一上下文），
//      顺带让 HTTP/HAP 回复更快；网页根页新增“上次发送”一行，
//      点完按钮回页就能看到是否已执行。
//
// ---- 菜单结构 ----
//   首页（WiFi 已连显示时钟，未连显示 Esp32-IR）
//     KEY4 -> 一级菜单：灯 / 空调 / 红外解码 / 红外测试 / WiFi设置 / I2C寻址 / 红外复制 / 关于 / 返回
//       灯：9 个 NEC 键码，KEY2/KEY3 选码，KEY1 发送，KEY4 回菜单
//       空调：15 项滚动（开机/关机/温度+/温度-/模式循环/风速+/风速-/
//             屏显切换/左右风开/关/上下风开/关/强劲开/关/模式），
//             KEY2/KEY3 选项目（到边缘滚动），KEY1 执行，KEY4 回菜单；
//             状态行显示当前模式/温度/风速，右侧滚动条指示位置
//       红外解码：显示最近一次接收的协议 / 数值 / 位数 / 原始项数，
//                 KEY1 清除，KEY4 回菜单
//       红外测试：3 秒亮 / 3 秒灭循环发 38kHz 载波，
//                 KEY1 开始/暂停，KEY4 回菜单并停止
//       WiFi设置：KEY1 开关网页，KEY2 开关配置热点，KEY4 回菜单；网页（控制+WiFi表单同页）开着时可用
//                 （含 WiFi 表单 + 灯/空调共 10 个红外按钮）
//     HomeKit：WiFi 连上后自动启动（灯 + 空调两个配件），不占菜单项
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
//   底部三行：发送提示（上次发送的键名）/ 解码提示（最近一次接收的键名）/ 按键提示。
//   解码提示：接收值先查灯码表、再查空调码表，查到显示“键名 + 数值”
//   （太长时只显示键名），查不到显示数值本身。
//
// ---- 多文件 ----
//   本文件：状态机、按键、红外收发、主循环。
//   codes.h：灯（NEC）与空调（R05D）码表。
//   ui.h：字库混排与全部屏幕绘制（含局部刷新）。
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
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <Wire.h>
#include "driver/temperature_sensor.h"  // 芯片内核温度（S2 内置 TSENS，新版 IDF 驱动）
#include "esp_task_wdt.h"  // 任务看门狗（r56：loop 卡死自动重启）
#include "HomeSpan.h"
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

#define I2C_SDA_PIN   33
#define I2C_SCL_PIN   35

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
enum MenuState { ST_HOME, ST_MENU, ST_LAMP, ST_AC, ST_DECODE, ST_IRTEST, ST_WIFI, ST_ABOUT, ST_I2C, ST_COPY, ST_HS, ST_DISP, ST_CHIP };
MenuState state = ST_HOME;
uint8_t menuSel = 0;   // 一级菜单选择（0~11，12 项）
uint8_t menuTop = 0;   // 一级菜单窗口顶行（可见 7 行，上限随 MENU_COUNT 动态算，空调页同款）

// 一级菜单状态钳制（r56 防御）：menuSel/menuTop 若被异常改写越界，行绘制会算出屏外坐标；
//   正常流程下两者恒在合法范围，本函数是 no-op，只防状态损坏后菜单假死。
static void fixMenuState() {
  if (menuSel >= MENU_COUNT) menuSel = 0;
  if (menuTop > MENU_COUNT - MENU_WIN_ROWS) menuTop = MENU_COUNT - MENU_WIN_ROWS;
}
// r45 注：r44 的一级菜单滑动动画已拿掉——每帧重画 3 张卡片 + 中文点阵字，
// S2 单核 + SPI 跟不上，跳帧还留残影（照片里行间拖影即此）；改回瞬时切换。
uint8_t lampSel = 0;   // 灯键码选择（0~8）
uint8_t acSel   = 0;   // 空调菜单选择（0~6）

// 空调跟踪状态（默认与开机码 B21FD8 一致：自动 26度 固定风）
// 改开机码时同步改这里，否则开机后状态显示与实际发送不一致
uint8_t acTemp = 26;   // 17~30
uint8_t acFan  = 4;    // AC_FANS 下标：0=自动，1=低风，2=中风，3=高风，4=固定风
uint8_t acMode = 0;    // AC_MODES 下标：0=自动，1=制冷，2=抽湿，3=制热，4=送风
uint8_t acTop = 0;      // 空调列表窗口顶行（0~8，可见 7 行）

// 红外测试状态（红外测试子菜单）
bool irTestRunning = false;   // KEY1 开始/暂停
bool irTestOnPhase = true;    // 当前相位：true=亮，false=灭
unsigned long irTestPhaseMs = 0;  // 当前相位开始时间
uint16_t irTestMark[2] = {20000, 100};  // RMT 载波突发：20ms 载波 + 0.1ms 间隔

// ---------------- 运行状态 ----------------
int8_t   lastKey     = -1;   // 上次按下的键 0..3；-1 = 还没按过
uint8_t  ledCode     = 0;
String   irRxProto   = "--";   // 接收到的协议名，如 NEC / COOLIX
String   irRxHex     = "--";   // 接收到的解码值，如 0x01FE807F
String   lastSent    = "--";   // 上次发送的键名（灯/空调子菜单底部显示）
String   lastRecv    = "--";   // 最近一次接收（灯/空调子菜单底部显示）
uint16_t recvBits    = 0;      // 接收到的位数
uint16_t recvLen     = 0;      // 接收到的原始项数
bool     screenDirty = true;
// 熄屏（r46）：3 分钟无按键 -> 背光灭；任意按键唤醒（唤醒那一下只唤醒，不执行按键动作）
bool     screenAsleep = false;
unsigned long lastKeyMs = 0;
const unsigned long SCREEN_SLEEP_MS = 180000;  // 3 分钟
unsigned long breathT0 = 0;      // 呼吸灯周期起点（熄屏时刻，周期 2s 从灭开始淡入）
unsigned long lastBreathMs = 0;  // 呼吸灯上次写占空比（20ms 节流，写寄存器够用）

unsigned long homeClockMs = 0;  // 首页时钟 1 秒刷新计时

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

// 发送灯码（NEC 32 位）：Raw 版只发红外 + 串口，不碰屏幕，供 act* 共用
void sendLampRaw(uint8_t idx) {
  // r58：发送时不再关接收头，让发送回波被接收头解码、显示在接收行（发送确认）；
  //   NEC/COOLIX 回波能干净解码，不会像 Raw 回波那样出 UNKNOWN
  irsend.sendNEC(LAMP_CODES[idx].nec, 32);
  Serial.print("发送灯码: ");
  Serial.print(LAMP_CODES[idx].name);
  Serial.print(" NEC ");
  Serial.println(LAMP_CODES[idx].hex);
}

// TFT 灯页：Raw + 设 lastSent + 只刷底部“发送:”行，不全屏
void sendLamp(uint8_t idx) {
  sendLampRaw(idx);
  lastSent = LAMP_CODES[idx].name;
  updateSendLine(lastSent);
}

// 发送空调码（R05D 构造发送）：Raw 版只发红外 + 串口，不碰屏幕
void sendAcRaw(uint8_t idx) {
  // r58：发送时不关接收头（同 sendLampRaw），回波显示在接收行
  sendR05D(AC_CODES[idx].a, AC_CODES[idx].b, AC_CODES[idx].c);
  Serial.print("发送空调码: ");
  Serial.println(AC_CODES[idx].name);
}

// TFT 空调页：Raw + 设 lastSent + 只刷底部“发送:”行
void sendAc(uint8_t idx) {
  sendAcRaw(idx);
  lastSent = AC_CODES[idx].name;
  updateAcSendLine(lastSent);
}

// 发送空调功能码（固定码，R05D 构造发送）
void sendAcFunc(uint8_t idx) {
  // r58：撤销 r57 加的开关接收，发送时不关接收头，回波显示在接收行（与灯/空调其它发送一致）
  sendR05D(ACFUNC_CODES[idx].a, ACFUNC_CODES[idx].b, ACFUNC_CODES[idx].c);
  lastSent = ACFUNC_CODES[idx].name;
  updateAcSendLine(lastSent);  // 只刷底部卡片内“发送:”行，不全屏
  Serial.print("发送空调功能码: ");
  Serial.println(ACFUNC_CODES[idx].name);
}

// 当前空调状态串（空调屏状态行用）：送风无温度，只显示模式+风速
String buildAcStatus() {
  String s = AC_MODES[acMode].name;
  s += " ";
  if (acMode != 4) { s += String(acTemp); s += "度"; s += " "; }
  s += AC_FANS[acFan].name;
  return s;
}

// 由当前状态拼出 R05D 三字节并发送（温度±/模式/风速±用）
//   B = 风速位<<5 | 11111；C = 温度位<<4 | 模式位<<2 | 00；送风时温度位=1110
// Raw 版只发红外 + 串口，不碰屏幕，供 act* 共用
void sendAcStateRaw() {
  uint8_t fanBits = AC_FANS[acFan].bits;
  uint8_t tempBits = (acMode == 4) ? 0xE : AC_TEMP_BITS[acTemp - 17];
  uint8_t modeBits = AC_MODES[acMode].bits;
  uint8_t b = (fanBits << 5) | 0x1F;
  uint8_t c = (tempBits << 4) | (modeBits << 2);
  // r58：发送时不关接收头（同 sendLampRaw），回波显示在接收行
  sendR05D(0xB2, b, c);
  Serial.print("发送空调状态: ");
  Serial.print(buildAcStatus());
  Serial.print(" -> B2");
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
  if (c < 0x10) Serial.print('0');
  Serial.println(c, HEX);
}

// TFT 空调页：Raw + 设 lastSent + 只刷底部“发送:”行
void sendAcState(const char *action) {
  sendAcStateRaw();
  lastSent = action;
  updateAcSendLine(lastSent);
}

// ---------------- 统一动作（TFT / HomeKit / 网页三路共用）----------------
// 只改状态 + 发红外 + 串口 + lastSent，不碰屏幕。屏幕刷新由调用方负责：
// TFT 在 dispatch 里同步刷行；HomeKit/网页置 hs*Dirty，由 loop 在对应页补刷。
bool lightIsOn = false;  // 灯跟踪状态（开/关共用一个拨动码，靠它区分）
// 灯码下标（codes.h 顺序：0 夜灯，1 开/关）
const uint8_t LAMP_NIGHT_IDX = 0;
const uint8_t LAMP_POWER_IDX = 1;
// HomeKit/网页触发的发送：loop 补刷用（只在灯/空调页刷行，不在别页乱画）
bool hsSentDirty = false;     // 发送行待刷
bool hsAcStateDirty = false;  // 空调状态行待刷

void actLampNight() { sendLampRaw(LAMP_NIGHT_IDX); lastSent = LAMP_CODES[LAMP_NIGHT_IDX].name; }
void actLampOn()  { if (!lightIsOn) { sendLampRaw(LAMP_POWER_IDX); lightIsOn = true; } lastSent = "开"; }
void actLampOff() { if (lightIsOn) { sendLampRaw(LAMP_POWER_IDX); lightIsOn = false; } lastSent = "关"; }
void actAcOn()    { acTemp = 26; acFan = 4; acMode = 0; sendAcRaw(0); lastSent = AC_CODES[0].name; }
void actAcOff()   { sendAcRaw(1); lastSent = AC_CODES[1].name; }
void actAcTempUp()   { if (acTemp < 30) acTemp++; sendAcStateRaw(); lastSent = "温度+"; }
void actAcTempDown() { if (acTemp > 17) acTemp--; sendAcStateRaw(); lastSent = "温度-"; }
void actAcMode()     { acMode = (acMode + 1) % AC_MODE_COUNT; if (acMode == 0 || acMode == 2) acFan = 4; sendAcStateRaw(); lastSent = "模式循环"; }
void actAcFanUp()    { acFan = (acFan + 1) % AC_FAN_COUNT; sendAcStateRaw(); lastSent = "风速+"; }
void actAcFanDown()  { acFan = (acFan + AC_FAN_COUNT - 1) % AC_FAN_COUNT; sendAcStateRaw(); lastSent = "风速-"; }

// 网页/HomeKit 的红外登记制：回调里只登记、不直接发红外，loop 在
// 同一位置统一执行（与按键同一上下文）。人手点按速度远低于 loop
// 速度，单槽“后到覆盖先到”够用；若同一轮里登记两次，只执行最后一次。
enum PendingIr : uint8_t {
  PENDING_NONE, PENDING_LAMP_NIGHT, PENDING_LAMP_ON, PENDING_LAMP_OFF,
  PENDING_AC_ON, PENDING_AC_OFF, PENDING_AC_TEMP_UP, PENDING_AC_TEMP_DOWN,
  PENDING_AC_MODE, PENDING_AC_FAN_UP, PENDING_AC_FAN_DOWN
};
SpanCharacteristic *pendingRevert = nullptr;  // 待弹回的瞬时按键特征（loop 里 setVal，勿在 update 里做）
// 网页/HomeKit 红外排队：登记时记时间，loop 里等网络安静（500ms）再发，
//   快点多次按顺序全发（单槽后到覆盖先到会丢动作）。按键不走排队。
#define PENDING_QUEUE_LEN 8
#define PENDING_SEND_DELAY_MS 500
struct PendingItem { uint8_t action; bool stateDirty; unsigned long at; };
PendingItem pendingQueue[PENDING_QUEUE_LEN];
uint8_t pendingHead = 0, pendingCount = 0;
const char *pendingName(uint8_t a) {  // 排队日志用可读动作名（串口确认一次点按只排一次）
  switch (a) {
    case PENDING_LAMP_NIGHT: return "灯夜灯";
    case PENDING_LAMP_ON: return "灯开";
    case PENDING_LAMP_OFF: return "灯关";
    case PENDING_AC_ON: return "空调打开";
    case PENDING_AC_OFF: return "空调关闭";
    case PENDING_AC_TEMP_UP: return "空调温度+";
    case PENDING_AC_TEMP_DOWN: return "空调温度-";
    case PENDING_AC_MODE: return "空调模式";
    case PENDING_AC_FAN_UP: return "空调风速+";
    case PENDING_AC_FAN_DOWN: return "空调风速-";
    default: return "未知";
  }
}
void requestIr(uint8_t p, bool stateDirty) {  // 不用 PendingIr：Arduino 自动原型插在 enum 前会报未声明
  // 去重：同一动作已在队里（还没发）就合并丢弃——家庭 App 一次点按可能触发多次
  //   HAP 写入（重试），不合并会连发多次；快速连点同一按钮（<500ms，上一发还没出队）
  //   也会合并成一次，这是故意的；隔开点按（上一发已出队）仍各发各的。
  for (uint8_t i = 0; i < pendingCount; i++) {
    uint8_t idx = (pendingHead + i) % PENDING_QUEUE_LEN;
    if (pendingQueue[idx].action == p) {
      Serial.print("红外请求已合并（同动作还在队里）：");
      Serial.println(pendingName(p));
      return;
    }
  }
  if (pendingCount >= PENDING_QUEUE_LEN) {
    Serial.println("待发队列已满，丢弃本次红外请求");
    return;
  }
  uint8_t tail = (pendingHead + pendingCount) % PENDING_QUEUE_LEN;
  pendingQueue[tail].action = p;
  pendingQueue[tail].stateDirty = stateDirty;
  pendingQueue[tail].at = millis();
  pendingCount++;
  Serial.print("红外请求已排队：");
  Serial.print(pendingName(p));
  Serial.print("（队列 ");
  Serial.print(pendingCount);
  Serial.println("）");
}
void execPendingIr() {
  if (pendingCount == 0) return;
  PendingItem &it = pendingQueue[pendingHead];
  if (millis() - it.at < PENDING_SEND_DELAY_MS) return;  // 等网络安静
  uint8_t a = it.action;
  bool sd = it.stateDirty;
  pendingHead = (pendingHead + 1) % PENDING_QUEUE_LEN;
  pendingCount--;
  switch (a) {
    case PENDING_LAMP_NIGHT: actLampNight(); break;
    case PENDING_LAMP_ON: actLampOn(); break;
    case PENDING_LAMP_OFF: actLampOff(); break;
    case PENDING_AC_ON: actAcOn(); break;
    case PENDING_AC_OFF: actAcOff(); break;
    case PENDING_AC_TEMP_UP: actAcTempUp(); break;
    case PENDING_AC_TEMP_DOWN: actAcTempDown(); break;
    case PENDING_AC_MODE: actAcMode(); break;
    case PENDING_AC_FAN_UP: actAcFanUp(); break;
    case PENDING_AC_FAN_DOWN: actAcFanDown(); break;
    default: break;
  }
  hsSentDirty = true;
  if (sd) hsAcStateDirty = true;
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

// 把接收到的红外值翻译成键名（灯/空调子菜单底部的“接收:”行用）：
//   先查灯码表（NEC 32 位），再查空调码表（R05D 24 位，由 A/B/C 拼成）；
//   查到返回“键名 + 数值”，太长时只返回键名；查不到返回数值本身。
String lookupRecvName(uint32_t value, const String &hex) {
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    if (value == LAMP_CODES[i].nec) {
      String withHex = String(LAMP_CODES[i].name) + " " + hex;
      if (cnTextWidth(("接收: " + withHex).c_str(), 1) + 8 <= TFT_W) return withHex;
      return String(LAMP_CODES[i].name);
    }
  }
  for (uint8_t i = 0; i < AC_COUNT; i++) {
    uint32_t acVal = ((uint32_t)AC_CODES[i].a << 16)
                   | ((uint32_t)AC_CODES[i].b << 8)
                   | AC_CODES[i].c;
    if (value == acVal) {
      String withHex = String(AC_CODES[i].name) + " " + hex;
      if (cnTextWidth(("接收: " + withHex).c_str(), 1) + 8 <= TFT_W) return withHex;
      return String(AC_CODES[i].name);
    }
  }
  for (uint8_t i = 0; i < ACFUNC_COUNT; i++) {
    uint32_t funcVal = ((uint32_t)ACFUNC_CODES[i].a << 16)
                     | ((uint32_t)ACFUNC_CODES[i].b << 8)
                     | ACFUNC_CODES[i].c;
    if (value == funcVal) {
      String withHex = String(ACFUNC_CODES[i].name) + " " + hex;
      if (cnTextWidth(("接收: " + withHex).c_str(), 1) + 8 <= TFT_W) return withHex;
      return String(ACFUNC_CODES[i].name);
    }
  }
  return hex;
}

// ---------------- 红外复制（10 个编号，NVS 持久化，重新上电不丢）----------------
// 键码以解码后的协议 + 数值 + 位数存储（NVS 命名空间 ircopy，键 p/v/b/n + 编号 1-10；
// 数组下标即编号（1-10），下标 0 不用，免去到处 -1）。名称由网页表单设置，存 n 键。
// 发射只支持 NEC（sendNEC）与 COOLIX（美的空调，走现有 sendR05D 三字节码）；
// 其它协议只显示，KEY2 提示不支持发射。
uint8_t copySel = 1;  // 当前编号（1-10）
bool copyLearning = false;  // 是否处于学习中
bool copyHasCapture = false;  // 学习中是否已解析到键码（待存储）
String copyCapProto = "";
uint64_t copyCapValue = 0;
uint8_t copyCapBits = 0;
String copyStatus = "按 KEY1 开始学习";
String copyProto[11];
uint64_t copyValue[11];
uint8_t copyBits[11];
bool copyHas[11];
String copyName[11];
// 屏显串（refreshCopyLines 生成，drawCopyScreen 只负责画）
String copySlotLine = "", copyModeLine = "", copyStatusLine = "", copyStoredLine = "", copyLearnedLine = "";

// 数值转 0x 大写 hex（位数按 bits 折算，不足补 0；bits 为 0 时按 8 位）。
String copyHex(uint64_t v, uint8_t bits) {
  uint8_t digits = (bits + 3) / 4;
  if (digits == 0) digits = 8;
  if (digits > 16) digits = 16;
  String s = "0x";
  for (int i = digits - 1; i >= 0; i--) {
    uint8_t nib = (v >> (i * 4)) & 0x0F;
    s += (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
  }
  return s;
}

// 由当前复制状态生成 5 行屏显串（进入页、换编号、学习、解析、存储、发射后调用）。
void refreshCopyLines() {
  copySlotLine = String("编号 ") + copySel;
  if (copyName[copySel].length()) copySlotLine += " " + copyName[copySel];
  copyModeLine = String("模式 ") + (copyLearning ? "学习中" : "空闲");
  copyStatusLine = copyStatus;
  if (copyHas[copySel]) copyStoredLine = String("已存 ") + copyProto[copySel] + " " + copyHex(copyValue[copySel], copyBits[copySel]);
  else copyStoredLine = "已存 空";
  if (copyLearning) {
    if (copyHasCapture) copyLearnedLine = String("学习 ") + copyCapProto + " " + copyHex(copyCapValue, copyCapBits);
    else copyLearnedLine = "学习 等待接收";
  } else copyLearnedLine = "学习 --";
}

// 开机从 NVS 读入 10 个编号的键码与名称（无键即空编号）。
void copyLoadAll() {
  Preferences prefs;
  prefs.begin("ircopy", true);
  for (uint8_t s = 1; s <= 10; s++) {
    copyProto[s] = prefs.getString(("p" + String(s)).c_str(), "");
    copyValue[s] = prefs.getULong64(("v" + String(s)).c_str(), 0);
    copyBits[s] = prefs.getUChar(("b" + String(s)).c_str(), 0);
    copyHas[s] = (copyProto[s].length() > 0);
    copyName[s] = prefs.getString(("n" + String(s)).c_str(), "");
  }
  prefs.end();
}

// 把指定编号的键码写入 NVS（存储时调用，内存已先更新）。
void copySaveSlot(uint8_t slot) {
  Preferences prefs;
  prefs.begin("ircopy", false);
  prefs.putString(("p" + String(slot)).c_str(), copyProto[slot]);
  prefs.putULong64(("v" + String(slot)).c_str(), copyValue[slot]);
  prefs.putUChar(("b" + String(slot)).c_str(), copyBits[slot]);
  prefs.end();
}

// 发射指定编号的键码；空编号或不支持的协议返回 false（不发，调用方置状态）。
// 发射时关接收头（与灯/空调发送一致，防自激回波）。
bool copySendSlot(uint8_t slot) {
  if (!copyHas[slot]) return false;
  String proto = copyProto[slot];
  uint64_t v = copyValue[slot];
  irrecv.disableIRIn();
  if (proto == "NEC") {
    irsend.sendNEC((uint32_t)v, copyBits[slot]);
  } else if (proto == "COOLIX") {
    uint32_t code = (uint32_t)(v & 0xFFFFFF);
    sendR05D((uint8_t)(code >> 16), (uint8_t)(code >> 8), (uint8_t)code);
  } else {
    irrecv.enableIRIn();
    return false;
  }
  irrecv.enableIRIn();
  return true;
}

// ---------------- WiFi 网页配置 ----------------
WebServer wifiServer(8080);  // 不用 80：HomeSpan 的 HomeKit 服务（HAP）占着 80
Preferences wifiPrefs;
bool wifiApOn = false;
bool webEnabled = true;  // 网页总开关（RAM，重启默认开；WiFi 页 KEY2 切换）
String wifiSavedSsid = "";
String wifiSavedPass = "";

// 前置声明（wifiHandleSave 里调用 wifiStopAp，定义在后）
void wifiStartAp();
void wifiStopAp();

// HTML 转义（SSID 回填到表单用）
String htmlEscape(const String &s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

// 网页（根页“/”）：红外控制按钮 + WiFi 配置表单合为一页（动态拼当前状态 + 已保存 SSID 回填，密码不回填）；
//   “/wifi”旧地址 302 回根页（兼容旧书签）
// ---------------- HomeSpan 配对码（NVS 可改，默认 46637726）----------------
// HomeSpan 官方支持事后改 Setup Code（用户指南：“you can always change this
// at a later date”）；本程序把码存在自己的 NVS（命名空间 hk，键 pair），
// 开机读出后调 setPairingCode，网页改码后也立即调一次（重启后确保生效）。
// 注意：改码只影响下次重新添加配件，已配对的不受影响；码必须是 8 位数字。
String curPairCode = "46637726";  // 开机后由 loadPairCode 从 NVS 覆盖
// 编译时间：__DATE__="Oct 29 2026" → "26/10/29:E"（:E 是固定结尾，不是星期也不是时刻）
//   （__DATE__ 的日为个位数时前面补空格，atoi 跳过空格照算）
String gBuildTime = "";
String fmtBuildTime() {
  const char *d = __DATE__;
  const char *mons[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  uint8_t mon = 1;
  for (uint8_t i = 0; i < 12; i++) {
    if (d[0] == mons[i][0] && d[1] == mons[i][1] && d[2] == mons[i][2]) { mon = i + 1; break; }
  }
  char buf[20];
  snprintf(buf, sizeof(buf), "%02d/%02d/%02d:E", atoi(d + 7) % 100, mon, atoi(d + 4));
  return String(buf);
}
String fmtPairCode(const String &c) {  // 46637726 -> 466-37-726（家庭里显示的格式）
  if (c.length() != 8) return c;
  return c.substring(0, 3) + "-" + c.substring(3, 5) + "-" + c.substring(5);
}
void loadPairCode() {
  Preferences prefs;
  prefs.begin("hk", true);
  String saved = prefs.getString("pair", "");
  prefs.end();
  bool ok = (saved.length() == 8);
  for (uint8_t k = 0; ok && k < 8; k++) if (saved[k] < '0' || saved[k] > '9') ok = false;
  if (ok) curPairCode = saved;
}

// 显示设置掩码（默认 31 全显示，存 NVS disp/mask）：bit0=时间，bit1=年/月，
//   bit2=今天是一年中的第几天，bit3=日历，bit4=农历；关掉的行留空不重排
//   （声明放这里是因为 loadDisplaySettings/saveDisplaySettings 在此用到它，
//   .ino 函数体只能用其上已声明的全局量，放首页变量区会报未声明）
// 显示设置掩码（NVS 命名空间 disp，键 mask；无保存时默认 31 全显示）
uint8_t dispMask = 31;
uint8_t dispSel = 0;  // 显示设置页当前选中项（0-4）
void loadDisplaySettings() {
  Preferences prefs;
  prefs.begin("disp", true);
  dispMask = prefs.getUChar("mask", 31);
  prefs.end();
  dispMask &= 0x1F;  // 只取低 5 位（防旧值越界）
}
void saveDisplaySettings() {
  Preferences prefs;
  prefs.begin("disp", false);
  prefs.putUChar("mask", dispMask);
  prefs.end();
}

String wifiPageHtml() {
  // 配置页美化（r53）：深色卡片风（与屏上深色卡片同款观感），手机优先；
  //   样式全内联（热点模式下没外网，不能引用外部 CSS）；所有接口、字段名、跳转原样保留
  String sta = (WiFi.status() == WL_CONNECTED)
    ? "已连接：" + WiFi.SSID() + "（" + WiFi.localIP().toString() + "）"
    : "未连接";
  String h = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32-S2 红外控制</title>"
    "<style>body{background:#0b1622;color:#e8eef4;font-family:system-ui,sans-serif;margin:0;padding:16px}"
    "h2{font-size:20px;margin:4px 0 16px}"
    ".card{background:#162838;border:1px solid #2a3f55;border-radius:12px;padding:16px;margin:0 0 16px}"
    "h3{font-size:17px;margin:0 0 12px}"
    "p{line-height:1.6;margin:8px 0}.note{color:#9fb3c8;font-size:14px}"
    ".btns{display:flex;flex-wrap:wrap;gap:8px}"
    ".btn{display:inline-block;padding:13px 18px;background:#1f7ae0;color:#fff;text-decoration:none;border-radius:10px;font-size:16px}"
    "label{display:block;color:#9fb3c8;font-size:14px;margin:10px 0 4px}"
    "input{width:100%;padding:12px;background:#0b1622;border:1px solid #2a3f55;border-radius:8px;color:#fff;box-sizing:border-box;font-size:16px}"
    "button{width:100%;padding:14px;margin-top:14px;background:#1f7ae0;color:#fff;border:0;border-radius:10px;font-size:16px}</style></head><body>"
    "<h2>ESP32-S2 红外控制</h2>"
    "<div class=\"card\"><p>当前状态：" + sta + "</p>"
    "<p>上次发送：" + htmlEscape(lastSent) + "</p></div>"
    "<div class=\"card\"><h3>灯</h3><div class=\"btns\">"
    "<a class=\"btn\" href=\"/ir?cmd=lamp_night\">夜灯</a>"
    "<a class=\"btn\" href=\"/ir?cmd=lamp_on\">开</a>"
    "<a class=\"btn\" href=\"/ir?cmd=lamp_off\">关</a></div></div>"
    "<div class=\"card\"><h3>空调</h3><div class=\"btns\">"
    "<a class=\"btn\" href=\"/ir?cmd=ac_on\">打开</a>"
    "<a class=\"btn\" href=\"/ir?cmd=ac_off\">关闭</a>"
    "<a class=\"btn\" href=\"/ir?cmd=ac_temp_up\">温度+</a>"
    "<a class=\"btn\" href=\"/ir?cmd=ac_temp_down\">温度-</a>"
    "<a class=\"btn\" href=\"/ir?cmd=ac_mode\">模式</a>"
    "<a class=\"btn\" href=\"/ir?cmd=ac_fan_up\">风速+</a>"
    "<a class=\"btn\" href=\"/ir?cmd=ac_fan_down\">风速-</a></div>"
    "<p class=\"note\">按钮通过设备红外发射，需对准灯/空调。</p></div>"
    "<div class=\"card\"><h3>WiFi 设置</h3>"
    "<form method=\"post\" action=\"/save\">"
    "<label>WiFi 名称（SSID）</label><input name=\"ssid\" value=\"" + htmlEscape(wifiSavedSsid) + "\" required>"
    "<label>密码</label><input type=\"password\" name=\"pass\" required>"
    "<button type=\"submit\">保存并连接</button></form>"
    "<p class=\"note\">保存后设备会尝试连接，成功后配置热点自动关闭。</p></div>";
  h += "<div class=\"card\"><h3>红外复制编号命名</h3><form method=\"post\" action=\"/ircopy_name\">";
  for (uint8_t s = 1; s <= 10; s++) {
    h += "<label>编号 " + String(s) + "</label><input name=\"n" + String(s) + "\" maxlength=\"8\" value=\"" + htmlEscape(copyName[s]) + "\">";
  }
  h += "<button type=\"submit\">保存名称</button></form>"
    "<p class=\"note\">名称显示在设备的红外复制页（编号旁），不影响已存键码。</p></div>";
  h += "<div class=\"card\"><h3>HomeSpan 配对码</h3><form method=\"post\" action=\"/paircode\">"
    "<label>配对码（8 位数字）</label><input name=\"paircode\" maxlength=\"8\" value=\"" + htmlEscape(curPairCode) + "\">"
    "<button type=\"submit\">保存配对码</button></form>"
    "<p class=\"note\">改码只影响下次重新添加配件（已配对的不受影响）；重启后确保生效。</p></div></body></html>";
  return h;
}

void wifiHandleRoot() { wifiServer.send(200, "text/html", wifiPageHtml()); }
void wifiHandleWifi() {  // “/wifi”已并入根页，旧地址 302 回根页
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(302, "text/plain", "");
}

// 网页红外按钮：执行统一动作，置 hs*Dirty 由 loop 补刷屏，302 回主页
void wifiHandleIr() {
  String cmd = wifiServer.arg("cmd");  // 只登记，红外由 loop 执行（见 requestIr）
  if (cmd == "lamp_night") requestIr(PENDING_LAMP_NIGHT, false);
  else if (cmd == "lamp_on") requestIr(PENDING_LAMP_ON, false);
  else if (cmd == "lamp_off") requestIr(PENDING_LAMP_OFF, false);
  else if (cmd == "ac_on") requestIr(PENDING_AC_ON, true);
  else if (cmd == "ac_off") requestIr(PENDING_AC_OFF, false);
  else if (cmd == "ac_temp_up") requestIr(PENDING_AC_TEMP_UP, true);
  else if (cmd == "ac_temp_down") requestIr(PENDING_AC_TEMP_DOWN, true);
  else if (cmd == "ac_mode") requestIr(PENDING_AC_MODE, true);
  else if (cmd == "ac_fan_up") requestIr(PENDING_AC_FAN_UP, true);
  else if (cmd == "ac_fan_down") requestIr(PENDING_AC_FAN_DOWN, true);
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(302, "text/plain", "");
}

void wifiHandleSave() {
  String ssid = wifiServer.arg("ssid");
  String pass = wifiServer.arg("pass");
  ssid.trim();  // SSID 两端空格通常是误输入，去掉
  wifiSavedSsid = ssid; wifiSavedPass = pass;
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  // 保持热点开着（以便回传结果），尝试连接，最多等 10 秒
  WiFi.begin(ssid, pass);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { esp_task_wdt_reset(); delay(500); }  // r56：等连接时喂狗，防看门狗误触发
  if (WiFi.status() == WL_CONNECTED) {
    String body = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head><body>"
      "<h2>连接成功</h2><p>网络：" + htmlEscape(ssid) + "</p><p>IP：" + WiFi.localIP().toString() + "</p>"
      "<p>配置热点即将关闭，可在设备 WiFi 页查看状态。</p></body></html>";
    wifiServer.send(200, "text/html", body);
    delay(1000);  // 让结果页发出去再关热点
    wifiStopAp();
    screenDirty = true;
  } else {
    String body = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head><body>"
      "<h2>连接失败</h2><p>已保存，但连不上（请检查名称/密码）。</p>"
      "<p><a href=\"/\">返回重填</a></p></body></html>";
    wifiServer.send(200, "text/html", body);
    screenDirty = true;  // 热点保持开启，可在原页重试
  }
}

// 红外复制编号命名保存（POST /ircopy_name）：10 个名称同时写 NVS 与内存，
// 设备屏立即显示新名（不用重启），不影响已存键码；保存后 302 回根页。
void wifiHandleCopyName() {
  Preferences prefs;
  prefs.begin("ircopy", false);
  for (uint8_t s = 1; s <= 10; s++) {
    String key = "n" + String(s);
    String nm = wifiServer.arg(key.c_str());
    nm.trim();
    prefs.putString(key.c_str(), nm);
    copyName[s] = nm;
  }
  prefs.end();
  if (state == ST_COPY) { refreshCopyLines(); screenDirty = true; }
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(302, "text/plain", "");
}

// HomeSpan 配对码保存（POST /paircode）：必须是 8 位数字；写 NVS 与内存，
// 立即调一次 setPairingCode（重启后确保生效）；在 HomeSpan 页时立即刷屏。
// 不合法时回错误页（不写 NVS、不跳转）。
void wifiHandlePairCode() {
  String code = wifiServer.arg("paircode");
  code.trim();
  bool ok = (code.length() == 8);
  for (uint8_t k = 0; ok && k < 8; k++) if (code[k] < '0' || code[k] > '9') ok = false;
  if (!ok) {
    wifiServer.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
      "</head><body><h2>配对码必须是 8 位数字</h2><p><a href=\"/\">返回重填</a></p></body></html>");
    return;
  }
  Preferences prefs;
  prefs.begin("hk", false);
  prefs.putString("pair", code);
  prefs.end();
  curPairCode = code;
  homeSpan.setPairingCode(code.c_str());
  if (state == ST_HS) screenDirty = true;
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(302, "text/plain", "");
}

void wifiStartAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32S2-Setup");
  if (webEnabled) wifiServer.begin();  // 网页关着时不开监听，免得偷偷重起服务
  wifiApOn = true;
  Serial.println("WiFi 配置热点已开启：ESP32S2-Setup，网页（控制+WiFi配置同页） " + WiFi.softAPIP().toString() + ":8080");
}

void wifiStopAp() {
  wifiServer.close();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiApOn = false;
  Serial.println("WiFi 配置热点已关闭");
}

// 网页总开关（WiFi 页 KEY1 调用）：只翻布尔不够，必须把服务彻底停/起，
//   否则浏览器悬着的连接会攒下来拖慢主循环。路由注册不受 stop/begin 影响。
void setWebEnabled(bool on) {
  if (on == webEnabled) return;
  webEnabled = on;
  if (on) {
    wifiServer.begin();  // 重起监听（关掉期间的重试已被拒，不会攒半截连接）
    Serial.println("网页服务已开启");
  } else {
    wifiServer.close();  // 先断当前客户端
    wifiServer.stop();   // 再停监听
    Serial.println("网页服务已关闭（重启后默认恢复开启）");
  }
  screenDirty = true;
}

// WiFi 页显示（状态/网络/IP/两行提示，由当前 WiFi 状态拼出）
void showWifiScreen() {
  String status, ssid, ip, tip1, tip2;
  if (wifiApOn) {
    status = "状态: 配置热点已开启";
    ssid = "网络: " + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : (wifiSavedSsid.length() ? wifiSavedSsid : "无"));
    ip = "AP IP: " + WiFi.softAPIP().toString();
    tip1 = "连热点 ESP32S2-Setup";
    tip2 = WiFi.softAPIP().toString() + ":8080";  // 只放地址（加“浏览器打开”会超宽）
  } else if (WiFi.status() == WL_CONNECTED) {
    status = "状态: 已连接";
    ssid = "网络: " + WiFi.SSID();
    ip = "IP: " + WiFi.localIP().toString();
    tip1 = "按 KEY2 开启配置热点";
    tip2 = "";
  } else {
    status = "状态: 未连接";
    ssid = "网络: " + (wifiSavedSsid.length() ? wifiSavedSsid : "无");
    ip = "IP: -";
    tip1 = "按 KEY2 开启配置热点";
    tip2 = "";
  }
  String webLine = webEnabled ? "网页: 开启 (KEY1 开关)" : "网页: 关闭 (KEY1 开启)";
  if (!webEnabled) {  // 网页关着时地址打不开，提示改成重开方法，免误导
    tip1 = "网页服务已关闭";
    tip2 = "按 KEY1 开启网页服务";
  }
  // 访问地址：热点模式已在卡内 tip2 显示（192.168.4.1:8080，短， fits），这里只补 STA 模式
  //   （STA 地址长，卡内 208px 会截断错地址，放卡外整宽显示；网页关着或未联网时不显示）
  String access = "";
  if (webEnabled && !wifiApOn && WiFi.status() == WL_CONNECTED) {
    access = WiFi.localIP().toString() + ":8080";
  }
  drawWifiScreen(status, ssid, ip, tip1, tip2, webLine, access);
}

// ---------------- HomeSpan（HomeKit：灯 + 空调 + 温度×2）----------------
// WiFi 连上后由 loop 启动一次；配对码默认 466-37-726（NVS hk/pair 可改，
// 网页 HomeSpan 配对码表单 + 设备 HomeSpan 页查看，见配对码一节）。
// 灯：LightBulb（开/关，拨动码靠 lightIsOn 区分）+ 夜灯瞬时开关（点按发码弹回）。
// 空调：7 个瞬时开关（点按发码弹回），与 TFT 空调页前 7 项、网页按钮一一对应。
// 温度：两个标准 TemperatureSensor（家庭显示“温度”与“芯片温度”），
//   每 1 秒各推一次当前值（无变化阈值）；气压无标准服务不进家庭
//   （与 BMP 温度同一次读数，屏上同步刷新）。
// 芯片温度是 S2 内核温度（受主频/负载/WiFi 影响，比室温高），看芯片发热用，
//   不是环境温度；量程 -10~80°C（误差 <1°C 的一档）。
struct DEV_Light : Service::LightBulb {
  SpanCharacteristic *power;
  DEV_Light() : Service::LightBulb() {
    power = new Characteristic::On(false);
    new Characteristic::ConfiguredName("灯");  // 服务显示名用 ConfiguredName，不是 Name
  }
  boolean update() override {
    if (power->getNewVal()) requestIr(PENDING_LAMP_ON, false);
    else requestIr(PENDING_LAMP_OFF, false);
    return true;
  }
};

struct DEV_NightLight : Service::Switch {
  SpanCharacteristic *power;
  DEV_NightLight() : Service::Switch() {
    power = new Characteristic::On(false);
    new Characteristic::ConfiguredName("夜灯");  // 服务显示名用 ConfiguredName，不是 Name
  }
  boolean update() override {
    if (power->getNewVal()) {
      requestIr(PENDING_LAMP_NIGHT, false);
      pendingRevert = power;  // 只登记弹回，loop 里 setVal（update 里 setVal 会警告并可能重发）
    }
    return true;
  }
};

struct DEV_AcButton : Service::Switch {
  SpanCharacteristic *power;
  uint8_t act;  // 0 打开 1 关闭 2 温度+ 3 温度- 4 模式 5 风速+ 6 风速-
  DEV_AcButton(uint8_t a, const char *name) : Service::Switch(), act(a) {
    power = new Characteristic::On(false);
    new Characteristic::ConfiguredName(name);  // 服务显示名用 ConfiguredName，不是 Name
  }
  boolean update() override {
    if (power->getNewVal()) {
      switch (act) {
        case 0: requestIr(PENDING_AC_ON, true); break;
        case 1: requestIr(PENDING_AC_OFF, false); break;
        case 2: requestIr(PENDING_AC_TEMP_UP, true); break;
        case 3: requestIr(PENDING_AC_TEMP_DOWN, true); break;
        case 4: requestIr(PENDING_AC_MODE, true); break;
        case 5: requestIr(PENDING_AC_FAN_UP, true); break;
        case 6: requestIr(PENDING_AC_FAN_DOWN, true); break;
      }
      pendingRevert = power;  // 只登记弹回，loop 里 setVal（update 里 setVal 会警告并可能重发）
    }
    return true;
  }
};

// 温度服务（通用）：标准 TemperatureSensor，家庭里显示为温度瓷砖。
// 配件 3 用它挂 BMP085 环境温度（名“温度”），配件 4 用它挂芯片内核温度（名“芯片温度”）。
// 气压没有标准 HomeKit 服务（苹果未定义气压特征，Eve 的自定义气压特征
// 家庭也看不到），所以气压不进家庭；气压与 BMP 温度同一次读数，屏上同步刷新。
struct DEV_TempSensor : Service::TemperatureSensor {
  SpanCharacteristic *temp;
  DEV_TempSensor(float t0, const char *name) : Service::TemperatureSensor() {
    temp = new Characteristic::CurrentTemperature(t0);
    new Characteristic::ConfiguredName(name);
  }
};

// 家庭温度推送：BMP 温度与芯片温度，每 1 秒各推一次（无变化阈值）
SpanCharacteristic *bmpTempChar = nullptr;
SpanCharacteristic *chipTempChar = nullptr;

// 芯片内核温度传感器（ESP32-S2 内置 TSENS，新版 IDF 驱动；core 3.x 用
// driver/temperature_sensor.h，旧版 driver/temp_sensor.h 在 IDF 5.x 已移除）
temperature_sensor_handle_t chipTempHandle = NULL;
bool chipTempOk = false;

bool hsStarted = false;

void setupHomeSpan() {
  homeSpan.setPairingCode(curPairCode.c_str());
  homeSpan.setQRID("HOMESPAN");
  homeSpan.setWifiCredentials(wifiSavedSsid.c_str(), wifiSavedPass.c_str());
  homeSpan.begin(Category::Other, "ESP32S2-IR");
  // 配件 1：灯
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("灯");
      new Characteristic::Manufacturer("ESP32S2");
      new Characteristic::Model("HWTest");
      new Characteristic::FirmwareRevision("r23");
    new DEV_Light();
    new DEV_NightLight();
  // 配件 2：空调
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("空调");
      new Characteristic::Manufacturer("ESP32S2");
      new Characteristic::Model("HWTest");
      new Characteristic::FirmwareRevision("r23");
    new DEV_AcButton(0, "打开");
    new DEV_AcButton(1, "关闭");
    new DEV_AcButton(2, "温度加");
    new DEV_AcButton(3, "温度减");
    new DEV_AcButton(4, "模式");
    new DEV_AcButton(5, "风速加");
    new DEV_AcButton(6, "风速减");
  // 配件 3：BMP085 环境温度（初始 0，启动后 loop 立即推一次，之后每 1 秒推一次）
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("温度");
      new Characteristic::Manufacturer("ESP32S2");
      new Characteristic::Model("HWTest");
      new Characteristic::FirmwareRevision("r23");
    {
      DEV_TempSensor *d = new DEV_TempSensor(0, "温度");
      bmpTempChar = d->temp;
    }
  // 配件 4：芯片内核温度（初始 0，启动后 loop 立即推一次，之后每 1 秒推一次）
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("芯片温度");
      new Characteristic::Manufacturer("ESP32S2");
      new Characteristic::Model("HWTest");
      new Characteristic::FirmwareRevision("r23");
    {
      DEV_TempSensor *d = new DEV_TempSensor(0, "芯片温度");
      chipTempChar = d->temp;
    }
}

// ---------------- 首页时间数据 ----------------
// mode 0=无 WiFi（显示 Esp32-IR），1=时钟，2=已联网但 NTP 未同步
uint8_t homeMode = 0;
String homeClock = "", homeYday = "";
String homeBmpTemp = "BMP085 未连接", homeBmpPress = "";
String homeCalMonth = "";  // 首页周历：今天所在月的“YYYY年M月”
int homeCalDates[7] = {0, 0, 0, 0, 0, 0, 0};  // 周日..周六的日期号
int homeCalTodayIdx = 0;  // 今天在周历里的列号（0=周日..6=周六）
String homeLunar = "";       // 首屏农历（API 返回的 lunarMonthName + lunarDayName，如“九月二十”）
String lastLunarDate = "";    // 上次成功取到农历的公历日期（“YYYY-M-D”，隔日重取）
unsigned long lastLunarTryMs = 0;  // 上次尝试取农历的 millis（60s 节流，失败重试不刷屏）
// 首页局部刷新的“上次画出值”：全屏重画后由 syncHomeLines 同步，局部刷新时逐行比对
String lastHomeClock = "", lastHomeYday = "";
String lastHomeBmpTemp = "", lastHomeBmpPress = "";
String lastHomeCalMonth = "";
int lastHomeCalDates[7] = {0, 0, 0, 0, 0, 0, 0};
int lastHomeCalTodayIdx = -1;
String lastHomeLunar = "";
uint8_t lastHomeModeDrawn = 255;

// ---------------- BMP085（温度/气压，I2C 0x77，与 I2C 扫描同总线）----------------
// 直接用 Wire 实现，不另引库；算法为 Bosch 数据手册定点流程（与 Adafruit_BMP085 一致）。
#define BMP085_ADDR 0x77
int16_t bmpAC1, bmpAC2, bmpAC3, bmpB1, bmpB2, bmpMB, bmpMC, bmpMD;
uint16_t bmpAC4, bmpAC5, bmpAC6;
bool bmpPresent = false;
int32_t bmpTempTenths = 0;  // 温度，0.1°C 单位
int32_t bmpPressPa = 0;     // 气压，Pa
unsigned long bmpLastRead = 0;
unsigned long chipLastRefresh = 0;  // 芯片详情页卡片刷新计时（1 秒）

// 读 16 位（MSB 在前）；两字节分开读（不写进同一表达式，免求值顺序不定）。
// 返回 false 表示总线失败。
bool bmpReadU16(uint8_t reg, uint16_t *out) {
  Wire.beginTransmission(BMP085_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(BMP085_ADDR, (uint8_t)2) != 2) return false;
  int msb = Wire.read();
  int lsb = Wire.read();
  *out = ((uint16_t)msb << 8) | (uint16_t)lsb;
  return true;
}

// 读 EEPROM 校准系数（0xAA 起，11 个）；任一失败即判定不存在。
bool bmpInit() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  Wire.beginTransmission(BMP085_ADDR);
  if (Wire.endTransmission() != 0) return false;
  uint16_t v;
  if (!bmpReadU16(0xAA, &v)) return false; bmpAC1 = (int16_t)v;
  if (!bmpReadU16(0xAC, &v)) return false; bmpAC2 = (int16_t)v;
  if (!bmpReadU16(0xAE, &v)) return false; bmpAC3 = (int16_t)v;
  if (!bmpReadU16(0xB0, &v)) return false; bmpAC4 = v;
  if (!bmpReadU16(0xB2, &v)) return false; bmpAC5 = v;
  if (!bmpReadU16(0xB4, &v)) return false; bmpAC6 = v;
  if (!bmpReadU16(0xB6, &v)) return false; bmpB1 = (int16_t)v;
  if (!bmpReadU16(0xB8, &v)) return false; bmpB2 = (int16_t)v;
  if (!bmpReadU16(0xBA, &v)) return false; bmpMB = (int16_t)v;
  if (!bmpReadU16(0xBC, &v)) return false; bmpMC = (int16_t)v;
  if (!bmpReadU16(0xBE, &v)) return false; bmpMD = (int16_t)v;
  return true;
}

// 读一次温度+气压（OSS=0，最快，转换各 4.5ms；显示 0.1°C / 0.1hPa 已够）。
// 返回 false 表示本次失败（调用方置 bmpPresent=false，下次重初始化）。
bool bmpMeasure() {
  Wire.beginTransmission(BMP085_ADDR);
  Wire.write(0xF4); Wire.write(0x2E);  // 触发温度
  if (Wire.endTransmission() != 0) return false;
  delay(5);
  uint16_t ut;
  if (!bmpReadU16(0xF6, &ut)) return false;
  Wire.beginTransmission(BMP085_ADDR);
  Wire.write(0xF4); Wire.write(0x34);  // 触发气压（OSS=0）
  if (Wire.endTransmission() != 0) return false;
  delay(5);
  uint16_t up16;
  if (!bmpReadU16(0xF6, &up16)) return false;
  int32_t UT = ut;
  int32_t UP = up16;  // OSS=0 时 UP 即 (MSB<<8)|LSB（XLSB 经 >>8 后无用）
  int32_t X1 = (UT - (int32_t)bmpAC6) * (int32_t)bmpAC5 / 32768;
  int32_t X2 = (int32_t)bmpMC * 2048 / (X1 + (int32_t)bmpMD);
  int32_t B5 = X1 + X2;
  bmpTempTenths = (B5 + 8) / 16;
  int32_t B6 = B5 - 4000;
  X1 = ((int32_t)bmpB2 * (B6 * B6 / 4096)) / 2048;
  X2 = (int32_t)bmpAC2 * B6 / 2048;
  int32_t X3 = X1 + X2;
  int32_t B3 = (((int32_t)bmpAC1 * 4 + X3) + 2) / 4;  // OSS=0，<<0
  X1 = (int32_t)bmpAC3 * B6 / 8192;
  X2 = ((int32_t)bmpB1 * (B6 * B6 / 4096)) / 65536;
  X3 = ((X1 + X2) + 2) / 4;
  uint32_t B4 = ((uint32_t)bmpAC4 * (uint32_t)(X3 + 32768)) / 32768;
  uint32_t B7 = ((uint32_t)UP - (uint32_t)B3) * 50000;  // OSS=0，50000>>0
  int32_t p;
  if (B7 < 0x80000000) p = (int32_t)((B7 * 2) / B4);
  else p = (int32_t)((B7 / B4) * 2);
  X1 = (p >> 8) * (p >> 8);
  X1 = (X1 * 3038) / 65536;
  X2 = (-7357 * p) / 65536;
  p = p + (X1 + X2 + 3791) / 16;
  bmpPressPa = p;
  return true;
}

// 周期调用：未连接时重初始化（支持热插拔），已连接时测量。
void bmpRead() {
  if (!bmpPresent) { bmpPresent = bmpInit(); if (!bmpPresent) return; }
  if (!bmpMeasure()) bmpPresent = false;
}

// 由原始值算出首页显示串（未连接时温度行显示未连接、气压行留空）。
void refreshBmpStrings() {
  if (!bmpPresent) { homeBmpTemp = "BMP085 未连接"; homeBmpPress = ""; return; }
  int32_t t = bmpTempTenths;
  String sign = (t < 0) ? "-" : "";  // -0.5 度时 t/10 得 0 会丢负号，符号单拎出来
  int32_t at = (t < 0) ? -t : t;
  homeBmpTemp = "温度 " + sign + String(at / 10) + "." + String(at % 10) + "度";
  int32_t h = (bmpPressPa + 5) / 10;  // 四舍五入到 0.1hPa
  homeBmpPress = "气压 " + String(h / 10) + "." + String(h % 10) + "hPa";
}

// ---------------- 当天农历（API，替代 r48 的离线对照表） ----------------
// 接口：https://api.530.news/api/lunar?date=today&timezone=8（公开万年历，无密钥）
//   返回 {"code":200,"data":{"lunarMonthName":"九月","lunarDayName":"二十",...}}；
//   首屏只取 lunarMonthName + lunarDayName（如“九月二十”），其余字段（干支/生肖/节日）不用。
// 证书：公开数据、无凭证，用 setInsecure 跳过 CA 校验（免在 ESP32 上维护 CA 表）。
// 失败（未联网/超时/解析失败）返回 false，调用方保留旧值、60s 后重试，不刷屏。
bool jsonStringField(const String &payload, const char *key, String &value) {
  String pat = String("\"") + key + "\"";
  int i = payload.indexOf(pat);
  if (i < 0) return false;
  int c = payload.indexOf(':', i + pat.length());
  if (c < 0) return false;
  int s = payload.indexOf('"', c);
  if (s < 0) return false;
  int e = payload.indexOf('"', s + 1);
  if (e <= s) return false;
  value = payload.substring(s + 1, e);
  return value.length() > 0;
}
bool fetchLunar(String &monthName, String &dayName) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(2000);
  http.begin(client, "https://api.530.news/api/lunar?date=today&timezone=8");
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();
  int ci = payload.indexOf("\"code\"");
  if (ci < 0) return false;
  int cc = payload.indexOf(':', ci + 6);
  if (cc < 0) return false;
  int cs = cc + 1;
  while (cs < (int)payload.length() && payload[cs] == ' ') cs++;
  if (payload.substring(cs, cs + 3) != "200") return false;
  if (!jsonStringField(payload, "lunarMonthName", monthName)) return false;
  if (!jsonStringField(payload, "lunarDayName", dayName)) return false;
  return true;
}

void refreshHomeData() {
  refreshBmpStrings();  // BMP 与 WiFi/NTP 无关，三种模式都要刷
  if (WiFi.status() != WL_CONNECTED) { homeMode = 0; return; }
  time_t nowSec = time(nullptr);
  if (nowSec < 1700000000) { homeMode = 2; return; }  // NTP 还没同步到有效时间
  struct tm tm;
  localtime_r(&nowSec, &tm);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  homeClock = buf;
  // 一周日历（周日起）：今天列号 = wday 本身（0=日..6=六，与星期行标签顺序一致）；
  // 周日 = 今天 - 列号天（按天换算，UTC+8 无夏令时，跨月/跨年自动对）
  int todayIdx = tm.tm_wday;
  time_t sunday = nowSec - (time_t)todayIdx * 86400;
  for (uint8_t i = 0; i < 7; i++) {
    struct tm dtm;
    time_t d = sunday + (time_t)i * 86400;
    localtime_r(&d, &dtm);
    homeCalDates[i] = dtm.tm_mday;
  }
  homeCalTodayIdx = todayIdx;
  homeCalMonth = String(tm.tm_year + 1900) + "年" + String(tm.tm_mon + 1) + "月";
  // 年中第几天：xxx/yyy（yyy 按闰年取 365/366）
  int year = tm.tm_year + 1900;
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  homeYday = String(tm.tm_yday + 1) + "/" + String(leap ? 366 : 365);
  // 当天农历（API，date=today）：首次、隔日、或上次失败 60s 后重试；失败保留旧值
  //   （refreshHomeData 每秒跑一次，60s 节流 + 未联网时 fetchLunar 直接返回，不刷屏不阻塞久）
  String todayStr = String(tm.tm_year + 1900) + "-" + String(tm.tm_mon + 1) + "-" + String(tm.tm_mday);
  unsigned long nowMs = millis();
  if ((homeLunar.length() == 0 || todayStr != lastLunarDate) &&
      (lastLunarTryMs == 0 || nowMs - lastLunarTryMs >= 60000)) {
    lastLunarTryMs = nowMs;
    String mName, dName;
    if (fetchLunar(mName, dName)) {
      homeLunar = mName + dName;
      lastLunarDate = todayStr;
    }
  }
  homeMode = 1;
}

// 全屏重画后同步“上次画出值”（drawScreen 的 ST_HOME 分支调用）
void syncHomeLines() {
  lastHomeClock = homeClock;
  lastHomeYday = homeYday;
  lastHomeBmpTemp = homeBmpTemp; lastHomeBmpPress = homeBmpPress;
  lastHomeCalMonth = homeCalMonth;
  for (uint8_t i = 0; i < 7; i++) lastHomeCalDates[i] = homeCalDates[i];
  lastHomeCalTodayIdx = homeCalTodayIdx;
  lastHomeLunar = homeLunar;
  lastHomeModeDrawn = homeMode;
}

// 首页局部刷新（只在时钟模式下调用）：只重画变化的行/卡，不清屏。
//   行几何与 drawHomeScreen(mode 1) 一致：时钟 y=42（2 号字，擦 32 高）、
//   日历区 y=88..168（月份/星期/日期/农历四行，变化时整区擦了重画，免得新旧数字重叠）、
//   年中天数 y=174（居中，擦整行宽）、温压卡（206..270）变化时整卡重画。
void updateHomeClockLines() {
  if (homeMode != 1) return;
  if (homeClock != lastHomeClock) {
    if (dispMask & 0x01) {  // 时间开着才画时钟（关掉时只同步值，不画）
      tft.fillRect(0, 42, TFT_W, 32, UI_BG);
      int16_t cw = cnTextWidth(homeClock.c_str(), 2);
      printCN((TFT_W - cw) / 2, 42, homeClock.c_str(), ST77XX_WHITE, UI_BG, 2);
    }
    lastHomeClock = homeClock;
  }
  // 时钟与第几天只在各自开关开着时刷（关掉的行留空，不重画出来）
  bool calChanged = (homeCalMonth != lastHomeCalMonth) || (homeCalTodayIdx != lastHomeCalTodayIdx) || (homeLunar != lastHomeLunar);
  if (!calChanged) {
    for (uint8_t i = 0; i < 7; i++) {
      if (homeCalDates[i] != lastHomeCalDates[i]) { calChanged = true; break; }
    }
  }
  if (calChanged && (dispMask & 0x1A) != 0) {  // 日历区任一部分开着才重画（全关时跳过）
    tft.fillRect(0, 88, TFT_W, 80, UI_BG);  // 擦日历区（月份 88 + 星期 110 + 日期 132 + 农历 152）
    drawHomeCalendar(homeCalMonth, homeCalDates, homeCalTodayIdx, homeLunar, dispMask);
    lastHomeCalMonth = homeCalMonth;
    for (uint8_t i = 0; i < 7; i++) lastHomeCalDates[i] = homeCalDates[i];
    lastHomeCalTodayIdx = homeCalTodayIdx;
    lastHomeLunar = homeLunar;
  } else if (calChanged) {
    // 日历区全关：只同步上次值，不画（免得把关掉的行画出来）
    lastHomeCalMonth = homeCalMonth;
    for (uint8_t i = 0; i < 7; i++) lastHomeCalDates[i] = homeCalDates[i];
    lastHomeCalTodayIdx = homeCalTodayIdx;
    lastHomeLunar = homeLunar;
  }
  if (homeYday != lastHomeYday) {
    if (dispMask & 0x04) {  // 第几天开着才画（关掉时只同步值，不画）
      tft.fillRect(0, 174, TFT_W, 16, UI_BG);
      int16_t yw = cnTextWidth(homeYday.c_str(), 1);
      printCN((TFT_W - yw) / 2, 174, homeYday.c_str(), ST77XX_WHITE, UI_BG, 1);
    }
    lastHomeYday = homeYday;
  }
  // 温压：只刷变化的那张卡（present 切换时整区重画，避免单卡与通栏卡错位）
  if (homeBmpTemp != lastHomeBmpTemp || homeBmpPress != lastHomeBmpPress) {
    bool wasPresent = (lastHomeBmpPress.length() > 0);
    bool nowPresent = (homeBmpPress.length() > 0);
    if (wasPresent != nowPresent) {
      drawHomeBmpCards(homeBmpTemp, homeBmpPress);
    } else {
      if (homeBmpTemp != lastHomeBmpTemp) drawHomeTempCard(homeBmpTemp);
      if (homeBmpPress != lastHomeBmpPress) drawHomePressCard(homeBmpPress);
    }
    lastHomeBmpTemp = homeBmpTemp;
    lastHomeBmpPress = homeBmpPress;
  }
}

// ---------------- I2C 寻址 ----------------
// 方法参考 i2c_bus_scan.c（Pico SDK 示例）：扫全部 7 位地址、跳过保留地址
// （000 0xxx 与 111 1xxx，即 0x00-0x07 与 0x78-0x7F）、有应答（ACK）
// 即判定有设备。Pico 用 1 字节 dummy 读探查；Arduino 这里用等价的
// 地址写探查（beginTransmission + endTransmission 返回 0 即 ACK），
// 不额外读数据字节，对只写设备也安全。速率 100kHz（与参考一致）。
// 注意：总线需外部上拉（一般在设备板上），本程序不另开内部上拉。
const uint8_t I2C_FOUND_MAX = 32;  // 最多记录 32 个地址（屏上只显示前 8 行）
uint8_t i2cFound[I2C_FOUND_MAX];
uint8_t i2cFoundCount = 0;
bool    i2cScanned = false;  // 是否已扫过一次（未扫时显示“按 KEY1 开始寻址”）

void scanI2c() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  i2cFoundCount = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0 && i2cFoundCount < I2C_FOUND_MAX) {
      i2cFound[i2cFoundCount++] = addr;
    }
  }
  i2cScanned = true;
  screenDirty = true;
}

// ---------------- 屏幕刷新 ----------------
// 芯片详情（r55）：RAM=堆（ESP.getHeapSize/getFreeHeap），ROM=Flash（程序大小/Flash 容量），
//   温度=现读 TSENS（与 1c 家庭推送同一次读数口径，不存全局量）；PSRAM 本板没有，不显示
void chipStrings(String &chip, String &ram, String &ramPct, String &rom, String &romPct, String &temp) {
  chip = "芯片: " + String(ESP.getChipModel());
  size_t heapTotal = ESP.getHeapSize();
  size_t heapFree = ESP.getFreeHeap();
  size_t heapUsed = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
  ram = "RAM: " + String(heapUsed / 1024) + "KB/" + String(heapTotal / 1024) + "KB";
  ramPct = "RAM 占用 " + String(heapTotal ? (heapUsed * 100 / heapTotal) : 0) + "%";
  size_t sketch = ESP.getSketchSize();
  size_t flash = ESP.getFlashChipSize();
  String flashStr = (flash % 1048576 == 0) ? String(flash / 1048576) + "MB" : String(flash / 1048576.0, 1) + "MB";
  rom = "ROM: " + String(sketch / 1048576.0, 1) + "MB/" + flashStr;
  romPct = "ROM 占用 " + String(flash ? (sketch * 100 / flash) : 0) + "%";
  float tempC = 0;
  if (chipTempOk && temperature_sensor_get_celsius(chipTempHandle, &tempC) == ESP_OK) {
    temp = "芯片温度 " + String(tempC, 1) + "度";
  } else {
    temp = "芯片温度 --";
  }
}
void refreshChipCard() {  // 芯片页 1 秒刷新：只刷卡片（值是实时读的），不清屏
  String chip, ram, ramPct, rom, romPct, temp;
  chipStrings(chip, ram, ramPct, rom, romPct, temp);
  drawChipCard(chip, ram, ramPct, rom, romPct, temp);
}

void drawScreen() {
  switch (state) {
    case ST_HOME: refreshHomeData(); drawHomeScreen(homeMode, homeClock, homeYday, homeBmpTemp, homeBmpPress, homeCalMonth, homeCalDates, homeCalTodayIdx, homeLunar, dispMask); syncHomeLines(); break;
    case ST_MENU: fixMenuState(); drawMenuScreen(menuSel, menuTop); break;
    case ST_LAMP: drawLampScreen(lampSel, lastSent, lastRecv); break;
    case ST_AC: drawAcScreen(acSel, acTop, lastSent, lastRecv, buildAcStatus()); break;
    case ST_DECODE: drawDecodeScreen(irRxProto, irRxHex, recvBits, recvLen); break;
    case ST_IRTEST: drawIrTestScreen(irTestRunning, irTestOnPhase); break;
    case ST_WIFI: showWifiScreen(); break;
    case ST_ABOUT: drawAboutScreen(gBuildTime); break;
    case ST_I2C: drawI2cScreen(i2cScanned, i2cFound, i2cFoundCount); break;
    case ST_COPY: drawCopyScreen(copySlotLine, copyModeLine, copyStatusLine, copyStoredLine, copyLearnedLine); break;
    case ST_HS: drawHsScreen(fmtPairCode(curPairCode)); break;
    case ST_DISP: drawDispScreen(dispSel, dispMask); break;
    case ST_CHIP: {  // 进页整屏重画（值现算）；页内 1 秒刷新只刷卡片（见 loop 1d）
      String chip, ram, ramPct, rom, romPct, temp;
      chipStrings(chip, ram, ramPct, rom, romPct, temp);
      drawChipScreen(chip, ram, ramPct, rom, romPct, temp);
      break;
    }
  }
}

// ---------------- 按键 ----------------
// 注意：加减键导航只做局部刷新（重画旧行与新行），不置 screenDirty 全屏重画；
// 状态切换、发送、清除仍走 screenDirty 全屏重画。
void onKeyPressed(uint8_t i) {
  lastKey = i;
  applyLedCode(i);   // KEY1~KEY4 -> 0~3 -> LED 显示 00/01/10/11
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
      if (i == 3) { state = ST_MENU; screenDirty = true; }  // KEY4 -> 一级菜单（其他键只更新指示灯，不刷屏）
      break;
    case ST_MENU:
      fixMenuState();  // r56 防御：先钳制状态再切换，防越界行绘制
      if (i == 1) {  // KEY2 加 -> 下一项（到窗口边缘时滚动，空调页同款）
        uint8_t old = menuSel;
        menuSel = (menuSel + 1) % MENU_COUNT;
        if (menuSel == 0 || menuSel >= menuTop + MENU_WIN_ROWS) {
          menuTop = (menuSel == 0) ? 0 : menuSel - MENU_WIN_ROWS + 1;
          updateMenuListWindow(menuSel, menuTop);
        } else {
          updateMenuRow(old, menuTop, false);
          updateMenuRow(menuSel, menuTop, true);
        }
        Serial.print("菜单 -> "); Serial.print(menuSel); Serial.print("/"); Serial.print(MENU_COUNT); Serial.print(" top="); Serial.println(menuTop);
      }
      else if (i == 2) {  // KEY3 减 -> 上一项（到窗口边缘时滚动，空调页同款）
        uint8_t old = menuSel;
        menuSel = (menuSel + MENU_COUNT - 1) % MENU_COUNT;
        if (menuSel == MENU_COUNT - 1 || menuSel < menuTop) {
          menuTop = (menuSel == MENU_COUNT - 1) ? MENU_COUNT - MENU_WIN_ROWS : menuSel;
          updateMenuListWindow(menuSel, menuTop);
        } else {
          updateMenuRow(old, menuTop, false);
          updateMenuRow(menuSel, menuTop, true);
        }
        Serial.print("菜单 -> "); Serial.print(menuSel); Serial.print("/"); Serial.print(MENU_COUNT); Serial.print(" top="); Serial.println(menuTop);
      }
      else if (i == 0) {  // KEY1 确认 -> 进入子菜单
        // r57：进灯/空调/解码/复制页时强制重开接收头，防接收头被某条发送路径关掉后没开回来
        switch (menuSel) {
          case 0: state = ST_LAMP; irrecv.enableIRIn(); break;
          case 1: state = ST_AC; irrecv.enableIRIn(); break;
          case 2: state = ST_DECODE; irrecv.enableIRIn(); break;
          case 3: state = ST_IRTEST; break;
          case 4: state = ST_WIFI; break;
          case 5: state = ST_I2C; break;
          case 6: state = ST_COPY; irrecv.enableIRIn(); refreshCopyLines(); break;
          case 7: state = ST_HS; break;
          case 8: state = ST_DISP; break;
          case 9: state = ST_ABOUT; break;
          case 10: state = ST_CHIP; break;
          default: state = ST_HOME; break;  // 返回
        }
        screenDirty = true;
      }
      else if (i == 3) { state = ST_HOME; screenDirty = true; }  // KEY4 -> 首页
      break;
    case ST_LAMP:
      if (i == 1) {  // KEY2 加 -> 下一项（局部刷新）
        uint8_t old = lampSel;
        lampSel = (lampSel + 1) % LAMP_COUNT;
        updateLampRow(old, false);
        updateLampRow(lampSel, true);
      }
      else if (i == 2) {  // KEY3 减 -> 上一项（局部刷新）
        uint8_t old = lampSel;
        lampSel = (lampSel + LAMP_COUNT - 1) % LAMP_COUNT;
        updateLampRow(old, false);
        updateLampRow(lampSel, true);
      }
      else if (i == 0) {  // KEY1 -> 发送选中码
        sendLamp(lampSel);
        if (lampSel == LAMP_POWER_IDX) lightIsOn = !lightIsOn;  // 开/关是拨动码，翻转跟踪态，免网页/家庭判断脱节
      }
      else if (i == 3) { state = ST_MENU; screenDirty = true; }  // KEY4 -> 一级菜单
      break;
    case ST_AC:
      if (i == 1) {  // KEY2 加 -> 下一项（到窗口边缘时滚动）
        uint8_t old = acSel;
        acSel = (acSel + 1) % AC_MENU_COUNT;
        if (acSel == 0 || acSel >= acTop + AC_WIN_ROWS) {
          acTop = (acSel == 0) ? 0 : acSel - AC_WIN_ROWS + 1;
          updateAcListWindow(acSel, acTop);
        } else {
          updateAcRow(old, acTop, false);
          updateAcRow(acSel, acTop, true);
        }
      }
      else if (i == 2) {  // KEY3 减 -> 上一项（到窗口边缘时滚动）
        uint8_t old = acSel;
        acSel = (acSel + AC_MENU_COUNT - 1) % AC_MENU_COUNT;
        if (acSel == AC_MENU_COUNT - 1 || acSel < acTop) {
          acTop = (acSel == AC_MENU_COUNT - 1) ? AC_MENU_COUNT - AC_WIN_ROWS : acSel;
          updateAcListWindow(acSel, acTop);
        } else {
          updateAcRow(old, acTop, false);
          updateAcRow(acSel, acTop, true);
        }
      }
      else if (i == 0) {  // KEY1 -> 执行选中项（统一动作 + 同步刷发送行/状态行）
        switch (acSel) {
          case 0: actAcOn(); updateAcSendLine(lastSent); updateAcStatusLine(buildAcStatus()); break;
          case 1: actAcOff(); updateAcSendLine(lastSent); break;
          case 2: actAcTempUp(); updateAcSendLine(lastSent); updateAcStatusLine(buildAcStatus()); break;
          case 3: actAcTempDown(); updateAcSendLine(lastSent); updateAcStatusLine(buildAcStatus()); break;
          case 4: actAcMode(); updateAcSendLine(lastSent); updateAcStatusLine(buildAcStatus()); break;
          case 5: actAcFanUp(); updateAcSendLine(lastSent); updateAcStatusLine(buildAcStatus()); break;
          case 6: actAcFanDown(); updateAcSendLine(lastSent); updateAcStatusLine(buildAcStatus()); break;
          default:  // 附加功能固定码（屏显/摆风/强劲/模式）
            sendAcFunc(acSel - AC_COUNT - AC_CTRL_COUNT);
            break;
        }
      }
      else if (i == 3) { state = ST_MENU; screenDirty = true; }
      break;
    case ST_DECODE:
      if (i == 0) clearDecode();  // KEY1 -> 清除显示
      else if (i == 3) { state = ST_MENU; screenDirty = true; }
      break;
    case ST_IRTEST:
      if (i == 0) {  // KEY1 -> 开始/暂停
        irTestRunning = !irTestRunning;
        if (irTestRunning) { irTestOnPhase = true; irTestPhaseMs = millis(); }
        screenDirty = true;
      }
      else if (i == 3) {  // KEY4 -> 一级菜单（停止发射）
        irTestRunning = false;
        state = ST_MENU;
        screenDirty = true;
      }
      break;  // KEY2/KEY3 在此菜单无动作
    case ST_WIFI:
      if (i == 0) {  // KEY1 -> 开关网页服务（走 setWebEnabled，彻底停/起服务，不留半截连接）
        setWebEnabled(!webEnabled);
      }
      else if (i == 1) {  // KEY2 -> 开关配置热点（热点状态跨菜单保持）
        if (wifiApOn) wifiStopAp();
        else wifiStartAp();
        screenDirty = true;
      }
      else if (i == 3) {  // KEY4 -> 回菜单，热点状态保持不变（只由 KEY2 开关）
        state = ST_MENU; screenDirty = true;
      }
      break;
    case ST_ABOUT:
      if (i == 3) { state = ST_MENU; screenDirty = true; }
      break;
    case ST_CHIP:  // 芯片详情是纯显示页，只有 KEY4 回菜单
      if (i == 3) { state = ST_MENU; screenDirty = true; }
      break;
    case ST_I2C:
      if (i == 0) { scanI2c(); }  // KEY1 -> 寻址（scanI2c 内置 screenDirty）
      else if (i == 3) { state = ST_MENU; screenDirty = true; }  // KEY4 -> 回菜单
      break;
    case ST_COPY:
      if (i == 2) {  // KEY3 -> 选编号（1-10 循环；学习中也可换，存时以当前编号为准）
        copySel = (copySel % 10) + 1;
        // 换编号后旧提示（如“已存储到编号 N”“已发射”）不再属于新编号，清掉；
        // 学习中不清除（提示讲的是待存捕获，换编号后仍有效）。
        if (!copyLearning) copyStatus = "按 KEY1 开始学习";
        refreshCopyLines(); screenDirty = true;
      }
      else if (i == 1) {  // KEY2 -> 发射（学习中互斥，不发）
        if (copyLearning) { copyStatus = "学习中无法发射"; }
        else if (!copyHas[copySel]) { copyStatus = "空编号无法发射"; }
        else if (copySendSlot(copySel)) { copyStatus = "已发射"; }
        else { copyStatus = "该协议不支持发射"; }
        refreshCopyLines(); screenDirty = true;
      }
      else if (i == 0) {  // KEY1 -> 学习/存储切换
        if (!copyLearning) {
          copyLearning = true; copyHasCapture = false;
          copyStatus = "请按遥控器按键";
        } else if (copyHasCapture) {
          copyProto[copySel] = copyCapProto;
          copyValue[copySel] = copyCapValue;
          copyBits[copySel] = copyCapBits;
          copyHas[copySel] = true;
          copySaveSlot(copySel);
          copyLearning = false; copyHasCapture = false;
          copyStatus = "已存储到编号 " + String(copySel);
        } else {
          copyLearning = false;  // 学习中无捕获，按 KEY1 取消
          copyStatus = "已取消学习";
        }
        refreshCopyLines(); screenDirty = true;
      }
      else if (i == 3) {  // KEY4 -> 回菜单（取消学习、清除未存捕获）
        copyLearning = false; copyHasCapture = false;
        copyStatus = "按 KEY1 开始学习";
        state = ST_MENU; screenDirty = true;
      }
      break;
    case ST_HS:
      if (i == 3) { state = ST_MENU; screenDirty = true; }  // KEY4 -> 回菜单（仅显示配对码）
      break;
    case ST_DISP:
      if (i == 0) {  // KEY1 切换选中项的开关，立即存 NVS（断电也不丢），只刷该行
        dispMask ^= (1 << dispSel);
        saveDisplaySettings();
        updateDispRow(dispSel, dispMask, true);
      }
      else if (i == 1) {  // KEY2 下一项：只刷旧行（取消选中）+ 新行（选中），不清屏
        uint8_t old = dispSel;
        dispSel = (dispSel + 1) % DISP_COUNT;
        updateDispRow(old, dispMask, false);
        updateDispRow(dispSel, dispMask, true);
      }
      else if (i == 2) {  // KEY3 上一项：同上
        uint8_t old = dispSel;
        dispSel = (dispSel + DISP_COUNT - 1) % DISP_COUNT;
        updateDispRow(old, dispMask, false);
        updateDispRow(dispSel, dispMask, true);
      }
      else if (i == 3) { state = ST_MENU; screenDirty = true; }  // KEY4 -> 回菜单（换页仍需整屏重画）
      break;
  }
}

void pollKeys(unsigned long now) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t r = digitalRead(keyPins[i]);
    if (r != keyReading[i]) { keyReading[i] = r; keyT[i] = now; }
    if ((now - keyT[i] > DEBOUNCE_MS) && (keyReading[i] != keyStable[i])) {
      keyStable[i] = keyReading[i];
      if (keyStable[i] == LOW) {
        lastKeyMs = now;  // 任意按键都算一次操作，刷新熄屏计时
        if (screenAsleep) {
          // 唤醒：关呼吸 PWM，恢复按键指示灯 + 开背光 + 整屏重画（唤醒这一下不执行按键动作）
          ledcDetach(LED1);
          ledcDetach(LED2);
          pinMode(LED1, OUTPUT);
          pinMode(LED2, OUTPUT);
          applyLedCode(ledCode);  // 恢复熄屏前的按键指示（ledCode 由 applyLedCode 维护）
          digitalWrite(TFT_BL, BL_ON);
          screenAsleep = false;
          screenDirty = true;
        } else {
          onKeyPressed(i);   // 按下（低电平）才动作
        }
      }
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
  bootAnimation();      // 开机动画约 2s（此时 WiFi/HomeSpan 还没初始化，屏不黑着等）
  drawScreen();
  screenDirty = false;

  irrecv.enableIRIn();   // RMT 接收，IO16，缓存 512 项
  irsend.begin();        // RMT 发送，IO17

  // 芯片内核温度传感器（ESP32-S2 内置 TSENS）：量程 -10~80°C（误差 <1°C 的一档；
  // 芯片开 WiFi 时内核温度常在 50~60°C，量程不能选小）。装不上时 chipTempOk=false，
  // 家庭芯片温度瓷砖停在 0（看串口报错定位）。
  {
    temperature_sensor_config_t chipCfg = {};
    chipCfg.range_min = -10;
    chipCfg.range_max = 80;
    if (temperature_sensor_install(&chipCfg, &chipTempHandle) == ESP_OK
        && temperature_sensor_enable(chipTempHandle) == ESP_OK) {
      chipTempOk = true;
      Serial.println("芯片温度传感器已启动（量程 -10~80°C）");
    } else {
      Serial.println("芯片温度传感器启动失败（家庭芯片温度将停在 0）");
    }
  }

  Serial.println();
  Serial.println("===== ESP32-S2 mini 硬件测试启动（r24 菜单版）=====");
  Serial.println("首页：WiFi 已连显示时钟，未连显示 Esp32-IR，KEY4 进入一级菜单");
  Serial.println("一级菜单：灯 / 空调 / 红外解码 / 红外测试 / WiFi设置 / I2C寻址 / 红外复制 / HomeSpan / 关于 / 返回");
  Serial.println("按键：KEY4 菜单，KEY2 加（下一项），KEY3 减（上一项），KEY1 确认");
  Serial.println("灯：KEY2/KEY3 选码，KEY1 发送 NEC 码");
  Serial.println("空调：15 项滚动（状态控制 + 屏显/摆风/强劲/模式固定码，熟睡待补）");
  Serial.println("WiFi设置：KEY1 开关网页，KEY2 开关配置热点（ESP32S2-Setup），网页 192.168.4.1:8080（控制+WiFi配置同页）");
  Serial.println("红外测试：KEY1 开始/暂停 3 秒亮 3 秒灭，KEY4 返回");
  Serial.println("=====================================");

  // WiFi：读 NVS 里保存的网络，有则后台自动连接（不阻塞启动）
  wifiPrefs.begin("wifi", false);
  wifiSavedSsid = wifiPrefs.getString("ssid", "");
  wifiSavedPass = wifiPrefs.getString("pass", "");
  wifiServer.on("/", wifiHandleRoot);
  wifiServer.on("/wifi", wifiHandleWifi);
  wifiServer.on("/ir", wifiHandleIr);
  wifiServer.on("/save", HTTP_POST, wifiHandleSave);
  wifiServer.on("/ircopy_name", HTTP_POST, wifiHandleCopyName);
  wifiServer.on("/paircode", HTTP_POST, wifiHandlePairCode);
  wifiServer.onNotFound(wifiHandleRoot);
  if (wifiSavedSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSavedSsid.c_str(), wifiSavedPass.c_str());
    Serial.println("WiFi: 已保存 " + wifiSavedSsid + "，后台连接中");
  } else {
    Serial.println("WiFi: 无保存的网络（进 WiFi设置 页配置）");
  }

  // NTP：北京时间（UTC+8，无夏令时），联网后后台自动同步，首页时钟用
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.google.com");

  // 红外复制：开机从 NVS 读入 10 个编号的键码与名称
  copyLoadAll();

  // HomeSpan 配对码：开机从 NVS 读入（无保存时用默认 46637726）
  loadPairCode();

  // 显示设置掩码：开机从 NVS 读入（无保存时默认 31 全显示）
  loadDisplaySettings();
  gBuildTime = fmtBuildTime();  // 关于页显示用（开机时算一次，不用每次进关于页重算）

  lastKeyMs = millis();  // 熄屏计时从开机完成起算

  // 任务看门狗（r56）：loop 卡死超 10 秒自动重启，不再永久死机；开机初始化慢，
  //   故在 setup 末尾才启用（WiFi 等待循环里另有喂狗，见 wifiHandleSave）。
  {
    esp_task_wdt_config_t wdtCfg = {
      .timeout_ms = 10000,
      .idle_core_mask = 0,
      .trigger_panic = true,
    };
    esp_task_wdt_init(&wdtCfg);  // 若核心已启用则返回失败，可忽略（已有看门狗在跑）
    esp_task_wdt_add(NULL);      // 当前任务即 loop 任务；若已加入则返回失败，可忽略
  }
}

void loop() {
  unsigned long now = millis();
  esp_task_wdt_reset();  // r56：每轮喂狗，loop 卡死超 10 秒由看门狗重启

  // 1. 首页：联网/断网切换、模式变化时全屏重画；同模式下每秒只刷变化的
  //    行/卡（不全屏）：时钟模式走 updateHomeClockLines，同步中只刷温压卡；
  //    未联网模式的 BMP 由 1c 局部刷卡。r44 整屏重画卡片慢是首页一顿一顿的原因。
  if (state == ST_HOME) {
    static bool lastWifiConn = false;
    bool conn = (WiFi.status() == WL_CONNECTED);
    if (conn != lastWifiConn) { lastWifiConn = conn; screenDirty = true; }
    else if (conn && (now - homeClockMs >= 1000)) {
      homeClockMs = now;
      refreshHomeData();
      if (homeMode != lastHomeModeDrawn) screenDirty = true;
      else if (homeMode == 1 && !screenDirty) updateHomeClockLines();
      else if (homeMode == 2 && !screenDirty) {
        // 同步中：只刷变化的温压卡，不整屏
        if (homeBmpTemp != lastHomeBmpTemp || homeBmpPress != lastHomeBmpPress) {
          drawHomeBmpCards(homeBmpTemp, homeBmpPress);
          lastHomeBmpTemp = homeBmpTemp; lastHomeBmpPress = homeBmpPress;
        }
      }
    }
  }

  // 1b. WiFi 页：连接/热点/SSID 变了就重画（配网成功后 STA 连上、或掉线重连时，
  //     屏上状态自己跟上，不用 KEY4 重进才刷新；与首页的 lastWifiConn 同一思路）
  if (state == ST_WIFI) {
    static bool lastWifiPageConn = false;
    static bool lastWifiPageApOn = false;
    static String lastWifiPageSsid = "";
    static String lastWifiPageIp = "";
    bool conn = (WiFi.status() == WL_CONNECTED);
    String ssid = conn ? WiFi.SSID() : "";
    String ipNow = conn ? WiFi.localIP().toString() : (wifiApOn ? WiFi.softAPIP().toString() : "");
    if (conn != lastWifiPageConn || wifiApOn != lastWifiPageApOn || ssid != lastWifiPageSsid || ipNow != lastWifiPageIp) {
      lastWifiPageConn = conn;
      lastWifiPageApOn = wifiApOn;
      lastWifiPageSsid = ssid;
      lastWifiPageIp = ipNow;
      screenDirty = true;
    }
  }

  // 1c. 传感器：每 1 秒更新一次（BMP085 读一次约 10ms，1 秒分摊可忽略；
  //     芯片温度读数极快，不另起定时器，同 tick 推）。未连接时每次重初始化（支持热插拔）。
  //     首页未联网模式（mode 0）时 BMP 变了只刷底部温压卡，不整屏；
  //     时钟模式走 section 1 的 updateHomeClockLines，同步中走 section 1 的局部刷卡，
  //     这里不再给首页置 screenDirty（r44 整屏重画是首页一顿一顿的原因）。
  if (bmpLastRead == 0 || now - bmpLastRead >= 1000) {
    bmpLastRead = now;
    bmpRead();
    // 家庭温度推送：每 1 秒推一次当前值（无变化阈值）；BMP 温度与芯片温度各推各的。
    // 气压与 BMP 温度同一次读数，推温度时气压值也已同步刷新（屏上见）。
    if (bmpPresent && hsStarted && bmpTempChar) {
      bmpTempChar->setVal(bmpTempTenths / 10.0f);
    }
    if (hsStarted && chipTempChar && chipTempOk) {
      float chipC = 0;
      if (temperature_sensor_get_celsius(chipTempHandle, &chipC) == ESP_OK) {
        chipTempChar->setVal(chipC);
      }
    }
    if (state == ST_HOME && homeMode == 0) {
      String oldT = homeBmpTemp, oldP = homeBmpPress;
      refreshBmpStrings();
      if (homeBmpTemp != oldT || homeBmpPress != oldP) drawHomeBmpCards(homeBmpTemp, homeBmpPress);
    }
  }

  // 1d. 芯片详情页：每 1 秒只刷卡片内容（RAM/ROM/温度是实时值，不清屏不重画标题，免闪）
  if (state == ST_CHIP && (chipLastRefresh == 0 || now - chipLastRefresh >= 1000)) {
    chipLastRefresh = now;
    refreshChipCard();
  }

  // 2. 按键扫描（含 30ms 消抖）
  pollKeys(now);

  // 2b. 熄屏：3 分钟无按键 -> 背光灭（面板内容保留在 GRAM，唤醒即恢复；
  //      唤醒时 pollKeys 会置 screenDirty 整屏重画兜底，显示一定是最新的）
  if (!screenAsleep && (now - lastKeyMs >= SCREEN_SLEEP_MS)) {
    digitalWrite(TFT_BL, BL_OFF);
    screenAsleep = true;
    // 呼吸灯：两路 LED 改走硬件 PWM（LEDC 5kHz 8 位；LED 拉低点亮，占空比取反，
    //   255=常 HIGH=灭，0=常 LOW=最亮），从灭开始淡入，无上电闪烁
    ledcAttach(LED1, 5000, 8);
    ledcAttach(LED2, 5000, 8);
    breathT0 = now;
    lastBreathMs = now;
    ledcWrite(LED1, 255);
    ledcWrite(LED2, 255);
  }

  // 2c. 熄屏呼吸灯：正弦淡入淡出，周期 2s（0s 灭 -> 1s 最亮 -> 2s 灭），两灯同步；
  //      亮度由绝对时间算出（不依赖 loop 速率），20ms 写一次寄存器足够平滑
  if (screenAsleep && (now - lastBreathMs >= 20)) {
    lastBreathMs = now;
    float phase = ((now - breathT0) % 2000) / 2000.0f * 2.0f * PI;
    uint8_t brightness = (uint8_t)((1.0f - cosf(phase)) * 127.5f);
    uint8_t duty = 255 - brightness;  // 拉低点亮：亮度取反才是 LEDC 占空比
    ledcWrite(LED1, duty);
    ledcWrite(LED2, duty);
  }

  // 3. 红外测试：运行中时按 3 秒亮 / 3 秒灭循环发 38kHz 载波
  if (state == ST_IRTEST && irTestRunning) {
    if (now - irTestPhaseMs >= 3000) {
      irTestPhaseMs = now;
      irTestOnPhase = !irTestOnPhase;
      screenDirty = true;
    }
    if (irTestOnPhase) {
      irsend.sendRaw(irTestMark, 2, 38);  // 20ms 载波突发，循环发送即连续载波
    }
  }

  // 4. 红外接收：更新解码显示，串口打印完整解码（含原始数组）
  if (irrecv.decode(&irResults)) {
    irRxProto = typeToString(irResults.decode_type, irResults.repeat);
    irRxHex = hexPad((uint32_t)irResults.value, irResults.bits);
    if (irResults.repeat) irRxHex += "(R)";
    recvBits = irResults.bits;
    recvLen = irResults.rawlen - 1;
    // 底部解码提示（灯/空调子菜单）：显示解码出的键名，查不到显示数值
    lastRecv = lookupRecvName((uint32_t)irResults.value, irRxHex);
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
    // 灯/空调子菜单只刷底部“接收:”行；解码屏多行变化，全屏重画；
    // 首页不再显示接收，菜单/关于/红外测试不显示接收，都不重画
    if (state == ST_LAMP) {
      updateRecvLine(lastRecv);
    } else if (state == ST_AC) {
      updateAcRecvLine(lastRecv);
    } else if (state == ST_DECODE) {
      screenDirty = true;
    }
    // 红外复制学习中：解析到的键码存为待存储捕获（按住遥控器连发的重复码同值，覆盖无害）
    if (state == ST_COPY && copyLearning) {
      copyCapProto = irRxProto;
      copyCapValue = irResults.value;
      copyCapBits = (uint8_t)irResults.bits;
      copyHasCapture = true;
      copyStatus = "已解析 按 KEY1 存储";
      refreshCopyLines();
      screenDirty = true;
    }
  }

  // 4.5 网页服务：总开关开着、且热点开着或家里 WiFi 连上时才处理请求
  if (webEnabled && (wifiApOn || WiFi.status() == WL_CONNECTED)) wifiServer.handleClient();

  // 4.6 HomeSpan：WiFi 一连上就启动（只启动一次），启动后每轮 poll
  if (!hsStarted && WiFi.status() == WL_CONNECTED) {
    setupHomeSpan();
    hsStarted = true;
    // 启动即推一次两个温度的当前值（家庭里不先显示 0）；之后由 1 秒周期推
    if (bmpPresent && bmpTempChar) bmpTempChar->setVal(bmpTempTenths / 10.0f);
    if (chipTempChar && chipTempOk) {
      float chipC = 0;
      if (temperature_sensor_get_celsius(chipTempHandle, &chipC) == ESP_OK) chipTempChar->setVal(chipC);
    }
    Serial.println("HomeKit 已启动（配对码 " + fmtPairCode(curPairCode) + "，配件：灯、空调、温度、芯片温度）");
  }
  if (hsStarted) homeSpan.poll();

  // 4.7 网页/HomeKit 登记的红外：本轮统一执行（与按键同一上下文）
  execPendingIr();

  // 4.8 HomeKit 瞬时按键的弹回：红外发完后再 setVal(0)，不在 update 里做
  if (pendingRevert) {
    pendingRevert->setVal(0);
    pendingRevert = nullptr;
  }

  // 4.9 HomeKit/网页触发的发送：补刷灯/空调页的发送行与空调状态行
  if (hsSentDirty) {
    if (state == ST_LAMP) updateSendLine(lastSent);
    else if (state == ST_AC) updateAcSendLine(lastSent);
    hsSentDirty = false;
  }
  if (hsAcStateDirty) {
    if (state == ST_AC) updateAcStatusLine(buildAcStatus());
    hsAcStateDirty = false;
  }

  // 5. 屏幕刷新
  if (screenDirty) {
    drawScreen();
    screenDirty = false;
  }
}
