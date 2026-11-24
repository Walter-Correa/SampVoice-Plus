#!/usr/bin/env python3
"""Generates *_blocked.svg and *_blocked.png variants for all 7 channel icons.

Reads `kBlockedAccentArgb` directly from `common/include/svplus/common/Protocol.hpp` so the
blocked channel icon red matches the `[BLACKLIST]` HUD tag color from one single constant.
"""

from __future__ import annotations

import pathlib
import re
import cv2
import numpy as np


ICONS = ("speaker", "radio", "phone", "megaphone", "police", "admin", "global")
ORIG_TOP_HEX = "#FFE08A"
ORIG_BOT_HEX = "#D4F06A"
BLOCKED_TOP_HEX = "#FF9A8A"


def hex_to_rgb(hex_str: str) -> tuple[int, int, int]:
    h = hex_str.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def read_blocked_accent_hex(repo_root: pathlib.Path) -> str:
    proto = (repo_root / "common" / "include" / "svplus" / "common" / "Protocol.hpp").read_text(encoding="utf-8")
    m = re.search(r"kBlockedAccentArgb\s*=\s*0x[0-9A-Fa-f]{2}([0-9A-Fa-f]{6})u", proto)
    if not m:
        raise RuntimeError("Could not locate kBlockedAccentArgb in Protocol.hpp")
    return f"#{m.group(1).upper()}"


def parse_gradient_y_bounds(svg_text: str) -> tuple[float, float]:
    y1_m = re.search(r'<linearGradient[^>]*y1="([^"]+)"', svg_text)
    y2_m = re.search(r'<linearGradient[^>]*y2="([^"]+)"', svg_text)
    if not y1_m or not y2_m:
        raise RuntimeError("Missing y1/y2 on linearGradient in SVG")
    return float(y1_m.group(1)), float(y2_m.group(1))


def main() -> None:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    res_dir = repo_root / "client" / "resources"
    svg_dir = res_dir / "svg"

    blocked_bot_hex = read_blocked_accent_hex(repo_root)
    top_rgb = np.array(hex_to_rgb(BLOCKED_TOP_HEX), dtype=np.float32)
    bot_rgb = np.array(hex_to_rgb(blocked_bot_hex), dtype=np.float32)

    for name in ICONS:
        src_svg_path = svg_dir / f"{name}.svg"
        dst_svg_path = svg_dir / f"{name}_blocked.svg"
        src_png_path = res_dir / f"{name}.png"
        dst_png_path = res_dir / f"{name}_blocked.png"

        svg_text = src_svg_path.read_text(encoding="utf-8")
        if ORIG_TOP_HEX not in svg_text or ORIG_BOT_HEX not in svg_text:
            raise RuntimeError(f"{src_svg_path} does not use expected gradient stops")

        blocked_svg = svg_text.replace(ORIG_TOP_HEX, BLOCKED_TOP_HEX).replace(ORIG_BOT_HEX, blocked_bot_hex)
        dst_svg_path.write_text(blocked_svg, encoding="utf-8")

        y1, y2 = parse_gradient_y_bounds(svg_text)
        src_bgra = cv2.imread(str(src_png_path), cv2.IMREAD_UNCHANGED)
        if src_bgra is None or src_bgra.ndim != 3 or src_bgra.shape[2] != 4:
            raise RuntimeError(f"Expected 4-channel PNG at {src_png_path}")

        h, w, _ = src_bgra.shape
        alpha = src_bgra[:, :, 3]
        ys = np.arange(h, dtype=np.float32) + 0.5
        t = np.clip((ys - y1) / max(1.0, y2 - y1), 0.0, 1.0)[:, None, None]
        grad_rgb = np.round(top_rgb * (1.0 - t) + bot_rgb * t).astype(np.uint8)
        grad_rgb = np.broadcast_to(grad_rgb, (h, w, 3)).copy()

        # Straight RGBA with zeroed RGB where alpha == 0 (matches original resources/*.png)
        out_bgra = np.zeros((h, w, 4), dtype=np.uint8)
        mask = alpha > 0
        out_bgra[mask, 0] = grad_rgb[mask, 2]
        out_bgra[mask, 1] = grad_rgb[mask, 1]
        out_bgra[mask, 2] = grad_rgb[mask, 0]
        out_bgra[:, :, 3] = alpha
        if not cv2.imwrite(str(dst_png_path), out_bgra):
            raise RuntimeError(f"Failed to write {dst_png_path}")

        print(f"[OK] {name}: {dst_svg_path.name}, {dst_png_path.name} ({w}x{h}, {BLOCKED_TOP_HEX} -> {blocked_bot_hex})")


if __name__ == "__main__":
    main()
