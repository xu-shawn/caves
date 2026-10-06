// The exact volume of a cavern, natively: a flood fill that makes the game's terrain (gamegen.hpp, block-for-block the
// same as Minecraft 26.3's noise fill and aquifers) only where the cavern goes.
//
// The measure (passage width 7): underground air = air blocks with a non-air block above them; the core = the centres
// of 7 x 7 x 7 cubes of underground air; a cavern = a connected piece of the core (6 neighbours) and all the underground
// air within 3 blocks (Chebyshev) of it. Its volume is its number of air blocks.
//
// How: a column of blocks is 320 bits. The core is the AND of shifted columns (along y, then x, then z), the flood fill
// moves whole runs of core from column to column, the swept air is the OR of shifted columns. One thread per cavern;
// THREADS caverns at a time.
//
// ---- deepdark/ddmeasure.cpp: a copy of native/cavemeasure.cpp that also says how much of the cavern is deep dark ----
// The measure itself is unchanged (same volume, core, box). Added: the biome of the cavern's air blocks, from cubiomes
// (ddbiome.hpp). Deep dark is the lowest cells of a column of biome cells (checked on 190 M cells by ddbox, and with
// DD_FULLSCAN=1 here), so a column of cells is summed up by its highest deep dark cell.
//   cellblocks: the cavern's air blocks whose own 4 x 4 x 4 biome cell is deep dark (the definition used)
//   zoomblocks: the cavern's air blocks the game's BiomeManager zoom puts in deep dark (what F3 shows at the block)
//   cells / ddcells: the biome cells that hold air of the cavern / the deep dark ones among them
//   ddairtop: the highest air block of the cavern in a deep dark cell; witness: the lowest such block of the first
//   column found (a block to look at in the game)
// Environment:
//   DD_EARLY=1       stop at the first core block of the cavern in a deep dark cell and print
//                    "SEED X Z HIT x y z chunks N seconds S | rest" (cheap proof that a cavern is not deep-dark free);
//                    a cavern with no such block is measured in full as usual.
//   DD_EARLY=both    the same, but the block has to be deep dark by the zoom as well (not free by either definition).
//   DD_EXPORT_DIR=d  write d/cells_SEED_X_Y_Z.bin: per column of cells within one cell of the cavern, 48 bytes little
//                    endian: int32 qx, qz; uint64 own[2] (cells with air of the cavern, bit qy + 16); uint64 near[2]
//                    (those and the cells touching them); int32 qtop (highest deep dark cell per cubiomes, INT_MIN:
//                    none); int32 0. For java/BiomeCells, which asks the real game about each of those cells.
//   DD_BUMP=n        n units of 1/10000 added to every cell's depth (ddbiome.hpp): -3 = deep dark with a margin, no tie
//                    in play; +2 = deep dark or a tie the game may turn into deep dark.
//   DD_FULLSCAN=1    look up all 80 cells of every column and stop if deep dark is not exactly the lowest ones.
//
// build: g++ -O2 -std=c++17 -ffp-contract=off -c ddbiome.cpp -I ../vendor/cubiomes -o ddbiome.o
//        g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes ddmeasure.cpp ddbiome.o ../vendor/cub_*.o -lpthread -o ddmeasure
// usage: ddmeasure [THREADS=1] < lines "SEED X Y Z [anything]"
// The fill starts at the block X Y Z if it is in a core, otherwise in the largest piece of core within 4 chunks of it.
// prints "SEED X Z VOLUME CORE x0 x1 y0 y1 z0 z1 at TX TY TZ chunks N seconds S | dd cellblocks N zoomblocks N cells N
// ddcells N ddairtop Y witness x y z | rest" as each cavern is done (TX TY TZ: a core block near the middle of the
// cavern, a place to teleport to).
#include "../native/gamegen.hpp"
#include "ddbiome.hpp"
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

static const int R = 3;
struct Bits {
    uint64_t w[GEN_WORDS];
    bool any() const { uint64_t all = 0; for (uint64_t v : w) all |= v; return all != 0; }
    int count() const { int n = 0; for (uint64_t v : w) n += __builtin_popcountll(v); return n; }
    bool test(int i) const { return w[i >> 6] >> (i & 63) & 1; }
    static Bits none() { Bits r; for (uint64_t &v : r.w) v = 0; return r; }
    static Bits one(int i) { Bits r = none(); r.w[i >> 6] = 1ULL << (i & 63); return r; }
};
static inline Bits operator&(Bits a, const Bits &b) { for (int i = 0; i < GEN_WORDS; i++) a.w[i] &= b.w[i]; return a; }
static inline Bits operator|(Bits a, const Bits &b) { for (int i = 0; i < GEN_WORDS; i++) a.w[i] |= b.w[i]; return a; }
static inline Bits without(Bits a, const Bits &b) { for (int i = 0; i < GEN_WORDS; i++) a.w[i] &= ~b.w[i]; return a; }
static inline Bits up(const Bits &a, int k) { // towards higher y; what leaves the column is lost
    Bits r; int words = k >> 6, bits = k & 63;
    for (int i = GEN_WORDS - 1; i >= 0; i--) {
        int from = i - words; uint64_t v = 0;
        if (from >= 0) { v = a.w[from] << bits; if (bits && from > 0) v |= a.w[from - 1] >> (64 - bits); }
        r.w[i] = v;
    }
    return r;
}
static inline Bits down(const Bits &a, int k) {
    Bits r; int words = k >> 6, bits = k & 63;
    for (int i = 0; i < GEN_WORDS; i++) {
        int from = i + words; uint64_t v = 0;
        if (from < GEN_WORDS) { v = a.w[from] >> bits; if (bits && from + 1 < GEN_WORDS) v |= a.w[from + 1] << (64 - bits); }
        r.w[i] = v;
    }
    return r;
}
static inline Bits erodeY(const Bits &a) { Bits t = a & up(a, 1) & down(a, 1); return t & up(t, 2) & down(t, 2); }     // all of y - 3 .. y + 3
static inline Bits dilateY(const Bits &a) { Bits t = a | up(a, 1) | down(a, 1); return t | up(t, 2) | down(t, 2); }    // any of y - 3 .. y + 3
// every bit of open that is joined to a bit of seed along y (seed must be inside open)
static inline Bits fillY(const Bits &seed, const Bits &open) {
    Bits high = seed, low = seed, pass = open;
    for (int k = 1; k < GEN_H; k <<= 1) { high = high | (pass & up(high, k)); pass = pass & up(pass, k); }
    pass = open;
    for (int k = 1; k < GEN_H; k <<= 1) { low = low | (pass & down(low, k)); pass = pass & down(pass, k); }
    return high | low;
}

static inline Bits lowBits(int n) { // the lowest n bits (blocks below y = GEN_MIN_Y + n)
    Bits r = Bits::none();
    for (int i = 0; i < GEN_WORDS && n > 0; i++, n -= 64) r.w[i] = n >= 64 ? ~0ULL : (1ULL << n) - 1;
    return r;
}
static inline uint64_t groups4(uint64_t w) { // bit k: any of the bits 4k .. 4k + 3 (a word of blocks to its 16 biome cells)
    w |= w >> 1; w |= w >> 2; w &= 0x1111111111111111ULL;
    w = (w | w >> 3) & 0x0303030303030303ULL; w = (w | w >> 6) & 0x000F000F000F000FULL;
    w = (w | w >> 12) & 0x000000FF000000FFULL; w = (w | w >> 24) & 0xFFFFULL;
    return w;
}
static const int QY_MIN = GEN_MIN_Y >> 2, QY_CELLS = GEN_H / 4, DD_NONE = INT_MIN; // 80 cells of a column: 64 + 16 bits
struct Cells { uint64_t lo = 0, hi = 0; };
static inline Cells cellsOf(const Bits &a) {
    Cells c;
    for (int i = 0; i < GEN_WORDS; i++) { uint64_t g = groups4(a.w[i]); if (i < 4) c.lo |= g << (16 * i); else c.hi |= g << (16 * (i - 4)); }
    return c;
}
static inline int cellCount(const Cells &c) { return __builtin_popcountll(c.lo) + __builtin_popcountll(c.hi); }
static inline Cells cellsUpTo(int qtop) { // the cells qy <= qtop
    Cells c; int n = qtop == DD_NONE ? 0 : std::min(QY_CELLS, qtop - QY_MIN + 1);
    if (n > 0) c.lo = n >= 64 ? ~0ULL : (1ULL << n) - 1;
    if (n > 64) c.hi = (1ULL << (n - 64)) - 1;
    return c;
}

struct Chunk { Bits air[256], core[256], seen[256]; bool hasCore = false, touched = false; int x, z; };
struct Event { int x, z; Bits bits; };

struct Measure {
    static const int SPAN = 1024; // the chunks around the start that can be made, each way: +-8192 blocks
    AirMaker maker; int originX = 0, originZ = 0;
    std::vector<Chunk *> directory; std::vector<std::unique_ptr<Chunk>> chunks;
    std::vector<Event> stack, waiting;
    size_t core = 0; double sumX = 0, sumY = 0, sumZ = 0;
    const DDBiome *dd; long long seed; bool early, earlyBoth, fullScan, watch = false, hit = false; int hitAt[3] = {0, 0, 0}; const char *exportDir;
    std::unordered_map<uint64_t, int> ddTops;
    Measure(const World &w, const DDBiome *biome) : maker(w), directory((size_t) SPAN * SPAN, NULL), dd(biome), seed((long long) w.seed) {
        early = getenv("DD_EARLY") != NULL; earlyBoth = early && !strcmp(getenv("DD_EARLY"), "both"); fullScan = getenv("DD_FULLSCAN") != NULL; exportDir = getenv("DD_EXPORT_DIR");
    }
    static uint64_t cellKey(int qx, int qz) { return ((uint64_t) (uint32_t) qx << 32) | (uint32_t) qz; }
    // the highest deep dark cell (qy) of the column of cells qx, qz, or DD_NONE: the cells at and below it are deep dark
    int ddTop(int qx, int qz) {
        auto it = ddTops.find(cellKey(qx, qz));
        if (it != ddTops.end()) return it->second;
        DDClimate cc; ddColumn(dd, qx, qz, &cc); int top = DD_NONE;
        if (ddBiomeAt(&cc, QY_MIN) == DD_DEEP_DARK) {
            int lo = QY_MIN, hi = QY_MIN + QY_CELLS; // lo is deep dark, hi is not (or is above the world)
            while (hi - lo > 1) { int mid = lo + (hi - lo) / 2; if (ddBiomeAt(&cc, mid) == DD_DEEP_DARK) lo = mid; else hi = mid; }
            top = lo;
        }
        if (fullScan) for (int qy = QY_MIN; qy < QY_MIN + QY_CELLS; qy++) if ((ddBiomeAt(&cc, qy) == DD_DEEP_DARK) != (top != DD_NONE && qy <= top)) {
            fprintf(stderr, "deep dark is not the bottom of the column of cells %d %d (cell %d, top %d), seed %lld\n", qx, qz, qy, top, seed); abort();
        }
        ddTops.emplace(cellKey(qx, qz), top);
        return top;
    }

    bool inRange(int cx, int cz) const { return (unsigned) (cx - originX) < (unsigned) SPAN && (unsigned) (cz - originZ) < (unsigned) SPAN; }
    Chunk *chunkAt(int cx, int cz) const { return inRange(cx, cz) ? directory[(size_t) (cz - originZ) * SPAN + (cx - originX)] : NULL; }
    Chunk *withAir(int cx, int cz) {
        if (!inRange(cx, cz)) return NULL;
        Chunk *&slot = directory[(size_t) (cz - originZ) * SPAN + (cx - originX)];
        if (slot) return slot;
        chunks.emplace_back(new Chunk); Chunk *c = chunks.back().get(); c->x = cx; c->z = cz;
        maker.chunkAir(cx, cz, c->air[0].w);
        for (int i = 0; i < 256; i++) { // underground air: below the column's highest block that is not air
            Bits &a = c->air[i]; int word = GEN_WORDS - 1;
            while (word >= 0 && a.w[word] == ~0ULL) a.w[word--] = 0;
            if (word >= 0) { int top = 63 - __builtin_clzll(~a.w[word]); a.w[word] &= (1ULL << top) - 1; }
            c->core[i] = c->seen[i] = Bits::none();
        }
        return slot = c;
    }
    // a column of the 3 x 3 chunks around a chunk: x, z from -16 to 31
    static const Bits *column(Chunk *const near[3][3], Bits (Chunk::*plane)[256], int x, int z) {
        Chunk *c = near[(z >> 4) + 1][(x >> 4) + 1];
        return c ? &(c->*plane)[(z & 15) * 16 + (x & 15)] : NULL;
    }
    // out[z * 16 + x]: for erode, the bits that are set in plane at every block of the 7 x 7 x 7 cube around; else at any
    void cubes(int cx, int cz, Bits (Chunk::*plane)[256], bool erode, Bits *out) {
        Chunk *near[3][3]; Bits alongY[16 + 2 * R][16 + 2 * R], alongX[16 + 2 * R][16];
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) near[dz + 1][dx + 1] = chunkAt(cx + dx, cz + dz);
        for (int pz = 0; pz < 16 + 2 * R; pz++) for (int px = 0; px < 16 + 2 * R; px++) {
            const Bits *b = column(near, plane, px - R, pz - R);
            alongY[pz][px] = b ? (erode ? erodeY(*b) : dilateY(*b)) : Bits::none();
        }
        for (int pz = 0; pz < 16 + 2 * R; pz++) for (int x = 0; x < 16; x++) {
            Bits v = alongY[pz][x];
            for (int k = 1; k <= 2 * R; k++) v = erode ? v & alongY[pz][x + k] : v | alongY[pz][x + k];
            alongX[pz][x] = v;
        }
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            Bits v = alongX[z][x];
            for (int k = 1; k <= 2 * R; k++) v = erode ? v & alongX[z + k][x] : v | alongX[z + k][x];
            out[z * 16 + x] = v;
        }
    }
    Chunk *withCore(int cx, int cz) {
        Chunk *c = withAir(cx, cz);
        if (!c || c->hasCore) return c;
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) withAir(cx + dx, cz + dz);
        cubes(cx, cz, &Chunk::air, true, c->core);
        c->hasCore = true;
        return c;
    }

    // The fill. arrive: the core of column x, z that touches bits (of a neighbouring column, or the start) is reached.
    // With a limit (chunk x0..x1, z0..z1) the fill stays inside it; without, columns of chunks not made yet wait.
    void arrive(int x, int z, const Bits &bits, const int *limit) {
        int cx = x >> 4, cz = z >> 4;
        if (limit && (cx < limit[0] || cx > limit[1] || cz < limit[2] || cz > limit[3])) return;
        Chunk *c = chunkAt(cx, cz);
        if (!c || !c->hasCore) { if (inRange(cx, cz)) waiting.push_back({x, z, bits}); return; }
        int i = (z & 15) * 16 + (x & 15);
        Bits fresh = without(bits & c->core[i], c->seen[i]);
        if (!fresh.any()) return;
        fresh = without(fillY(fresh, c->core[i]), c->seen[i]);
        c->seen[i] = c->seen[i] | fresh; c->touched = true;
        if (watch && !hit) { // DD_EARLY: is any of this core in a deep dark cell?
            int top = ddTop(x >> 2, z >> 2);
            if (top != DD_NONE) {
                Bits deep = fresh & lowBits(top * 4 + 4 - GEN_MIN_Y);
                if (deep.any() && !earlyBoth) { int word = 0; while (!deep.w[word]) word++; hit = true; hitAt[0] = x; hitAt[1] = GEN_MIN_Y + word * 64 + __builtin_ctzll(deep.w[word]); hitAt[2] = z; }
                else for (int word = 0; word < GEN_WORDS && !hit; word++) for (uint64_t v = deep.w[word]; v && !hit; v &= v - 1) { // DD_EARLY=both: and deep dark by the zoom
                    int y = GEN_MIN_Y + word * 64 + __builtin_ctzll(v), qx, qy, qz;
                    ddVoronoi(dd, x, y, z, &qx, &qy, &qz);
                    int zoomTop = ddTop(qx, qz);
                    if (zoomTop != DD_NONE && qy <= zoomTop) { hit = true; hitAt[0] = x; hitAt[1] = y; hitAt[2] = z; }
                }
            }
        }
        int n = fresh.count(); core += n; sumX += (double) x * n; sumZ += (double) z * n;
        for (int word = 0; word < GEN_WORDS; word++) for (uint64_t v = fresh.w[word]; v; v &= v - 1) sumY += GEN_MIN_Y + word * 64 + __builtin_ctzll(v);
        stack.push_back({x, z, fresh});
    }
    void fill(int x, int y, int z, const int *limit) {
        core = 0; sumX = sumY = sumZ = 0;
        arrive(x, z, Bits::one(y - GEN_MIN_Y), limit);
        while (true) {
            while (!stack.empty() && !hit) {
                Event e = stack.back(); stack.pop_back();
                arrive(e.x + 1, e.z, e.bits, limit); arrive(e.x - 1, e.z, e.bits, limit); arrive(e.x, e.z + 1, e.bits, limit); arrive(e.x, e.z - 1, e.bits, limit);
            }
            if (waiting.empty() || hit) break;
            std::vector<Event> again; again.swap(waiting);
            for (const Event &e : again) withCore(e.x >> 4, e.z >> 4);
            for (const Event &e : again) arrive(e.x, e.z, e.bits, limit);
        }
    }

    std::string measure(int startX, int startY, int startZ) {
        auto t0 = std::chrono::steady_clock::now();
        int cx0 = startX >> 4, cz0 = startZ >> 4; originX = cx0 - SPAN / 2; originZ = cz0 - SPAN / 2;
        const int limit[4] = {cx0 - 4, cx0 + 4, cz0 - 4, cz0 + 4};
        // where to start: the given block if it is core, else the largest piece of core among the first chunks
        int start[3] = {startX, startY, startZ}; bool found = false;
        auto startIsCore = [&] { return startY >= GEN_MIN_Y && startY < GEN_MIN_Y + GEN_H && chunkAt(cx0, cz0)->core[(startZ & 15) * 16 + (startX & 15)].test(startY - GEN_MIN_Y); };
        if (early) { withCore(cx0, cz0); found = startIsCore(); } // DD_EARLY: no need for the 9 x 9 chunks when the start is core
        if (!found) for (int cz = limit[2]; cz <= limit[3]; cz++) for (int cx = limit[0]; cx <= limit[1]; cx++) withCore(cx, cz);
        if (found || startIsCore()) found = true;
        else {
            size_t largest = 0;
            for (int cz = limit[2]; cz <= limit[3]; cz++) for (int cx = limit[0]; cx <= limit[1]; cx++) for (int i = 0; i < 256; i++) {
                Chunk *c = chunkAt(cx, cz);
                for (Bits left = without(c->core[i], c->seen[i]); left.any(); left = without(c->core[i], c->seen[i])) {
                    int word = 0; while (!left.w[word]) word++;
                    int x = (cx << 4) + (i & 15), y = GEN_MIN_Y + word * 64 + __builtin_ctzll(left.w[word]), z = (cz << 4) + (i >> 4);
                    fill(x, y, z, limit);
                    if (core > largest) { largest = core; start[0] = x; start[1] = y; start[2] = z; found = true; }
                }
            }
            for (int cz = limit[2]; cz <= limit[3]; cz++) for (int cx = limit[0]; cx <= limit[1]; cx++) { Chunk *c = chunkAt(cx, cz); c->touched = false; for (Bits &b : c->seen) b = Bits::none(); }
        }
        char buf[512];
        auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        if (!found) { snprintf(buf, sizeof buf, "0 0 0 0 0 0 0 0 at 0 0 0 chunks %zu seconds %.1f", chunks.size(), elapsed()); return buf; }
        watch = early; fill(start[0], start[1], start[2], NULL); watch = false;
        if (hit) { snprintf(buf, sizeof buf, "HIT %d %d %d chunks %zu seconds %.1f", hitAt[0], hitAt[1], hitAt[2], chunks.size(), elapsed()); return buf; }

        // the air the cube sweeps: within R of the core that was reached, in the chunks reached and the ones around them
        double mx = sumX / core, my = sumY / core, mz = sumZ / core;
        size_t volume = 0; int box[6] = {INT_MAX, INT_MIN, INT_MAX, INT_MIN, INT_MAX, INT_MIN}; double nearest = 1e300; int stand[3] = {0, 0, 0};
        std::vector<Chunk *> sweep; // (the chunks around a reached one exist: their air was made for its core)
        for (size_t k = 0; k < chunks.size(); k++) if (chunks[k]->touched) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            Chunk *c = chunkAt(chunks[k]->x + dx, chunks[k]->z + dz);
            if (c) sweep.push_back(c);
        }
        std::sort(sweep.begin(), sweep.end()); sweep.erase(std::unique(sweep.begin(), sweep.end()), sweep.end());
        Bits swept[256];
        size_t ddCell = 0, ddZoom = 0; int ddAirTop = INT_MIN, witness[3] = {0, 0, 0}; bool haveWitness = false;
        std::unordered_map<uint64_t, Cells> own; // the biome cells with air of the cavern, by column of cells
        for (Chunk *c : sweep) {
            cubes(c->x, c->z, &Chunk::seen, false, swept);
            int tops[6][6], maxTop = DD_NONE; bool haveTops = false; // the columns of cells of the chunk and one more all round
            Cells mine[16]; bool anyAir = false;
            for (int i = 0; i < 256; i++) {
                Bits a = swept[i] & c->air[i];
                if (!a.any()) continue;
                volume += a.count();
                int wx = (c->x << 4) + (i & 15), wz = (c->z << 4) + (i >> 4), low = 0, high = GEN_WORDS - 1;
                while (!a.w[low]) low++;
                while (!a.w[high]) high--;
                int y0 = GEN_MIN_Y + low * 64 + __builtin_ctzll(a.w[low]), y1 = GEN_MIN_Y + high * 64 + 63 - __builtin_clzll(a.w[high]);
                box[0] = std::min(box[0], wx); box[1] = std::max(box[1], wx); box[2] = std::min(box[2], y0); box[3] = std::max(box[3], y1); box[4] = std::min(box[4], wz); box[5] = std::max(box[5], wz);
                // ---- deep dark ----
                if (!haveTops) {
                    for (int pz = 0; pz < 6; pz++) for (int px = 0; px < 6; px++) { tops[pz][px] = ddTop(4 * c->x - 1 + px, 4 * c->z - 1 + pz); maxTop = std::max(maxTop, tops[pz][px]); }
                    haveTops = true;
                }
                { Cells g = cellsOf(a); Cells &m = mine[((i >> 4) >> 2) * 4 + ((i & 15) >> 2)]; m.lo |= g.lo; m.hi |= g.hi; anyAir = true; }
                int ownTop = tops[1 + ((i >> 4) >> 2)][1 + ((i & 15) >> 2)];
                if (ownTop != DD_NONE) { // the blocks of the column in its own cells that are deep dark
                    Bits deep = a & lowBits(ownTop * 4 + 4 - GEN_MIN_Y);
                    if (deep.any()) {
                        ddCell += deep.count();
                        int l = 0, h = GEN_WORDS - 1; while (!deep.w[l]) l++; while (!deep.w[h]) h--;
                        ddAirTop = std::max(ddAirTop, GEN_MIN_Y + h * 64 + 63 - __builtin_clzll(deep.w[h]));
                        if (!haveWitness) { haveWitness = true; witness[0] = wx; witness[1] = GEN_MIN_Y + l * 64 + __builtin_ctzll(deep.w[l]); witness[2] = wz; }
                    }
                }
                if (maxTop != DD_NONE) { // the zoom: a block at y gets the biome of a cell qy >= (y - 2) >> 2 of the 2 x 2 columns around
                    Bits maybe = a & lowBits(4 * maxTop + 6 - GEN_MIN_Y);
                    for (int word = 0; word < GEN_WORDS; word++) for (uint64_t v = maybe.w[word]; v; v &= v - 1) {
                        int wy = GEN_MIN_Y + word * 64 + __builtin_ctzll(v), qx, qy, qz;
                        ddVoronoi(dd, wx, wy, wz, &qx, &qy, &qz);
                        int top = tops[qz - (4 * c->z - 1)][qx - (4 * c->x - 1)];
                        if (top != DD_NONE && qy <= top) ddZoom++;
                    }
                }
                // ---- end deep dark ----
                if (!c->touched) continue;
                double flat = (wx - mx) * (wx - mx) + (wz - mz) * (wz - mz);
                if (flat >= nearest) continue;
                for (int word = 0; word < GEN_WORDS; word++) for (uint64_t v = c->seen[i].w[word]; v; v &= v - 1) {
                    int wy = GEN_MIN_Y + word * 64 + __builtin_ctzll(v); double d = flat + (wy - my) * (wy - my);
                    if (d < nearest) { nearest = d; stand[0] = wx; stand[1] = wy; stand[2] = wz; }
                }
            }
            if (anyAir) for (int k = 0; k < 16; k++) if (mine[k].lo | mine[k].hi) { Cells &m = own[cellKey(4 * c->x + (k & 3), 4 * c->z + (k >> 2))]; m.lo |= mine[k].lo; m.hi |= mine[k].hi; }
        }
        size_t cells = 0, ddCells = 0;
        for (const auto &e : own) {
            Cells deep = cellsUpTo(ddTop((int) (e.first >> 32), (int) (uint32_t) e.first)); deep.lo &= e.second.lo; deep.hi &= e.second.hi;
            cells += cellCount(e.second); ddCells += cellCount(deep);
        }
        if (exportDir) { // the cells of the cavern and the ones touching them, for the real game to look up
            std::unordered_map<uint64_t, Cells> around;
            for (const auto &e : own) {
                Cells m = e.second, d; // d: m and its neighbours along y
                d.lo = m.lo | m.lo << 1 | m.lo >> 1 | m.hi << 63; d.hi = (m.hi | m.hi << 1 | m.hi >> 1 | m.lo >> 63) & 0xFFFFULL;
                int qx = (int) (e.first >> 32), qz = (int) (uint32_t) e.first;
                for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { Cells &t = around[cellKey(qx + dx, qz + dz)]; t.lo |= d.lo; t.hi |= d.hi; }
            }
            char path[600]; snprintf(path, sizeof path, "%s/cells_%lld_%d_%d_%d.bin", exportDir, seed, startX, startY, startZ);
            FILE *f = fopen(path, "wb");
            if (f) {
                for (const auto &e : around) {
                    int32_t q[2] = {(int32_t) (e.first >> 32), (int32_t) (uint32_t) e.first}; uint64_t m[4] = {0, 0, e.second.lo, e.second.hi};
                    auto it = own.find(e.first); if (it != own.end()) { m[0] = it->second.lo; m[1] = it->second.hi; }
                    int32_t t[2] = {ddTop(q[0], q[1]), 0};
                    fwrite(q, 4, 2, f); fwrite(m, 8, 4, f); fwrite(t, 4, 2, f);
                }
                fclose(f);
            } else fprintf(stderr, "cannot write %s\n", path);
        }
        char deepdark[200], top[16]; if (ddCell) snprintf(top, sizeof top, "%d", ddAirTop); else strcpy(top, "-");
        snprintf(deepdark, sizeof deepdark, " | dd cellblocks %zu zoomblocks %zu cells %zu ddcells %zu ddairtop %s witness %d %d %d", ddCell, ddZoom, cells, ddCells, top, witness[0], witness[1], witness[2]);
        snprintf(buf, sizeof buf, "%zu %zu %d %d %d %d %d %d at %d %d %d chunks %zu seconds %.1f%s", volume, core, box[0], box[1], box[2], box[3], box[4], box[5], stand[0], stand[1], stand[2], chunks.size(), elapsed(), deepdark);
        return buf;
    }
};

int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 1;
    if (getenv("DD_BUMP")) ddSetDepthBump(atoi(getenv("DD_BUMP")));
    std::vector<std::string> lines; std::string line;
    while (std::getline(std::cin, line)) lines.push_back(line);
    std::atomic<size_t> next(0); std::mutex print; std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, std::min<int>(threads, (int) lines.size())); t++) pool.emplace_back([&] {
        uint64_t cachedSeed = 0; std::unique_ptr<World> world; DDBiome *biome = NULL;
        for (size_t k; (k = next++) < lines.size();) {
            long long seed; double x, y, z; int used = 0;
            if (sscanf(lines[k].c_str(), "%lld %lf %lf %lf%n", &seed, &x, &y, &z, &used) < 4) continue;
            if (!world || cachedSeed != (uint64_t) seed) { world.reset(new World((uint64_t) seed)); if (biome) ddFree(biome); biome = ddNew((uint64_t) seed); cachedSeed = (uint64_t) seed; }
            std::string result;
            { std::unique_ptr<Measure> measure(new Measure(*world, biome)); result = measure->measure((int) x, (int) y, (int) z); }
            std::string rest = lines[k].substr(used); size_t firstChar = rest.find_first_not_of(' ');
            std::lock_guard<std::mutex> guard(print);
            printf("%lld %d %d %s | %s\n", seed, (int) x, (int) z, result.c_str(), firstChar == std::string::npos ? "" : rest.c_str() + firstChar);
            fflush(stdout);
        }
    });
    for (auto &t : pool) t.join();
}
