"""Make data/litclock_extra.csv: quotes from the JohannesNE literature-clock list for
minutes that data/litclock.csv has no quote for. Quotes tagged "sfw" are kept, and so are
"unknown" (unrated) ones, which were read by hand; those found unsuitable are in EXCLUDE.
Quotes tagged "nsfw" are never used.

Source:  https://github.com/JohannesNE/literature-clock (litclock_annotated.csv)
License: CC BY-NC-SA 2.5, Johannes Enevoldsen. The copyright in each quote belongs to
         its author, translator or publisher.

Usage:  python tools/import_extra.py path/to/litclock_annotated.csv
Then run build_quotes.py, build_fonts.py and preview.py.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_quotes import ROOT, SRC, load_quotes  # noqa: E402

OUT = ROOT / "data" / "litclock_extra.csv"
BR = re.compile(r"<br\s*/?>", re.I)
TAG = re.compile(r"</?time\s*/?>", re.I)
KEEP = {"sfw", "unknown"}
# Unrated quotes left out after reading them (time, title): grim, crude, garbled, or
# using characters the font doesn't have.
EXCLUDE = {
    ("00:51", "All the Stars Above Us"),        # garbled text
    ("00:52", "The Year of Lovemaking & Crying"),  # stray backslash-n in the text
    ("04:37", "My Year of Rest and Relaxation"),  # vomit
    ("21:16", "Reprieve"),                      # math-italic letters, not in Literata
    ("21:40", "Devices and Desires"),           # murder of a child
}
AUTHOR_FIX = {"KateThompson": "Kate Thompson", "Raymon Queneau": "Raymond Queneau",
              "Kim Grimwood": "Ken Grimwood"}


def clean(s):
    return TAG.sub("", BR.sub("\n", s)).strip()


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    covered = {q[0] for q in load_quotes([SRC])[0]}
    rows = []
    for line in Path(sys.argv[1]).read_text(encoding="utf-8-sig").splitlines():
        f = line.split("|")
        if len(f) != 6 or f[5].strip() not in KEEP or (f[0].strip(), f[3].strip()) in EXCLUDE:
            continue
        h, m = f[0].strip().split(":")
        if int(h) * 60 + int(m) in covered:
            continue
        fields = [f[0].strip()] + [clean(x) for x in f[1:5]]
        fields[4] = AUTHOR_FIX.get(fields[4], fields[4])
        if any("|" in x for x in fields):
            continue
        rows.append("|".join(fields))
    OUT.write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
    minutes = {r[:5] for r in rows}
    print(f"{len(rows)} quotes for {len(minutes)} new minutes -> {OUT}")


if __name__ == "__main__":
    main()
