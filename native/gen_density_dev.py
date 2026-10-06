#!/usr/bin/env python3
"""density_gen.hpp (the generated overworld density functions) rewritten for the GPU kernel of gpudensity.cu:
density_dev.hpp. Device functions kept as calls; and every noise sample at a fixed scale of the block position
("p.w->noise[N].get(p.x * A, p.y * B, p.z * C)" with B not 0, and the blended base noise) becomes siteGet(p, K) /
blendedGet(p), which read the lattice positions the kernel has made once per column (x, z) and once per level (y)
instead of redoing the double-precision arithmetic for every sample (gamegen_dev.hpp). Also writes density_sites.inc:
the sites' noise and scales.     usage: gen_density_dev.py   (in native/)"""
import re
src = open('density_gen.hpp').read()
src = re.sub(r'^static float ', '__device__ __noinline__ static float ', src, flags=re.M)
src = src.replace('#include "gamegen_types.hpp"', '#include "gamegen_dev.hpp"').replace('std::min', 'gmin').replace('std::max', 'gmax').replace('std::fabs', 'fabsf')
sites = []
def site(m):
    key = (int(m.group(1)), m.group(2), m.group(3), m.group(4))
    if float(key[2]) == 0.0: return m.group(0)   # flat in y: a per-column value, made once per column anyway
    if key not in sites: sites.append(key)
    return f'siteGet(p, {sites.index(key)})'
number = r'([0-9.eE+-]+)'
src, n = re.subn(r'p\.w->noise\[(\d+)\]\.get\(p\.x \* ' + number + r', p\.y \* ' + number + r', p\.z \* ' + number + r'\)', site, src)
src, nb = re.subn(r'p\.w->blended\.get\(p\.x, p\.y, p\.z\)', 'blendedGet(p)', src)
left = len(re.findall(r'p\.w->noise\[\d+\]\.get\(', src))
open('density_dev.hpp', 'w').write(src)
open('density_sites.inc', 'w').write('// made by gen_density_dev.py: {noise, x scale, y scale, z scale} of the sampling sites\n' + ''.join(f'{{{k[0]}, {k[1]}, {k[2]}, {k[3]}}},\n' for k in sites))
print(f'{n} samples at {len(sites)} sites, {nb} of the blended noise; {left} samples left as they were (flat or shifted: per column)')
