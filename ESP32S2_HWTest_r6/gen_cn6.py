#!/usr/bin/env python3
# gen_cn6.py - 生成 16 点阵中文字库 cn6.h
#   GB2312 一级 3755 字 + 二级 3008 字，共 6763 字
#   字模来源：Noto Sans SC Bold（开源字体，OFL 协议）
#
# 使用方法：
#   1. 安装 Python 3 和依赖：pip install pillow fonttools
#   2. 下载 Noto Sans SC 字体，放到脚本同一目录，改名 NotoSansSC.ttf
#      下载地址：
#      https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf
#   3. 运行：python gen_cn6.py
#   4. 生成的 cn6.h 放到 Arduino sketch 文件夹，和 .ino 一起编译
#
# 原理：
#   按 GB2312 编码顺序逐个取双字节（高字节 0xB0~0xD7 是一级，
#   0xD8~0xF7 是二级，低字节 0xA1~0xFE），用 GB2312 解码成 Unicode
#   字符，再用字体把每个字画成 16x16 点阵（先 32px 粗体渲染，
#   再缩小到 16x16，阈值化成 1bit）。
from PIL import Image, ImageDraw, ImageFont

FONT = "NotoSansSC.ttf"   # 字体文件放脚本同一目录


def gb_chars(hi_lo, hi_hi):
    """按 GB2312 高字节范围取汉字，返回 Unicode 字符列表。"""
    out = []
    for hi in range(hi_lo, hi_hi + 1):
        for lo in range(0xA1, 0xFF):  # 低字节 0xA1~0xFE
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue  # 该位置在 GB2312 里是空位，跳过
            if len(ch) == 1:
                out.append(ch)
    return out


# 一级 16~55 区（高字节 0xB0~0xD7），二级 56~87 区（0xD8~0xF7）
l1 = gb_chars(0xB0, 0xD7)
l2 = gb_chars(0xD8, 0xF7)
print(f"level1={len(l1)} (first {l1[0]} U+{ord(l1[0]):04X}, "
      f"last {l1[-1]} U+{ord(l1[-1]):04X})")
print(f"level2={len(l2)} (first {l2[0]} U+{ord(l2[0]):04X}, "
      f"last {l2[-1]} U+{ord(l2[-1]):04X})")
assert len(l1) == 3755, len(l1)
assert len(l2) == 3008, len(l2)
chars = l1 + l2
assert len(set(chars)) == len(chars), "duplicate chars!"

# 检查字体是否覆盖全部汉字（缺字会在屏幕上显示成方框）
try:
    from fontTools.ttLib import TTFont
    cmap = TTFont(FONT).getBestCmap()
    missing = [ch for ch in chars if ord(ch) not in cmap]
    print(f"cmap missing: {len(missing)}",
          [f"{c} U+{ord(c):04X}" for c in missing[:20]])
except ImportError:
    print("fontTools not available, skip cmap check")

font = ImageFont.truetype(FONT, 32)
try:
    font.set_variation_by_axes([700])  # 可变字体设为粗体，笔画不断
    print("variation set to wght=700")
except Exception as e:
    print("variation failed (use regular weight):", e)

glyphs = {}
zero = []
for idx, ch in enumerate(chars):
    canvas = Image.new("L", (96, 96), 0)
    d = ImageDraw.Draw(canvas)
    d.text((32, 32), ch, font=font, fill=255)
    bbox = canvas.getbbox()
    if not bbox:
        zero.append(ch)
        glyphs[ch] = bytes(32)
        continue
    ink = canvas.crop(bbox)
    w, h = ink.size
    scale = min(16 / w, 16 / h)
    ink = ink.resize((max(1, round(w * scale)), max(1, round(h * scale))),
                     Image.LANCZOS)
    w, h = ink.size
    cell = Image.new("L", (16, 16), 0)
    cell.paste(ink, ((16 - w) // 2, (16 - h) // 2))
    px = cell.load()
    data = bytearray()
    for y in range(16):
        for bx in range(2):
            b = 0
            for bit in range(8):
                if px[bx * 8 + bit, y] > 110:
                    b |= 0x80 >> bit
            data.append(b)
    if not any(data):
        zero.append(ch)
    glyphs[ch] = bytes(data)
    if (idx + 1) % 2000 == 0:
        print(f"  rendered {idx + 1}/{len(chars)}")

print(f"empty glyphs: {len(zero)}",
      [f"{c} U+{ord(c):04X}" for c in zero[:20]])

lines = []
lines.append("// cn6.h - 16x16 bitmap font, ALL GB2312 level-1 + level-2 chars")
lines.append("// (3755 + 3008 = 6763 glyphs). Generated from Noto Sans SC Bold (OFL).")
lines.append("// Format: per glyph, 32 bytes, row-major, 2 bytes per row,")
lines.append("// MSB of each byte = leftmost pixel. 1 = ink.")
lines.append("// Flash cost: 6763 * 34 bytes (2-byte code + 32-byte bitmap)")
lines.append("//           = 229,942 bytes (~225 KiB), const data lives in flash.")
lines.append("// Put this file in the same folder as the .ino sketch.")
lines.append("#pragma once")
lines.append("#include <stdint.h>")
lines.append("")
lines.append("struct CN16Glyph {")
lines.append("  uint16_t code;      // Unicode code point")
lines.append("  uint8_t  bitmap[32];")
lines.append("};")
lines.append("")
lines.append("static const CN16Glyph CN16_FONT[] = {")
for ch in chars:
    data = glyphs[ch]
    hexs = ", ".join(f"0x{b:02X}" for b in data)
    lines.append(f"  {{ 0x{ord(ch):04X}, {{ {hexs} }} }},  // {ch}")
lines.append("};")
lines.append("")
lines.append("static const unsigned int CN16_FONT_COUNT = "
             "sizeof(CN16_FONT) / sizeof(CN16_FONT[0]);")
lines.append("")
with open("cn6.h", "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print("cn6.h written")
