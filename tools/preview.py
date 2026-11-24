"""PC preview of the clock screen, using the same fonts and layout rules as main/clock_ui.c.

    python tools/preview.py                 # preview.png (dark + paper) and a fit report
    python tools/preview.py 13:47 paper     # build/preview_1347_paper.png

Requires Pillow and fonttools. Text measurement differs slightly from LVGL's
Tiny TTF, so treat it as a close approximation; device screenshots
(`mosaico.py iris screenshot`) are the real thing.
"""
import sys
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_quotes import load_quotes  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "main" / "fonts"
# Same constants as clock_ui.c
QUOTE_X, QUOTE_W, QUOTE_TOP = 40, 400, 50
ATTR_RIGHT, ATTR_W, ATTR_BOTTOM, ATTR_GAP, ATTR_SIZE = 416, 340, 432, 16, 20
TIME_Y, DOTS_Y, DOT_D, DOT_GAP = 16, 448, 6, 7
SIZES = [46, 42, 38, 35, 32, 30, 28, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16]
THEMES = {
    "dark": dict(bg=0x000000, text=0xB9B2A7, accent=0xFFC86B, meta=0x7C766D, on=0xC9C2B6, off=0x3A3732),
    "paper": dict(bg=0xF3EEE3, text=0x3B3731, accent=0x9A2A1A, meta=0x6E675D, on=0x3B3731, off=0xCFC7B8),
}
SCALE = 2  # render at 2x for smoother preview, then downsample


def rgb(v):
    return (v >> 16 & 255, v >> 8 & 255, v & 255)


class Face:
    """Pillow font sized like Tiny TTF: font_size = ascent - descent in pixels."""
    _cache = {}

    def __init__(self, name):
        tt = TTFont(FONTS / name)
        hhea = tt["hhea"]
        self.path = str(FONTS / name)
        self.ratio = tt["head"].unitsPerEm / (hhea.ascent - hhea.descent)
        self.asc = hhea.ascent / (hhea.ascent - hhea.descent)

    def get(self, px):
        key = (self.path, px)
        if key not in Face._cache:
            Face._cache[key] = ImageFont.truetype(self.path, max(1, round(px * self.ratio * SCALE)))
        return Face._cache[key]


REG, BOLD, ITAL = Face("literata_regular.ttf"), Face("literata_bold.ttf"), Face("literata_italic.ttf")


def layout(runs, width, px, line_space):
    """Greedy word wrap of [(text, face, colour)] runs. Returns (lines, height)."""
    tokens = []  # (text, face, colour) pieces split into words with trailing spaces
    for text, face, col in runs:
        word = ""
        for ch in text:
            if ch == "\n":
                if word:
                    tokens.append((word, face, col))
                tokens.append(("\n", face, col))
                word = ""
                continue
            word += ch
            if ch in " -":
                tokens.append((word, face, col))
                word = ""
        if word:
            tokens.append((word, face, col))
    # glue pieces that belong to one word (a highlight can start mid-word)
    words, cur = [], []
    for t in tokens:
        cur.append(t)
        if t[0] == "\n" or t[0].endswith((" ", "-")):
            words.append(cur)
            cur = []
    if cur:
        words.append(cur)

    def w_of(pieces, strip=False):
        total = 0
        for i, (t, face, _) in enumerate(pieces):
            if strip and i == len(pieces) - 1:
                t = t.rstrip(" ")
            total += face.get(px).getlength(t) / SCALE
        return total

    lines, line, lw = [], [], 0.0
    for wp in words:
        if wp[0][0] == "\n":
            lines.append(line)
            line, lw = [], 0.0
            continue
        ww = w_of(wp)
        if line and lw + w_of(wp, strip=True) > width:
            lines.append(line)
            line, lw = [], 0.0
        line.extend(wp)
        lw += ww
    if line:
        lines.append(line)
    h = len(lines) * px + max(0, len(lines) - 1) * line_space
    return lines, h, w_of


def draw_lines(d, lines, x, y, width, px, line_space, align, w_of):
    for i, line in enumerate(lines):
        lw = w_of(line, strip=True)
        cx = x + (width - lw if align == "right" else 0)
        ly = y + i * (px + line_space)
        for t, face, col in line:
            f = face.get(px)
            d.text((cx * SCALE, (ly + face.asc * px) * SCALE), t, font=f, fill=col, anchor="ls")
            cx += f.getlength(t) / SCALE


def fit(q):
    _, hs, hl, text, title, author = q
    b = text.encode("utf-8")
    parts = [b[:hs].decode(), b[hs:hs + hl].decode(), b[hs + hl:].decode()]
    attr = [("— ", REG), (title, ITAL), (", " if author else "", REG), (author, REG)]
    a_lines, ah, aw = layout([(t, f, 0) for t, f in attr], ATTR_W, ATTR_SIZE, 2)
    avail = ATTR_BOTTOM - ah - ATTR_GAP - QUOTE_TOP
    for px in SIZES:
        lines, h, w_of = layout([(parts[0], REG, 0), (parts[1], BOLD, 1), (parts[2], REG, 0)],
                                QUOTE_W, px, px // 5)
        if h <= avail:
            return px, h, avail, ah, True
    return SIZES[-1], h, avail, ah, False


def render(minute, idx, theme, quotes, out):
    th = THEMES[theme]
    group = [q for q in quotes if q[0] == minute]
    exact = bool(group)
    m = minute
    while not group:
        m = (m - 1) % 1440
        group = [q for q in quotes if q[0] == m]
    q = group[idx % len(group)]
    _, hs, hl, text, title, author = q
    b = text.encode("utf-8")
    parts = [b[:hs].decode(), b[hs:hs + hl].decode(), b[hs + hl:].decode()]
    px, h, avail, ah, ok = fit(q)

    img = Image.new("RGB", (480 * SCALE, 480 * SCALE), rgb(th["bg"]))
    d = ImageDraw.Draw(img)
    runs = [(parts[0], REG, rgb(th["text"])), (parts[1], BOLD, rgb(th["accent"])),
            (parts[2], REG, rgb(th["text"]))]
    lines, h, w_of = layout(runs, QUOTE_W, px, px // 5)
    draw_lines(d, lines, QUOTE_X, QUOTE_TOP + (avail - h) // 2, QUOTE_W, px, px // 5, "left", w_of)
    meta = rgb(th["meta"])
    attr = [("— ", REG, meta), (title, ITAL, meta), (", " if author else "", REG, meta), (author, REG, meta)]
    a_lines, ah, aw = layout(attr, ATTR_W, ATTR_SIZE, 2)
    draw_lines(d, a_lines, ATTR_RIGHT - ATTR_W, ATTR_BOTTOM - ah, ATTR_W, ATTR_SIZE, 2, "right", aw)
    if not exact:
        f = REG.get(20)
        d.text((240 * SCALE, (TIME_Y + REG.asc * 20) * SCALE), f"{minute // 60:02d}:{minute % 60:02d}",
               font=f, fill=meta, anchor="ms")
    n = len(group) if len(group) > 1 else 0
    x0 = 240 - (n * DOT_D + (n - 1) * DOT_GAP) // 2
    for i in range(n):
        x = x0 + i * (DOT_D + DOT_GAP)
        d.ellipse([x * SCALE, DOTS_Y * SCALE, (x + DOT_D) * SCALE, (DOTS_Y + DOT_D) * SCALE],
                  fill=rgb(th["on"] if i == idx % len(group) else th["off"]))
    img = img.resize((480, 480), Image.LANCZOS)
    # rounded panel corners, for the look only
    mask = Image.new("L", (480, 480), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, 479, 479], radius=96, fill=255)
    framed = Image.new("RGB", (480, 480), (24, 24, 24))
    framed.paste(img, (0, 0), mask)
    return framed


def main():
    quotes, _ = load_quotes()
    (ROOT / "build").mkdir(exist_ok=True)
    if len(sys.argv) > 1:
        hh, mm = map(int, sys.argv[1].split(":"))
        theme = sys.argv[2] if len(sys.argv) > 2 else "dark"
        idx = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        out = ROOT / "build" / f"preview_{hh:02d}{mm:02d}_{theme}.png"
        render(hh * 60 + mm, idx, theme, quotes, out).save(out)
        print(out)
        return
    sizes, bad = {}, []
    for q in quotes:
        px, h, avail, ah, ok = fit(q)
        sizes[px] = sizes.get(px, 0) + 1
        if not ok:
            bad.append((q[0], q[4], h, avail))
    print("chosen sizes:", dict(sorted(sizes.items(), reverse=True)))
    print("too tall even at 16 px:", bad or "none")
    a = render(15 * 60 + 15, 0, "dark", quotes, None)
    b = render(15 * 60 + 15, 0, "paper", quotes, None)
    sheet = Image.new("RGB", (480 * 2 + 24, 480), (24, 24, 24))
    sheet.paste(a, (0, 0))
    sheet.paste(b, (480 + 24, 0))
    sheet.save(ROOT / "preview.png")
    print(ROOT / "preview.png")


if __name__ == "__main__":
    main()
