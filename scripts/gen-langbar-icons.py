#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成语言栏（输入法工具栏）图标。

背景：语言栏的图标不是 Qt 画的，而是 TIP 在运行时从 DLL 资源段里
`LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_ZH), ...)` 加载的（见
`3rdparty/weasel/WeaselTSF/LanguageBar.cpp`）。所以它们必须是**真的 .ico 资源**，
不能是运行时绘制 —— 这也是为什么这套图标是「静态」的：它没法跟随用户的主题强调色。

视觉语言直接对齐本项目自己的托盘图标（`src/tray_menu.cpp` 的 `makeTrayIcon`）：
**圆角方块 + 纯色底 + 居中的白色粗体字形**，并且沿用托盘那套语义：
中文 = 强调色，英文 = 中性灰。

产物（`assets/langbar/`）：
    langbar-zh.ico      IDI_ZH          中文模式：强调蓝 + 白「中」
    langbar-en.ico      IDI_EN          英文模式：中性灰 + 白「A」
    langbar-full.ico    IDI_FULL_SHAPE  全角：强调蓝 + 白「全」
    langbar-half.ico    IDI_HALF_SHAPE  半角：中性灰 + 白「半」
    langbar-reload.ico  IDI_RELOAD      重新加载：强调蓝 + 白色循环箭头（矢量绘制）
    langbar-app.ico     IDI_WEASEL      应用图标：中性深底 + 白「业」（沿用上游标记字形）

每张都带 16/20/24/32/40/48/64/256 八种分辨率 —— 语言栏实际用的是
`SM_CXSMICON`（16），但资源管理器大图标视图、任务栏缩放、登录界面都会用到更大尺寸。

用法：
    python scripts/gen-langbar-icons.py
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "assets", "langbar")

# Fluent 色板
ACCENT = (0x00, 0x78, 0xD4)      # Fluent 品牌强调蓝
NEUTRAL = (0x6E, 0x6E, 0x6E)    # Fluent 中性灰（英文/半角）
CHARCOAL = (0x20, 0x20, 0x20)   # 应用图标的深底
WHITE = (255, 255, 255)

# 多分辨率
SIZES = [(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)]
BASE = 256   # 渲染基准尺寸，最后交给 PIL 缩放

FONT_CJK_BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
FONT_LATIN_BOLD = r"C:\Windows\Fonts\segoeuib.ttf"


def _font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        # 退回到 Pillow 自带位图字体（只在极端环境发生）
        return ImageFont.load_default()


def _rounded_tile(size, color, radius_ratio=0.22):
    """画一个 Fluent 风格的圆角方块底。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = max(1, int(size * radius_ratio))
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=r, fill=color + (255,))
    return img


def _glyph_tile(ch, color, font_path, scale=0.74, offset_y=0.02):
    """圆角方块 + 居中的白色字形。

    字号按 em box 给；YaHei 的汉字墨迹比 em box 略小，所以视觉上会再补一点。
    `anchor="mm"` 让 PIL 用字体的锚点做真正的居中（之前用 "lm" + 手工平移会偏）。
    """
    img = _rounded_tile(BASE, color)
    d = ImageDraw.Draw(img)
    font = _font(font_path, int(BASE * scale))
    # 中文字形在 em box 里偏上，往下压 2% 才视觉居中
    d.text((BASE / 2, BASE / 2 + BASE * offset_y), ch, font=font,
           fill=WHITE + (255,), anchor="mm")
    return img


def _reload_tile(color):
    """循环箭头：纯矢量画，避免 16px 下字体笔画糊成一团。

    几何：PIL 的 arc 角度以 3 点钟方向为 0°、顺时针为正。取 0°→300° 的圆弧，
    缺口留在右上方；箭头在弧的**终点**（300°，约 1 点钟方向），沿顺时针切线指向右下。
    """
    img = _rounded_tile(BASE, color)
    d = ImageDraw.Draw(img)
    cx = cy = BASE / 2
    r = BASE * 0.27
    w = max(2, int(BASE * 0.085))
    d.arc([cx - r, cy - r, cx + r, cy + r], start=0, end=300,
          fill=WHITE + (255,), width=w)

    # 箭头：尖端沿切线，底边垂直于切线
    theta = math.radians(300)
    px = cx + r * math.cos(theta)
    py = cy + r * math.sin(theta)
    tx, ty = -math.sin(theta), math.cos(theta)      # 顺时针切线
    nx, ny = ty, -tx                                # 法线
    head = BASE * 0.15
    half_w = BASE * 0.082
    tip = (px + tx * head, py + ty * head)
    b1 = (px + nx * half_w, py + ny * half_w)
    b2 = (px - nx * half_w, py - ny * half_w)
    d.polygon([tip, b1, b2], fill=WHITE + (255,))
    return img


def _save_ico(img, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    # PIL 的 ICO 保存：从基准图缩放出各个尺寸（LANCZOS）
    img.save(path, format="ICO", sizes=SIZES, bitmap_format="png")
    return path


def build():
    outputs = []

    outputs.append(_save_ico(_glyph_tile("中", ACCENT, FONT_CJK_BOLD), "langbar-zh.ico"))
    outputs.append(_save_ico(_glyph_tile("A", NEUTRAL, FONT_LATIN_BOLD), "langbar-en.ico"))
    outputs.append(_save_ico(_glyph_tile("全", ACCENT, FONT_CJK_BOLD), "langbar-full.ico"))
    outputs.append(_save_ico(_glyph_tile("半", NEUTRAL, FONT_CJK_BOLD), "langbar-half.ico"))
    outputs.append(_save_ico(_reload_tile(ACCENT), "langbar-reload.ico"))
    outputs.append(_save_ico(_glyph_tile("业", CHARCOAL, FONT_CJK_BOLD), "langbar-app.ico"))

    for p in outputs:
        print("wrote", os.path.relpath(p, REPO))
    return outputs


def preview():
    """把 16/24/32/48px 的实际观感拼成一张对比图，方便目视验收。

    刻意用 NEAREST 放大 2 倍 —— 要看的是像素级效果，不是平滑后的「好看」。
    """
    out = os.path.join(OUT_DIR, "_preview.png")
    names = ["langbar-zh", "langbar-en", "langbar-full",
             "langbar-half", "langbar-reload", "langbar-app"]
    zoom = 3
    cell = 56 * zoom
    label_w = 130

    sheet = Image.new("RGBA", (label_w + 4 * cell, len(names) * cell), (245, 245, 245, 255))
    d = ImageDraw.Draw(sheet)
    font = _font(r"C:\Windows\Fonts\segoeui.ttf", 20)

    for r, n in enumerate(names):
        d.text((10, r * cell + cell // 2), n, font=font, fill=(30, 30, 30, 255), anchor="lm")
        for c, s in enumerate((16, 24, 32, 48)):
            im = Image.open(os.path.join(OUT_DIR, n + ".ico"))
            im.size = (s, s)
            im = im.convert("RGBA").resize((s * zoom, s * zoom), Image.NEAREST)
            sheet.alpha_composite(im, (label_w + c * cell + (cell - im.width) // 2,
                                       r * cell + (cell - im.height) // 2))
    sheet.save(out)
    print("wrote", os.path.relpath(out, REPO))


if __name__ == "__main__":
    build()
    preview()
    sys.exit(0)
