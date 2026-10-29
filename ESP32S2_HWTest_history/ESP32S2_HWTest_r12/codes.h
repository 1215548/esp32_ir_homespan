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
  {"开机", 0xB2, 0xBF, 0xD0, "B2BFD0"},  // 制冷26度自动风（实测有效）
  {"关机", 0xB2, 0x7B, 0xE0, "B27BE0"},  // 目前无效（见空调子菜单提示）
};
const uint8_t AC_COUNT = sizeof(AC_CODES) / sizeof(AC_CODES[0]);
