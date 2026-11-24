"""Make data/litclock_extra.csv: quotes from the JohannesNE literature-clock list for
minutes that data/litclock.csv has no quote for. Only quotes tagged "sfw" are kept.

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


def clean(s):
    return TAG.sub("", BR.sub("\n", s)).strip()


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    covered = {q[0] for q in load_quotes([SRC])[0]}
    rows = []
    for line in Path(sys.argv[1]).read_text(encoding="utf-8-sig").splitlines():
        f = line.split("|")
        if len(f) != 6 or f[5].strip() != "sfw":
            continue
        h, m = f[0].strip().split(":")
        if int(h) * 60 + int(m) in covered:
            continue
        fields = [f[0].strip()] + [clean(x) for x in f[1:5]]
        if any("|" in x for x in fields):
            continue
        rows.append("|".join(fields))
    OUT.write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
    minutes = {r[:5] for r in rows}
    print(f"{len(rows)} quotes for {len(minutes)} new minutes -> {OUT}")


if __name__ == "__main__":
    main()
