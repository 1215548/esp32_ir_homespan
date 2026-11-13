//================================================================
// ESP32S2_HWTest_r61.ino
// CoreBoard ESP32-S2 mini 硬件测试
// r61：setup/loop 重构为 FreeRTOS 多任务（appTask/netTask/sensorTask/lunarTask），
//      UI 与网络/传感器解耦；配网改异步（不再阻塞网页 10 秒）；BMP085 与 I2C 寻址
//      共用总线加互斥；跨任务共享字符串加锁；各业务任务独立喂狗。
//      TEA5767 收音机（I2C 0x60，与 BMP085 共用 SDA33/SCL35）：硬件搜台/手动调谐/
//      频道存 NVS，后台播放时首屏 KEY2/KEY3 切台；另加任务详情页、设备名网页可改。
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
//   本文件：状态机、按键、红外收发、FreeRTOS 任务。
//   codes.h：灯（NEC）与空调（R05D）码表。
//   ui.h：字库混排与全部屏幕绘制（含局部刷新）。
//   cn6.h：16 点阵字库（与 r8/r9 相同）。
//
// ---- 任务框架（FreeRTOS，r61）----
//   appTask   ：菜单状态机、按键扫描、红外收发、TFT 绘制、熄屏/呼吸灯（原 loop 主体）
//   netTask   ：WiFi 状态与重连、WebServer、HomeSpan poll、家庭温度推送、异步配网
//   sensorTask：BMP085 / 芯片温度 1 秒采样（I2C 总线互斥，支持热插拔）
//   lunarTask ：农历 HTTPS（独立 12KB 栈，避免 TLS 栈溢出）
//   跨任务：红外请求队列（网页/HomeKit 登记，appTask 统一执行，与按键同一上下文）、
//           温度队列（sensorTask -> netTask）、共享字符串用 stateMutex 保护，
//           WiFi/Web/HomeSpan 用 wifiMutex 串行化。ESP32-S2 为单核，
//           任务按优先级调度：appTask(3) > netTask(2) > sensorTask(1)。
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
// ---- 开发板设置（r61 实测可用）----
//   开发板选 "ESP32S2 Dev Module"；若串口无输出，把
//   Tools > USB CDC On Boot 设为 Enabled 后重新上传。
//   本板芯片为 ESP32-S2FNR2（内置 4MB Flash + 2MB PSRAM）：
//   - Tools > PSRAM 选 Enabled（r61 起必须；内部分配只够跑 5KB，开 PSRAM 后
//     网页/HomeKit/WiFi 的大块内存走 PSRAM，内部堆留 17KB 给任务和协议栈）
//   - Tools > Partition Scheme 选 "Huge APP (3MB No OTA/1MB SPIFFS)"
//     （固件约 1.9MB，默认 1.2MB APP 分区放不下）
//   上传端口：COM6（本机），原生 USB CDC，Upload Mode 选 Internal USB。
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
#include "esp_heap_caps.h" // 内部分配/PSRAM 余量诊断
#include "esp_system.h"    // 复位原因诊断
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "HomeSpan.h"
#include "cn6.h"

SET_LOOP_TASK_STACK_SIZE(8192);

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

// 红外接收头开关（幂等）：IRremoteESP8266 在 Arduino core 3.x 下每次
// enableIRIn() 都会 timerBegin() 申请一个硬件定时器，S2 只有 4 个定时器；
// 以前每次进灯/空调/解码/复制页都 enableIRIn()，进出 3~4 次定时器耗尽，
// assert(timer != NULL) 直接 panic 重启（r61 实测复现）。用标志位只申请一次。
volatile bool irReceiverOn = false;
void enableIrReceiver() {
  if (!irReceiverOn) {
    irrecv.enableIRIn();
    irReceiverOn = true;
  }
}
void disableIrReceiver() {
  if (irReceiverOn) {
    irrecv.disableIRIn();
    irReceiverOn = false;
  }
}

// ---------------- FreeRTOS 跨任务通信 ----------------
// 单核 S2：所有任务跑在同一个核上，靠优先级 + 阻塞让出 CPU。
SemaphoreHandle_t stateMutex = nullptr;  // 跨任务共享字符串（lastSent/SSID/名称/配对码）
SemaphoreHandle_t wifiMutex  = nullptr;  // WiFi/WebServer/HomeSpan 操作串行化
SemaphoreHandle_t i2cMutex   = nullptr;  // BMP085 采样与 I2C 寻址共用总线
SemaphoreHandle_t irReqMutex = nullptr;  // 红外请求队列的入队/去重
QueueHandle_t     irReqQueue = nullptr;  // 网页/HomeKit -> appTask 待发红外
QueueHandle_t     tempQueue  = nullptr;  // sensorTask -> netTask 温度推送
QueueHandle_t     hsEvtQueue = nullptr;  // appTask -> netTask HomeKit 弹回
QueueHandle_t     keyCmdQueue = nullptr; // 网页注入按键（/key?i=3）-> appTask
TaskHandle_t      appTaskHandle    = nullptr;  // 诊断用（栈水位）
TaskHandle_t      netTaskHandle    = nullptr;
TaskHandle_t      sensorTaskHandle = nullptr;
StaticTask_t      netTaskBuf, sensorTaskBuf, lunarTaskBuf;  // 静态 TCB（栈在 PSRAM）
uint32_t dbgMemEntry = 0, dbgMemSync = 0, dbgMemTft = 0;  // 启动分段内存诊断
volatile bool pairCodeDirty = false;     // 网页改了配对码，待 appTask 刷屏
volatile bool copyNameDirty = false;     // 网页改了复制编号名，待 appTask 刷屏
volatile bool sensorUpdated = false;     // sensorTask 更新了测量值
volatile float chipTempC = 0.0f;         // 芯片内核温度（sensorTask 读，appTask 显示）
volatile bool chipTempReadOk = false;    // 芯片温度至少读到过一次

struct TempUpdate {  // sensorTask 每秒产出一帧，netTask 推给 HomeKit
  bool  bmpValid;
  float bmpC;
  bool  chipValid;
  float chipC;
};
enum HsEvent : uint8_t { HS_EVT_IR_SENT = 1 };

const uint8_t keyPins[4] = {KEY1, KEY2, KEY3, KEY4};
const char *keyNames[4]  = {"KEY1", "KEY2", "KEY3", "KEY4"};

#include "codes.h"
#include "ui.h"

// ---------------- 菜单状态 ----------------
enum MenuState { ST_HOME, ST_MENU, ST_LAMP, ST_AC, ST_DECODE, ST_IRTEST, ST_WIFI, ST_TASKS, ST_RADIO, ST_ABOUT, ST_I2C, ST_COPY, ST_HS, ST_DISP, ST_CHIP };
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
volatile bool screenDirty = true;  // 由 appTask 消费；其它任务只置位
// 熄屏（r46）：3 分钟无按键 -> 背光灭；任意按键唤醒（唤醒那一下只唤醒，不执行按键动作）
bool     screenAsleep = false;
volatile bool forceSleep = false;  // 调试：网页 /sleep 强制进入熄屏，测唤醒路径
unsigned long lastKeyMs = 0;
const unsigned long SCREEN_SLEEP_MS = 180000;  // 3 分钟
unsigned long breathT0 = 0;      // 呼吸灯周期起点（熄屏时刻，周期 2s 从灭开始淡入）
unsigned long lastBreathMs = 0;  // 呼吸灯上次写占空比（20ms 节流，写寄存器够用）

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

// ---------------- 跨任务共享字符串 ----------------
// lastSent 由 appTask 写、网页（netTask）读，必须加锁（String 会重新分配内存）。
void setLastSent(const String &s) {
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    lastSent = s;
    xSemaphoreGive(stateMutex);
  } else {
    lastSent = s;
  }
}
String getLastSent() {
  String s;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    s = lastSent;
    xSemaphoreGive(stateMutex);
  } else {
    s = lastSent;
  }
  return s;
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
  setLastSent(LAMP_CODES[idx].name);
  updateSendLine(getLastSent());
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
  setLastSent(AC_CODES[idx].name);
  updateAcSendLine(getLastSent());
}

// 发送空调功能码（固定码，R05D 构造发送）
void sendAcFunc(uint8_t idx) {
  // r58：撤销 r57 加的开关接收，发送时不关接收头，回波显示在接收行（与灯/空调其它发送一致）
  sendR05D(ACFUNC_CODES[idx].a, ACFUNC_CODES[idx].b, ACFUNC_CODES[idx].c);
  setLastSent(ACFUNC_CODES[idx].name);
  updateAcSendLine(getLastSent());  // 只刷底部卡片内“发送:”行，不全屏
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
  setLastSent(action);
  updateAcSendLine(getLastSent());
}

// ---------------- 统一动作（TFT / HomeKit / 网页三路共用）----------------
// 只改状态 + 发红外 + 串口 + lastSent，不碰屏幕（屏幕由 appTask 调用方刷新）。
bool lightIsOn = false;  // 灯跟踪状态（开/关共用一个拨动码，靠它区分）
// 灯码下标（codes.h 顺序：0 夜灯，1 开/关）
const uint8_t LAMP_NIGHT_IDX = 0;
const uint8_t LAMP_POWER_IDX = 1;

void actLampNight() { sendLampRaw(LAMP_NIGHT_IDX); setLastSent(LAMP_CODES[LAMP_NIGHT_IDX].name); }
void actLampOn()  { if (!lightIsOn) { sendLampRaw(LAMP_POWER_IDX); lightIsOn = true; } setLastSent("开"); }
void actLampOff() { if (lightIsOn) { sendLampRaw(LAMP_POWER_IDX); lightIsOn = false; } setLastSent("关"); }
void actAcOn()    { acTemp = 26; acFan = 4; acMode = 0; sendAcRaw(0); setLastSent(AC_CODES[0].name); }
void actAcOff()   { sendAcRaw(1); setLastSent(AC_CODES[1].name); }
void actAcTempUp()   { if (acTemp < 30) acTemp++; sendAcStateRaw(); setLastSent("温度+"); }
void actAcTempDown() { if (acTemp > 17) acTemp--; sendAcStateRaw(); setLastSent("温度-"); }
void actAcMode()     { acMode = (acMode + 1) % AC_MODE_COUNT; if (acMode == 0 || acMode == 2) acFan = 4; sendAcStateRaw(); setLastSent("模式循环"); }
void actAcFanUp()    { acFan = (acFan + 1) % AC_FAN_COUNT; sendAcStateRaw(); setLastSent("风速+"); }
void actAcFanDown()  { acFan = (acFan + AC_FAN_COUNT - 1) % AC_FAN_COUNT; sendAcStateRaw(); setLastSent("风速-"); }

// 网页/HomeKit 的红外登记制（FreeRTOS 队列版）：回调里只入队、不直接发红外，
//   appTask 在统一位置执行（与按键同一上下文）。队列由 irReqMutex 保护：
//   - 去重：同一动作已在队里（还没发）就合并丢弃（家庭 App 一次点按会重复写 HAP）。
//   - 延后 500ms 发送，等网络安静；隔开点按仍各发各的。
enum PendingIr : uint8_t {
  PENDING_NONE, PENDING_LAMP_NIGHT, PENDING_LAMP_ON, PENDING_LAMP_OFF,
  PENDING_AC_ON, PENDING_AC_OFF, PENDING_AC_TEMP_UP, PENDING_AC_TEMP_DOWN,
  PENDING_AC_MODE, PENDING_AC_FAN_UP, PENDING_AC_FAN_DOWN
};
SpanCharacteristic *pendingRevert = nullptr;  // 待弹回的瞬时按键特征（netTask 里 setVal）
#define PENDING_QUEUE_LEN 8
#define PENDING_SEND_DELAY_MS 500
struct PendingItem { uint8_t action; bool stateDirty; unsigned long at; };
uint16_t pendingMask = 0;  // 已在队里的动作位图（去重）
const char *pendingName(uint8_t a) {  // 排队日志用可读动作名
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
void requestIr(uint8_t p, bool stateDirty) {  // 可由网页/HomeKit 任务调用
  if (!irReqQueue || !irReqMutex) return;
  if (xSemaphoreTake(irReqMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  if (pendingMask & (1u << p)) {
    xSemaphoreGive(irReqMutex);
    Serial.print("红外请求已合并（同动作还在队里）：");
    Serial.println(pendingName(p));
    return;
  }
  if (uxQueueSpacesAvailable(irReqQueue) == 0) {
    xSemaphoreGive(irReqMutex);
    Serial.println("待发队列已满，丢弃本次红外请求");
    return;
  }
  PendingItem it = {p, stateDirty, millis()};
  if (xQueueSend(irReqQueue, &it, 0) == pdPASS) {
    pendingMask |= (1u << p);
    Serial.print("红外请求已排队：");
    Serial.print(pendingName(p));
    Serial.print("（队里 ");
    Serial.print(uxQueueMessagesWaiting(irReqQueue));
    Serial.println(" 条）");
  }
  xSemaphoreGive(irReqMutex);
}
// appTask 每轮调用：队首满 500ms 就取出执行，并同步刷新当前页的行。
void execPendingIr() {
  if (!irReqQueue || !irReqMutex) return;
  if (xSemaphoreTake(irReqMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  PendingItem it;
  if (xQueuePeek(irReqQueue, &it, 0) != pdPASS) { xSemaphoreGive(irReqMutex); return; }
  if (millis() - it.at < PENDING_SEND_DELAY_MS) { xSemaphoreGive(irReqMutex); return; }
  xQueueReceive(irReqQueue, &it, 0);
  pendingMask &= ~(1u << it.action);
  xSemaphoreGive(irReqMutex);
  switch (it.action) {
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
  // 网页/HomeKit 触发的发送：当前在灯/空调页就同步刷行（appTask 持有屏幕）
  if (state == ST_LAMP) {
    updateSendLine(getLastSent());
  } else if (state == ST_AC) {
    updateAcSendLine(getLastSent());
    if (it.stateDirty) updateAcStatusLine(buildAcStatus());
  }
  // 通知 netTask 弹回 HomeKit 瞬时按键（setVal 必须回 poll 所在任务做）
  uint8_t evt = HS_EVT_IR_SENT;
  if (hsEvtQueue) xQueueSend(hsEvtQueue, &evt, 0);
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
  String slotName;  // 名称可能被网页任务改写，取快照
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    slotName = copyName[copySel];
    xSemaphoreGive(stateMutex);
  } else {
    slotName = copyName[copySel];
  }
  copySlotLine = String("编号 ") + copySel;
  if (slotName.length()) copySlotLine += " " + slotName;
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
  disableIrReceiver();
  if (proto == "NEC") {
    irsend.sendNEC((uint32_t)v, copyBits[slot]);
  } else if (proto == "COOLIX") {
    uint32_t code = (uint32_t)(v & 0xFFFFFF);
    sendR05D((uint8_t)(code >> 16), (uint8_t)(code >> 8), (uint8_t)code);
  } else {
    enableIrReceiver();
    return false;
  }
  enableIrReceiver();
  return true;
}

// ---------------- WiFi 网页配置 ----------------
WebServer wifiServer(8080);  // 不用 80：HomeSpan 的 HomeKit 服务（HAP）占着 80
Preferences wifiPrefs;
volatile bool wifiApOn = false;
volatile bool webEnabled = true;  // 网页总开关（RAM，重启默认开；WiFi 页 KEY1 切换）
String wifiSavedSsid = "";
String wifiSavedPass = "";
// 异步配网状态（wifiHandleSave 只发起连接，netTask 负责等结果/关热点）
volatile bool wifiConnecting = false;
volatile unsigned long wifiConnectStart = 0;

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
String gDeviceName = "ESP32-S2-IR";  // 首屏名称（NVS cfg/name，网页可改，掉电保存）
volatile bool devNameDirty = false;  // 网页改了设备名，待 appTask 刷首屏
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

// 首屏设备名（NVS 命名空间 cfg，键 name；默认 ESP32-S2-IR）
void loadDeviceName() {
  Preferences prefs;
  prefs.begin("cfg", true);
  String saved = prefs.getString("name", "");
  prefs.end();
  saved.trim();
  if (saved.length()) gDeviceName = saved;
}
void saveDeviceName(const String &nm) {
  Preferences prefs;
  prefs.begin("cfg", false);
  prefs.putString("name", nm);
  prefs.end();
}

// 跨任务共享字符串访问（wifiSavedSsid/copyName/curPairCode 由 netTask 写、appTask 读）
void setWifiSavedCreds(const String &ssid, const String &pass) {
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    wifiSavedSsid = ssid;
    wifiSavedPass = pass;
    xSemaphoreGive(stateMutex);
  } else {
    wifiSavedSsid = ssid;
    wifiSavedPass = pass;
  }
}
void getWifiSavedCreds(String &ssid, String &pass) {
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    ssid = wifiSavedSsid;
    pass = wifiSavedPass;
    xSemaphoreGive(stateMutex);
  } else {
    ssid = wifiSavedSsid;
    pass = wifiSavedPass;
  }
}
String getWifiSavedSsid() {
  String s, p;
  getWifiSavedCreds(s, p);
  return s;
}
String getNameCopy(uint8_t slot) {
  String s;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    s = copyName[slot];
    xSemaphoreGive(stateMutex);
  } else {
    s = copyName[slot];
  }
  return s;
}
void setNameCopy(uint8_t slot, const String &nm) {
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    copyName[slot] = nm;
    xSemaphoreGive(stateMutex);
  } else {
    copyName[slot] = nm;
  }
}
String getPairCode() {
  String s;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    s = curPairCode;
    xSemaphoreGive(stateMutex);
  } else {
    s = curPairCode;
  }
  return s;
}
String getDeviceName() {
  String s;
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    s = gDeviceName;
    xSemaphoreGive(stateMutex);
  } else {
    s = gDeviceName;
  }
  return s;
}
void setDeviceName(const String &nm) {
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    gDeviceName = nm;
    xSemaphoreGive(stateMutex);
  } else {
    gDeviceName = nm;
  }
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
    "<p>上次发送：" + htmlEscape(getLastSent()) + "</p></div>"
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
    "<label>WiFi 名称（SSID）</label><input name=\"ssid\" value=\"" + htmlEscape(getWifiSavedSsid()) + "\" required>"
    "<label>密码</label><input type=\"password\" name=\"pass\" required>"
    "<button type=\"submit\">保存并连接</button></form>"
    "<p class=\"note\">保存后设备会尝试连接，成功后配置热点自动关闭。</p></div>";
  h += "<div class=\"card\"><h3>红外复制编号命名</h3><form method=\"post\" action=\"/ircopy_name\">";
  for (uint8_t s = 1; s <= 10; s++) {
    h += "<label>编号 " + String(s) + "</label><input name=\"n" + String(s) + "\" maxlength=\"8\" value=\"" + htmlEscape(getNameCopy(s)) + "\">";
  }
  h += "<button type=\"submit\">保存名称</button></form>"
    "<p class=\"note\">名称显示在设备的红外复制页（编号旁），不影响已存键码。</p></div>";
  h += "<div class=\"card\"><h3>首屏设备名称</h3><form method=\"post\" action=\"/devname\">"
    "<label>显示在屏幕左上角（最长 16 字符）</label><input name=\"name\" maxlength=\"16\" value=\"" + htmlEscape(getDeviceName()) + "\" required>"
    "<button type=\"submit\">保存名称</button></form>"
    "<p class=\"note\">立即生效并写入 NVS，掉电不丢；只改首屏标题，不改 HomeKit 名称。</p></div>";
  h += "<div class=\"card\"><h3>HomeSpan 配对码</h3><form method=\"post\" action=\"/paircode\">"
    "<label>配对码（8 位数字）</label><input name=\"paircode\" maxlength=\"8\" value=\"" + htmlEscape(getPairCode()) + "\">"
    "<button type=\"submit\">保存配对码</button></form>"
    "<p class=\"note\">改码只影响下次重新添加配件（已配对的不受影响）；重启后确保生效。</p></div></body></html>";
  return h;
}

// 隐藏调试接口：模拟按 KEY1~KEY4（i=0..3），用于远程/自动化测试菜单路径。
// 只入队，真正执行仍在 appTask（与物理按键同一上下文）。
void wifiHandleKey() {
  int i = wifiServer.arg("i").toInt();
  if (i >= 0 && i < 4 && keyCmdQueue) {
    uint8_t k = (uint8_t)i;
    xQueueSend(keyCmdQueue, &k, 0);
    wifiServer.send(200, "text/plain", "ok");
  } else {
    wifiServer.send(400, "text/plain", "bad");
  }
}

// 调试：强制熄屏（验证 3 分钟熄屏/按键唤醒路径，不用真等 3 分钟）
void wifiHandleSleep() {
  forceSleep = true;
  wifiServer.send(200, "text/plain", "sleeping");
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

// WiFi 保存改为异步（r61）：以前在请求里 while 等 10 秒，期间整个网页/HomeSpan
//   都停摆；现在只保存 + 发起连接 + 立即返回“连接中”页（6 秒后自动回首页），
//   连接结果由 netTask 监视：成功则关热点并刷屏，失败则保留热点可重试。
void wifiHandleSave() {
  String ssid = wifiServer.arg("ssid");
  String pass = wifiServer.arg("pass");
  ssid.trim();  // SSID 两端空格通常是误输入，去掉
  setWifiSavedCreds(ssid, pass);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  WiFi.begin(ssid, pass);
  wifiConnecting = true;
  wifiConnectStart = millis();
  String body = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta http-equiv=\"refresh\" content=\"6;url=/\"></head><body>"
    "<h2>已保存，正在连接</h2><p>网络：" + htmlEscape(ssid) + "</p>"
    "<p>连接成功后配置热点会自动关闭；6 秒后自动返回首页查看状态。</p>"
    "<p><a href=\"/\">立即返回</a></p></body></html>";
  wifiServer.send(200, "text/html", body);
  Serial.println("WiFi: 已保存 " + ssid + "，后台连接中");
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
    setNameCopy(s, nm);  // 加锁写内存；屏由 appTask 收到 copyNameDirty 后刷新
  }
  prefs.end();
  copyNameDirty = true;
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(302, "text/plain", "");
}

// 首屏设备名称保存（POST /devname）：trim + UTF-8 安全截断到 32 字节，
// 写 NVS 与内存，首屏在 appTask 收到 devNameDirty 后立即重画。
void wifiHandleDevName() {
  String nm = wifiServer.arg("name");
  nm.trim();
  if (!nm.length()) nm = "ESP32-S2-IR";
  if (nm.length() > 32) {
    int cut = 32;
    while (cut > 0 && ((uint8_t)nm[cut] & 0xC0) == 0x80) cut--;  // 不截断 UTF-8 中间
    nm = nm.substring(0, cut);
  }
  saveDeviceName(nm);
  setDeviceName(nm);
  devNameDirty = true;
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
  if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    curPairCode = code;
    xSemaphoreGive(stateMutex);
  } else {
    curPairCode = code;
  }
  homeSpan.setPairingCode(code.c_str());  // 与 poll 同任务（netTask），无并发
  pairCodeDirty = true;  // appTask 在 HomeSpan 页时刷屏
  wifiServer.sendHeader("Location", "/");
  wifiServer.send(302, "text/plain", "");
}

// 网页监听状态：以前只在开热点时才 begin()，家里 WiFi 连上后 8080 实际没监听；
//   r61 改为开机就起（STA 模式也能用网页），启停都记状态避免重复 begin。
volatile bool webServerStarted = false;
void ensureWebServer() {
  if (webEnabled && !webServerStarted) {
    wifiServer.begin();
    webServerStarted = true;
  }
}

// WiFi/网页操作加锁：appTask（按键开关热点/网页）与 netTask（WebServer/HomeSpan）
//   可能并发，softAP/server 的启停必须串行，否则会把监听套接字拆在半路。
void wifiStartAp() {
  if (wifiMutex) xSemaphoreTake(wifiMutex, portMAX_DELAY);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP32S2-Setup");
  ensureWebServer();  // 网页关着时不开监听，免得偷偷重起服务
  wifiApOn = true;
  if (wifiMutex) xSemaphoreGive(wifiMutex);
  Serial.println("WiFi 配置热点已开启：ESP32S2-Setup，网页（控制+WiFi配置同页） " + WiFi.softAPIP().toString() + ":8080");
}

void wifiStopAp() {
  if (wifiMutex) xSemaphoreTake(wifiMutex, portMAX_DELAY);
  wifiServer.close();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiApOn = false;
  if (wifiMutex) xSemaphoreGive(wifiMutex);
  Serial.println("WiFi 配置热点已关闭");
}

// 网页总开关（WiFi 页 KEY1 调用）：只翻布尔不够，必须把服务彻底停/起，
//   否则浏览器悬着的连接会攒下来拖慢主循环。路由注册不受 stop/begin 影响。
void setWebEnabled(bool on) {
  if (on == webEnabled) return;
  if (wifiMutex) xSemaphoreTake(wifiMutex, portMAX_DELAY);
  webEnabled = on;
  if (on) {
    ensureWebServer();  // 重起监听（关掉期间的重试已被拒，不会攒半截连接）
    Serial.println("网页服务已开启");
  } else {
    wifiServer.close();  // 先断当前客户端
    wifiServer.stop();   // 再停监听
    webServerStarted = false;
    Serial.println("网页服务已关闭（重启后默认恢复开启）");
  }
  if (wifiMutex) xSemaphoreGive(wifiMutex);
  screenDirty = true;
}

// WiFi 页显示（状态/网络/IP/两行提示，由当前 WiFi 状态拼出）
void showWifiScreen() {
  String status, ssid, ip, tip1, tip2;
  String savedSsid = getWifiSavedSsid();  // 网页任务可能改写，取快照
  if (wifiApOn) {
    status = "状态: 配置热点已开启";
    ssid = "网络: " + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : (savedSsid.length() ? savedSsid : "无"));
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
    ssid = "网络: " + (savedSsid.length() ? savedSsid : "无");
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
  String pair = getPairCode();
  String ssid, pass;
  getWifiSavedCreds(ssid, pass);
  homeSpan.setPairingCode(pair.c_str());
  homeSpan.setQRID("HOMESPAN");
  homeSpan.setWifiCredentials(ssid.c_str(), pass.c_str());
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

// 农历异步拉取（修复上电后无操作随机重启）：
//   TLS 握手要吃掉数 KB 栈，在 loop 任务（Arduino 默认栈仅 8KB）里直接跑
//   WiFiClientSecure 容易栈溢出重启；同时 DNS/TLS 阻塞时主循环停摆，
//   Arduino 默认任务看门狗是 5 秒且 panic=true，会被直接复位。
//   这里改到独立任务（12KB 栈）里取，loop 只发通知、取结果，不再被网络阻塞。
#define LUNAR_TASK_STACK 12288
static TaskHandle_t     lunarTaskHandle  = NULL;
static SemaphoreHandle_t lunarMutex      = NULL;
static volatile bool    lunarFetching    = false;  // 任务正在取（loop 不重复下发）
static volatile bool    lunarResultReady = false;
static char lunarResMonth[32] = "";
static char lunarResDay[32]   = "";
static char lunarResDate[16]  = "";  // 取到结果时的公历日期 "YYYY-M-D"

static void lunarFetchTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // 等 loop 通知
    String mName, dName;
    if (fetchLunar(mName, dName)) {
      time_t nowSec = time(nullptr);
      struct tm tmNow;
      localtime_r(&nowSec, &tmNow);
      if (xSemaphoreTake(lunarMutex, portMAX_DELAY) == pdTRUE) {
        snprintf(lunarResMonth, sizeof(lunarResMonth), "%s", mName.c_str());
        snprintf(lunarResDay, sizeof(lunarResDay), "%s", dName.c_str());
        snprintf(lunarResDate, sizeof(lunarResDate), "%d-%d-%d",
                 tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
        lunarResultReady = true;
        xSemaphoreGive(lunarMutex);
      }
    }
    lunarFetching = false;
  }
}

// loop 调用：先并入任务取回的结果，再按需下发新的拉取请求（60 秒节流沿用原逻辑；
//   任务没建起来时农历不显示，不影响其它功能）。
static void pollLunarAsync(const String &todayStr) {
  if (lunarMutex && xSemaphoreTake(lunarMutex, 0) == pdTRUE) {
    if (lunarResultReady) {
      homeLunar = String(lunarResMonth) + lunarResDay;
      lastLunarDate = lunarResDate;
      lunarResultReady = false;
    }
    xSemaphoreGive(lunarMutex);
  }
  if (!lunarTaskHandle || lunarFetching) return;
  if (homeLunar.length() != 0 && todayStr == lastLunarDate) return;
  unsigned long nowMs = millis();
  if (lastLunarTryMs != 0 && nowMs - lastLunarTryMs < 60000) return;
  lastLunarTryMs = nowMs;
  lunarFetching = true;
  xTaskNotifyGive(lunarTaskHandle);
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
  // 当天农历（API，date=today）：首次、隔日、或上次失败 60s 后重试；失败保留旧值。
  //   拉取走独立任务（见 pollLunarAsync），loop 不再直接跑 HTTPS，
  //   避免 TLS 栈溢出 / 网络阻塞触发看门狗复位。
  String todayStr = String(tm.tm_year + 1900) + "-" + String(tm.tm_mon + 1) + "-" + String(tm.tm_mday);
  pollLunarAsync(todayStr);
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
  // 与 sensorTask 的 BMP085 采样共用总线，必须互斥（否则扫描期间采样会插进来）
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  i2cFoundCount = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0 && i2cFoundCount < I2C_FOUND_MAX) {
      i2cFound[i2cFoundCount++] = addr;
    }
  }
  if (i2cMutex) xSemaphoreGive(i2cMutex);
  i2cScanned = true;
  screenDirty = true;
}

// ---------------- 屏幕刷新 ----------------
// 芯片详情（r55）：RAM=堆（ESP.getHeapSize/getFreeHeap），ROM=Flash（程序大小/Flash 容量），
//   温度用 sensorTask 每秒采到的值（TSENS 只在 sensorTask 里读，避免并发访问驱动）
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
  if (chipTempOk && chipTempReadOk) {
    temp = "芯片温度 " + String(chipTempC, 1) + "度";
  } else {
    temp = "芯片温度 --";
  }
}
// 上次复位原因（芯片详情页显示，r61 诊断用）：能区分上电/软复位/Panic/看门狗/欠压
String gResetReason = "";
String fmtResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "复位 上电";
    case ESP_RST_EXT:       return "复位 外部";
    case ESP_RST_SW:        return "复位 软复位";
    case ESP_RST_PANIC:     return "复位 Panic";
    case ESP_RST_INT_WDT:   return "复位 中断狗";
    case ESP_RST_TASK_WDT:  return "复位 任务狗";
    case ESP_RST_WDT:       return "复位 看门狗";
    case ESP_RST_DEEPSLEEP: return "复位 深睡";
    case ESP_RST_BROWNOUT:  return "复位 欠压";
    case ESP_RST_SDIO:      return "复位 SDIO";
    default:                return "复位 未知";
  }
}

void refreshChipCard() {  // 芯片页 1 秒刷新：只刷卡片（值是实时读的），不清屏
  String chip, ram, ramPct, rom, romPct, temp;
  chipStrings(chip, ram, ramPct, rom, romPct, temp);
  drawChipCard(chip, ram, ramPct, rom, romPct, temp, gResetReason);
}

// ---------------- 任务详情（FreeRTOS 实时状态）----------------
#define TASKS_MAX 24
TaskViewRow taskRows[TASKS_MAX];
uint8_t taskRowCount = 0;
uint8_t tasksTop = 0;
uint8_t tasksCpu = 0;
uint32_t tasksHeapK = 0;
struct PrevRunTip { char name[12]; uint32_t counter; };
PrevRunTip prevRuns[TASKS_MAX];
uint8_t prevRunCount = 0;
uint64_t prevRunUs = 0;

static const char *taskStateText(eTaskState st) {
  switch (st) {
    case eRunning:   return "运";
    case eReady:     return "就";
    case eBlocked:   return "阻";
    case eSuspended: return "挂";
    case eDeleted:   return "删";
    default:         return "?";
  }
}
static uint16_t taskStateColor(eTaskState st) {
  switch (st) {
    case eRunning:   return UI_GREEN;
    case eReady:     return UI_ACCENT;
    case eBlocked:   return UI_MUTED;
    case eSuspended: return UI_AMBER;
    case eDeleted:   return ST77XX_RED;
    default:         return UI_MUTED;
  }
}

// 采集一次任务状态：状态/优先级/栈余/CPU（运行时间统计已开启，esp_timer 微秒）。
// CPU% = 本秒任务运行时间增量 / 本秒真实经过时间；总 CPU = 100 - IDLE 占用。
void refreshTaskRows() {
  static TaskStatus_t st[TASKS_MAX];
  configRUN_TIME_COUNTER_TYPE totalRt = 0;
  uint64_t nowUs = esp_timer_get_time();
  UBaseType_t n = uxTaskGetSystemState(st, TASKS_MAX, &totalRt);
  if (n > TASKS_MAX) n = TASKS_MAX;
  bool hadPrev = (prevRunUs != 0);
  uint64_t elapsed = (hadPrev && nowUs > prevRunUs) ? (uint64_t)(nowUs - prevRunUs) : 0;
  uint8_t idleCpu = 0;
  bool idleSeen = false;
  for (UBaseType_t i = 0; i < n; i++) {
    TaskViewRow &r = taskRows[i];
    snprintf(r.name, sizeof(r.name), "%.10s", st[i].pcTaskName ? st[i].pcTaskName : "?");
    r.state = taskStateText(st[i].eCurrentState);
    r.stateColor = taskStateColor(st[i].eCurrentState);
    r.prio = (uint8_t)st[i].uxCurrentPriority;
    uint32_t hwm = uxTaskGetStackHighWaterMark(st[i].xHandle);  // 字节
    r.stackK = (uint16_t)(hwm / 1024);
    if (elapsed) {
      uint32_t cur = (uint32_t)st[i].ulRunTimeCounter;
      uint32_t delta = 0;
      for (uint8_t p = 0; p < prevRunCount; p++) {
        if (strncmp(prevRuns[p].name, r.name, sizeof(r.name)) == 0) {
          delta = cur - prevRuns[p].counter;  // U32 微秒，自然回绕
          break;
        }
      }
      uint32_t pct = (uint32_t)((uint64_t)delta * 100 / elapsed);
      if (pct > 100) pct = 100;
      if (strncmp(r.name, "IDLE", 4) == 0) { idleCpu = (uint8_t)pct; idleSeen = true; }
    }
  }
  // 按优先级从高到低排序，便于查看
  for (UBaseType_t a = 0; a < n; a++) {
    for (UBaseType_t b = a + 1; b < n; b++) {
      if (taskRows[b].prio > taskRows[a].prio) {
        TaskViewRow t = taskRows[a]; taskRows[a] = taskRows[b]; taskRows[b] = t;
      }
    }
  }
  // 保存本帧运行时间，下一帧算 CPU
  for (UBaseType_t i = 0; i < n; i++) {
    snprintf(prevRuns[i].name, sizeof(prevRuns[i].name), "%.10s", st[i].pcTaskName ? st[i].pcTaskName : "?");
    prevRuns[i].counter = (uint32_t)st[i].ulRunTimeCounter;
  }
  prevRunCount = (uint8_t)n;
  prevRunUs = nowUs;
  taskRowCount = (uint8_t)n;
  tasksCpu = (hadPrev && idleSeen) ? (uint8_t)(100 - idleCpu) : 0;
  tasksHeapK = ESP.getFreeHeap() / 1024;
  uint8_t maxTop = (taskRowCount > TASK_WIN_ROWS) ? (taskRowCount - TASK_WIN_ROWS) : 0;
  if (tasksTop > maxTop) tasksTop = maxTop;
}

// ---------------- TEA5767 收音机 ----------------
#define RADIO_ADDR        0x60
#define RADIO_MIN_KHZ     87500
#define RADIO_MAX_KHZ     108000
#define RADIO_MANUAL_STEP 50     // 手动细调 0.05MHz
#define RADIO_SEARCH_POLL_MS     30    // 硬件 Search 状态轮询
#define RADIO_SEARCH_TIMEOUT_MS  4000  // 单次搜索超时
#define RADIO_VERIFY_WAIT_MS     120   // 搜到后正常调谐复核等待
#define RADIO_SEARCH_STEP_KHZ    200   // 下一次搜索从当前台 +0.2MHz 开始
#define RADIO_SIGNAL_MIN  8            // ADC 信号阈值 0~15（高门限，滤掉雪花台）
#define RADIO_MAX_STATIONS 30

uint32_t radioStations[RADIO_MAX_STATIONS];
uint8_t radioStationCount = 0;
int16_t radioStationIdx = -1;      // 当前在保存列表中的下标，-1=手动频率
uint32_t radioFreqKHz = 98000;
bool radioMuted = true;
bool radioBackground = false;      // 后台播放（在首屏显示频率、KEY2/3 切台）
bool radioPresent = false;
bool radioStereo = false;
uint8_t radioSignal = 0;
uint8_t radioSel = 0;              // 收音机功能菜单选择
uint8_t radioTop = 0;
bool radioScanning = false;
uint32_t radioScanFreq = RADIO_MIN_KHZ;
unsigned long radioScanWaitMs = 0;
uint32_t radioScanFound[RADIO_MAX_STATIONS];
uint8_t radioScanFoundCount = 0;
unsigned long radioDrawMs = 0;
enum RadioScanPhase : uint8_t { RSCAN_IDLE, RSCAN_SEARCH, RSCAN_VERIFY };
RadioScanPhase radioScanPhase = RSCAN_IDLE;
uint32_t radioSearchStartKHz = RADIO_MIN_KHZ;
uint32_t radioScanCandidate = 0;

bool radioI2cTake() {
  return !i2cMutex || xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(200)) == pdTRUE;
}
void radioI2cGive() {
  if (i2cMutex) xSemaphoreGive(i2cMutex);
}

// 读 5 字节状态：b[0]&0x3F/b[1]=PLL，b[2] bit7=立体声，b[3] 高 4 位=信号强度
bool radioReadStatus(uint8_t *b) {
  if (!radioI2cTake()) return false;
  bool ok = false;
  if (Wire.requestFrom(RADIO_ADDR, (uint8_t)5) == 5) {
    for (uint8_t i = 0; i < 5; i++) b[i] = Wire.read();
    ok = true;
  }
  radioI2cGive();
  return ok;
}

// 写 5 字节：PLL14 位 + 静音位；0x11=PORT1高+高边注入，0x1E=32.768k晶振+软静音+高切+立体声降噪
bool radioWritePll(uint32_t freqKHz, bool muted) {
  if (!radioI2cTake()) return false;
  uint32_t pll = ((uint32_t)(freqKHz + 225) * 4000) / 32768;
  uint8_t b[5];
  b[0] = (muted ? 0x80 : 0x00) | ((pll >> 8) & 0x3F);
  b[1] = pll & 0xFF;
  b[2] = 0x11;
  b[3] = 0x1E;
  b[4] = 0x00;
  Wire.beginTransmission(RADIO_ADDR);
  for (uint8_t i = 0; i < 5; i++) Wire.write(b[i]);
  bool ok = (Wire.endTransmission() == 0);
  radioI2cGive();
  return ok;
}

bool radioProbe() {
  if (!radioI2cTake()) return false;
  Wire.beginTransmission(RADIO_ADDR);
  bool ok = (Wire.endTransmission() == 0);
  radioI2cGive();
  return ok;
}

// 读一次信号/立体声/实际频率（非搜台时同步当前频率）
bool radioPollStatus() {
  uint8_t b[5];
  if (!radioReadStatus(b)) return false;
  radioSignal = (b[3] & 0xF0) >> 4;
  radioStereo = (b[2] & 0x80) != 0;
  if (!radioScanning) {
    uint16_t pll = ((uint16_t)(b[0] & 0x3F) << 8) | b[1];
    uint32_t fHz = (uint32_t)pll * 32768UL / 4;
    if (fHz >= 225000) {
      uint32_t khz = (fHz - 225000) / 1000;
      if (khz >= RADIO_MIN_KHZ && khz <= RADIO_MAX_KHZ) {
        radioFreqKHz = (uint32_t)(((khz + RADIO_MANUAL_STEP / 2) / RADIO_MANUAL_STEP) * RADIO_MANUAL_STEP);
      }
    }
  }
  return true;
}

int16_t radioStationIndexAt(uint32_t f) {
  for (uint8_t i = 0; i < radioStationCount; i++) {
    int32_t d = (int32_t)f - (int32_t)radioStations[i];
    if (d < 0) d = -d;
    if (d <= 25) return i;
  }
  return -1;
}

void radioSetFrequency(uint32_t f, bool updateIdx) {
  if (f < RADIO_MIN_KHZ) f = RADIO_MIN_KHZ;
  if (f > RADIO_MAX_KHZ) f = RADIO_MAX_KHZ;
  radioFreqKHz = (uint32_t)(((f + RADIO_MANUAL_STEP / 2) / RADIO_MANUAL_STEP) * RADIO_MANUAL_STEP);
  radioWritePll(radioFreqKHz, radioMuted);
  if (updateIdx) radioStationIdx = radioStationIndexAt(radioFreqKHz);
}

void radioSelectStation(int8_t dir) {
  if (radioStationCount == 0) return;
  if (radioStationIdx < 0) radioStationIdx = (dir >= 0) ? 0 : (int16_t)radioStationCount - 1;
  else radioStationIdx = (radioStationIdx + dir + radioStationCount) % radioStationCount;
  radioSetFrequency(radioStations[radioStationIdx], false);
  radioStationIdx = (radioStationIdx + radioStationCount) % radioStationCount;
  Serial.printf("[RADIO] station %d/%u %u.%02uMHz mute=%d\n",
                (int)radioStationIdx + 1, radioStationCount,
                radioFreqKHz / 1000, (radioFreqKHz % 1000) / 10, radioMuted);
}

void radioManualStep(int8_t dir) {
  int32_t f = (int32_t)radioFreqKHz + dir * RADIO_MANUAL_STEP;
  radioSetFrequency((uint32_t)f, true);
}

void radioSaveStations() {
  Preferences prefs;
  prefs.begin("radio", false);
  size_t a = prefs.putUChar("n", radioStationCount);
  size_t b = 0;
  if (radioStationCount) b = prefs.putBytes("f", radioStations, radioStationCount * sizeof(radioStations[0]));
  prefs.end();
  Serial.printf("[RADIO] save n=%u a=%u b=%u\n", radioStationCount, (unsigned)a, (unsigned)b);
}
void radioLoadStations() {
  Preferences prefs;
  prefs.begin("radio", true);
  uint8_t n = prefs.getUChar("n", 0);
  if (n > RADIO_MAX_STATIONS) n = 0;
  if (n) {
    size_t got = prefs.getBytes("f", radioStations, n * sizeof(radioStations[0]));
    if (got != n * sizeof(radioStations[0])) n = 0;
  }
  prefs.end();
  Serial.printf("[RADIO] load raw n=%u s0=%u\n", n, n ? radioStations[0] : 0);
  for (uint8_t i = 0; i < n; i++) {
    if (radioStations[i] < RADIO_MIN_KHZ || radioStations[i] > RADIO_MAX_KHZ) { n = 0; break; }
  }
  radioStationCount = n;
  radioStationIdx = n ? 0 : -1;
  radioFreqKHz = n ? radioStations[0] : 98000;
  Serial.printf("[RADIO] load final n=%u f0=%u freq=%u\n", radioStationCount, n ? radioStations[0] : 0, radioFreqKHz);
}

void radioRedrawStatus() {
  // 局部刷新：进入/换页的全屏绘制用 drawRadioStatus（整卡一次），
  // 这里只更新变化区域，避免每秒/搜台时上半卡闪烁。
  updateRadioStatus(radioFreqKHz, radioStationIdx, radioStationCount, radioMuted, radioStereo,
                    radioSignal, radioScanning, radioScanFreq, radioScanFoundCount, radioPresent);
}
void radioRedrawList() {
  drawRadioList(radioSel, radioTop, radioScanning, radioMuted, radioStationIdx, radioStationCount, radioPresent);
}
void radioRedrawAll() { radioRedrawStatus(); radioRedrawList(); }

// 启动芯片硬件 Search（向上、高电平门限），从 startKHz 开始
bool radioSearchStart(uint32_t startKHz) {
  if (!radioI2cTake()) return false;
  uint32_t pll = ((uint32_t)(startKHz + 225) * 4000) / 32768;
  uint8_t b[5];
  b[0] = 0x80 | 0x40 | ((pll >> 8) & 0x3F);  // MUTE + SEARCH + PLL 高 6 位
  b[1] = pll & 0xFF;
  b[2] = 0x80 | 0x60 | 0x10;                 // SEARCH_UP + 高电平门限 + 高边注入
  b[3] = 0x1E;                               // 32.768k 晶振 + 软静音 + 高切 + 立体声降噪
  b[4] = 0x00;
  Wire.beginTransmission(RADIO_ADDR);
  for (uint8_t i = 0; i < 5; i++) Wire.write(b[i]);
  bool ok = (Wire.endTransmission() == 0);
  radioI2cGive();
  return ok;
}

// 从读状态字节解析 PLL 频率（kHz，50kHz 对齐；无效返回 0）
uint32_t radioReadFreqKHz(const uint8_t *b) {
  uint16_t pll = ((uint16_t)(b[0] & 0x3F) << 8) | b[1];
  uint32_t fHz = (uint32_t)pll * 32768UL / 4;
  if (fHz < 225000) return 0;
  uint32_t khz = (fHz - 225000) / 1000;
  if (khz < RADIO_MIN_KHZ || khz > RADIO_MAX_KHZ) return 0;
  return ((khz + RADIO_MANUAL_STEP / 2) / RADIO_MANUAL_STEP) * RADIO_MANUAL_STEP;
}

// 自动搜台（r62）：用 TEA5767 硬件 Search（向上、高电平门限）找台，
//   找到后正常调谐 120ms 复核信号，只保存强台；下一次从当前台 +0.2MHz 继续。
//   这样不会把手动步进扫到的噪声/偏频点存进频道表（雪花台的根因）。
void radioScanStart() {
  if (!radioPresent) return;
  radioScanning = true;
  radioScanPhase = RSCAN_SEARCH;
  radioSearchStartKHz = RADIO_MIN_KHZ;
  radioScanFreq = RADIO_MIN_KHZ;
  radioScanCandidate = 0;
  radioScanFoundCount = 0;
  radioMuted = true;
  radioSearchStart(radioSearchStartKHz);
  radioScanWaitMs = millis();
  radioDrawMs = 0;
  Serial.println("收音机: 开始自动搜台（硬件 Search 高门限）");
  screenDirty = true;
}
void radioScanFinish() {
  if (!radioScanning) return;
  radioScanning = false;
  radioScanPhase = RSCAN_IDLE;
  radioStationCount = radioScanFoundCount;
  for (uint8_t i = 0; i < radioStationCount; i++) radioStations[i] = radioScanFound[i];
  radioSaveStations();
  if (radioStationCount) {
    radioStationIdx = 0;
    radioFreqKHz = radioStations[0];
    radioMuted = false;
    radioWritePll(radioFreqKHz, false);
  } else {
    radioStationIdx = -1;
    radioMuted = true;
    radioWritePll(radioFreqKHz, true);
  }
  radioSignal = 0;
  radioStereo = false;
  Serial.printf("收音机: 搜台完成，共 %u 台\n", radioStationCount);
  screenDirty = true;
}
void radioScanTick(unsigned long now) {
  if (!radioScanning) return;
  if (now - radioScanWaitMs < RADIO_SEARCH_POLL_MS) return;
  uint8_t b[5];
  if (radioScanPhase == RSCAN_SEARCH) {
    if (!radioReadStatus(b)) return;
    if (b[0] & 0x80) {  // READY：搜索结束
      bool bandLimit = (b[0] & 0x40) != 0;
      uint32_t f = radioReadFreqKHz(b);
      if (bandLimit || f == 0) { radioScanFinish(); return; }
      radioScanCandidate = f;
      radioScanFreq = f;
      radioWritePll(f, true);  // 退出 Search，正常调谐到候选台（保持静音）
      radioScanPhase = RSCAN_VERIFY;
      radioScanWaitMs = now;
    } else if (now - radioScanWaitMs > RADIO_SEARCH_TIMEOUT_MS) {
      Serial.println("收音机: 搜索超时，提前结束");
      radioScanFinish();
      return;
    }
  } else if (radioScanPhase == RSCAN_VERIFY) {
    if (now - radioScanWaitMs < RADIO_VERIFY_WAIT_MS) return;
    uint8_t sig = 0;
    if (radioReadStatus(b)) {
      sig = (b[3] & 0xF0) >> 4;
      radioSignal = sig;
      radioStereo = (b[2] & 0x80) != 0;
    }
    if (sig >= RADIO_SIGNAL_MIN && radioScanFoundCount < RADIO_MAX_STATIONS) {
      bool dup = false;
      for (uint8_t i = 0; i < radioScanFoundCount; i++) {
        int32_t d = (int32_t)radioScanCandidate - (int32_t)radioScanFound[i];
        if (d < 0) d = -d;
        if (d < 300) { dup = true; break; }
      }
      if (!dup) {
        radioScanFound[radioScanFoundCount++] = radioScanCandidate;
        Serial.printf("[RADIO] found %u.%02uMHz sig=%u\n",
                      radioScanCandidate / 1000, (radioScanCandidate % 1000) / 10, sig);
      }
    }
    uint32_t next = radioScanCandidate + RADIO_SEARCH_STEP_KHZ;
    if (next > RADIO_MAX_KHZ) { radioScanFinish(); return; }
    radioSearchStartKHz = next;
    radioScanFreq = next;
    if (!radioSearchStart(next)) { radioScanFinish(); return; }
    radioScanPhase = RSCAN_SEARCH;
    radioScanWaitMs = now;
  }
  if (state == ST_RADIO && (radioDrawMs == 0 || now - radioDrawMs >= 250)) {
    radioDrawMs = now;
    radioRedrawStatus();  // 只刷状态卡；列表在开始时/结束后重画
  }
}

void radioInit() {
  if (i2cMutex && xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000);
    xSemaphoreGive(i2cMutex);
  }
  radioPresent = radioProbe();
  if (radioPresent) {
    radioWritePll(radioFreqKHz, true);  // 开机静音，进收音机页后由用户切换
    Serial.printf("[RADIO] TEA5767 已连接：%u.%02uMHz，已存 %u 台\n",
                  radioFreqKHz / 1000, (radioFreqKHz % 1000) / 10, radioStationCount);
  } else {
    Serial.println("[RADIO] 未检测到 TEA5767（检查 I2C 0x60 / SDA33 / SCL35）");
  }
}

// 首屏后台播放提示（不含音量，只有频率/序号/静音）
String radioHomeLine() {
  if (!radioBackground) return "";
  char b[48];
  snprintf(b, sizeof(b), "FM %u.%02uMHz", radioFreqKHz / 1000, (radioFreqKHz % 1000) / 10);
  String s = b;
  if (radioStationCount) {
    s += " ";
    if (radioStationIdx >= 0) s += String(radioStationIdx + 1);
    else s += "--";
    s += "/";
    s += String(radioStationCount);
  }
  if (radioMuted) s += " 静音";
  return s;
}

// 调试：收音机状态 + NVS 原始值（排查保存/加载）
void wifiHandleRadioDbg() {
  Preferences p;
  p.begin("radio", true);
  uint8_t n = p.getUChar("n", 0);
  uint32_t tmp[RADIO_MAX_STATIONS] = {0};
  size_t got = 0;
  if (n && n <= RADIO_MAX_STATIONS) got = p.getBytes("f", tmp, sizeof(tmp));
  p.end();
  char b[300];
  snprintf(b, sizeof(b),
           "present=%d stations=%u idx=%d freq=%u mute=%d bg=%d scanning=%d scanFreq=%u found=%u nvsN=%u got=%u nvs0=%u\n",
           radioPresent, radioStationCount, (int)radioStationIdx, radioFreqKHz, radioMuted,
           radioBackground, radioScanning, radioScanFreq, radioScanFoundCount,
           n, (unsigned)got, n ? tmp[0] : 0);
  wifiServer.send(200, "text/plain", b);
}

// 调试：清空收音机保存列表并静音
void wifiHandleRadioClear() {
  Preferences p;
  p.begin("radio", false);
  p.putUChar("n", 0);
  p.end();
  radioStationCount = 0;
  radioStationIdx = -1;
  radioFreqKHz = 98000;
  radioMuted = true;
  radioBackground = false;
  radioScanning = false;
  radioWritePll(radioFreqKHz, true);
  screenDirty = true;
  wifiServer.send(200, "text/plain", "radio cleared");
}

void drawScreen() {
  switch (state) {
    case ST_HOME: refreshHomeData(); drawHomeScreen(homeMode, homeClock, homeYday, homeBmpTemp, homeBmpPress, homeCalMonth, homeCalDates, homeCalTodayIdx, homeLunar, dispMask, getDeviceName(), radioHomeLine()); syncHomeLines(); break;
    case ST_MENU: fixMenuState(); drawMenuScreen(menuSel, menuTop); break;
    case ST_LAMP: drawLampScreen(lampSel, lastSent, lastRecv); break;
    case ST_AC: drawAcScreen(acSel, acTop, lastSent, lastRecv, buildAcStatus()); break;
    case ST_DECODE: drawDecodeScreen(irRxProto, irRxHex, recvBits, recvLen); break;
    case ST_IRTEST: drawIrTestScreen(irTestRunning, irTestOnPhase); break;
    case ST_WIFI: showWifiScreen(); break;
    case ST_TASKS: drawTasksScreen(taskRows, taskRowCount, tasksTop, tasksCpu, tasksHeapK); break;
    case ST_RADIO:
      drawRadioScreen(radioFreqKHz, radioStationIdx, radioStationCount, radioMuted, radioStereo,
                      radioSignal, radioScanning, radioScanFreq, radioScanFoundCount,
                      radioPresent, radioSel, radioTop);
      break;
    case ST_ABOUT: drawAboutScreen(gBuildTime); break;
    case ST_I2C: drawI2cScreen(i2cScanned, i2cFound, i2cFoundCount); break;
    case ST_COPY: drawCopyScreen(copySlotLine, copyModeLine, copyStatusLine, copyStoredLine, copyLearnedLine); break;
    case ST_HS: drawHsScreen(fmtPairCode(getPairCode())); break;
    case ST_DISP: drawDispScreen(dispSel, dispMask); break;
    case ST_CHIP: {  // 进页整屏重画（值现算）；页内 1 秒刷新只刷卡片（见 loop 1d）
      String chip, ram, ramPct, rom, romPct, temp;
      chipStrings(chip, ram, ramPct, rom, romPct, temp);
      drawChipScreen(chip, ram, ramPct, rom, romPct, temp, gResetReason);
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
      if (i == 3) {
        state = ST_MENU;
        screenDirty = true;  // KEY4 -> 一级菜单
      } else if (radioBackground && (i == 1 || i == 2)) {
        // 收音机后台播放：首屏 KEY2/KEY3 直接上/下一个频道，只刷提示行
        radioSelectStation(i == 1 ? 1 : -1);
        updateHomeRadioLine(radioHomeLine());
      }
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
        // r61：接收头改成幂等开关，反复进出子菜单不会再重复申请硬件定时器
        switch (menuSel) {
          case 0: state = ST_LAMP; enableIrReceiver(); break;
          case 1: state = ST_AC; enableIrReceiver(); break;
          case 2: state = ST_DECODE; enableIrReceiver(); break;
          case 3: state = ST_IRTEST; break;
          case 4: state = ST_WIFI; break;
          case 5: state = ST_I2C; break;
          case 6: state = ST_COPY; enableIrReceiver(); refreshCopyLines(); break;
          case 7: state = ST_HS; break;
          case 8: state = ST_DISP; break;
          case 9: refreshTaskRows(); tasksTop = 0; state = ST_TASKS; break;
          case 10: state = ST_ABOUT; break;
          case 11: state = ST_CHIP; break;
          case 12: {  // 收音机：进页重新探测模块，切前台
            radioPresent = radioProbe();
            if (radioPresent) radioPollStatus();
            radioSel = 0;
            radioTop = 0;
            radioBackground = false;
            state = ST_RADIO;
            Serial.printf("[RADIO] page present=%d stations=%u freq=%u.%02u\n",
                          radioPresent, radioStationCount,
                          radioFreqKHz / 1000, (radioFreqKHz % 1000) / 10);
            break;
          }
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
    case ST_TASKS:  // 任务详情：KEY2/KEY3 滚动列表，KEY4 回菜单
      if (i == 1) {
        if (taskRowCount > TASK_WIN_ROWS && tasksTop + TASK_WIN_ROWS < taskRowCount) {
          tasksTop++;
          drawTasksCard(taskRows, taskRowCount, tasksTop, tasksCpu);
        }
      } else if (i == 2) {
        if (tasksTop > 0) {
          tasksTop--;
          drawTasksCard(taskRows, taskRowCount, tasksTop, tasksCpu);
        }
      } else if (i == 3) {
        state = ST_MENU;
        screenDirty = true;
      }
      break;
    case ST_RADIO:
      if (i == 1) {  // KEY2：下一功能
        radioSel = (radioSel + 1) % RADIO_ITEM_COUNT;
        if (radioSel == 0) radioTop = 0;
        else if (radioSel >= radioTop + RADIO_WIN_ROWS) radioTop = radioSel - RADIO_WIN_ROWS + 1;
        radioRedrawList();
      } else if (i == 2) {  // KEY3：上一功能
        radioSel = (radioSel + RADIO_ITEM_COUNT - 1) % RADIO_ITEM_COUNT;
        if (radioSel == RADIO_ITEM_COUNT - 1) radioTop = RADIO_ITEM_COUNT - RADIO_WIN_ROWS;
        else if (radioSel < radioTop) radioTop = radioSel;
        radioRedrawList();
      } else if (i == 0) {  // KEY1：执行当前功能
        switch (radioSel) {
          case 0:  // 自动搜台（再按一次提前结束）
            if (radioScanning) radioScanFinish();
            else radioScanStart();
            radioRedrawAll();
            break;
          case 1:  // 静音/取消静音
            if (!radioScanning) {
              radioMuted = !radioMuted;
              radioWritePll(radioFreqKHz, radioMuted);
              radioRedrawAll();
            }
            break;
          case 2:  // 上一个频道
            if (!radioScanning) { radioSelectStation(-1); radioRedrawAll(); }
            break;
          case 3:  // 下一个频道
            if (!radioScanning) { radioSelectStation(1); radioRedrawAll(); }
            break;
          case 4:  // 手动调频 -
            if (!radioScanning) { radioManualStep(-1); radioRedrawAll(); }
            break;
          case 5:  // 手动调频 +
            if (!radioScanning) { radioManualStep(1); radioRedrawAll(); }
            break;
          case 6:  // 后台播放：保持播放并回菜单
            if (radioScanning) radioScanFinish();
            radioBackground = true;
            state = ST_MENU;
            screenDirty = true;
            Serial.printf("[RADIO] background %u.%02uMHz idx=%d/%u mute=%d\n",
                          radioFreqKHz / 1000, (radioFreqKHz % 1000) / 10,
                          (int)radioStationIdx, radioStationCount, radioMuted);
            break;
          case 7:  // 返回菜单：静音并回菜单
            if (radioScanning) radioScanFinish();
            radioMuted = true;
            radioWritePll(radioFreqKHz, true);
            radioBackground = false;
            state = ST_MENU;
            screenDirty = true;
            Serial.println("[RADIO] return + mute");
            break;
        }
      } else if (i == 3) {  // KEY4：默认后台播放并回菜单（保持当前播放/静音状态）
        if (radioScanning) radioScanFinish();
        radioBackground = true;
        state = ST_MENU;
        screenDirty = true;
        Serial.println("[RADIO] key4 background");
      }
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

// 按键事件统一入口：物理扫描与网页注入共用（含熄屏唤醒逻辑）
void handleKeyEvent(uint8_t i, unsigned long now) {
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

void pollKeys(unsigned long now) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t r = digitalRead(keyPins[i]);
    if (r != keyReading[i]) { keyReading[i] = r; keyT[i] = now; }
    if ((now - keyT[i] > DEBOUNCE_MS) && (keyReading[i] != keyStable[i])) {
      keyStable[i] = keyReading[i];
      if (keyStable[i] == LOW) handleKeyEvent(i, now);
    }
  }
}

// ---------------- FreeRTOS 任务 ----------------
// 临时诊断开关：1 = 开机前 30 秒自动模拟按 KEY4/KEY2/KEY3（复现上电按键重启）
#define AUTO_KEY_STRESS 0

// 首页局部刷新：联网/断网切换、模式变化时全屏重画；同模式下每秒只刷变化的
//   行/卡（不全屏）：时钟模式走 updateHomeClockLines，同步中只刷温压卡。
static void tickHomeScreen(unsigned long now) {
  static bool lastWifiConn = false;
  static unsigned long homeClockMs = 0;
  bool conn = (WiFi.status() == WL_CONNECTED);
  if (conn != lastWifiConn) { lastWifiConn = conn; screenDirty = true; }
  else if (conn && (now - homeClockMs >= 1000)) {
    homeClockMs = now;
    refreshHomeData();
    if (homeMode != lastHomeModeDrawn) screenDirty = true;
    else if (homeMode == 1 && !screenDirty) updateHomeClockLines();
    else if (homeMode == 2 && !screenDirty) {
      if (homeBmpTemp != lastHomeBmpTemp || homeBmpPress != lastHomeBmpPress) {
        drawHomeBmpCards(homeBmpTemp, homeBmpPress);
        lastHomeBmpTemp = homeBmpTemp; lastHomeBmpPress = homeBmpPress;
      }
    }
  }
}

// WiFi 页自动刷新：连接/热点/SSID/网页开关变了就重画
static void tickWifiPage() {
  static bool lastWifiPageConn = false;
  static bool lastWifiPageApOn = false;
  static bool lastWifiPageWeb = false;
  static String lastWifiPageSsid = "";
  static String lastWifiPageIp = "";
  bool conn = (WiFi.status() == WL_CONNECTED);
  String ssid = conn ? WiFi.SSID() : "";
  String ipNow = conn ? WiFi.localIP().toString() : (wifiApOn ? WiFi.softAPIP().toString() : "");
  if (conn != lastWifiPageConn || wifiApOn != lastWifiPageApOn || webEnabled != lastWifiPageWeb
      || ssid != lastWifiPageSsid || ipNow != lastWifiPageIp) {
    lastWifiPageConn = conn;
    lastWifiPageApOn = wifiApOn;
    lastWifiPageWeb = webEnabled;
    lastWifiPageSsid = ssid;
    lastWifiPageIp = ipNow;
    screenDirty = true;
  }
}

// 传感器结果并入 UI：显示串只在 appTask 里生成（String 不能跨任务裸写）。
// 首页未联网模式（mode 0）只刷底部温压卡；时钟/同步中模式由 tickHomeScreen 的
// 每秒局部刷新统一处理。
static void tickSensorUi() {
  if (!sensorUpdated) return;
  sensorUpdated = false;
  String oldT = homeBmpTemp, oldP = homeBmpPress;
  refreshBmpStrings();
  if (state == ST_HOME && homeMode == 0) {
    if (homeBmpTemp != oldT || homeBmpPress != oldP) drawHomeBmpCards(homeBmpTemp, homeBmpPress);
  }
}

// 把非 UI 任务放到 PSRAM 栈上（r61 稳定性优化）：
//   内部 RAM 只剩 16KB，反复刷菜单/网页/HomeKit 并发时有 String 分配失败风险；
//   网络/传感器/农历任务对栈访问速度不敏感，放 PSRAM 可给内部 RAM 腾出 ~23KB。
// 分配 stackBytes*sizeof(StackType_t) 字节、传 stackBytes：
//   IDF 语义按字节算栈（用前 stackBytes 字节），vanilla 语义按字算（正好用满缓冲），
//   两种实现都不会越界。
static TaskHandle_t createTaskPsram(TaskFunction_t fn, const char *name, uint32_t stackBytes,
                                    UBaseType_t prio, StaticTask_t *tcb, TaskHandle_t *out) {
  *out = nullptr;
  if (psramFound()) {
    StackType_t *stack = (StackType_t *)heap_caps_malloc(
        (size_t)stackBytes * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stack) {
      TaskHandle_t h = xTaskCreateStatic(fn, name, stackBytes, nullptr, prio, stack, tcb);
      if (h) {
        *out = h;
        return h;
      }
      heap_caps_free(stack);
    }
  }
  if (xTaskCreate(fn, name, stackBytes, nullptr, prio, out) != pdPASS) *out = nullptr;
  return *out;
}

// UI/状态机任务（原 loop 主体）：独占 TFT、按键、红外收发
static void appTask(void *) {
  esp_task_wdt_add(NULL);
  unsigned long chipLastRefresh = 0;
  unsigned long tasksLastRefresh = 0;
  unsigned long radioLastPoll = 0;
  for (;;) {
    esp_task_wdt_reset();
    unsigned long now = millis();

    // UI 任务心跳（10 秒）：即使 netTask 创建失败也能从串口确认 UI 存活与内存余量
    static unsigned long lastAppStat = 0;
    if (lastAppStat == 0 || now - lastAppStat >= 10000) {
      lastAppStat = now;
      Serial.printf("[APP] up=%lus int=%uKB psram=%uKB min=%uKB maxBlock=%uKB | entry=%u sync=%u tft=%u KB | net=%d\n",
                    (unsigned long)(now / 1000),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                    (unsigned)(ESP.getMinFreeHeap() / 1024),
                    (unsigned)(ESP.getMaxAllocHeap() / 1024),
                    (unsigned)(dbgMemEntry / 1024), (unsigned)(dbgMemSync / 1024), (unsigned)(dbgMemTft / 1024),
                    netTaskHandle != NULL);
    }

    // 1. 首页
    if (state == ST_HOME) tickHomeScreen(now);

    // 1b. WiFi 页
    if (state == ST_WIFI) tickWifiPage();

    // 1c. 网页改动的异步刷新（配对码/复制编号名）
    if (pairCodeDirty) {
      pairCodeDirty = false;
      if (state == ST_HS) screenDirty = true;
    }
    if (copyNameDirty) {
      copyNameDirty = false;
      if (state == ST_COPY) { refreshCopyLines(); screenDirty = true; }
    }
    if (devNameDirty) {  // 网页改了首屏名称：在首页时立即重画
      devNameDirty = false;
      if (state == ST_HOME) screenDirty = true;
    }

    // 1d. 传感器 UI
    tickSensorUi();

    // 1e. 芯片详情页：每 1 秒只刷卡片内容（不清屏不重画标题，免闪）
    if (state == ST_CHIP && (chipLastRefresh == 0 || now - chipLastRefresh >= 1000)) {
      chipLastRefresh = now;
      refreshChipCard();
    }

    // 1f. 任务详情页：每 1 秒采集并局部刷新（状态/优先级/CPU/栈余）
    if (state == ST_TASKS && (tasksLastRefresh == 0 || now - tasksLastRefresh >= 1000)) {
      tasksLastRefresh = now;
      refreshTaskRows();
      drawTasksCard(taskRows, taskRowCount, tasksTop, tasksCpu);
      static unsigned long tasksLogMs = 0;
      if (now - tasksLogMs >= 5000) {  // 页内 5 秒一行串口摘要，便于确认统计正常
        tasksLogMs = now;
        Serial.printf("[TASKS] n=%u cpu=%u%% heap=%uKB top=%u\n",
                      taskRowCount, tasksCpu, (unsigned)tasksHeapK, tasksTop);
      }
    }

    // 1g. 收音机：自动搜台状态机；收音机页每 1 秒读一次信号
    if (radioScanning) radioScanTick(now);
    if (state == ST_RADIO && !radioScanning &&
        (radioLastPoll == 0 || now - radioLastPoll >= 1000)) {
      radioLastPoll = now;
      if (radioPresent && radioPollStatus()) radioRedrawStatus();
    }

    // 1h. 收音机页底部信号条模拟律动：未静音/未搜台时每 120ms 轻微起伏
    if (state == ST_RADIO && radioPresent && !radioScanning && !radioMuted) {
      static unsigned long radioAnimMs = 0;
      if (now - radioAnimMs >= 120) {
        radioAnimMs = now;
        radioDrawBars(radioSignal, radioMuted, radioScanning, radioScanFreq, radioPresent);
      }
    }

    // 2. 按键扫描（含 30ms 消抖）
    pollKeys(now);

    // 2a. 网页注入的按键（调试/自动化测试）：走与物理按键相同的入口（含唤醒）
    uint8_t injectedKey;
    while (keyCmdQueue && xQueueReceive(keyCmdQueue, &injectedKey, 0) == pdPASS) {
      if (injectedKey < 4) handleKeyEvent(injectedKey, now);
    }
    if (forceSleep) {
      forceSleep = false;
      lastKeyMs = now - SCREEN_SLEEP_MS - 100;  // 下一轮进入熄屏
    }

#if AUTO_KEY_STRESS
    // 开机前 30 秒反复"进菜单/滚动/回首页"，覆盖 WiFi/HomeSpan/农历启动并发窗口
    static unsigned long stressNext = 0;
    static uint8_t stressStep = 0;
    if (now < 30000 && now >= stressNext) {
      stressNext = now + 120;
      static const uint8_t seq[] = {3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0};
      handleKeyEvent(seq[stressStep % (sizeof(seq) / sizeof(seq[0]))], now);
      stressStep++;
    }
#endif

    // 2b. 熄屏：3 分钟无按键 -> 背光灭（面板内容保留在 GRAM，唤醒即恢复）
    if (!screenAsleep && (now - lastKeyMs >= SCREEN_SLEEP_MS)) {
      digitalWrite(TFT_BL, BL_OFF);
      screenAsleep = true;
      // 呼吸灯：两路 LED 改走硬件 PWM（LEDC 5kHz 8 位；拉低点亮，占空比取反）
      ledcAttach(LED1, 5000, 8);
      ledcAttach(LED2, 5000, 8);
      breathT0 = now;
      lastBreathMs = now;
      ledcWrite(LED1, 255);
      ledcWrite(LED2, 255);
    }

    // 2c. 熄屏呼吸灯：正弦淡入淡出，周期 2s，20ms 写一次寄存器
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
      if (irTestOnPhase) irsend.sendRaw(irTestMark, 2, 38);  // 20ms 载波突发
    }

    // 4. 红外接收：更新解码显示，串口打印完整解码（含原始数组）
    if (irrecv.decode(&irResults)) {
      irRxProto = typeToString(irResults.decode_type, irResults.repeat);
      irRxHex = hexPad((uint32_t)irResults.value, irResults.bits);
      if (irResults.repeat) irRxHex += "(R)";
      recvBits = irResults.bits;
      recvLen = irResults.rawlen - 1;
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
      if (state == ST_LAMP) {
        updateRecvLine(lastRecv);
      } else if (state == ST_AC) {
        updateAcRecvLine(lastRecv);
      } else if (state == ST_DECODE) {
        screenDirty = true;
      }
      // 红外复制学习中：解析到的键码存为待存储捕获（连发的重复码同值，覆盖无害）
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

    // 4.5 网页/HomeKit 登记的红外：本任务统一执行（与按键同一上下文）
    execPendingIr();

    // 5. 屏幕刷新
    if (screenDirty) {
      drawScreen();
      screenDirty = false;
    }

    // 让出 CPU：5ms 轮询足够跟上按键/红外，也避免单核空转饿死网络任务
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// 网络任务：WiFi 状态、WebServer、HomeSpan、家庭温度推送、异步配网监视。
// 全部网络相关操作与 appTask 的按键操作用 wifiMutex 串行化。
static void netTask(void *) {
  esp_task_wdt_add(NULL);
  unsigned long apStopAt = 0;
  uint8_t evt;
  TempUpdate tu;
  for (;;) {
    esp_task_wdt_reset();

    if (wifiMutex && xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      // HomeSpan：WiFi 稳定连接 3 秒后再启动（只启动一次）。避免刚连上时的
      //   抖动/重连与 HomeSpan 自己的 WiFi 管理撞车（r61 实测会有一次假连接）。
      static unsigned long wifiStableSince = 0;
      if (!hsStarted) {
        if (WiFi.status() == WL_CONNECTED) {
          if (wifiStableSince == 0) {
            wifiStableSince = millis();
          } else if (millis() - wifiStableSince >= 3000) {
            setupHomeSpan();
            hsStarted = true;
            wifiStableSince = 0;
            // 启动即推一次两个温度的当前值（家庭里不先显示 0）
            if (bmpPresent && bmpTempChar) bmpTempChar->setVal(bmpTempTenths / 10.0f);
            if (chipTempChar && chipTempOk && chipTempReadOk) chipTempChar->setVal(chipTempC);
            Serial.println("HomeKit 已启动（配对码 " + fmtPairCode(getPairCode()) + "，配件：灯、空调、温度、芯片温度）");
          }
        } else {
          wifiStableSince = 0;
        }
      }
      if (hsStarted) homeSpan.poll();
      if (webEnabled && (wifiApOn || WiFi.status() == WL_CONNECTED)) {
        wifiServer.handleClient();
      }
      xSemaphoreGive(wifiMutex);
    }

    // HomeKit 瞬时按键弹回：红外发完后由本任务 setVal(0)（与 poll 同任务）
    while (hsEvtQueue && xQueueReceive(hsEvtQueue, &evt, 0) == pdPASS) {
      if (evt == HS_EVT_IR_SENT && pendingRevert) {
        pendingRevert->setVal(0);
        pendingRevert = nullptr;
      }
    }

    // 传感器温度 -> 家庭（每 1 秒一帧，无变化阈值；未启动 HomeSpan 时丢弃）
    while (tempQueue && xQueueReceive(tempQueue, &tu, 0) == pdPASS) {
      if (hsStarted) {
        if (bmpTempChar && tu.bmpValid) bmpTempChar->setVal(tu.bmpC);
        if (chipTempChar && tu.chipValid) chipTempChar->setVal(tu.chipC);
      }
    }

    // 异步配网结果监视：成功 -> 1.5s 后关热点（让结果页发完）；超时 -> 保留热点
    if (wifiConnecting) {
      unsigned long elapsed = millis() - wifiConnectStart;
      if (elapsed > 300 && WiFi.status() == WL_CONNECTED) {  // 300ms 后看，避开旧连接状态
        wifiConnecting = false;
        Serial.println("WiFi: 连接成功 IP=" + WiFi.localIP().toString());
        if (wifiApOn) apStopAt = millis() + 1500;
      } else if (elapsed > 15000) {
        wifiConnecting = false;
        Serial.println("WiFi: 连接超时（配置热点保留，可重试）");
      }
    }
    if (apStopAt && (long)(millis() - apStopAt) >= 0) {
      apStopAt = 0;
      wifiStopAp();
    }

    // 运行状态心跳（r61）：10 秒一行，便于串口确认任务存活、观察堆与栈余量
    static unsigned long lastStatMs = 0;
    if (lastStatMs == 0 || millis() - lastStatMs >= 10000) {
      lastStatMs = millis();
      Serial.printf("[STAT] up=%lus int=%uKB psram=%uKB stackFree app=%u net=%u sensor=%u B\n",
                    (unsigned long)(millis() / 1000),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                    (unsigned)(appTaskHandle ? uxTaskGetStackHighWaterMark(appTaskHandle) : 0),
                    (unsigned)uxTaskGetStackHighWaterMark(NULL),
                    (unsigned)(sensorTaskHandle ? uxTaskGetStackHighWaterMark(sensorTaskHandle) : 0));
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// 传感器任务：BMP085 + 芯片内核温度每秒采样一次（I2C 总线与寻址互斥）
static void sensorTask(void *) {
  esp_task_wdt_add(NULL);
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    esp_task_wdt_reset();
    if (i2cMutex && xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
      bmpRead();  // 未连接时每次重初始化，支持热插拔
      xSemaphoreGive(i2cMutex);
    }
    bool chipValid = false;
    if (chipTempOk) {
      float ct = 0;
      if (temperature_sensor_get_celsius(chipTempHandle, &ct) == ESP_OK) {
        chipTempC = ct;
        chipTempReadOk = true;
        chipValid = true;
      }
    }
    TempUpdate tu = { bmpPresent, bmpTempTenths / 10.0f, chipValid, chipTempC };
    if (tempQueue) xQueueOverwrite(tempQueue, &tu);  // 长度 1：只保最新帧
    sensorUpdated = true;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 2000)) delay(10);  // 等 native USB，最多 2s
  dbgMemEntry = ESP.getFreeHeap();
  gResetReason = fmtResetReason();
  Serial.printf("[BOOT] reset_reason=%d（3=软复位 4=Panic 6=任务看门狗 9=欠压）\n", (int)esp_reset_reason());
  Serial.printf("[MEM] 0 entry: free=%uKB size=%uKB min=%uKB psram=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024),
                (unsigned)(ESP.getMinFreeHeap() / 1024), (unsigned)(ESP.getPsramSize() / 1024));
  if (!psramFound()) {
    Serial.println("警告: PSRAM 未启用！请在 工具>PSRAM 选 Enabled（否则内部堆只剩几 KB，网络功能会失败）");
  }

  // 跨任务同步对象先建好，后面任何任务/回调都能安全使用
  stateMutex = xSemaphoreCreateMutex();
  wifiMutex  = xSemaphoreCreateMutex();
  i2cMutex   = xSemaphoreCreateMutex();
  irReqMutex = xSemaphoreCreateMutex();
  irReqQueue = xQueueCreate(PENDING_QUEUE_LEN, sizeof(PendingItem));
  tempQueue  = xQueueCreate(1, sizeof(TempUpdate));
  hsEvtQueue = xQueueCreate(4, sizeof(uint8_t));
  keyCmdQueue = xQueueCreate(8, sizeof(uint8_t));
  if (!stateMutex || !wifiMutex || !i2cMutex || !irReqMutex || !irReqQueue || !tempQueue || !hsEvtQueue || !keyCmdQueue) {
    Serial.println("FreeRTOS 同步对象创建失败，功能可能不完整");
  }
  dbgMemSync = ESP.getFreeHeap();
  Serial.printf("[MEM] 1 sync: free=%uKB size=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024));

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
  dbgMemTft = ESP.getFreeHeap();
  Serial.printf("[MEM] 2 tft: free=%uKB size=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024));

  enableIrReceiver();    // RMT 接收，IO16，缓存 512 项（定时器只申请一次）
  irsend.begin();        // RMT 发送，IO17

  // 芯片内核温度传感器（ESP32-S2 内置 TSENS）：量程 -10~80°C（误差 <1°C 的一档；
  // 芯片开 WiFi 时内核温度常在 50~60°C，量程不能选小）。装不上时 chipTempOk=false。
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

  Serial.printf("[MEM] 3 ir/tsens: free=%uKB size=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024));

  Serial.println();
  Serial.println("===== ESP32-S2 mini 硬件测试启动（r61 FreeRTOS 版）=====");
  Serial.println("任务：appTask(UI/按键/红外) / netTask(WiFi/网页/HomeKit) / sensorTask(BMP085+芯片温度) / lunarTask(农历)");
  Serial.println("首页：WiFi 已连显示时钟，未连显示 Esp32-IR，KEY4 进入一级菜单");
  Serial.println("按键：KEY4 菜单，KEY2 加（下一项），KEY3 减（上一项），KEY1 确认");
  Serial.println("WiFi设置：KEY1 开关网页，KEY2 开关配置热点（ESP32S2-Setup），网页 192.168.4.1:8080");
  Serial.println("=====================================");

  // 启动前先把 NVS 数据读完（任务里会用到配对码/复制编号/显示设置）
  wifiPrefs.begin("wifi", false);
  wifiSavedSsid = wifiPrefs.getString("ssid", "");
  wifiSavedPass = wifiPrefs.getString("pass", "");
  copyLoadAll();                 // 红外复制 10 个编号的键码与名称
  loadPairCode();                // HomeSpan 配对码（无保存时默认 46637726）
  loadDeviceName();              // 首屏设备名称（无保存时默认 ESP32-S2-IR）
  loadDisplaySettings();         // 显示设置掩码（无保存时默认 31 全显示）
  radioLoadStations();           // 收音机保存的频道（NVS radio）
  gBuildTime = fmtBuildTime();   // 关于页显示用（开机时算一次）
  lastKeyMs = millis();          // 熄屏计时从开机完成起算
  radioInit();                   // TEA5767 探测 + 开机静音（在任务启动前，避免抢 I2C）
  screenDirty = true;            // NVS 读完重画一次，应用保存的设备名/显示设置

  // 网页路由（在任务启动前注册好，WiFi 连上后 netTask 直接服务）
  wifiServer.on("/", wifiHandleRoot);
  wifiServer.on("/wifi", wifiHandleWifi);
  wifiServer.on("/ir", wifiHandleIr);
  wifiServer.on("/save", HTTP_POST, wifiHandleSave);
  wifiServer.on("/ircopy_name", HTTP_POST, wifiHandleCopyName);
  wifiServer.on("/devname", HTTP_POST, wifiHandleDevName);
  wifiServer.on("/paircode", HTTP_POST, wifiHandlePairCode);
  wifiServer.on("/key", wifiHandleKey);     // 调试：网页注入 KEY1~KEY4
  wifiServer.on("/sleep", wifiHandleSleep); // 调试：强制熄屏
  wifiServer.on("/radiodbg", wifiHandleRadioDbg);     // 调试：收音机状态/NVS
  wifiServer.on("/radioclear", wifiHandleRadioClear); // 调试：清空收音机频道
  wifiServer.onNotFound(wifiHandleRoot);

  // NTP：北京时间（UTC+8，无夏令时），联网后后台自动同步，首页时钟用
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org", "time.google.com");

  // 任务看门狗（10 秒、panic）：各业务任务自己喂狗，卡死自动重启。
  // 注意：Arduino 核心启动时已按 IDF 默认（5 秒、panic）初始化过看门狗，
  //   esp_task_wdt_init 此时返回 ESP_ERR_INVALID_STATE 且不会改超时；必须 reconfigure。
  {
    esp_task_wdt_config_t wdtCfg = {
      .timeout_ms = 10000,
      .idle_core_mask = 0,
      .trigger_panic = true,
    };
    esp_err_t wdtErr = esp_task_wdt_init(&wdtCfg);
    if (wdtErr == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&wdtCfg);
  }

  // 业务任务必须在 WiFi.begin 之前创建（r61 实测：WiFi/lwIP 先吃掉堆后，
  //   16KB 任务的连续内存申请会失败，netTask/sensorTask 直接创建不出来）。
  //   ESP32-S2 单核：不绑核，按优先级调度。栈用字节数。
  Serial.printf("[MEM] 4 nvs/wdt: free=%uKB size=%uKB min=%uKB | history entry=%uKB sync=%uKB tft=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024),
                (unsigned)(ESP.getMinFreeHeap() / 1024),
                (unsigned)(dbgMemEntry / 1024), (unsigned)(dbgMemSync / 1024), (unsigned)(dbgMemTft / 1024));
  Serial.printf("[TASK] 创建前 heap=%uKB maxBlock=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
  // UI 任务留在内部 RAM（12KB，反复刷菜单/红外收发响应快）；
  // net/sensor 任务放 PSRAM，给内部堆腾空间（HomeKit/网页/String 都吃内部堆）。
  if (xTaskCreate(appTask, "appTask", 12288, NULL, 3, &appTaskHandle) != pdPASS) {
    Serial.printf("appTask 创建失败 heap=%uKB maxBlock=%uKB\n",
                  (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
  }
  if (xTaskCreate(netTask, "netTask", 12288, NULL, 2, &netTaskHandle) != pdPASS) {
    Serial.printf("netTask 创建失败 heap=%uKB maxBlock=%uKB\n",
                  (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
  }
  if (xTaskCreate(sensorTask, "sensorTask", 4096, NULL, 1, &sensorTaskHandle) != pdPASS) {
    Serial.printf("sensorTask 创建失败 heap=%uKB maxBlock=%uKB\n",
                  (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
  }
  Serial.printf("[TASK] 创建后 heap=%uKB maxBlock=%uKB（app=%d net=%d sensor=%d）\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024),
                appTaskHandle != NULL, netTaskHandle != NULL, sensorTaskHandle != NULL);

  // 开机就起 8080 网页监听：STA 模式（家里 WiFi）也能直接访问，不必先开热点
  if (wifiMutex) xSemaphoreTake(wifiMutex, portMAX_DELAY);
  ensureWebServer();
  if (wifiMutex) xSemaphoreGive(wifiMutex);

  // WiFi：读 NVS 里保存的网络，有则后台自动连接（不阻塞启动）
  WiFi.setAutoReconnect(true);  // 掉线后自动重连（r61，网页新配的网同样生效）
  if (wifiSavedSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSavedSsid.c_str(), wifiSavedPass.c_str());
    Serial.println("WiFi: 已保存 " + wifiSavedSsid + "，后台连接中");
  } else {
    Serial.println("WiFi: 无保存的网络（进 WiFi设置 页配置）");
  }

  // 农历拉取任务：HTTPS 放独立栈里跑（见 pollLunarAsync 上方注释）；栈放 PSRAM
  lunarMutex = xSemaphoreCreateMutex();
  if (lunarMutex) {
    if (!createTaskPsram(lunarFetchTask, "lunarFetch", LUNAR_TASK_STACK, 1, &lunarTaskBuf, &lunarTaskHandle)) {
      Serial.println("农历拉取任务创建失败（首页农历将不显示）");
    }
  } else {
    Serial.println("农历互斥锁创建失败（首页农历将不显示）");
  }
  Serial.println("初始化完成：业务任务已启动（appTask/netTask/sensorTask）");
}

// Arduino 主循环只保持空转：全部业务在独立任务里，避免 loopTask 抢占栈和优先级。
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
