// Host side of gputerrain.cu: a seed's terrain octaves and the offset spline in the kernel's layout, from cubiomes.
#pragma once
extern "C" {
#include "biomenoise.h"
}
#include "gputerrain.h"
#include <cmath>
#include <cstring>
#include <vector>
static inline void terrainSeed(const BiomeNoise &bn, TerrainSeed &out) {
    const int noises[3] = {NP_CONTINENTALNESS, NP_EROSION, NP_WEIRDNESS}, octaves[3] = {3, 2, 2}; const double f = 337.0 / 331.0, FX = 1099511627776.0; int at = 0;
    for (int n = 0; n < 3; n++) {
        const DoublePerlinNoise &noise = bn.climate[noises[n]]; out.scale[n] = (float) noise.amplitude;
        for (int half = 0; half < 2; half++) for (int i = 0; i < octaves[n]; i++) {
            const OctaveNoise &stack = half ? noise.octB : noise.octA; TerrainOctave &o = out.octave[at++];
            if (i >= stack.octcnt) { memset(&o, 0, sizeof o); continue; }
            const PerlinNoise &p = stack.octaves[i]; memcpy(o.d, p.d, 257); o.d[257] = o.d[258] = o.d[259] = 0;
            o.lac = llround(p.lacunarity * (half ? f : 1.0) * FX); o.a = llround(p.a * FX); o.c = llround(p.c * FX); o.amplitude = (float) p.amplitude;
            const double y = 0.0 + p.b, i2 = std::floor(y), d2 = y - i2; o.h2 = (int) (uint8_t) (int) i2; o.d2 = (float) d2; o.t2 = (float) (d2 * d2 * d2 * (d2 * (d2 * 6.0 - 15.0) + 10.0));
        }
    }
    out.pad = 0;
}
// the spline tree as an array (node 0 the root); returns its depth
static inline int flattenSpline(const Spline *sp, std::vector<FlatSpline> &nodes) {
    const int index = (int) nodes.size(); nodes.emplace_back(); FlatSpline node; memset(&node, 0, sizeof node);
    if (sp->len == 1) { node.len = 1; node.val = ((const FixSpline *) sp)->val; nodes[index] = node; return 0; }
    node.len = sp->len; node.typ = sp->typ; int depth = 0;
    for (int i = 0; i < sp->len; i++) { node.loc[i] = sp->loc[i]; node.der[i] = sp->der[i]; node.child[i] = (int) nodes.size(); depth = std::max(depth, 1 + flattenSpline(sp->val[i], nodes)); }
    nodes[index] = node; return depth;
}
