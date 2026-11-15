#!/bin/sh
# Paula's Revenge - the printed docs, made together every release (after README.md and the manual are updated):
#   the manual PDF (from docs/manual/print.html) and the KEYS sheet (every shortcut, from README.md's Keys table),
#   both in the manual's look. Needs Node with Playwright (Chromium) and Python 3.
set -e
cd "$(dirname "$0")/.."
D="$PWD/docs/manual"
node tools/pdf.js "$D/print.html" "$D/Paulas-Revenge-Manual.pdf"
cp "$D/Paulas-Revenge-Manual.pdf" "$D/Paulas Revenge Manual.pdf"   # (the older name: links to it keep working)
python3 tools/make_shortcuts.py README.md "$D/print.html" "$D/keys.html"
node tools/pdf.js "$D/keys.html" "$D/Paulas-Revenge-Keys.pdf" "PAULA'S REVENGE KEYS"
echo "docs: manual and keys PDFs made in docs/manual"
