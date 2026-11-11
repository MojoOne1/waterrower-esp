#!/usr/bin/env python3
"""Take the screenshots used in docs/ from the mock ESP (mock_server.py).

    pip install playwright        # plus any Chrome/Chromium
    python3 screenshots.py --out ../../../docs/screenshots [--chrome /usr/bin/google-chrome]

The data is generated, not recorded; see mock_server.py.
"""
import argparse
import pathlib
import subprocess
import sys
import time

from playwright.sync_api import sync_playwright

HERE = pathlib.Path(__file__).resolve().parent
PORT = 8099
BASE = f"http://127.0.0.1:{PORT}"


def box(page, selector):
    b = page.locator(selector).first.bounding_box()
    assert b, selector
    return b


def clip(page, top_sel, bottom_sel, pad=16, width=None):
    """A clip from the top of one element to the bottom of another, full width.

    Page coordinates, so it works together with full_page=True."""
    t, b = box(page, top_sel), box(page, bottom_sel)
    vw = width or page.viewport_size["width"]
    scroll = page.evaluate("window.scrollY")
    y = max(0, t["y"] + scroll - pad)
    return {"x": 0, "y": y, "width": vw, "height": b["y"] + scroll + b["height"] + pad / 2 - y}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--chrome", default=None, help="path to a Chrome/Chromium binary")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    server = subprocess.Popen([sys.executable, str(HERE / "mock_server.py"), "--port", str(PORT)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        with sync_playwright() as pw:
            kw = {"executable_path": a.chrome} if a.chrome else {}
            browser = pw.chromium.launch(args=["--no-sandbox"], **kw)

            def context(w, h, scheme="light", mobile=False):
                return browser.new_context(
                    viewport={"width": w, "height": h}, locale="de-DE",
                    timezone_id="Europe/Berlin", color_scheme=scheme,
                    device_scale_factor=2 if mobile else 1.5, is_mobile=mobile, has_touch=mobile)

            def open_page(ctx, path="/"):
                page = ctx.new_page()
                page.goto(BASE + path)
                page.wait_for_function("document.getElementById('lcd-dist').textContent !== '---'")
                page.wait_for_selector("#sessions li")
                page.wait_for_selector("#facts div")
                page.wait_for_timeout(700)
                return page

            # Desktop, light
            ctx = context(1180, 900)
            page = open_page(ctx)
            page.screenshot(path=out / "live.png", clip=clip(page, "header.top", "#session-line", 20), full_page=True)
            page.screenshot(path=out / "history.png",
                            clip=clip(page, "#sessions", "#ch-spm", 24), full_page=True)
            page.click("#conn")
            page.wait_for_selector("#settings:not([hidden])")
            page.wait_for_timeout(700)
            page.screenshot(path=out / "settings.png", clip=clip(page, "#settings", "#settings", 12), full_page=True)
            ctx.close()

            # Desktop, dark
            ctx = context(1180, 900, scheme="dark")
            page = open_page(ctx)
            page.screenshot(path=out / "live-dark.png", clip=clip(page, "header.top", "#session-line", 20), full_page=True)
            ctx.close()

            # Phone
            ctx = context(390, 844, mobile=True)
            page = open_page(ctx)
            page.screenshot(path=out / "live-phone.png", clip=clip(page, "header.top", "#session-line", 16), full_page=True)
            page.click("#conn")
            page.wait_for_selector("#settings:not([hidden])")
            page.wait_for_timeout(500)
            page.screenshot(path=out / "settings-phone.png", full_page=False)
            ctx.close()

            # Captive portal page, phone
            ctx = browser.new_context(viewport={"width": 390, "height": 760}, locale="de-DE",
                                      device_scale_factor=2, is_mobile=True, has_touch=True)
            page = ctx.new_page()
            page.goto(BASE + "/setup")
            page.click("#scan")
            page.wait_for_selector("#nets li")
            page.click("#nets li")                      # picks the network name
            page.fill("input[type=password]", "geheim1234")
            page.wait_for_timeout(400)
            page.screenshot(path=out / "setup-portal.png")
            ctx.close()
            browser.close()
    finally:
        server.terminate()
    shrink(out)


def shrink(out):
    """Palette-reduce the PNGs (about a third of the size, no visible change)."""
    try:
        from PIL import Image
    except ImportError:
        return
    for f in out.glob("*.png"):
        im = Image.open(f).convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        im.save(f, optimize=True)


if __name__ == "__main__":
    main()
