#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

DESIGN_DIR = Path(__file__).resolve().parent
TARGET_W, TARGET_H = 1328, 800
ICON_DISPLAY_PX = 68
ICON_SIZE = ICON_DISPLAY_PX * 2
APP_CENTER_X = 430
FOLDER_CENTER_X = 900
APP_ICON = Path("/tmp/maho-icon/appicon.png")
BG_V1 = DESIGN_DIR / "dmg-background-v1.png"
BG_V2 = DESIGN_DIR / "dmg-background-v2.png"
PREVIEW = DESIGN_DIR / "dmg-preview-v2.png"
SF_FONT_PATH = "/System/Library/Fonts/SFNS.ttf"

OLD_TEXT_REGION = (430, 58, 900, 268)
HEADER_MARGIN_X = 72
WORDMARK_Y = 88
WORDMARK_SIZE = 150
SUBLINE_Y = 240
SUBLINE_SIZE = 36
TONE_PIVOT_IN = (37, 31, 84)
TONE_PIVOT_OUT = (29, 26, 71)
TONE_COMPRESS = 0.2
TONE_LUM_LO, TONE_LUM_HI = 60, 140
STAR_MOTIF_CENTER = (1070, 250)
STAR_MOTIF_RADIUS = 290
STAR_MOTIF_OUTER_TO_INNER = 0.52
STAR_MOTIF_COLOR = (50, 46, 88, 255)
STAR_MOTIF_STROKE_PX = 4
STAR_MOTIF_TIP_ANGLE_DEG = -96
INDIGO = (30, 23, 71, 255)
LAVENDER = (167, 159, 224, 255)
WHITE = (255, 255, 255, 255)


def resize_crop_to(img: Image.Image, w: int, h: int) -> Image.Image:
    src_ratio, dst_ratio = img.width / img.height, w / h
    if src_ratio > dst_ratio:
        new_w = int(img.height * dst_ratio)
        left = (img.width - new_w) // 2
        img = img.crop((left, 0, left + new_w, img.height))
    else:
        new_h = int(img.width / dst_ratio)
        top = (img.height - new_h) // 2
        img = img.crop((0, top, img.width, top + new_h))
    return img.resize((w, h), Image.Resampling.LANCZOS)


def erase_region_with_smooth_fill(img: Image.Image, box: tuple[int, int, int, int]) -> None:
    left, top, right, bottom = box
    pad = 16
    blur = 8
    strip_h = 14
    outer_l, outer_t = left - pad, top - pad
    outer_r, outer_b = right + pad, bottom + pad
    patch_w, patch_h = right - left, bottom - top
    top_stretched = img.crop((outer_l, outer_t - strip_h, outer_r, outer_t)) \
        .resize((patch_w, patch_h), Image.Resampling.BILINEAR)
    bottom_stretched = img.crop((outer_l, outer_b, outer_r, outer_b + strip_h)) \
        .resize((patch_w, patch_h), Image.Resampling.BILINEAR)
    vertical_falloff = Image.linear_gradient("L").resize((patch_w, patch_h), Image.Resampling.BILINEAR)
    patch = Image.composite(bottom_stretched, top_stretched, vertical_falloff)
    patch = patch.filter(ImageFilter.GaussianBlur(2))
    feather = Image.new("L", patch.size, 0)
    ImageDraw.Draw(feather).rounded_rectangle([pad, pad, patch.width - pad, patch.height - pad],
                                              radius=56, fill=255)
    feather = feather.filter(ImageFilter.GaussianBlur(blur))
    img.paste(patch, (left, top), feather)


def sf_font(size: int, variation: bytes) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(SF_FONT_PATH, size)
    font.set_variation_by_name(variation)
    return font


def match_tone_to_windows_mockup(img: Image.Image) -> Image.Image:
    bands = []
    for i, (pivot_in, pivot_out) in enumerate(zip(TONE_PIVOT_IN, TONE_PIVOT_OUT)):
        lut = [max(0, min(255, round(pivot_out + (v - pivot_in) * TONE_COMPRESS))) for v in range(256)]
        bands.append(img.getchannel(i).point(lut))
    bands.append(img.getchannel(3))
    transformed = Image.merge("RGBA", bands)
    luminance = img.convert("L")
    rolloff = max(1, TONE_LUM_HI - TONE_LUM_LO)
    mask_lut = [max(0, min(255, round((v - TONE_LUM_LO) / rolloff * 255))) for v in range(256)]
    return Image.composite(img, transformed, luminance.point(mask_lut))


def draw_star_motif(img: Image.Image) -> None:
    supersample = 3
    canvas = STAR_MOTIF_RADIUS * 2 + STAR_MOTIF_STROKE_PX * 8
    overlay = Image.new("RGBA", (canvas * supersample, canvas * supersample), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    mid = canvas * supersample / 2
    import math

    vertices = []
    for i in range(24):
        angle = math.radians(STAR_MOTIF_TIP_ANGLE_DEG + i * 15)
        radius = STAR_MOTIF_RADIUS * supersample
        if i % 2:
            radius *= STAR_MOTIF_OUTER_TO_INNER
        vertices.append((mid + radius * math.cos(angle), mid + radius * math.sin(angle)))
    draw.polygon(vertices, outline=STAR_MOTIF_COLOR, width=STAR_MOTIF_STROKE_PX * supersample)
    overlay = overlay.resize((canvas, canvas), Image.Resampling.LANCZOS)
    cx, cy = STAR_MOTIF_CENTER
    img.alpha_composite(overlay, (cx - canvas // 2, cy - canvas // 2))


def build_background_v2() -> None:
    bg = Image.open(BG_V1).convert("RGBA")
    bg = match_tone_to_windows_mockup(bg)
    erase_region_with_smooth_fill(bg, OLD_TEXT_REGION)
    draw_star_motif(bg)
    draw = ImageDraw.Draw(bg)

    wordmark = sf_font(WORDMARK_SIZE, b"Semibold")
    subline = sf_font(SUBLINE_SIZE, b"Regular")
    draw.text((HEADER_MARGIN_X, WORDMARK_Y), "Maho", font=wordmark, fill=WHITE)
    draw.text((HEADER_MARGIN_X, SUBLINE_Y), "Drag Maho into the Applications folder",
              font=subline, fill=LAVENDER)
    bg.save(BG_V2)
    print(f"wrote {BG_V2}")


def draw_placeholder_applications_folder(size: int) -> Image.Image:
    s = size / 128.0
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([6 * s, 26 * s, 122 * s, 108 * s], radius=14 * s, fill=(64, 118, 214, 255))
    d.rounded_rectangle([14 * s, 14 * s, 58 * s, 34 * s], radius=8 * s, fill=(70, 126, 224, 255))
    front = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(front).rounded_rectangle([6 * s, 40 * s, 122 * s, 112 * s], radius=12 * s,
                                            fill=(92, 148, 240, 255))
    sheen = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(sheen).rounded_rectangle([6 * s, 40 * s, 122 * s, 74 * s], radius=12 * s,
                                            fill=(255, 255, 255, 46))
    front = Image.alpha_composite(front, sheen.filter(ImageFilter.GaussianBlur(3 * s)))
    img = Image.alpha_composite(img, front)
    cell = 16 * s
    for r in range(3):
        for c in range(3):
            x0, y0 = 40 * s + c * cell, 56 * s + r * cell
            d.rounded_rectangle([x0, y0, x0 + 11 * s, y0 + 11 * s], radius=3 * s,
                                fill=(255, 255, 255, 235))
    return img


def main() -> int:
    if not BG_V1.is_file():
        print(f"error: {BG_V1} missing", file=sys.stderr)
        return 1
    if not APP_ICON.is_file():
        print(f"error: {APP_ICON} missing", file=sys.stderr)
        return 1

    build_background_v2()

    bg = Image.open(BG_V2).convert("RGBA")
    app = Image.open(APP_ICON).convert("RGBA").resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)
    folder = draw_placeholder_applications_folder(ICON_SIZE)

    top = int(TARGET_H * 0.68) - ICON_SIZE // 2
    app_x = APP_CENTER_X - ICON_SIZE // 2
    folder_x = FOLDER_CENTER_X - ICON_SIZE // 2

    icon_shadows = Image.new("RGBA", (TARGET_W, TARGET_H), (0, 0, 0, 0))
    shadow_draw = ImageDraw.Draw(icon_shadows)
    shadow_draw.ellipse([folder_x + 12, top + ICON_SIZE - 6, folder_x + ICON_SIZE - 12, top + ICON_SIZE + 10],
                        fill=(10, 6, 30, 120))

    app_shadow = Image.new("RGBA", (TARGET_W, TARGET_H), (0, 0, 0, 0))
    ImageDraw.Draw(app_shadow).rounded_rectangle(
        [app_x + 2, top + 14, app_x + ICON_SIZE - 2, top + ICON_SIZE + 16],
        radius=30, fill=(18, 18, 20, 170))

    bg = Image.alpha_composite(bg, app_shadow.filter(ImageFilter.GaussianBlur(14)))
    bg = Image.alpha_composite(bg, icon_shadows.filter(ImageFilter.GaussianBlur(9)))
    bg.alpha_composite(app, (app_x, top))
    bg.alpha_composite(folder, (folder_x, top))

    window_mask = Image.new("L", (TARGET_W, TARGET_H), 0)
    ImageDraw.Draw(window_mask).rounded_rectangle([0, 0, TARGET_W - 1, TARGET_H - 1], radius=24, fill=255)
    framed = Image.new("RGB", (TARGET_W, TARGET_H), (10, 8, 20))
    framed.paste(bg.convert("RGB"), (0, 0), window_mask)
    framed.save(PREVIEW)
    print(f"wrote {PREVIEW}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
