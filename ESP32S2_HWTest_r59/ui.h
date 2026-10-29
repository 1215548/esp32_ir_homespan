//================================================================
// ui.h - 屏幕绘制（r59 菜单版，深色卡片风）
// 只负责把给定内容画到屏幕上，不含业务逻辑。
//
// 依赖（主文件须按此顺序准备好再 include 本文件）：
//   1. Adafruit_ST7789.h 已 include，tft 对象已定义；
//   2. TFT_W 已定义；
//   3. cn6.h 已 include（字库 CN16_FONT）；
//   4. codes.h 已 include（LAMP_CODES / AC_CODES 码表）。
// 屏幕 240x320，字体 16 点阵（中文 16px，ASCII size2 为 12px 宽）。
//
// 刷新方式：
//   全屏绘制（draw*Screen）用于状态切换、清除等内容全变的场合；
//   局部刷新用于单行变化：加减键导航只重画旧选中行与新选中行
//   （updateMenuRow / updateLampRow / updateAcRow；首页时钟模式只刷变化的行/卡）；
//   灯发送只刷底部“发送:”行（updateSendLine），空调发送/接收只刷底部卡片内
//   两行（updateAcSendLine / updateAcRecvLine），接收解码只刷底部“接收:”行
//   （updateRecvLine），不清全屏、不闪屏。
//   两类绘制共用同一行绘制函数与几何常量，不会错位。
//================================================================
#pragma once
#include <Arduino.h>

// ---------------- 标题栏（子菜单标题用）----------------
#define TITLE_H   24

// ---------------- 新主题（r44：深色卡片风，与定稿 mockup 一致）----------------
// RGB565（ST7789）：mockup 的 #0F172A / #1E293B / #22D3EE 等换算而来。
// 字体仍是 cn6.h 16 点阵（无 19px 档），标题用 16px，卡片圆角取 6（兼顾小行高）。
#define UI_BG      0x08A5  // 深海军底
#define UI_CARD    0x1947  // 卡片底
#define UI_BORDER  0x320A  // 卡片边框
#define UI_SEL     0x12EE  // 选中卡片底（深青）
#define UI_ACCENT  0x269D  // 青色点缀（选中边框、状态、滚动条滑块）
#define UI_AMBER   0xF4E1  // 琥珀（学习中徽章）
#define UI_GREEN   0x3693  // 绿（已连接）
#define UI_MUTED   0x9517  // 次要文字（提示、标签）
#define UI_DARKTXT 0x10C4  // 徽章深色字

// ---------------- 列表几何（全屏绘制与局部刷新共用，保持一致）----------------
// 卡片：x=8、宽 224（8..231），滚动条 x=232..235，文字区 x=16..216（200px，
// 最长灯行“辅助光源 0x01FE58A7”=196px，刚好放下）；行距=行高+间隙。
#define MENU_Y0   40
#define MENU_ROWH 28
#define MENU_ROWGAP 4   // 菜单 7 行：40 + 7*28 + 6*4 = 260，提示行 264/288 不变
#define MENU_WIN_ROWS 7
#define LAMP_Y0   28
#define LAMP_ROWH 22
#define LAMP_ROWGAP 2   // 灯 9 行：28 + 9*22 + 8*2 = 242，发送行 248 不变
#define AC_Y0     52    // 空调标题下加状态副标题行（y=28），列表从 52 起
#define AC_ROWH   28
#define AC_ROWGAP 3     // 空调 7 行：52 + 7*28 + 6*3 = 266，发送行 274 / 接收行 292
#define AC_WIN_ROWS 7

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

// 一行文字截到指定像素宽内（卡片内文用；ASCII 12px、汉字 16px）
String truncToW(const String &s, int16_t maxW) {
  String out;
  int w = 0;
  for (unsigned int i = 0; i < s.length();) {
    uint8_t c = (uint8_t)s[i];
    int cw, clen;
    if (c < 0x80) { cw = 12; clen = 1; }
    else { cw = 16; clen = 3; }
    if (w + cw > maxW) break;
    out += s.substring(i, i + clen);
    w += cw;
    i += clen;
  }
  return out;
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
// 标题栏（新主题）：底色 + 左侧青色竖条 + 白字标题（TITLE_H=24 不变）
void uiTitle(const char *title) {
  tft.fillRect(0, 0, TFT_W, TITLE_H, UI_BG);
  tft.fillRect(8, 8, 4, 16, UI_ACCENT);
  printCN(18, 8, title, ST77XX_WHITE, UI_BG, 1);
}

// 单行绘制（全屏列表与局部刷新共用）：圆角卡片 + 右侧 chevron。
// 选中：深青底 + 青色边框；未选中：卡片底 + 深灰边框；文字均为白字
// （16 点阵无粗体，选中靠卡片区分）。文字背景须与卡片底一致，否则字块发黑。
void uiListRow(const char *name, const char *hex, int16_t y, int16_t rowH, bool isSel) {
  uint16_t fill = isSel ? UI_SEL : UI_CARD;
  uint16_t border = isSel ? UI_ACCENT : UI_BORDER;
  tft.fillRoundRect(8, y, 224, rowH, 6, fill);
  tft.drawRoundRect(8, y, 224, rowH, 6, border);
  String line = name;
  if (hex && hex[0]) { line += " "; line += hex; }
  int16_t ty = y + (rowH - 16) / 2;
  printCN(16, ty, line.c_str(), ST77XX_WHITE, fill, 1);
  uint16_t cc = isSel ? UI_ACCENT : UI_MUTED;
  tft.drawLine(216, ty + 2, 226, ty + 8, cc);
  tft.drawLine(226, ty + 8, 216, ty + 14, cc);
}

// 列表：col1 为名，col2 为码串（可为 NULL，只显示名）；行距=行高+间隙。
void uiList(const char *const *col1, const char *const *col2,
            uint8_t count, uint8_t sel, int16_t y0, int16_t rowH, int16_t gap) {
  for (uint8_t i = 0; i < count; i++) {
    uiListRow(col1[i], col2 ? col2[i] : NULL, y0 + i * (rowH + gap), rowH, i == sel);
  }
}

// ---------------- 首页 ----------------
// mode 0=无 WiFi（Esp32-IR），1=时钟，2=已联网但 NTP 未同步
// 新布局（r49）：状态栏 y=8，时钟 y=42（2 号字），日历月份 y=88、星期 y=110、
// 日期 y=132、农历 y=152（仅时钟模式，关掉的行留空不重排），年中天数 y=174，
// 温压卡 y=206..270，提示 y=303；显示设置掩码：bit0=时间，bit1=年/月，bit2=第几天，
//   bit3=日历，bit4=农历，默认 31（全显示）；
// 三种模式温压卡位置相同。首页不显示 SSID（状态栏只显示 WiFi/未连接）。
// 首页温度卡（8,206,108,64）：温度计图标 + “温度”标签 + 数值行
// bmpTemp 形如“温度 25.3度”（refreshBmpStrings 固定格式），按空格拆出数值
void drawHomeTempCard(const String &bmpTemp) {
  tft.fillRoundRect(8, 206, 108, 64, 6, UI_CARD);
  tft.drawRoundRect(8, 206, 108, 64, 6, UI_BORDER);
  tft.drawRoundRect(18, 214, 5, 20, 2, UI_ACCENT);  // 温度计管
  tft.drawCircle(20, 236, 6, UI_ACCENT);            // 温度计泡
  printCN(30, 214, "温度", UI_MUTED, UI_CARD, 1);
  String v = bmpTemp;
  int sp = v.indexOf(' ');
  if (sp > 0) v = v.substring(sp + 1);
  printCN(16, 240, truncToW(v, 92).c_str(), ST77XX_WHITE, UI_CARD, 1);
}

// 首页气压卡（124,206,108,64）：表盘图标 + “气压hPa”标签 + 数值行
// bmpPress 形如“气压 1013.2hPa”，数值去 “hPa” 后缀（单位放标签里），
// 否则 120px 塞不进 92px 内宽
void drawHomePressCard(const String &bmpPress) {
  tft.fillRoundRect(124, 206, 108, 64, 6, UI_CARD);
  tft.drawRoundRect(124, 206, 108, 64, 6, UI_BORDER);
  tft.drawCircle(140, 224, 10, UI_ACCENT);  // 表盘
  tft.drawLine(140, 224, 148, 216, UI_ACCENT);  // 指针
  tft.fillCircle(140, 224, 2, UI_ACCENT);  // 轴心
  printCN(156, 214, "气压hPa", UI_MUTED, UI_CARD, 1);
  String v = bmpPress;
  int sp = v.indexOf(' ');
  if (sp > 0) v = v.substring(sp + 1);
  if (v.endsWith("hPa")) v = v.substring(0, v.length() - 3);
  printCN(132, 240, truncToW(v, 92).c_str(), ST77XX_WHITE, UI_CARD, 1);
}

// 首页底部温压区：有气压串时两张并排卡，无气压串（BMP 未连接）时一张通栏卡
void drawHomeBmpCards(const String &bmpTemp, const String &bmpPress) {
  if (bmpPress.length() == 0) {
    tft.fillRoundRect(8, 206, 224, 64, 6, UI_CARD);
    tft.drawRoundRect(8, 206, 224, 64, 6, UI_BORDER);
    String s = truncToW(bmpTemp, 208);
    int16_t w = cnTextWidth(s.c_str(), 1);
    printCN((TFT_W - w) / 2, 230, s.c_str(), UI_MUTED, UI_CARD, 1);
  } else {
    drawHomeTempCard(bmpTemp);
    drawHomePressCard(bmpPress);
  }
}

// 首页一周日历（仅时钟模式）：月份标题 + 星期行 + 日期行 + 农历行，每列 32px（x=8+i*32）
// 今天所在的星期与日期用青色标出；农历是当天单值（API 返回的月名+日名，如“九月二十”），
//   居中画在 y=152，不再是每列的日名。各行是否画出由显示设置掩码 mask 决定：
//   bit1=年/月，bit3=日历（星期+日期），bit4=农历（mask 由主文件 dispMask 传入）
void drawHomeCalendar(const String &calMonth, const int *calDates, int calTodayIdx, const String &lunarStr, uint8_t mask) {
  if (mask & 0x02) {  // 年/月
    int16_t mw = cnTextWidth(calMonth.c_str(), 1);
    printCN((TFT_W - mw) / 2, 88, calMonth.c_str(), ST77XX_WHITE, UI_BG, 1);
  }
  if (mask & 0x08) {  // 日历（星期 + 日期）
    const char *wdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (uint8_t i = 0; i < 7; i++) {
      int16_t x = 8 + i * 32;
      bool today = (i == calTodayIdx);
      printCN(x + 8, 110, wdays[i], today ? UI_ACCENT : UI_MUTED, UI_BG, 1);
      String d = String(calDates[i]);
      int16_t dw = cnTextWidth(d.c_str(), 1);
      printCN(x + (32 - dw) / 2, 132, d.c_str(), today ? UI_ACCENT : ST77XX_WHITE, UI_BG, 1);
    }
  }
  if ((mask & 0x10) && lunarStr.length() > 0) {  // 农历（当天月+日，青色居中；未取到时留空）
    int16_t lw = cnTextWidth(lunarStr.c_str(), 1);
    printCN((TFT_W - lw) / 2, 152, lunarStr.c_str(), UI_ACCENT, UI_BG, 1);
  }
}

// 开机动画（约 2s，setup 里 TFT 初始化后调用）：深底 + 标题 + 进度条
void bootAnimation() {
  tft.fillScreen(UI_BG);
  const char *title = "ESP32-S2-IR";
  int16_t tw = cnTextWidth(title, 2);
  printCN((TFT_W - tw) / 2, 100, title, ST77XX_WHITE, UI_BG, 2);
  const char *sub = "硬件测试 r59";
  int16_t sw = cnTextWidth(sub, 1);
  printCN((TFT_W - sw) / 2, 150, sub, UI_MUTED, UI_BG, 1);
  tft.drawRoundRect(40, 180, 160, 12, 6, UI_BORDER);
  for (uint8_t i = 1; i <= 10; i++) {
    tft.fillRoundRect(42, 182, (uint16_t)(156 * i / 10), 8, 4, UI_ACCENT);
    delay(150);
  }
  delay(200);
}

void drawHomeScreen(uint8_t mode, const String &clock, const String &yday,
                    const String &bmpTemp, const String &bmpPress,
                    const String &calMonth, const int *calDates, int calTodayIdx, const String &lunarStr, uint8_t mask) {
  tft.fillScreen(UI_BG);
  // 状态栏：左设备名，右连接状态（绿点 + WiFi / 未连接；只显示状态，不显示 SSID）
  printCN(8, 8, "ESP32-S2-IR", UI_MUTED, UI_BG, 1);
  bool conn = (mode == 1 || mode == 2);
  String rs = conn ? "WiFi" : "未连接";
  int16_t rw = cnTextWidth(rs.c_str(), 1);
  int16_t rx = TFT_W - 8 - rw;
  if (conn) tft.fillCircle(rx - 10, 16, 4, UI_GREEN);
  printCN(rx, 8, rs.c_str(), conn ? ST77XX_WHITE : UI_MUTED, UI_BG, 1);
  if (mode == 1) {
    if (mask & 0x01) {  // 时间
      int16_t cw = cnTextWidth(clock.c_str(), 2);
      printCN((TFT_W - cw) / 2, 42, clock.c_str(), ST77XX_WHITE, UI_BG, 2);
    }
    drawHomeCalendar(calMonth, calDates, calTodayIdx, lunarStr, mask);
    if (mask & 0x04) {  // 今天是一年中的第几天
      int16_t yw = cnTextWidth(yday.c_str(), 1);
      printCN((TFT_W - yw) / 2, 174, yday.c_str(), ST77XX_WHITE, UI_BG, 1);
    }
  } else if (mode == 2) {
    String s = "时间同步中...";
    int16_t sw = cnTextWidth(s.c_str(), 1);
    printCN((TFT_W - sw) / 2, 103, s.c_str(), ST77XX_WHITE, UI_BG, 1);
  } else {
    int16_t tw = cnTextWidth("Esp32-IR", 2);
    printCN((TFT_W - tw) / 2, 103, "Esp32-IR", ST77XX_WHITE, UI_BG, 2);
    String s = "进 WiFi设置 页联网显示时间";
    int16_t sw = cnTextWidth(s.c_str(), 1);
    printCN((TFT_W - sw) / 2, 124, s.c_str(), UI_MUTED, UI_BG, 1);
  }
  drawHomeBmpCards(bmpTemp, bmpPress);
  printCN(8, 303, "KEY4 进入菜单", UI_MUTED, UI_BG, 1);
}

// ---------------- 一级菜单 ----------------
const char *MENU_ITEMS[] = {"灯", "空调", "红外解码", "红外测试", "WiFi设置", "I2C寻址", "红外复制", "HomeSpan", "显示设置", "关于", "芯片详情", "返回"};
const uint8_t MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

// 一级菜单滚动条（x=232..235；项数不多于可见行时不画，免除零分母）
void drawMenuScrollbar(uint8_t top) {
  if (MENU_COUNT <= MENU_WIN_ROWS) return;
  int16_t listH = MENU_WIN_ROWS * MENU_ROWH + (MENU_WIN_ROWS - 1) * MENU_ROWGAP;  // 220
  tft.fillRect(232, MENU_Y0, 4, listH, UI_CARD);  // 轨道
  uint8_t thumbH = listH * MENU_WIN_ROWS / MENU_COUNT;  // 220*7/12=128
  uint8_t thumbY = MENU_Y0 + top * (listH - thumbH) / (MENU_COUNT - MENU_WIN_ROWS);
  tft.fillRect(232, thumbY, 4, thumbH, UI_ACCENT);  // 滑块
}

// 一级菜单单行局部刷新（加减键导航时只刷旧行与新行，顺带重画滚动条）
void updateMenuRow(uint8_t idx, uint8_t top, bool isSel) {
  uiListRow(MENU_ITEMS[idx], NULL, MENU_Y0 + (idx - top) * (MENU_ROWH + MENU_ROWGAP), MENU_ROWH, isSel);
  drawMenuScrollbar(top);
}

// 一级菜单窗口重画（滚动时刷整个可见窗口，顺带重画滚动条）
void updateMenuListWindow(uint8_t sel, uint8_t top) {
  for (uint8_t r = 0; r < MENU_WIN_ROWS && top + r < MENU_COUNT; r++) {
    uint8_t idx = top + r;
    uiListRow(MENU_ITEMS[idx], NULL, MENU_Y0 + r * (MENU_ROWH + MENU_ROWGAP), MENU_ROWH, idx == sel);
  }
  drawMenuScrollbar(top);
}

void drawMenuScreen(uint8_t sel, uint8_t top) {
  tft.fillScreen(UI_BG);
  uiTitle("一级菜单");
  updateMenuListWindow(sel, top);
  printCN(8, 264, "KEY2 下 KEY3 上", UI_MUTED, UI_BG, 1);
  printCN(8, 288, "KEY1 进入 KEY4 返回", UI_MUTED, UI_BG, 1);
}

// ---------------- 显示设置 ----------------
// 5 项（顺序即掩码位序）：时间 bit0、年/月 bit1、今天是一年中的第几天 bit2、
//   日历 bit3、农历 bit4；默认掩码 31（全显示），存 NVS（disp/mask）。
// 行卡片与一级菜单同款（选中深青底+青边），右侧是开关状态（开青色/关灰色），无 chevron。
const char *DISP_ITEMS[] = {"时间", "年/月", "今天是一年中的第几天", "日历", "农历"};
const uint8_t DISP_COUNT = sizeof(DISP_ITEMS) / sizeof(DISP_ITEMS[0]);
// 显示设置单行局部刷新（选择移动/开关切换时只刷该行，不清屏不重画标题；
//   5 行全在屏内无滚动，KEY2/KEY3 只刷旧行（取消选中）+ 新行（选中），与一级菜单同款）
void updateDispRow(uint8_t idx, uint8_t mask, bool isSel) {
  int16_t y = MENU_Y0 + idx * (MENU_ROWH + MENU_ROWGAP);
  bool on = (mask & (1 << idx)) != 0;
  uint16_t fill = isSel ? UI_SEL : UI_CARD;
  uint16_t border = isSel ? UI_ACCENT : UI_BORDER;
  tft.fillRoundRect(8, y, 224, MENU_ROWH, 6, fill);
  tft.drawRoundRect(8, y, 224, MENU_ROWH, 6, border);
  int16_t ty = y + (MENU_ROWH - 16) / 2;
  printCN(16, ty, DISP_ITEMS[idx], ST77XX_WHITE, fill, 1);
  const char *st = on ? "开" : "关";
  int16_t sw = cnTextWidth(st, 1);
  printCN(216 - sw, ty, st, on ? UI_ACCENT : UI_MUTED, fill, 1);
}
void drawDispScreen(uint8_t sel, uint8_t mask) {
  tft.fillScreen(UI_BG);
  uiTitle("显示设置");
  for (uint8_t i = 0; i < DISP_COUNT; i++) updateDispRow(i, mask, i == sel);
  printCN(8, 264, "KEY2 下 KEY3 上", UI_MUTED, UI_BG, 1);
  printCN(8, 288, "KEY1 开关 KEY4 返回", UI_MUTED, UI_BG, 1);
}

// 灯子菜单底部两行局部刷新（发送/接收时只刷对应行，不全屏；底色 UI_BG）
void updateSendLine(const String &sent) {
  tft.fillRect(0, 248, TFT_W, 16, UI_BG);
  printCN(8, 248, ("发送: " + sent).c_str(), ST77XX_WHITE, UI_BG, 1);
}

void updateRecvLine(const String &recv) {
  tft.fillRect(0, 272, TFT_W, 16, UI_BG);
  printCN(8, 272, ("接收: " + recv).c_str(), ST77XX_WHITE, UI_BG, 1);
}

// 空调页底部两行局部刷新（在底部卡片内刷，只擦卡片内宽，不碰卡片边框；
// 接收行超长时按卡片内宽 208 截断，完整值在串口）
void updateAcSendLine(const String &sent) {
  tft.fillRect(16, 274, 208, 16, UI_CARD);
  printCN(16, 274, ("发送: " + sent).c_str(), ST77XX_WHITE, UI_CARD, 1);
}

void updateAcRecvLine(const String &recv) {
  tft.fillRect(16, 292, 208, 16, UI_CARD);
  String s = "接收: " + recv;
  printCN(16, 292, truncToW(s, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
}

// ---------------- 灯子菜单 ----------------
// 列表 9 行（卡片高 22 + 间隙 2），底部三行：发送 / 接收 / 按键提示
void drawLampScreen(uint8_t sel, const String &sent, const String &recv) {
  tft.fillScreen(UI_BG);
  uiTitle("灯");
  const char *names[16];
  const char *hexes[16];
  for (uint8_t i = 0; i < LAMP_COUNT; i++) {
    names[i] = LAMP_CODES[i].name;
    hexes[i] = LAMP_CODES[i].hex;
  }
  uiList(names, hexes, LAMP_COUNT, sel, LAMP_Y0, LAMP_ROWH, LAMP_ROWGAP);
  updateSendLine(sent);
  updateRecvLine(recv);
  printCN(8, 296, "KEY1 发送 KEY4 返回", UI_MUTED, UI_BG, 1);
}

// 灯列表单行局部刷新（加减键导航时只刷旧行与新行）
void updateLampRow(uint8_t idx, bool isSel) {
  uiListRow(LAMP_CODES[idx].name, LAMP_CODES[idx].hex,
            LAMP_Y0 + idx * (LAMP_ROWH + LAMP_ROWGAP), LAMP_ROWH, isSel);
}

// ---------------- 空调子菜单 ----------------
// 菜单：0=开机，1=关机（码表），2~6=控制项（无固定码），7~=附加功能固定码；
// 可见 7 行（卡片高 28 + 间隙 3，从 y=52 起），状态作标题下副标题（青色，y=28），
// 底部卡片内发送（y=274）/ 接收（y=292）两行；本页不画按键提示（键位与菜单一致）
const char *AC_CTRL_NAMES[] = {"温度+", "温度-", "模式循环", "风速+", "风速-"};
const uint8_t AC_CTRL_COUNT = 5;
const uint8_t AC_MENU_COUNT = AC_COUNT + AC_CTRL_COUNT + ACFUNC_COUNT;  // 15 项
void acRowText(uint8_t idx, const char *&name, const char *&hex) {
  if (idx < AC_COUNT) { name = AC_CODES[idx].name; hex = AC_CODES[idx].hex; }
  else if (idx < AC_COUNT + AC_CTRL_COUNT) { name = AC_CTRL_NAMES[idx - AC_COUNT]; hex = ""; }
  else { uint8_t f = idx - AC_COUNT - AC_CTRL_COUNT; name = ACFUNC_CODES[f].name; hex = ACFUNC_CODES[f].hex; }
}

// 空调列表滚动条（x=232..235，滑块位置反映窗口顶行）
void drawAcScrollbar(uint8_t top) {
  int16_t listH = AC_WIN_ROWS * AC_ROWH + (AC_WIN_ROWS - 1) * AC_ROWGAP;  // 214
  tft.fillRect(232, AC_Y0, 4, listH, UI_CARD);  // 轨道
  uint8_t thumbH = listH * AC_WIN_ROWS / AC_MENU_COUNT;  // 214*7/15=99
  uint8_t thumbY = AC_Y0 + top * (listH - thumbH) / (AC_MENU_COUNT - AC_WIN_ROWS);
  tft.fillRect(232, thumbY, 4, thumbH, UI_ACCENT);  // 滑块
}

// 空调状态副标题局部刷新（控制项执行后只刷标题下这一行，青色；调用点不变）
void updateAcStatusLine(const String &status) {
  tft.fillRect(0, 28, TFT_W, 16, UI_BG);
  printCN(8, 28, status.c_str(), UI_ACCENT, UI_BG, 1);
}

// 空调列表单行局部刷新（加减键导航时只刷旧行与新行，顺带重画滚动条）
void updateAcRow(uint8_t idx, uint8_t top, bool isSel) {
  const char *name, *hex;
  acRowText(idx, name, hex);
  uiListRow(name, hex, AC_Y0 + (idx - top) * (AC_ROWH + AC_ROWGAP), AC_ROWH, isSel);
  drawAcScrollbar(top);
}

// 空调列表窗口重画（滚动时刷整个可见窗口，顺带重画滚动条）
void updateAcListWindow(uint8_t sel, uint8_t top) {
  for (uint8_t r = 0; r < AC_WIN_ROWS; r++) {
    uint8_t idx = top + r;
    const char *name, *hex;
    acRowText(idx, name, hex);
    uiListRow(name, hex, AC_Y0 + r * (AC_ROWH + AC_ROWGAP), AC_ROWH, idx == sel);
  }
  drawAcScrollbar(top);
}

void drawAcScreen(uint8_t sel, uint8_t top, const String &sent, const String &recv, const String &status) {
  tft.fillScreen(UI_BG);
  uiTitle("空调");
  printCN(8, 28, status.c_str(), UI_ACCENT, UI_BG, 1);  // 状态副标题
  updateAcListWindow(sel, top);
  tft.fillRoundRect(8, 268, 224, 44, 6, UI_CARD);  // 底部发送/接收卡
  tft.drawRoundRect(8, 268, 224, 44, 6, UI_BORDER);
  updateAcSendLine(sent);
  updateAcRecvLine(recv);
}



// ---------------- 红外解码子菜单 ----------------
void drawDecodeScreen(const String &proto, const String &hex,
                      uint16_t bits, uint16_t rawLen) {
  tft.fillScreen(UI_BG);
  uiTitle("红外解码");
  tft.fillRoundRect(8, 40, 224, 112, 6, UI_CARD);
  tft.drawRoundRect(8, 40, 224, 112, 6, UI_BORDER);
  printCN(16, 48, truncToW("协议: " + proto, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 72, truncToW("数值: " + hex, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  String bitsLine = "位数: ";
  if (bits == 0) bitsLine += "--";
  else { bitsLine += String(bits); bitsLine += " Bits"; }
  printCN(16, 96, bitsLine.c_str(), ST77XX_WHITE, UI_CARD, 1);
  String rawLine = "原始: ";
  if (rawLen == 0) rawLine += "--";
  else { rawLine += String(rawLen); rawLine += "项"; }
  printCN(16, 120, rawLine.c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(8, 164, "对着板子按遥控器", UI_MUTED, UI_BG, 1);
  printCN(8, 288, "KEY1 清除 KEY4 返回", UI_MUTED, UI_BG, 1);
}

// ---------------- 红外测试子菜单 ----------------
// 3 秒亮 / 3 秒灭循环发 38kHz 载波；亮相位时中间显示黄色块同步指示
void drawIrTestScreen(bool running, bool onPhase) {
  tft.fillScreen(UI_BG);
  uiTitle("红外测试");
  tft.fillRoundRect(8, 40, 224, 124, 6, UI_CARD);
  tft.drawRoundRect(8, 40, 224, 124, 6, UI_BORDER);
  printCN(16, 48, running ? "状态: 运行中" : "状态: 已暂停",
          ST77XX_WHITE, UI_CARD, 1);
  String phase = "--";
  if (running) phase = onPhase ? "亮" : "灭";
  printCN(16, 76, ("当前: " + phase).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 104, "3秒亮 3秒灭 循环", ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 132, "38kHz 载波", ST77XX_WHITE, UI_CARD, 1);
  if (running && onPhase) {
    tft.fillRect(20, 180, TFT_W - 40, 60, ST77XX_YELLOW);
  } else {
    tft.fillRect(20, 180, TFT_W - 40, 60, UI_BG);
    tft.drawRect(20, 180, TFT_W - 40, 60, UI_BORDER);
  }
  printCN(8, 264, "KEY1 开始/暂停", UI_MUTED, UI_BG, 1);
  printCN(8, 288, "KEY4 返回", UI_MUTED, UI_BG, 1);
}

// I2C 寻址页（r38）：未扫显示提示，扫后显示地址列表（每行一个 0xXX，
// 最多 8 行；超过 8 个时前 7 个 + "…"，标题行显示总数）
void drawI2cScreen(bool scanned, const uint8_t *addrs, uint8_t count) {
  tft.fillScreen(UI_BG);
  uiTitle("I2C寻址");
  if (!scanned) {
    printCN(8, 40, "按 KEY1 开始寻址", ST77XX_WHITE, UI_BG, 1);
  } else if (count == 0) {
    printCN(8, 40, "未发现设备", ST77XX_WHITE, UI_BG, 1);
  } else {
    printCN(8, 40, (String("发现 ") + count + " 个设备").c_str(), ST77XX_WHITE, UI_BG, 1);
  }
  printCN(8, 64, "SDA IO33 SCL IO35", UI_MUTED, UI_BG, 1);
  uint8_t show = count;
  if (show > 8) show = 7;
  if (show > 0) {  // 有设备时地址列表放一张卡里；未扫/无设备时只显示状态行
    tft.fillRoundRect(8, 84, 224, 208, 6, UI_CARD);
    tft.drawRoundRect(8, 84, 224, 208, 6, UI_BORDER);
  }
  const char *hexd = "0123456789ABCDEF";
  for (uint8_t i = 0; i < show; i++) {
    char abuf[8];
    abuf[0] = '0'; abuf[1] = 'x';
    abuf[2] = hexd[addrs[i] >> 4];
    abuf[3] = hexd[addrs[i] & 0x0F];
    abuf[4] = '\0';
    printCN(16, 92 + i * 24, abuf, ST77XX_WHITE, UI_CARD, 1);
  }
  if (count > 8) printCN(16, 92 + 7 * 24, "...", ST77XX_WHITE, UI_CARD, 1);
  printCN(8, 296, "KEY1 寻址 KEY4 返回", UI_MUTED, UI_BG, 1);
}

// 一行文字截到屏宽内（ASCII 12px、汉字 16px，留 8px 左边距；长 SSID 用）
String truncLine(const String &s) {
  String out;
  int w = 0;
  for (unsigned int i = 0; i < s.length();) {
    uint8_t c = (uint8_t)s[i];
    int cw, clen;
    if (c < 0x80) { cw = 12; clen = 1; }
    else { cw = 16; clen = 3; }  // 汉字 UTF-8 占 3 字节
    if (w + cw + 8 > TFT_W) break;
    out += s.substring(i, i + clen);
    w += cw;
    i += clen;
  }
  return out;
}

// ---------------- WiFi 设置页 ----------------
// 状态/网络/IP/两行提示 + 底部按键提示（全屏重画，状态变化时由主文件置 screenDirty）
void drawWifiScreen(const String &status, const String &ssid, const String &ip, const String &tip1, const String &tip2, const String &webLine, const String &access) {
  tft.fillScreen(UI_BG);
  uiTitle("WiFi设置");
  tft.fillRoundRect(8, 32, 224, 152, 6, UI_CARD);
  tft.drawRoundRect(8, 32, 224, 152, 6, UI_BORDER);
  printCN(16, 40, truncToW(status, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 64, truncToW(ssid, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 88, truncToW(ip, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 112, truncToW(tip1, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  if (tip2.length()) printCN(16, 136, truncToW(tip2, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  if (webLine.length()) printCN(16, 160, truncToW(webLine, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  // 访问地址（仅 STA 已连 WiFi 时有值）：STA 地址较长，卡内 208px 会截断错地址，
  //   故放卡外整宽显示（不截断）；热点模式的地址已在卡内 tip2 显示，这里不重复
  if (access.length()) {
    printCN(8, 200, "浏览器访问:", UI_MUTED, UI_BG, 1);
    printCN(8, 224, access.c_str(), ST77XX_WHITE, UI_BG, 1);
  }
  printCN(8, 272, "KEY1 网页 KEY2 热点", UI_MUTED, UI_BG, 1);
  printCN(8, 296, "KEY4 返回", UI_MUTED, UI_BG, 1);
}

// ---------------- 关于子菜单 ----------------
void drawAboutScreen(const String &build) {
  tft.fillScreen(UI_BG);
  uiTitle("关于");
  tft.fillRoundRect(8, 32, 224, 152, 6, UI_CARD);
  tft.drawRoundRect(8, 32, 224, 152, 6, UI_BORDER);
  printCN(16, 40, "ESP32-S2 硬件测试 r59", ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 68, "红外接收 IO16 发送 IO17", ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 96, ("编译" + build).c_str(), ST77XX_WHITE, UI_CARD, 1);  // "编译26/10/29:E" 152px，卡内 208px 放得下
  printCN(16, 124, "KEY4 菜单 KEY1 确认", ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 152, "KEY2 加 KEY3 减", ST77XX_WHITE, UI_CARD, 1);
  // 仓库地址：完整 URL 共 41 字放不下一行，拆两行卡外整宽显示（https:// 前缀省略，浏览器里补上即可）
  printCN(8, 200, "GitHub 仓库:", UI_MUTED, UI_BG, 1);
  printCN(8, 224, "github.com/1215548", ST77XX_WHITE, UI_BG, 1);
  printCN(8, 248, "esp32_ir_homespan", ST77XX_WHITE, UI_BG, 1);
  printCN(8, 288, "KEY4 返回", UI_MUTED, UI_BG, 1);
}

// 芯片详情页（r55）：卡片 6 行（芯片型号 / RAM 已用总量 / RAM 占用率 / ROM 已用总量 / ROM 占用率 / 芯片温度）；
//   drawChipCard 只刷卡片（1 秒刷新用，不清屏不重画标题，免闪），drawChipScreen 整页（进页时用）
void drawChipCard(const String &chip, const String &ram, const String &ramPct, const String &rom, const String &romPct, const String &temp) {
  tft.fillRoundRect(8, 32, 224, 176, 6, UI_CARD);
  tft.drawRoundRect(8, 32, 224, 176, 6, UI_BORDER);
  printCN(16, 40, chip.c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 68, ram.c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 96, ramPct.c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 124, rom.c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 152, romPct.c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 180, temp.c_str(), ST77XX_WHITE, UI_CARD, 1);
}
void drawChipScreen(const String &chip, const String &ram, const String &ramPct, const String &rom, const String &romPct, const String &temp) {
  tft.fillScreen(UI_BG);
  uiTitle("芯片详情");
  drawChipCard(chip, ram, ramPct, rom, romPct, temp);
  printCN(8, 288, "KEY4 返回", UI_MUTED, UI_BG, 1);
}

// HomeSpan 页（r42）：只显示当前配对码（已按 466-37-726 格式分段），
// 改码走网页表单（重启后确保生效），本页不改码。
void drawHsScreen(const String &code) {
  tft.fillScreen(UI_BG);
  uiTitle("HomeSpan");
  tft.fillRoundRect(8, 40, 224, 64, 6, UI_CARD);
  tft.drawRoundRect(8, 40, 224, 64, 6, UI_BORDER);
  printCN(16, 52, truncToW("配对码 " + code, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(16, 76, "重新添加配件时输入", UI_MUTED, UI_CARD, 1);
  printCN(8, 296, "KEY4 返回", UI_MUTED, UI_BG, 1);
}

// 红外复制页（r40）：5 行 + 3 行提示（全屏重画，状态变化时由主文件置 screenDirty；
// 各行先截到屏宽，长名称/长协议键码不溢出）
// 红外复制页（r44 卡片风，mockup 布局）：编号卡（含模式徽章）+ 状态行
// （青色）+ 已存卡 + 学习卡（学习中时青色边框）+ 3 行提示（素字）。
// 各串格式由 refreshCopyLines 固定：“编号 N [名字]”“模式 学习中/空闲”
// “已存 …”“学习 …”，按前缀拆出卡内标题与值；格式不对时整串显示，不丢信息。
void drawCopyScreen(const String &slot, const String &mode, const String &status,
                    const String &stored, const String &learned) {
  tft.fillScreen(UI_BG);
  uiTitle("红外复制");
  // 编号卡（8,40,224,54）：大“编号 N”+ 名字（有则显示）+ 右侧模式徽章
  tft.fillRoundRect(8, 40, 224, 54, 6, UI_CARD);
  tft.drawRoundRect(8, 40, 224, 54, 6, UI_BORDER);
  String numPart = slot, namePart = "";
  if (slot.startsWith("编号 ")) {
    String rest = slot.substring(7);  // “编号 ”= 编3 + 号3 + 空格1 = 7 字节
    int sp = rest.indexOf(' ');
    if (sp > 0) { numPart = "编号 " + rest.substring(0, sp); namePart = rest.substring(sp + 1); }
    else numPart = "编号 " + rest;
  }
  if (namePart.length()) {
    printCN(16, 48, truncToW(numPart, 140).c_str(), ST77XX_WHITE, UI_CARD, 1);
    printCN(16, 70, truncToW(namePart, 140).c_str(), UI_MUTED, UI_CARD, 1);
  } else {
    printCN(16, 59, truncToW(numPart, 140).c_str(), ST77XX_WHITE, UI_CARD, 1);
  }
  bool learning = (mode.indexOf("学习中") >= 0);
  String badge = learning ? "学习中" : "空闲";
  int16_t bw = (badge == "学习中") ? 48 : 32;  // 3 字 48px，2 字 32px
  int16_t bx = 232 - 8 - 60;  // 徽章宽 60，右贴卡片内边
  if (learning) {
    tft.fillRoundRect(bx, 54, 60, 20, 10, UI_AMBER);
    printCN(bx + (60 - bw) / 2, 56, badge.c_str(), UI_DARKTXT, UI_AMBER, 1);
  } else {
    tft.fillRoundRect(bx, 54, 60, 20, 10, UI_CARD);
    tft.drawRoundRect(bx, 54, 60, 20, 10, UI_BORDER);
    printCN(bx + (60 - bw) / 2, 56, badge.c_str(), UI_MUTED, UI_CARD, 1);
  }
  // 状态行（青色，素字）
  printCN(8, 104, truncLine(status).c_str(), UI_ACCENT, UI_BG, 1);
  // 已存卡（8,128,224,50）：标签 + 值（去“已存 ”前缀）
  tft.fillRoundRect(8, 128, 224, 50, 6, UI_CARD);
  tft.drawRoundRect(8, 128, 224, 50, 6, UI_BORDER);
  printCN(16, 134, "已存", UI_MUTED, UI_CARD, 1);
  String sv = stored;
  if (sv.startsWith("已存 ")) sv = sv.substring(7);
  printCN(16, 154, truncToW(sv, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  // 学习卡（8,186,224,50）：学习中时青色边框突出
  tft.fillRoundRect(8, 186, 224, 50, 6, UI_CARD);
  tft.drawRoundRect(8, 186, 224, 50, 6, learning ? UI_ACCENT : UI_BORDER);
  printCN(16, 192, "学习", UI_MUTED, UI_CARD, 1);
  String lv = learned;
  if (lv.startsWith("学习 ")) lv = lv.substring(7);
  printCN(16, 212, truncToW(lv, 208).c_str(), ST77XX_WHITE, UI_CARD, 1);
  printCN(8, 250, "KEY1 学习/存储", UI_MUTED, UI_BG, 1);
  printCN(8, 274, "KEY2 发射 KEY3 选编号", UI_MUTED, UI_BG, 1);
  printCN(8, 298, "KEY4 返回", UI_MUTED, UI_BG, 1);
}
