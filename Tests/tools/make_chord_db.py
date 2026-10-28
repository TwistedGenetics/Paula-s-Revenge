#!/usr/bin/env python3
"""Builds Source/engine/ChordDatabase.cpp - the chord list the CHORDS page picks from.

Source: third_party/tonal/chord-type-data.ts, the chord list of the tonal music
library (MIT licence, github.com/tonaljs/tonal), which in turn comes from
Wikibooks' "Music Theory/Complete List of Chord Patterns".

Every chord type is written out in several voicings:
  ROOT      the chord as written, root at the bottom
  INV 1..n  each inversion: the lowest note moved up an octave, one at a time
  DROP 2    (4-note chords) the second-highest note of the root position dropped an octave
  DROP 3    (4-note chords) the third-highest note dropped an octave
  OPEN      (3 notes) the middle note raised an octave - a spread triad

Notes are semitones from the played note, so the played note is the chord's root
(an inversion of C major played on C sounds E G C).

Run:  python3 Tests/tools/make_chord_db.py
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "third_party", "tonal", "chord-type-data.ts")
OUT = os.path.join(ROOT, "Source", "engine", "ChordDatabase.cpp")

MAJOR_STEPS = [0, 2, 4, 5, 7, 9, 11]          # degrees 1..7 of the major scale
PERFECT = {1, 4, 5}                           # (mod 7) perfect intervals

def semitones(iv):
    m = re.fullmatch(r"(\d+)([PMmAd]+)", iv)
    if not m:
        raise ValueError(iv)
    num, q = int(m.group(1)), m.group(2)
    deg = (num - 1) % 7
    octave = (num - 1) // 7
    base = MAJOR_STEPS[deg] + 12 * octave
    perfect = (deg + 1) in PERFECT
    if q == "P" or q == "M":
        off = 0
    elif q == "m":
        off = -1
    elif set(q) == {"A"}:
        off = len(q)
    elif set(q) == {"d"}:
        off = -len(q) if perfect else -1 - len(q)
    else:
        raise ValueError(iv)
    return base + off

def parse(path):
    text = open(path, encoding="utf-8").read()
    body = text[text.index("[") + 1 : text.rindex("]")]
    entries = re.findall(r'\[\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",?\s*\]', body)
    return entries

# --- names in the app's pixel font (A-Z 0-9 and some symbols; \x07 = flat sign) -------------
# abbreviations that read badly in capitals or are too terse, and better spellings of a few
AVOID = {"M", "^", "h", "H", "2", "4", "67", "sus", "7sus", "9sus", "13sus", "m+", "+"}
PREFER = {"1P 3M 5A": "aug", "1P 4P 7m 10m": "quartal", "1P 3M 5P 9M": "add9",
          "1P 3m 5A 7m 9M 11P": "m11#5", "1P 3M 5A 7m 9A": "7#5#9", "1P 3m 5P 6M 9M": "m6add9",
          "1P 3M 5P 6M 9M": "6add9", "1P 3M 5P 6M 9M 11A": "6add9#11", "1P 3m 5d 6M 7M": "dim7maj7",
          "1P 3m 6m 7M": "mb6maj7", "1P 3M 5P 7M 11A": "maj7#11", "1P 4P 5P 7m 9m": "7sus4b9",
          "1P 3M 5P 7m 13M": "7add13", "1P 2M 4P 5P": "sus2sus4"}

# a few common voicings the list doesn't have: (intervals, name, symbol)
EXTRAS = [("1P 8P", "OCTAVE", "OCT"), ("1P 5P 8P", "POWER CHORD WITH OCTAVE", "5 OCT"),
          ("1P 3M 5P 11P", "MAJOR ADD 11", "ADD11"), ("1P 3m 5P 11P", "MINOR ADD 11", "MINADD11"),
          ("1P 2M 3M 5P", "MAJOR ADD 2", "ADD2"), ("1P 2M 3m 5P", "MINOR ADD 2", "MINADD2"),
          ("1P 2M 5P 7m", "SUSPENDED SECOND SEVENTH", "7SUS2"), ("1P 2M 5P 7M", "MAJOR 7 SUS 2", "MAJ7SUS2"),
          ("1P 4P 5P 6M", "SIXTH SUSPENDED FOURTH", "6SUS4"), ("1P 3m 5P 7m 11P 13M", "MINOR 13 NO 9", "MIN13NO9")]

def symbol_from(ivs, abbrevs):
    if ivs in PREFER:
        return PREFER[ivs]
    cands = [a for a in abbrevs.split() if re.fullmatch(r"[A-Za-z0-9#+/]+", a)]
    good = [a for a in cands if a not in AVOID and "+" not in a and "/" not in a]
    return (good or cands or ["?"])[0]

QUALITY = [("mMaj", "MINMAJ", "MINOR MAJOR"), ("mM", "MINMAJ", "MINOR MAJOR"),
           ("maj", "MAJ", "MAJOR"), ("Maj", "MAJ", "MAJOR"), ("ma", "MAJ", "MAJOR"), ("M", "MAJ", "MAJOR"),
           ("min", "MIN", "MINOR"), ("dim", "DIM", "DIMINISHED"), ("oM", "DIMMAJ", "DIMINISHED MAJOR"),
           ("o", "DIM", "DIMINISHED"), ("aug", "AUG", "AUGMENTED"), ("m", "MIN", "MINOR")]
TOKENS = [(r"b(\d+)", "\x07%s", "FLAT %s"), (r"#(\d+)", "#%s", "SHARP %s"), (r"add#(\d+)", "ADD#%s", "ADD SHARP %s"), (r"addb(\d+)", "ADD\x07%s", "ADD FLAT %s"), (r"add(\d+)", "ADD%s", "ADD %s"),
          (r"sus(\d*)", "SUS%s", "SUS %s"), (r"no(\d+)", "NO%s", "NO %s"), (r"maj(\d+)", "MAJ%s", "MAJOR %s"),
          (r"M(\d+)", "MAJ%s", "MAJOR %s"), (r"alt", "ALT", "ALTERED"), (r"\+", "AUG", "AUGMENTED"), (r"M", "MAJ", "MAJOR"), (r"quartal", "QUARTAL", "QUARTAL"),
          (r"(\d+)", "%s", "%s")]

def symbol_and_name(a):
    """an ASCII chord abbreviation -> (symbol for the pixel font, words)"""
    sym, words, s = "", [], a
    for pre, ps, pw in QUALITY:
        rx = {"ma": r"ma(?=\d)", "m": r"m(?!aj|in)"}.get(pre, re.escape(pre))
        m = re.match(rx, s)
        if m:
            sym, words, s = ps, [pw], s[m.end():]
            break
    if a == "add9" or a.startswith("add"):
        sym, words = "", ["MAJOR"]
    while s:
        for rx, fs, fw in TOKENS:
            m = re.match(rx, s)
            if m:
                g = m.group(1) if m.groups() else ""
                sym += fs % g if "%s" in fs else fs
                words.append((fw % g if "%s" in fw else fw).strip())
                s = s[m.end():]
                break
        else:
            raise ValueError("can't read chord abbreviation " + a)
    if not sym:
        sym = "MAJ"
    return sym, " ".join(w for w in words if w)

def category(semis):
    st = set(x % 12 for x in semis)
    has3m, has3M = 3 in st, 4 in st
    if len(semis) == 4 and semis == [0, 5, 10, 15]:
        return "OTHER"                                   # quartal
    if has3m and 6 in st and 7 not in st and not has3M:
        return "DIMINISHED"
    if has3m and not has3M:
        return "MINOR"
    if has3M and 8 in st and 7 not in st and 10 not in st:
        return "AUGMENTED"
    if has3M and 10 in st:
        return "DOMINANT"
    if has3M:
        return "MAJOR"
    if 5 in st or 2 in st:
        return "SUSPENDED"
    return "OTHER"

def voicings(notes):
    """root position, the inversions, and drop / open voicings - each one different"""
    n = len(notes)
    out = [("ROOT", notes[:])]
    if len(set(x % 12 for x in notes)) < n:          # octaves in the chord: no inversions
        return out
    for k in range(1, n):
        moved = []
        for x in notes[:k]:
            y = x + 12
            while y in notes[k:] or y in moved:
                y += 12
            moved.append(y)
        out.append(("INV %d" % k, sorted(notes[k:] + moved)))
    if n == 4:                                       # drop voicings: the four-note chords
        d2 = notes[:]
        d2[-2] -= 12
        out.append(("DROP 2", sorted(d2)))
        d3 = notes[:]
        d3[-3] -= 12
        out.append(("DROP 3", sorted(d3)))
    if n == 3:
        out.append(("OPEN", sorted([notes[0], notes[2], notes[1] + 12])))
    seen, unique = set(), []
    for name, v in out:
        if tuple(v) not in seen and len(set(v)) == len(v):
            seen.add(tuple(v))
            unique.append((name, v))
    return unique

def main():
    entries = parse(SRC)
    if len(entries) < 100:
        sys.exit("only %d chords parsed" % len(entries))
    seen = set()
    types = []
    for ivs, name, abbr in entries:
        semis = [semitones(x) for x in ivs.split()]
        key = tuple(semis)
        if key in seen:
            continue
        seen.add(key)
        sym, words = symbol_and_name(symbol_from(ivs, abbr))
        full = name.upper().replace("/", " ").replace("(", "").replace(")", "") if name else words
        full = re.sub(r"\s+", " ", full.replace("-", " ")).strip()
        types.append((category(semis), sym, full, semis))
    for ivs, full, sym in EXTRAS:
        semis = [semitones(x) for x in ivs.split()]
        if tuple(semis) not in seen:
            seen.add(tuple(semis))
            types.append((category(semis), sym, full, semis))
    order = ["MAJOR", "MINOR", "DOMINANT", "SUSPENDED", "DIMINISHED", "AUGMENTED", "OTHER"]
    # common chords first: the list's own order within each category (it starts with the everyday ones)
    types = [t for _, t in sorted(enumerate(types), key=lambda it: (order.index(it[1][0]), it[0]))]

    lines = []
    total = 0
    for cat, sym, full, semis in types:
        for vname, notes in voicings(semis):
            assert len(notes) <= 8 and all(-24 <= x <= 48 for x in notes)
            lines.append('    { "%s", "%s", "%s", "%s", { %s }, %d },' % (
                cat, cpp(sym), cpp(full), vname, ", ".join(str(x) for x in notes), len(notes)))
            total += 1

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("// Paula's Revenge - the chord database (%d chord types, %d voicings)\n" % (len(types), total))
        f.write("// GENERATED by Tests/tools/make_chord_db.py - edit the script, not this file.\n")
        f.write("// Chord types: the tonal music library (MIT licence, github.com/tonaljs/tonal),\n")
        f.write("// from Wikibooks' \"Music Theory/Complete List of Chord Patterns\".\n")
        f.write("// Notes are semitones from the played note; \\x07 is the pixel font's flat sign.\n")
        f.write('#include "Chords.h"\n\nnamespace prv\n{\n')
        f.write("static const ChordEntry kChords[] = {\n")
        f.write("\n".join(lines))
        f.write("\n};\n\n")
        f.write("const ChordEntry* chordDatabase (int& count)\n{\n")
        f.write("    count = (int) (sizeof (kChords) / sizeof (kChords[0]));\n    return kChords;\n}\n")
        f.write("} // namespace prv\n")
    print("%d chord types, %d voicings -> %s" % (len(types), total, OUT))

def cpp(s):
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\x07", '\\x07" "')

if __name__ == "__main__":
    main()
