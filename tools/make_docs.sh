#!/bin/sh
# Paula's Revenge - the printed docs, made together every release (after README.md and the manual are updated):
#   the manual PDF (from docs/manual/print.html) and the KEYS sheet (every shortcut, from README.md's Keys table),
#   and the ADDENDUM (docs/manual/addendum.html), all in the manual's look. Needs Node with Playwright (Chromium) and Python 3.
set -e
cd "$(dirname "$0")/.."
D="$PWD/docs/manual"
node tools/pdf.js "$D/print.html" "$D/Paulas-Revenge-Manual.pdf"
# (the screenshots as 256-colour indexed images: lossless for them, and half the size - needs pikepdf)
python3 tools/pdf_index.py "$D/Paulas-Revenge-Manual.pdf" "$D/manual-small.pdf" && mv "$D/manual-small.pdf" "$D/Paulas-Revenge-Manual.pdf"
cp "$D/Paulas-Revenge-Manual.pdf" "$D/Paulas Revenge Manual.pdf"   # (the older name: links to it keep working)
python3 tools/make_shortcuts.py README.md "$D/print.html" "$D/keys.html"
node tools/pdf.js "$D/keys.html" "$D/Paulas-Revenge-Keys.pdf" "PAULA'S REVENGE KEYS"
# (v1.01) the addendum: what's changed since the manual was last brought up to date, until it's folded in
node tools/pdf.js "$D/addendum.html" "$D/Paulas-Revenge-Addendum.pdf" "PAULA'S REVENGE MANUAL ADDENDUM"
echo "docs: manual, keys and addendum PDFs made in docs/manual"
