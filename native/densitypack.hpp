// A seed's World (gamegen_types.hpp, on the host) in the layout the GPU kernel reads (gamegen_dev.hpp). Include after
// gamegen.hpp or gamegen_types.hpp.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
namespace dev {
#include "gamegen_dev.hpp"
}
static inline bool packLayers(const std::vector<GLayer> &from, dev::GLayer *to, int capacity, int &count) {
    if ((int) from.size() > capacity) { fprintf(stderr, "packWorld: %zu layers, room for %d\n", from.size(), capacity); return false; }
    count = (int) from.size();
    for (int i = 0; i < count; i++) {
        const GLayer &l = from[i]; dev::GLayer &d = to[i]; memset(&d, 0, sizeof d);
        memcpy(d.perlin.perm, l.perlin.perm, 256); d.perlin.offsetX = l.perlin.offsetX; d.perlin.offsetY = l.perlin.offsetY; d.perlin.offsetZ = l.perlin.offsetZ; d.perlin.fudgeYScale = l.perlin.fudgeYScale;
        d.frequency = l.frequency; d.amplitude = l.amplitude;
    }
    return true;
}
static inline bool packWorld(const World &w, dev::World &out) {
    memset(&out, 0, sizeof out); bool ok = true;
    for (int i = 0; i < GEN_NOISES; i++) ok = packLayers(w.noise[i].layers, out.noise[i].layers, dev::GEN_MAX_LAYERS, out.noise[i].count) && ok;
    ok = packLayers(w.blended.minLimit.layers, out.blended.minLimit.layers, 16, out.blended.minLimit.count) && ok;
    ok = packLayers(w.blended.maxLimit.layers, out.blended.maxLimit.layers, 16, out.blended.maxLimit.count) && ok;
    ok = packLayers(w.blended.mainNoise.layers, out.blended.mainNoise.layers, 16, out.blended.mainNoise.count) && ok;
    out.blended.xzMultiplier = w.blended.xzMultiplier; out.blended.yMultiplier = w.blended.yMultiplier; out.blended.xzMain = w.blended.xzMain; out.blended.yMain = w.blended.yMain;
    return ok;
}
