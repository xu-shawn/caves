// icecaves/icemeasure.cpp: ../native/cavemeasure.cpp with one more number: how many of the cavern's blocks are in
// ice caves cells of Minecraft 26.4-snapshot-3 (icebiome.hpp; a block belongs to the 4 x 4 x 4 biome cell it is in).
// The cavern itself is measured as before (the snapshot's terrain is 26.3's: same density functions, noise router and
// aquifers). Output: "... chunks N seconds S ice N | rest". ICE_TIES=1 counts cells exactly as near to another biome.
// ICE_STRICT=1: for caverns that are to be ice caves throughout: the fill stops at the first core block outside the ice
// caves and the line reads "0 CORE-SO-FAR ... ice 0 left" (a fraction of the cost of measuring a cavern that is not).
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../native -I ../vendor/cubiomes icemeasure.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icemeasure
//
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
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes cavemeasure.cpp ../vendor/cub_noise.o -lpthread -o cavemeasure
// usage: cavemeasure [THREADS=8] < lines "SEED X Y Z [anything]"
// env CAVE_DUMP=file: also writes the cavern's blocks, column by column (x, z as int32, then the column's 320 bits from y -64 up); for pictures, one cavern at a time
// env GPU_DENSITY=1: the density functions at the cell corners (95% of the time) come from gpusrv on the GPU, bit for bit the same
// The fill starts at the block X Y Z if it is in a core, otherwise in the largest piece of core within 4 chunks of it.
// prints "SEED X Z VOLUME CORE x0 x1 y0 y1 z0 z1 at TX TY TZ chunks N seconds S | rest" as each cavern is done (TX TY TZ:
// a core block near the middle of the cavern, a place to teleport to).
#include "gamegen.hpp"
#include "icebiome.hpp"
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

struct Chunk { Bits air[256], core[256], seen[256]; bool hasCore = false, touched = false; int x, z; };
struct Event { int x, z; Bits bits; };

struct Measure {
    static const int SPAN = 1024; // the chunks around the start that can be made, each way: +-8192 blocks
    AirMaker maker; int originX = 0, originZ = 0;
    std::vector<Chunk *> directory; std::vector<std::unique_ptr<Chunk>> chunks;
    std::vector<Event> stack, waiting;
    size_t core = 0; double sumX = 0, sumY = 0, sumZ = 0;
    const IceBiome *ice = NULL; std::unordered_map<uint64_t, Bits> iceMasks; int iceTies = 0; bool strict = false, left = false; int leftAt[3] = {0, 0, 0};   // leftAt: the first core block found outside the ice caves
    // the blocks of the column of cells (qx, qz) that are in ice caves cells (bit b is y = GEN_MIN_Y + b)
    const Bits &iceMask(int qx, int qz) {
        const uint64_t key = ((uint64_t) (uint32_t) qx << 32) | (uint32_t) qz; auto it = iceMasks.find(key); if (it != iceMasks.end()) return it->second;
        IceClimate c; iceClimate(ice, qx, qz, &c, true, true); uint64_t cells[2]; iceColumn(&c, cells, iceTies);
        Bits m = Bits::none(); for (int k = 0; k < GEN_H / 4; k++) if (cells[k >> 6] >> (k & 63) & 1) for (int y = 4 * k; y < 4 * k + 4; y++) m.w[y >> 6] |= 1ULL << (y & 63);
        return iceMasks.emplace(key, m).first->second;
    }
    explicit Measure(const World &w, const IceBiome *biome = NULL) : maker(w), directory((size_t) SPAN * SPAN, NULL), ice(biome) { if (getenv("GPU_DENSITY")) maker.connectGpu(); if (getenv("ICE_TIES")) iceTies = atoi(getenv("ICE_TIES")); strict = ice && getenv("ICE_STRICT") != NULL; } // GPU_DENSITY=1: the corner densities from gpusrv
    // before chunks are made: the corner columns of all of them (and of the chunks around, whose air their cores need) in one request
    void prefetch(const std::vector<std::pair<int, int>> &wanted) {
        if (maker.densityFd < 0) return;
        // (ahead: when a column is missing, ask for that many rings of chunks more than the fill needs now (2: with the card
        // shared with the gate, 2 rings gave 6% more on the yield index than 8, which fetch two thirds more columns). It will mostly
        // want them next, and what a request costs is the wait for the card, not the number of columns)
        static const int ahead = getenv("CAVE_AHEAD") ? atoi(getenv("CAVE_AHEAD")) : 2;
        std::vector<int> xz; std::unordered_map<uint64_t, char> listed;
        for (int pass = 0; pass < (ahead ? 2 : 1); pass++) { // first what is needed now; if any of that is missing, the wider list
            const int reach = pass ? 1 + ahead : 1; xz.clear(); listed.clear();
            for (const auto &w : wanted) for (int dz = -reach; dz <= reach; dz++) for (int dx = -reach; dx <= reach; dx++) {
                int cx = w.first + dx, cz = w.second + dz;
                if (!inRange(cx, cz) || chunkAt(cx, cz) || listed.count(AirMaker::key(cx, cz))) continue;
                listed[AirMaker::key(cx, cz)] = 1;
                for (int ix = 0; ix < 5; ix++) for (int iz = 0; iz < 5; iz++) { xz.push_back((cx << 4) + 4 * ix); xz.push_back((cz << 4) + 4 * iz); }
            }
            if (!pass && ahead && !maker.missingAny(xz)) return;
        }
        maker.fetch(xz);
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
        if (strict && !limit && !left) {   // ICE_STRICT: the cavern has core outside the ice caves
            const Bits out = without(fresh, iceMask(x >> 2, z >> 2));
            if (out.any()) { left = true; int word = 0; while (!out.w[word]) word++; leftAt[0] = x; leftAt[1] = GEN_MIN_Y + word * 64 + __builtin_ctzll(out.w[word]); leftAt[2] = z; }
        }
        int n = fresh.count(); core += n; sumX += (double) x * n; sumZ += (double) z * n;
        for (int word = 0; word < GEN_WORDS; word++) for (uint64_t v = fresh.w[word]; v; v &= v - 1) sumY += GEN_MIN_Y + word * 64 + __builtin_ctzll(v);
        stack.push_back({x, z, fresh});
    }
    void fill(int x, int y, int z, const int *limit) {
        core = 0; sumX = sumY = sumZ = 0;
        arrive(x, z, Bits::one(y - GEN_MIN_Y), limit);
        while (true) {
            while (!stack.empty()) {
                if (left) { stack.clear(); waiting.clear(); return; }
                Event e = stack.back(); stack.pop_back();
                arrive(e.x + 1, e.z, e.bits, limit); arrive(e.x - 1, e.z, e.bits, limit); arrive(e.x, e.z + 1, e.bits, limit); arrive(e.x, e.z - 1, e.bits, limit);
            }
            if (waiting.empty()) break;
            std::vector<Event> again; again.swap(waiting);
            { std::vector<std::pair<int, int>> wanted; for (const Event &e : again) wanted.push_back({e.x >> 4, e.z >> 4}); prefetch(wanted); }
            for (const Event &e : again) withCore(e.x >> 4, e.z >> 4);
            for (const Event &e : again) arrive(e.x, e.z, e.bits, limit);
        }
    }

    std::string measure(int startX, int startY, int startZ) {
        auto t0 = std::chrono::steady_clock::now();
        int cx0 = startX >> 4, cz0 = startZ >> 4; originX = cx0 - SPAN / 2; originZ = cz0 - SPAN / 2;
        const int limit[4] = {cx0 - 4, cx0 + 4, cz0 - 4, cz0 + 4};
        { std::vector<std::pair<int, int>> wanted; for (int cz = limit[2]; cz <= limit[3]; cz++) for (int cx = limit[0]; cx <= limit[1]; cx++) wanted.push_back({cx, cz}); prefetch(wanted); }
        for (int cz = limit[2]; cz <= limit[3]; cz++) for (int cx = limit[0]; cx <= limit[1]; cx++) withCore(cx, cz);
        // where to start: the given block if it is core, else the largest piece of core among the first chunks
        int start[3] = {startX, startY, startZ}; bool found = false;
        if (startY >= GEN_MIN_Y && startY < GEN_MIN_Y + GEN_H && chunkAt(cx0, cz0)->core[(startZ & 15) * 16 + (startX & 15)].test(startY - GEN_MIN_Y)) found = true;
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
        if (!found) { snprintf(buf, sizeof buf, "0 0 0 0 0 0 0 0 at 0 0 0 chunks %zu seconds %.1f ice 0", chunks.size(), elapsed()); return buf; }
        fill(start[0], start[1], start[2], NULL);
        if (left) {   // where it left, and the climate there: temperature, humidity, continentalness, weirdness, depth
            IceClimate c; iceClimate(ice, leftAt[0] >> 2, leftAt[2] >> 2, &c, true, true);
            snprintf(buf, sizeof buf, "0 %zu 0 0 0 0 0 0 at %d %d %d chunks %zu seconds %.1f ice 0 left at %d %d %d t %.3f h %.3f c %.3f w %.3f depth %.3f", core, start[0], start[1], start[2], chunks.size(), elapsed(), leftAt[0], leftAt[1], leftAt[2],
                     c.t, c.h, c.c, c.w, 1.0 - ((leftAt[1] >> 2) * 4) / 128.0 - 83.0 / 160.0 + c.off);
            return buf; }
        if (getenv("CAVE_TIMES")) fprintf(stderr, "after the fill %.2f s: corner columns fetched %ld in %ld requests, %.2f s, made here %ld in %.2f s; surface levels %.2f s; cells and aquifers %.2f s; %zu chunks\n", elapsed(), maker.fetched, maker.fetchCalls, maker.fetchSeconds, maker.madeHere, maker.columnSeconds, maker.skipSeconds, maker.airSeconds, chunks.size());

        // the air the cube sweeps: within R of the core that was reached, in the chunks reached and the ones around them
        double mx = sumX / core, my = sumY / core, mz = sumZ / core;
        size_t volume = 0, iceVolume = 0; int box[6] = {INT_MAX, INT_MIN, INT_MAX, INT_MIN, INT_MAX, INT_MIN}; double nearest = 1e300; int stand[3] = {0, 0, 0};
        std::vector<Chunk *> sweep; // (the chunks around a reached one exist: their air was made for its core)
        for (size_t k = 0; k < chunks.size(); k++) if (chunks[k]->touched) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            Chunk *c = chunkAt(chunks[k]->x + dx, chunks[k]->z + dz);
            if (c) sweep.push_back(c);
        }
        std::sort(sweep.begin(), sweep.end()); sweep.erase(std::unique(sweep.begin(), sweep.end()), sweep.end());
        FILE *dump = getenv("CAVE_DUMP") ? fopen(getenv("CAVE_DUMP"), "wb") : NULL; // the cavern's blocks, a record per column that has any: x, z (int32) and the column's bits (GEN_WORDS x uint64, bit b is y = GEN_MIN_Y + b)
        Bits swept[256];
        for (Chunk *c : sweep) {
            cubes(c->x, c->z, &Chunk::seen, false, swept);
            for (int i = 0; i < 256; i++) {
                Bits a = swept[i] & c->air[i];
                if (!a.any()) continue;
                if (dump) { const int32_t at[2] = {(c->x << 4) + (i & 15), (c->z << 4) + (i >> 4)}; fwrite(at, sizeof at, 1, dump); fwrite(a.w, sizeof a.w, 1, dump); }
                volume += a.count();
                if (ice) { const Bits in = a & iceMask(((c->x << 4) + (i & 15)) >> 2, ((c->z << 4) + (i >> 4)) >> 2); iceVolume += in.count(); }
                int wx = (c->x << 4) + (i & 15), wz = (c->z << 4) + (i >> 4), low = 0, high = GEN_WORDS - 1;
                while (!a.w[low]) low++;
                while (!a.w[high]) high--;
                int y0 = GEN_MIN_Y + low * 64 + __builtin_ctzll(a.w[low]), y1 = GEN_MIN_Y + high * 64 + 63 - __builtin_clzll(a.w[high]);
                box[0] = std::min(box[0], wx); box[1] = std::max(box[1], wx); box[2] = std::min(box[2], y0); box[3] = std::max(box[3], y1); box[4] = std::min(box[4], wz); box[5] = std::max(box[5], wz);
                if (!c->touched) continue;
                double flat = (wx - mx) * (wx - mx) + (wz - mz) * (wz - mz);
                if (flat >= nearest) continue;
                for (int word = 0; word < GEN_WORDS; word++) for (uint64_t v = c->seen[i].w[word]; v; v &= v - 1) {
                    int wy = GEN_MIN_Y + word * 64 + __builtin_ctzll(v); double d = flat + (wy - my) * (wy - my);
                    if (d < nearest) { nearest = d; stand[0] = wx; stand[1] = wy; stand[2] = wz; }
                }
            }
        }
        if (dump) fclose(dump);
        snprintf(buf, sizeof buf, "%zu %zu %d %d %d %d %d %d at %d %d %d chunks %zu seconds %.1f ice %zu", volume, core, box[0], box[1], box[2], box[3], box[4], box[5], stand[0], stand[1], stand[2], chunks.size(), elapsed(), iceVolume);
        return buf;
    }
};

int main(int argc, char **argv) {
    int threads = argc > 1 ? atoi(argv[1]) : 8;
    std::vector<std::string> lines; std::string line;
    while (std::getline(std::cin, line)) lines.push_back(line);
    std::atomic<size_t> next(0); std::mutex print; std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, std::min<int>(threads, (int) lines.size())); t++) pool.emplace_back([&] {
        uint64_t cachedSeed = 0; std::unique_ptr<World> world; IceBiome *biome = NULL;
        for (size_t k; (k = next++) < lines.size();) {
            long long seed; double x, y, z; int used = 0;
            if (sscanf(lines[k].c_str(), "%lld %lf %lf %lf%n", &seed, &x, &y, &z, &used) < 4) continue;
            if (!world || cachedSeed != (uint64_t) seed) { world.reset(new World((uint64_t) seed)); if (biome) iceFree(biome); biome = iceNew((uint64_t) seed, false); cachedSeed = (uint64_t) seed; }
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
