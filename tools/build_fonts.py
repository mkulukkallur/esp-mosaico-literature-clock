"""Make the three small static fonts the app embeds from the Literata variable fonts.

Literata (SIL Open Font License 1.1, fonts/OFL.txt) ships as variable fonts. LVGL's
Tiny TTF renderer reads static TrueType only, so this pins the axes (optical size
12, weight 400/700) and keeps only the characters the quotes use (build/charset.txt,
written by build_quotes.py), the 'kern' feature and no hinting.

Requires fonttools:  python -m pip install "fonttools>=4.50"
Usage:               python tools/build_quotes.py && python tools/build_fonts.py
"""
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "fonts"
OUT = ROOT / "main" / "fonts"
OPSZ = 12  # Literata's text optical size; sturdy at 18-40 px on the 480x480 panel

TARGETS = [
    ("Literata-VF.ttf", 400, "literata_regular.ttf"),
    ("Literata-VF.ttf", 700, "literata_bold.ttf"),
    ("Literata-Italic-VF.ttf", 400, "literata_italic.ttf"),
]


def main():
    chars = (ROOT / "build" / "charset.txt").read_text(encoding="utf-8")
    chars += "…‘’“”–—·°"  # always keep typographic basics
    OUT.mkdir(parents=True, exist_ok=True)
    for src, wght, dst in TARGETS:
        font = TTFont(FONTS / src)
        font = instancer.instantiateVariableFont(font, {"opsz": OPSZ, "wght": wght},
                                                 updateFontNames=False)
        opts = subset.Options()
        opts.layout_features = ["kern"]
        opts.hinting = False
        opts.name_IDs = ["*"]       # keep copyright / licence names (OFL requirement)
        opts.notdef_outline = True
        opts.drop_tables += ["DSIG", "STAT"]
        sub = subset.Subsetter(opts)
        sub.populate(unicodes={ord(c) for c in set(chars)})
        sub.subset(font)
        missing = [c for c in set(chars) if ord(c) not in font.getBestCmap() and c.isprintable()]
        font.save(OUT / dst)
        print(f"{dst}: {(OUT / dst).stat().st_size} bytes" + (f", missing {missing}" if missing else ""))


if __name__ == "__main__":
    main()
