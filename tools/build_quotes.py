"""Convert data/litclock.csv into main/quotes_data.c (the quote table compiled into the app).

CSV format, one quote per record, '|' separated, UTF-8:
    HH:MM|time phrase|quote|title|author
A quote may span several lines. Quote fields copied from spreadsheets may carry
CSV quoting ("...""..."); that is undone here.

The time phrase is located in the quote (case-insensitive, apostrophe variants
treated alike) and stored as a UTF-8 byte offset + length so the device can
highlight it without searching.

Usage:  python tools/build_quotes.py [data/litclock.csv] [main/quotes_data.c]
Also writes the set of characters used to build/charset.txt for build_fonts.py.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "data" / "litclock.csv"
OUT = ROOT / "main" / "quotes_data.c"

# Characters treated as equal when searching for the time phrase (same length,
# so indices in the normalised text are indices in the original).
FOLD = str.maketrans({"\u2019": "'", "\u2018": "'", "\u00b4": "'", "`": "'",
                      "\u201c": '"', "\u201d": '"',
                      "\u2013": "-", "\u2014": "-", "\u2212": "-", "\u00a0": " "})
# Characters replaced in the displayed text.
FIX = str.maketrans({"\u00b4": "\u2019", "\u00a0": " "})
NBSP = "\u00a0"
OPENERS = " \n\t([{\u2014\u2013-/"


def smarten(s):
    """Straight quotes to typographic ones. Keeps the length, so offsets stay valid."""
    out = []
    for i, c in enumerate(s):
        prev = s[i - 1] if i else " "
        nxt = s[i + 1] if i + 1 < len(s) else " "
        if c == '"':
            closing = i + 1 == len(s) or nxt.isspace() or nxt in ".,;:!?)]}\u2014\u2019"
            c = "\u201c" if not closing and (prev in OPENERS or prev == "\u2018") else "\u201d"
        elif c == "'":
            if prev in OPENERS or prev == "\u201c":
                c = "\u2018" if nxt.isalpha() and not s[i + 1:i + 4].lower() in ("tis", "twa", "em ") else "\u2019"
            else:
                c = "\u2019"
        out.append(c)
    return "".join(out)


def read_records(text):
    """Yield [minute, phrase, quote, title, author] lists, joining multi-line quotes."""
    buf = ""
    for line in text.split("\n"):
        buf = line if not buf else buf + "\n" + line
        n = buf.count("|")
        if n < 4:
            continue
        if n > 4:
            raise SystemExit(f"Too many '|' in record: {buf[:80]!r}")
        yield buf.split("|")
        buf = ""
    if buf.strip():
        raise SystemExit(f"Unterminated record at end of file: {buf[:80]!r}")


def unquote_csv(s):
    """Undo spreadsheet CSV quoting: "a ""b"" c" -> a "b" c."""
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        inner = s[1:-1]
        if '"' not in inner.replace('""', ""):
            return inner.replace('""', '"')
    return s


def c_string(s):
    """C string literal using octal escapes (a hex escape would swallow following hex digits)."""
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c == '"':
            out.append('\\"')
        elif c == "\\":
            out.append("\\\\")
        elif c == "\n":
            out.append("\\n")
        elif 32 <= b < 127 and c != "?":  # '?' escaped to avoid trigraphs
            out.append(c)
        else:
            out.append(f"\\{b:03o}")
    return '"' + "".join(out) + '"'


def find_phrase(quote, phrase):
    q, p = quote.translate(FOLD).lower(), phrase.translate(FOLD).lower()
    i = q.find(p)
    if i >= 0:
        return i, len(p)
    # Tolerate a trailing full stop or comma in the phrase field.
    p2 = p.rstrip(".,;:")
    i = q.find(p2) if p2 else -1
    return (i, len(p2)) if i >= 0 else (-1, 0)


def load_quotes(src=SRC):
    """Return ([(minute, hl_start_bytes, hl_len_bytes, quote, title, author)], warnings), sorted by minute."""
    text = Path(src).read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    quotes, warnings = [], []
    for rec in read_records(text):
        hhmm, phrase, quote, title, author = (f.strip() for f in rec)
        h, m = hhmm.split(":")
        minute = int(h) * 60 + int(m)
        if not 0 <= minute < 1440:
            raise SystemExit(f"Bad time {hhmm!r}")
        quote = smarten(unquote_csv(quote).translate(FIX).strip())
        title = smarten(unquote_csv(title).translate(FIX).strip())
        # Keep short author names on one line in the attribution.
        author = smarten(unquote_csv(author).translate(FIX).strip())
        if len(author) <= 24:
            author = author.replace(" ", NBSP)
        phrase = unquote_csv(phrase).translate(FIX).strip()
        if "�" in quote + title + author:
            warnings.append(f"{hhmm}: replacement character in record")
        i, n = find_phrase(quote, phrase)
        if i < 0:
            warnings.append(f"{hhmm}: time phrase {phrase!r} not found; no highlight")
            start = length = 0
        else:
            start = len(quote[:i].encode("utf-8"))
            length = len(quote[i:i + n].encode("utf-8"))
        quotes.append((minute, start, length, quote, title, author))
    quotes.sort(key=lambda q: q[0])  # stable: keeps file order within a minute
    return quotes, warnings


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else OUT
    quotes, warnings = load_quotes(src)
    if len(quotes) > 65535:
        raise SystemExit("Too many quotes for uint16_t indices")

    first = [0] * 1441
    qi = 0
    for minute in range(1441):
        while qi < len(quotes) and quotes[qi][0] < minute:
            qi += 1
        first[minute] = qi
    covered = sum(1 for m in range(1440) if first[m + 1] > first[m])

    lines = [
        "/* Generated by tools/build_quotes.py from data/litclock.csv. Do not edit.",
        " * Copyright in each quote belongs to its author, translator or publisher; the quotes",
        " * are not covered by this project's MIT license. See README.md. */",
        '#include "quotes_data.h"',
        "",
        f"const uint16_t lc_quote_count = {len(quotes)};",
        f"const uint16_t lc_minutes_covered = {covered};",
        "",
        "/* lc_minute_first[m] .. lc_minute_first[m + 1] - 1 are the quotes for minute m. */",
        "const uint16_t lc_minute_first[1441] = {",
    ]
    for r in range(0, 1441, 16):
        lines.append("    " + ", ".join(str(v) for v in first[r:r + 16]) + ",")
    lines += ["};", "", "const lc_quote_t lc_quotes[] = {"]
    for minute, start, length, quote, title, author in quotes:
        lines.append(f"    {{{minute}, {start}, {length},")
        lines.append(f"     {c_string(quote)},")
        lines.append(f"     {c_string(title)}, {c_string(author)}}},")
    lines += ["};", ""]
    out.write_text("\n".join(lines), encoding="ascii", newline="\n")

    chars = set(" 0123456789:-—·" + NBSP)
    chars |= {chr(c) for c in range(0x20, 0x7F)}
    for q in quotes:
        chars |= set(q[3] + q[4] + q[5])
    chars.discard("\n")
    (ROOT / "build").mkdir(exist_ok=True)
    (ROOT / "build" / "charset.txt").write_text("".join(sorted(chars)), encoding="utf-8")

    for w in warnings:
        print("warning:", w)
    print(f"{len(quotes)} quotes, {covered}/1440 minutes covered, {len(chars)} distinct characters -> {out}")


if __name__ == "__main__":
    main()
