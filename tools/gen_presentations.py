"""Generate the store cover icon and the prebuilt Mosaico Idea-grid presentations.

    python tools/gen_presentations.py <esp-mosaico-utils dir>

Writes store/icon.png (512x512) and store/device/<locale>/{display.json,title.mif,body.mif,icon.png}
using Espressif's shared.ideas generator. That generator shapes text with libraqm;
on Windows (no libraqm) this script swaps in a FreeType-only shaper with kerning,
which is exact for the Latin and Chinese text used here (no complex scripts).

Requires: Pillow, freetype-py. Font: Noto Sans SC from Vibe Mode's fonts
(SIL OFL 1.1); it is a subset, so the Chinese text is limited to its characters.
"""
import sys
from pathlib import Path

import freetype
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
UTILS = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".." / ".." / "submodule" / "esp-mosaico-utils"
FONT = UTILS / "esp-mosaico-vibe-mode" / "firmware" / "vibe-mode" / "ui" / "fonts" / "NotoSansSC-Regular.otf"
sys.path.insert(0, str(UTILS))
from shared.ideas import font as ideas_font  # noqa: E402
from shared.ideas import generate_resources  # noqa: E402

TEXTS = {
    "en": ("Literature Clock",
           "Tells the time with a line from a book: a quote for every minute, the time highlighted. "
           "Tap for another quote."),
    "zh-CN": ("文学时钟", "用小说中的文字报时：每分钟都有一条提到当下时间的名言，时间部分高亮显示。"),
}


def _init(self, path, pixels, locale):
    self.face = freetype.Face(str(path))
    self.face.set_pixel_sizes(0, pixels)
    self.pixels, self.locale = pixels, locale
    self.bitmaps = {}
    self.line_height = (self.face.size.height + 63) // 64
    self.ascender = (self.face.size.ascender + 63) // 64


def _shape(self, text, direction=0):
    """(glyph index, x in 26.6, y, cluster byte offset) per character, left to right."""
    if not text:
        return [], 0, 0
    out, x, prev, offset = [], 0, 0, 0
    for ch in text:
        gid = self.face.get_char_index(ord(ch))
        if not gid:
            raise ValueError(f"The font does not cover {ch!r}")
        if prev:
            x += self.face.get_kerning(prev, gid).x
        out.append((gid, x, 0, offset))
        self.face.load_glyph(gid, freetype.FT_LOAD_DEFAULT)
        x += self.face.glyph.advance.x
        prev, offset = gid, offset + len(ch.encode("utf-8"))
    return out, 0, x


ideas_font.Font.__init__ = _init
ideas_font.Font.shape = _shape


def draw_icon(path, size=512):
    """Open book with a clock above it: amber on black, like the dark theme."""
    s = size / 512
    img = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    amber, cream, grey = (255, 200, 107, 255), (243, 238, 227, 255), (124, 118, 109, 255)
    # book: two pages meeting at the spine
    left = [(70, 330), (250, 360), (250, 450), (70, 420)]
    right = [(262, 360), (442, 330), (442, 420), (262, 450)]
    for page in (left, right):
        d.polygon([(x * s, y * s) for x, y in page], fill=cream)
    for i in range(3):  # text lines
        y = 365 + i * 22
        d.line([(95 * s, (y + 2) * s), (225 * s, (y + 22) * s)], fill=grey, width=int(6 * s))
        d.line([(287 * s, (y + 22) * s), (417 * s, (y + 2) * s)], fill=grey, width=int(6 * s))
    # clock
    cx, cy, r = 256 * s, 185 * s, 120 * s
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=amber, width=int(16 * s))
    for k in range(12):
        import math
        a = math.radians(k * 30)
        r1, r2 = r - 22 * s, r - (40 if k % 3 == 0 else 32) * s
        d.line([(cx + r1 * math.sin(a), cy - r1 * math.cos(a)), (cx + r2 * math.sin(a), cy - r2 * math.cos(a))],
               fill=amber, width=int(8 * s))
    d.line([(cx, cy), (cx + 55 * s, cy - 40 * s)], fill=cream, width=int(14 * s))   # hour hand
    d.line([(cx, cy), (cx - 10 * s, cy - 88 * s)], fill=cream, width=int(10 * s))   # minute hand
    d.ellipse([cx - 12 * s, cy - 12 * s, cx + 12 * s, cy + 12 * s], fill=amber)
    img.save(path)


def main():
    store = ROOT / "store"
    store.mkdir(exist_ok=True)
    icon = store / "icon.png"
    draw_icon(icon)
    for locale, (name, desc) in TEXTS.items():
        res = generate_resources(name=name, description=desc, locale=locale, font=FONT, icon=icon)
        d = store / "device" / locale
        d.mkdir(parents=True, exist_ok=True)
        for f in d.iterdir():
            f.unlink()
        for fname, data in res.items():
            (d / fname).write_bytes(data)
        print(locale, {k: len(v) for k, v in res.items()})


if __name__ == "__main__":
    main()
