#!/bin/bash
# A Markdown file of docs/ as a PDF beside it, with the write-up's style sheet (needs pandoc and chromium).
# usage: docs/md2pdf.sh NAME      (docs/NAME.md -> docs/NAME.pdf)
set -e; cd "$(dirname "$0")"; n=${1%.md}
pandoc "$n.md" -f markdown+pipe_tables+implicit_figures -t html5 --standalone --wrap=none --metadata pagetitle="$(head -1 "$n.md" | sed 's/^# //')" --css progression.css -o "$n.html"
chromium --headless=new --disable-gpu --no-pdf-header-footer --virtual-time-budget=10000 --print-to-pdf="$PWD/$n.pdf" "file://$PWD/$n.html" 2>/dev/null
rm -f "$n.html"; echo "wrote $PWD/$n.pdf"
