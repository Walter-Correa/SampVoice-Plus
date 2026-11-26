#!/usr/bin/env python3
"""Generates all 7 DPI-scaled PNG assets for Inno Setup WizardImageFile & WizardSmallImageFile.

Requires Microsoft Edge or Google Chrome (`msedge.exe` / `chrome.exe`) for headless SVG
rasterization onto transparent 32-bit RGBA canvases, plus OpenCV (`cv2`) for cropping and
size verification.
"""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile
import cv2


LARGE_SCALES: list[tuple[str, int, int]] = [
    ("100", 202, 386),
    ("125", 269, 515),
    ("150", 336, 643),
    ("175", 403, 772),
    ("200", 430, 824),
    ("225", 498, 953),
    ("250", 534, 1022),
]

SMALL_SCALES: list[tuple[str, int, int]] = [
    ("100", 58, 58),
    ("125", 77, 77),
    ("150", 97, 97),
    ("175", 116, 116),
    ("200", 124, 124),
    ("225", 143, 143),
    ("250", 159, 159),
]


def find_browser() -> str:
    candidates = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    ]
    for path in candidates:
        if pathlib.Path(path).is_file():
            return path
    for name in ("msedge", "chrome", "chromium"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("No Chromium/Edge/Chrome executable found for headless SVG rasterization")


def render_svg_to_png(browser: str, svg_path: pathlib.Path, out_path: pathlib.Path, width: int, height: int) -> None:
    svg_text = svg_path.read_text(encoding="utf-8")
    win_w = max(800, width + 32)
    win_h = max(1200, height + 32)
    with tempfile.TemporaryDirectory(prefix="svplus_svg_") as tmpdir:
        tmp_path = pathlib.Path(tmpdir)
        html_path = tmp_path / "render.html"
        profile_dir = tmp_path / "profile"
        html_doc = (
            "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
            "html,body{margin:0;padding:0;width:100vw;height:100vh;overflow:hidden;background:transparent;}"
            f"svg{{position:fixed;left:0;top:0;width:{width}px;height:{height}px;display:block;}}"
            "</style></head><body>"
            f"{svg_text}"
            "</body></html>"
        )
        html_path.write_text(html_doc, encoding="utf-8")

        cmd = [
            browser,
            "--headless=new",
            "--disable-gpu",
            "--no-first-run",
            "--no-default-browser-check",
            "--hide-scrollbars",
            "--force-device-scale-factor=1",
            "--default-background-color=00000000",
            f"--user-data-dir={profile_dir}",
            f"--window-size={win_w},{win_h}",
            f"--screenshot={out_path}",
            html_path.as_uri(),
        ]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    img = cv2.imread(str(out_path), cv2.IMREAD_UNCHANGED)
    if img is None or img.shape[0] < height or img.shape[1] < width:
        actual = None if img is None else f"{img.shape[1]}x{img.shape[0]}"
        raise RuntimeError(f"Expected >= {width}x{height} at {out_path}, got {actual}")
    cropped = img[:height, :width]
    if cropped.ndim != 3 or cropped.shape[2] != 4:
        raise RuntimeError(f"Expected 32-bit RGBA output for {out_path}")
    if not cv2.imwrite(str(out_path), cropped):
        raise RuntimeError(f"Failed to write cropped image {out_path}")


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    art_dir = repo_root / "installer" / "art"
    large_svg = art_dir / "wizard-large.svg"
    small_light_svg = art_dir / "wizard-small-light.svg"
    small_dark_svg = art_dir / "wizard-small-dark.svg"
    for p in (large_svg, small_light_svg, small_dark_svg):
        if not p.is_file():
            print(f"Missing artwork SVG: {p}", file=sys.stderr)
            return 1

    browser = find_browser()
    print(f"Using headless browser: {browser}")

    for tag, w, h in LARGE_SCALES:
        dst = art_dir / f"wizard-large-{tag}.png"
        render_svg_to_png(browser, large_svg, dst, w, h)
        print(f"[OK] {dst.name} ({w}x{h})")

    for tag, w, h in SMALL_SCALES:
        dst_light = art_dir / f"wizard-small-light-{tag}.png"
        render_svg_to_png(browser, small_light_svg, dst_light, w, h)
        print(f"[OK] {dst_light.name} ({w}x{h})")

        dst_dark = art_dir / f"wizard-small-dark-{tag}.png"
        render_svg_to_png(browser, small_dark_svg, dst_dark, w, h)
        print(f"[OK] {dst_dark.name} ({w}x{h})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
