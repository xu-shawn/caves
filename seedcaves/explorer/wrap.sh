#!/bin/bash
# The page as the Artifact viewer serves it (its skeleton around index.html), for local screenshots:
# wrap.sh [dark] > file.html
cd "$(dirname "$0")"
printf '<!doctype html><html%s><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"><style>:root{color-scheme:light;padding:env(safe-area-inset-top,0px) 0 env(safe-area-inset-bottom,0px)}body{margin:0;font:14px system-ui,sans-serif;background:#faf9f5}img{max-width:100%%}[hidden]{display:none!important}</style></head><body>' "${1:+ data-theme=\"$1\"}"
cat index.html; printf '</body></html>'
