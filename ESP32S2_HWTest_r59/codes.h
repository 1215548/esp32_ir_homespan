//================================================================
// codes.h - 红外键码表（r12 菜单版）
// 纯数据文件，不依赖任何库。主文件 include 它即可使用。
// 灯码为 NEC 32 位（用户实测提供，串口打印时前导 0 会被省略，
// 这里按 32 位补齐，如 0x01FEA057）。
// 空调码为 R05D（A/B/C 三字节，hex 列为 24 位有效码）。
//================================================================
#pragma once
#include <stdint.h>

// ---------------- 灯（NEC 32 位）----------------
struct LampCode {
  const char *name;   // 屏幕显示名
  uint32_t nec;       // NEC 32 位码，sendNEC 发送
  const char *hex;    // 显示用 16 进制串（与 nec 对应）
};

const LampCode LAMP_CODES[] = {
  {"夜灯",     0x01FEA057, "0x01FEA057"},  // 注：末字节 57 不符反码规则（A0 的反码应为 5F），若此键无效可试 0x01FEA05F
  {"开/关",    0x01FE807F, "0x01FE807F"},
  {"亮度",     0x01FEFF00, "0x01FEFF00"},
  {"+",        0x01FE48B7, "0x01FE48B7"},
  {"-",        0x01FE7887, "0x01FE7887"},
  {"辅助光源", 0x01FE58A7, "0x01FE58A7"},
  {"色温-",    0x01FEC03F, "0x01FEC03F"},
  {"色温+",    0x01FE40B7, "0x01FE40B7"},  // 注：末字节 B7 不符反码规则（40 的反码应为 BF），若此键无效可试 0x01FE40BF
  {"分段",     0x01FE609F, "0x01FE609F"},
};
const uint8_t LAMP_COUNT = sizeof(LAMP_CODES) / sizeof(LAMP_CODES[0]);

// ---------------- 空调（R05D）----------------
struct AcCode {
  const char *name;   // 屏幕显示名
  uint8_t a, b, c;    // R05D 有效码三字节（帧 = A ~A B ~B C ~C）
  const char *hex;    // 显示用 24 位有效码串
};

const AcCode AC_CODES[] = {
  {"开机", 0xB2, 0x1F, 0xD8, "B21FD8"},  // 自动模式 26℃ 固定风
  {"关机", 0xB2, 0x7B, 0xE0, "B27BE0"},  // 帧结构合规，字段不按运行状态表（关机码）
};
const uint8_t AC_COUNT = sizeof(AC_CODES) / sizeof(AC_CODES[0]);

// ---------------- 空调状态编码（R05D，pinaland 格式）----------------
// 温度±/模式/风速±没有固定码：程序跟踪状态，控制项改状态后拼出新帧再发。
// 风速（B7B6B5）：自动 101、低风 100、中风 010、高风 001、固定风 000
// 注：中风另有来源说是 011，与 pinaland 的 010 有出入，待实测验证
struct FanSpeed { const char *name; uint8_t bits; };
const FanSpeed AC_FANS[] = {
  {"自动", 0x5}, {"低风", 0x4}, {"中风", 0x2}, {"高风", 0x1}, {"固定风", 0x0},
};
const uint8_t AC_FAN_COUNT = sizeof(AC_FANS) / sizeof(AC_FANS[0]);
// 模式（C3C2）：自动 10、制冷 00、抽湿 01、制热 11、送风 01
// 注：抽湿与送风模式位相同（01），以温度位区分：抽湿有温度，送风温度位=1110
struct AcMode { const char *name; uint8_t bits; };
const AcMode AC_MODES[] = {
  {"自动", 0x2}, {"制冷", 0x0}, {"抽湿", 0x1}, {"制热", 0x3}, {"送风", 0x1},
};
const uint8_t AC_MODE_COUNT = sizeof(AC_MODES) / sizeof(AC_MODES[0]);
// 温度（C7C6C5C4，17~30℃，下标 0=17℃）：
//   17=0000, 18=0001, 19=0011, 20=0010, 21=0110, 22=0111, 23=0101, 24=0100,
//   25=1100, 26=1101, 27=1001, 28=1000, 29=1010, 30=1011
const uint8_t AC_TEMP_BITS[14] = {
  0x0, 0x1, 0x3, 0x2, 0x6, 0x7, 0x5, 0x4, 0xC, 0xD, 0x9, 0x8, 0xA, 0xB,
};

// ---------------- 空调附加功能（固定码，sendR05D 双帧发送）----------------
// 注：B9 系列的 A 字节是 B9 不是 B2，与已验证的 R05D 识别码不同，
//     照原样发送，实测后才知道空调认不认；熟睡码图片截断未补（待完整码）
struct AcFuncCode {
  const char *name;
  uint8_t a, b, c;
  const char *hex;
};
const AcFuncCode ACFUNC_CODES[] = {
  {"屏显切换", 0xB9, 0xF5, 0x09, "B9F509"},
  {"左右风开", 0xB9, 0xF5, 0x07, "B9F507"},
  {"左右风关", 0xB9, 0xF5, 0x08, "B9F508"},
  {"上下风开", 0xB9, 0xF5, 0x04, "B9F504"},
  {"上下风关", 0xB9, 0xF5, 0x05, "B9F505"},
  {"强劲开", 0xB9, 0xF5, 0x01, "B9F501"},
  {"强劲关", 0xB9, 0xF5, 0x02, "B9F502"},
  {"模式", 0xB2, 0xE3, 0x06, "B2E306"},
};
const uint8_t ACFUNC_COUNT = sizeof(ACFUNC_CODES) / sizeof(ACFUNC_CODES[0]);
