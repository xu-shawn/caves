#!/usr/bin/env python3
"""The record cavern's page (record/cavern_page.html, a fragment for the Artifact tool, and record/preview.html, the same
in a full document for a local browser) from page_template.html, facts.json, volume4.bin and the three pictures.
usage: record/build_page.py     (after record/render.py)"""
import base64, json, os
here = os.path.dirname(os.path.abspath(__file__)); f = json.load(open(os.path.join(here, 'facts.json')))
def uri(name): return 'data:image/webp;base64,' + base64.b64encode(open(os.path.join(here, name), 'rb').read()).decode()
ny, nz, nx = f['viewer']['shape_yzx']; ybase = f['viewer']['origin'][1]; x0, x1, y0, y1, z0, z1 = f['box']
fill = {'VOLUME_B64': base64.b64encode(open(os.path.join(here, 'volume4.bin'), 'rb').read()).decode(), 'MAP': uri('map.webp'), 'SECTIONS': uri('sections.webp'), 'RENDER': uri('render3d.webp'),
        'VOLUME': f'{f["volume"]:,}', 'SEED': f['seed'], 'TPX': f['tp'][0], 'TPY': f['tp'][1], 'TPZ': f['tp'][2], 'SX': f'{f["size"][0]:,}', 'SY': f['size'][1], 'SZ': f'{f["size"][2]:,}',
        'X0': x0, 'X1': x1, 'Y0': y0, 'Y1': y1, 'Z0': z0, 'Z1': z1, 'YTOP': ybase + 4 * ny, 'COLUMNS': f'{f["columns"]:,}', 'MEAN': f'{f["mean_open_height"]:.0f}', 'MAXH': f['largest_open_height'],
        'SHARE': f'{100 * f["share_of_box_footprint"]:.0f}', 'WIDEST': f['widest_level'], 'WIDESTN': f'{f["columns_at_widest_level"]:,}', 'HALF': f['half_volume_below_y'], 'NX': nx, 'NY': ny, 'NZ': nz, 'YBASE': ybase}
page = open(os.path.join(here, 'page_template.html')).read()
for k in sorted(fill, key=len, reverse=True): page = page.replace(f'__{k}__', str(fill[k]))
assert '__' not in page.split('<script>')[0].replace('__proto__', ''), [w for w in page.split() if w.startswith('__')][:5]
open(os.path.join(here, 'cavern_page.html'), 'w').write(page)
open(os.path.join(here, 'preview.html'), 'w').write('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover"><style>body{margin:0;font:14px system-ui}img{max-width:100%}[hidden]{display:none!important}</style></head><body>' + page + '</body></html>')
print(f'wrote cavern_page.html, {len(page) / 1e6:.2f} MB')
