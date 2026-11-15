#!/usr/bin/env python3
"""Paula's Revenge - the KEYS sheet: every keyboard shortcut on a couple of A4 pages, in the manual's own look.

Made from README.md (the in-app HELP) each release, so it never falls behind: the "## Keys" table, sorted into groups,
plus the shortcuts that live in other chapters (EXTRAS below - add one there when a new one appears outside the table).
The look is the manual's: its print stylesheet (fonts included) is read from the manual's print.html.

    python3 tools/make_shortcuts.py README.md docs/manual-src/print.html out.html
    node tools/pdf.js out.html "Paulas Revenge Keys.pdf" "PAULA'S REVENGE KEYS"
"""
import html
import re
import sys

# shortcuts described outside the Keys table: (keys, what it does, group)
EXTRAS = [
    ("`Cmd+K`", "Command palette: type a few letters of anything - a button, a page, a skin, a menu command - and `Enter` does it", "ANYWHERE"),
    ("`Cmd+,`", "Audio/MIDI Settings (on a PC: File menu)", "ANYWHERE"),
    ("`Ctrl+Cmd+F`", "Full screen (on a PC: `F11`)", "ANYWHERE"),
    ("`Cmd+Q`", "Quit (on a PC: File > Exit)", "ANYWHERE"),
    ("`Enter` / `Cmd+G`", "HELP search: next match · `Shift+Enter` the one before · `Esc` clears, `Esc` again closes", "ANYWHERE"),
    ("`Alt+↑/↓`", "Move the highlighted sequence positions (in the pattern)", "SONG"),
    ("`Cmd+D`", "Duplicate the highlighted sequence positions (sequence panel, ARRANGE)", "SONG"),
]

# the Keys table's rows sorted by their key column (first match wins; anything new lands in MORE)
GROUPS = [
    ("PLAYING NOTES", [r"Z S X", r"Q 2 W", r"^`1`$", r"0–9", r"^`\[` `\]`", r"^`-` `=`", r"F4"]),
    ("PLAY & STOP", [r"^`Space`", r"^`Return`", r"F2", r"Cmd\+F`", r"Cmd\+\[` / `Cmd\+\]"]),
    ("MOVING", [r"^Arrows", r"^`Tab`", r"PgUp", r"^`Esc`"]),
    ("EDITING", [r"Delete", r"Cmd\+C / X / V", r"Cmd\+A", r"Cmd\+B", r"Cmd\+↑", r"Cmd\+Z", r"Cmd\+R"]),
    ("PAGES", [r"Cmd\+[1-8]", r"F1"]),
    ("FILES", [r"Cmd\+S", r"Cmd\+O"]),
]
ORDER = ["PLAYING NOTES", "PLAY & STOP", "MOVING", "EDITING", "SONG", "PAGES", "FILES", "ANYWHERE", "MORE"]
COLOURS = {"PLAYING NOTES": "t1", "PLAY & STOP": "t2", "MOVING": "t3", "EDITING": "t4", "SONG": "t1", "PAGES": "t2", "FILES": "t3", "ANYWHERE": "t4", "MORE": "t1"}


def inline(md: str) -> str:
    """README markdown in a cell -> the manual's HTML: `keys` as keys, **bold** as bold."""
    out, pos = [], 0
    for m in re.finditer(r"`([^`]+)`|\*\*([^*]+)\*\*", md):
        out.append(html.escape(md[pos:m.start()]))
        if m.group(1) is not None:
            out.append(keys(m.group(1)))
        else:
            out.append("<b>" + html.escape(m.group(2)) + "</b>")
        pos = m.end()
    out.append(html.escape(md[pos:]))
    return "".join(out)


def keys(k: str) -> str:
    """One `...` span: single keys as <kbd>, the + and / between them kept as text ("Cmd+Shift+Z" -> Cmd + Shift + Z).
    A note or a pattern value (`C-3`, `3 OF 12`) is pattern text, not keys."""
    if re.fullmatch(r"[A-G][-#]\d|\d+ OF \d+|[0-9A-F]{3,}", k.strip()):
        return "<code>%s</code>" % html.escape(k)
    if " " in k.strip() and "+" not in k and all(len(p) <= 3 for p in k.split()):   # a row of keys: Z S X D C ... / , L . ; /
        return " ".join("<kbd>%s</kbd>" % html.escape(p) for p in k.split())
    parts = re.split(r"(\s*\+\s*|\s+/\s+|/)", k)
    s = []
    for p in parts:
        if not p:
            continue
        if re.fullmatch(r"\s*\+\s*", p):
            s.append("+")
        elif re.fullmatch(r"\s*/\s*", p):
            s.append(" / ")
        else:
            s.append("<kbd>%s</kbd>" % html.escape(p.strip()))
    return "".join(s)


# the printable height of an A4 page (10 mm + 13 mm margins), and what the title and the PC line take, in px
PAGE_H = (297 - 23) / 25.4 * 96 - 8
GAP = 3 / 25.4 * 96   # (the space under each group)


def measure(style, extra_css, groups, group_html, header=""):
    """Each group's height as printed (Chromium, the same as the PDF): one column's width, every group on its own."""
    import json, os, subprocess, tempfile
    doc = ("<!doctype html><html><head><meta charset='utf-8'>" + style + extra_css + "</head><body>"
           "<div style='width:%.1fpx'>" % (((210 - 20) / 25.4 * 96 - 5 / 25.4 * 96) / 2)
           + "".join("<div class='m' data-g='%s'>%s</div>" % (html.escape(g), group_html(g)) for g in groups) + "</div>"
           + "<div style='width:%.1fpx'><div class='m' data-g='__header__' style='display:flow-root'>%s</div></div></body></html>" % ((210 - 20) / 25.4 * 96, header))
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, "m.html")
        open(f, "w", encoding="utf-8").write(doc)
        js = ("const { chromium } = require('playwright'); (async () => { const b = await chromium.launch(); const p = await b.newPage();"
              "await p.emulateMedia({ media: 'print' }); await p.goto('file://' + process.argv[2]); await p.evaluate(() => document.fonts.ready);"
              "const r = await p.evaluate(() => Object.fromEntries([...document.querySelectorAll('.m')].map(e => [e.dataset.g, e.getBoundingClientRect().height])));"
              "console.log(JSON.stringify(r)); await b.close(); })();")
        jf = os.path.join(d, "m.js")
        open(jf, "w").write(js)
        r = subprocess.run(["node", jf, f], capture_output=True, text=True, check=True)
        return {g: float(v) for g, v in json.loads(r.stdout.strip().splitlines()[-1]).items()}


def main():
    readme, css_from, out = sys.argv[1], sys.argv[2], sys.argv[3]
    t = open(readme, encoding="utf-8").read()
    sec = t[t.index("\n## Keys"):]
    sec = sec[: sec.index("\n## ", 5)]
    rows = []
    for line in sec.splitlines():
        if not line.startswith("|") or line.startswith("|---") or line.startswith("| Key"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 2:
            rows.append((cells[0], cells[1]))
    notes = [l.strip() for l in sec.splitlines() if l.strip() and not l.startswith("|") and not l.startswith("## ")]

    grouped = {g: [] for g in ORDER}
    for k, a in rows:
        g = next((name for name, pats in GROUPS if any(re.search(p, k) for p in pats)), "MORE")
        grouped[g].append((k, a))
    for k, a, g in EXTRAS:
        grouped[g].append((k, a))

    ver = "?"
    try:
        m = re.search(r"project\(PaulasRevenge VERSION (\d+)\.(\d+)", open("CMakeLists.txt", encoding="utf-8").read())
        if m:
            ver = "%s.%s" % (m.group(1), m.group(2).zfill(2))
    except OSError:
        pass

    src = open(css_from, encoding="utf-8").read()
    style = src[src.index("<style"): src.index("</style>") + len("</style>")]

    body = []
    body.append('<header class="titlebar raised"><span class="name">PAULA\'S REVENGE</span>'
                '<span class="rules" aria-hidden="true"></span><span class="ver">KEYS &middot; V%s</span></header>' % ver)
    body.append('<div class="status sunken"><span class="tag">PC</span><span>Every <kbd>Cmd</kbd> here is <kbd>Ctrl</kbd> on a PC '
                '(the app shows CTRL). Full screen is <kbd>F11</kbd>.</span></div>')
    # pages of two columns, the groups in order: each column filled until the next group won't fit (heights measured
    # once in a browser - see measure() - so nothing ever runs off a page), then the next column, then the next page
    present = [g for g in ORDER if grouped[g]]
    body.append("@@GROUPS@@")
    notes_html = "".join('<div class="status sunken note"><span class="tag">NOTE</span><span>%s</span></div>' % inline(n) for n in notes)

    extra_css = """
<style>
/* the KEYS sheet: two columns of groups, compact */
@page { size: A4; margin: 10mm 10mm 13mm; }
body { font-size: 13pt; line-height: 1.15; }
.keys { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 0 5mm; align-items: start; break-before: page; }
.keys.first { break-before: auto; }
.col { min-width: 0; }
section.grp { break-inside: avoid; padding: 3mm 3.5mm 3.5mm; margin: 0 0 3mm; }
section.grp h2 { font-size: 13pt; margin: 0 0 1mm; }
section.grp h2::before, section.grp h2::after { height: 11px; }
.accent { height: 4px; margin: 1mm 0 2.5mm; }
.tbl { margin: 0; }
table { font-size: 12pt; }
td { padding: 2px 6px; }
td:first-child { white-space: normal; width: 40%; }
tr:nth-child(4n+1) td { background: transparent; }
tr:nth-child(odd) td { background: #f2f2f2; }
kbd { font-size: 0.72em; padding: 0 4px; border-bottom-width: 3px; margin: 0 1px; }
.titlebar { margin-bottom: 4mm; }
.titlebar .name { font-size: 24pt; }
.status { max-width: none; margin: 0 0 4mm; font-size: 12pt; }
.status.note { margin: 0 0 3mm; }
</style>"""
    def group_html(g):
        if g == "__notes__":
            return notes_html
        s = ['<section class="grp raised"><h2>%s</h2><div class="accent %s"></div><div class="tbl sunken"><table><tbody>' % (html.escape(g), COLOURS[g])]
        for k, a in grouped[g]:
            s.append("<tr><td>%s</td><td>%s</td></tr>" % (inline(k), inline(a)))
        s.append("</tbody></table></div></section>")
        return "".join(s)

    def page_html(pages):
        out = []
        for pi, cols in enumerate(pages):
            out.append('<div class="keys%s">' % (" first" if pi == 0 else ""))
            for col in cols:
                out.append('<div class="col">' + "".join(group_html(g) for g in col) + "</div>")
            out.append("</div>")
        return "\n".join(out)

    header = "".join(b for b in body if b != "@@GROUPS@@")
    heights = measure(style, extra_css, present + (["__notes__"] if notes else []), group_html, header)
    pages, cols, col, used = [], [], [], 0.0
    first_cap, cap = PAGE_H - heights["__header__"] - GAP, PAGE_H
    for g in present:
        room = first_cap if not pages else cap
        h = heights[g] + GAP
        if col and used + h > room:
            cols.append(col); col, used = [], 0.0
            if len(cols) == 2:
                pages.append(cols); cols = []
        col.append(g); used += h
    if col: cols.append(col)
    if cols: pages.append(cols)
    # the notes: in the first column with room for them (else a column of their own)
    if notes:
        placed = False
        for pi, pg in enumerate(pages):
            room = first_cap if pi == 0 else cap
            for c in pg:
                if room - sum(heights[g] + GAP for g in c) >= heights["__notes__"] + GAP:
                    c.append("__notes__"); placed = True; break
            if placed: break
        if not placed:
            if len(pages[-1]) < 2: pages[-1].append(["__notes__"])
            else: pages.append([["__notes__"]])
    body = [b if b != "@@GROUPS@@" else page_html(pages) for b in body]

    doc = ("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Paula's Revenge Keys</title>"
           + style + extra_css + "</head><body>\n" + "\n".join(body) + "\n</body></html>")
    open(out, "w", encoding="utf-8").write(doc)
    unsorted = [k for k, _ in grouped["MORE"]]
    print("keys sheet: %d rows from the Keys table, %d extras%s" % (len(rows), len(EXTRAS),
          ("; not grouped (in MORE): " + ", ".join(unsorted)) if unsorted else ""))


if __name__ == "__main__":
    main()
