# ESP32-S2 mini 硬件测试 + 美的空调红外回放

CoreBoard ESP32-S2 mini 开发板的硬件测试程序，以及美的空调（COOLIX 协议）
红外遥控码的抓取与回放测试。每个版本一个文件夹，文件夹名与 .ino 文件名
一致（Arduino IDE 要求），`cn6.h` 须与 .ino 放在同一文件夹。

## 硬件与引脚（来自原理图）

| 功能 | 引脚 |
| --- | --- |
| ST7789 屏幕 | CS=IO5，DC=IO3，RST=IO1，MOSI=IO11，SCK=IO7，BL=IO2（高电平点亮），TE=IO12（不用） |
| 按键（按下=低电平，内部上拉） | KEY1=IO8，KEY2=IO10，KEY3=IO13，KEY4=IO14 |
| 指示灯（拉低点亮） | LED1=IO40，LED2=IO38 |
| 红外 | 接收 RX=IO16，发送 TX=IO17 |

屏幕：ST7789，竖屏 240x320，旋转 180 度（`setRotation(2)`）。

## 依赖库（Arduino IDE 库管理器安装）

- Adafruit GFX Library
- Adafruit ST7789 Library
- IRremoteESP8266（2.8 以上）

开发板选 ESP32S2 Dev Module；串口无输出时把 Tools > USB CDC On Boot 设为 Enabled。

## 按键分配（r8 最新）

| 按键 | 功能 |
| --- | --- |
| KEY1 | 开机双码：0xB21F94（COOLIX 24 位）+ 0xCAECDFEC（第二次发送，49 位），间隔约 140ms |
| KEY2 | 关机双码：0xB27BE0（COOLIX 24 位）+ 0x2CE182C8（第二次发送，49 位），间隔约 140ms |
| KEY3 | 关机 48 位老码：0xB27BE04D841F（原码 + 反码），连发 3 次 |
| KEY4 | 开机 48 位老码：0xB21FD44DE02B（原码 + 反码），连发 3 次 |

按键同时驱动 LED：KEY1~KEY4 对应值 0~3，LED1/LED2 显示二进制 00/01/10/11
（LED1 为高位，拉低点亮）。

## 版本历史

| 版本 | 改动 |
| --- | --- |
| r1 | 初版：屏幕颜色切换、按键 LED、红外收发 |
| r2 | 屏幕改为 240x320 竖屏 |
| r3 | 屏幕旋转 180 度 |
| r4 | 测试码改 0x20DF10EE，接收区两行显示（协议名 + hex） |
| r5 | 屏幕中文显示（cn16.h 16 点阵字模，25 字） |
| r6 | 全 GB2312 一二级字库 cn6.h（6763 字，约 225 KiB flash）+ 生成脚本 gen_cn6.py |
| r7 | 按键触发空调开关（KEY4 开机 / KEY3 关机，COOLIX 48 位），自动发送关闭 |
| r8 | KEY1/KEY2 发遥控器实测双码，KEY3/KEY4 保留 48 位老码 |
| IRRawCapture_r1 | 红外原始数据抓取工具：串口输出 sendRaw 可用的 raw 数组 |

## 字库说明

- `cn16.h`：仅含界面用到的 25 个字（r5）。
- `cn6.h`：GB2312 一级 3755 字 + 二级 3008 字，共 6763 字，16 点阵，
  由 Noto Sans SC Bold 生成，约 225 KiB flash（r6 起）。
- `gen_cn6.py`：字库生成脚本，需 Python 3 + pillow + fonttools + Noto Sans SC
  字体文件，详见脚本头注释。
- GB2312 以外的生僻字在屏幕上显示为方框。

## 红外说明（美的空调，遥控器 RB02D(2)）

- 遥控器每次按键发两个码：先 COOLIX 24 位码，约 140ms 后再发一次
  （第二次解为 UNKNOWN 49 位，数值是解码器通用阈值提取的，不一定
  等于真实帧，见 r8 注释）。
- 开机码随遥控器状态变化（曾抓到 B21FD4、B21F94），关机码多次
  稳定为 B27BE0。
- 码是完整状态码：B21FD4 开机实际显示 20°，不是单纯的开关命令；
  开/关码须成对取自同一遥控器状态。
- `ESP32S2_IRRawCapture_r1` 可抓取完整 raw 时序，用于 sendRaw 原样回放。
