// The exact volume of a cavern, natively: a flood fill that makes the game's terrain (gamegen.hpp, block-for-block the
// same as Minecraft 26.3's noise fill and aquifers) only where the cavern goes.
//
// The measure (passage width 7): underground air = air blocks with a non-air block above them; the core = the centres
// of 7 x 7 x 7 cubes of underground air; a cavern = a connected piece of the core (6 neighbours) and all the underground
// air within 3 blocks (Chebyshev) of it. Its volume is its number of air blocks.
//
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../vendor/cubiomes cavemeasure.cpp ../vendor/cub_noise.o -lpthread -o cavemeasure
// usage: cavemeasure [THREADS=8] < lines "SEED X Y Z [anything]"
// The fill starts at the block X Y Z if it is in a core, otherwise in the largest piece of core within 4 chunks of it.
// prints "SEED X Z VOLUME CORE x0 x1 y0 y1 z0 z1 at TX TY TZ chunks N seconds S | rest" (TX TY TZ: a core block near the
// middle of the cavern, a place to teleport to).
#include "gamegen.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

static const int R = 3, GROW = 2, PAD = 16 + 2 * R, PADH = GEN_H + 2 * R;
static const uint8_t F_AIR = 1, F_CORE = 2, F_SEEN = 4;
static int threadCount = 8;

static inline uint64_t chunkKey(int cx, int cz) { return ((uint64_t) (uint32_t) cx << 32) | (uint32_t) cz; }
static inline int keyX(uint64_t k) { return (int) (uint32_t) (k >> 32); }
static inline int keyZ(uint64_t k) { return (int) (uint32_t) k; }
struct Cell { int x, y, z; };

template <class Body> static void parallelFor(size_t n, Body body) {
    std::atomic<size_t> next(0); std::vector<std::thread> pool; int count = (int) std::min<size_t>(threadCount, std::max<size_t>(1, n));
    for (int t = 0; t < count; t++) pool.emplace_back([&, t] { for (size_t i; (i = next++) < n;) body(i, t); });
    for (auto &t : pool) t.join();
}

struct Measure {
    const World &world;
    std::vector<Generator> generators; // one per thread: each has its own surface cache
    std::unordered_map<uint64_t, std::vector<uint8_t>> chunks; std::unordered_set<uint64_t> withCore;
    explicit Measure(const World &w) : world(w) { for (int t = 0; t < threadCount; t++) generators.emplace_back(w); }

    uint8_t *cells(int cx, int cz) { auto found = chunks.find(chunkKey(cx, cz)); return found == chunks.end() ? NULL : found->second.data(); }
    uint8_t get(int x, int y, int z) {
        if (y < GEN_MIN_Y || y >= GEN_MIN_Y + GEN_H) return 0;
        uint8_t *c = cells(x >> 4, z >> 4);
        return c ? c[((z & 15) * 16 + (x & 15)) * GEN_H + (y - GEN_MIN_Y)] : 0;
    }
    uint8_t &ref(int x, int y, int z) { return cells(x >> 4, z >> 4)[((z & 15) * 16 + (x & 15)) * GEN_H + (y - GEN_MIN_Y)]; }

    // one flag of the 3 x 3 chunks around a chunk, padded by R each way: index ((pz * PAD) + px) * PADH + py
    void padded(int cx, int cz, uint8_t flag, std::vector<uint8_t> &out) {
        out.assign((size_t) PAD * PAD * PADH, 0);
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            const uint8_t *c = cells(cx + dx, cz + dz);
            if (!c) continue;
            int x0 = std::max(0, -R - 16 * dx), x1 = std::min(15, 15 + R - 16 * dx), z0 = std::max(0, -R - 16 * dz), z1 = std::min(15, 15 + R - 16 * dz);
            for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
                const uint8_t *from = c + (z * 16 + x) * GEN_H; uint8_t *to = &out[((size_t) (z + 16 * dz + R) * PAD + (x + 16 * dx + R)) * PADH + R];
                for (int y = 0; y < GEN_H; y++) to[y] = (from[y] & flag) != 0;
            }
        }
    }
    // along one axis: 1 where all (erode) or any (dilate) of the 2R + 1 cells around are 1
    static void run(const uint8_t *a, uint8_t *out, size_t start, size_t stride, int length, bool erode) {
        int sum = 0;
        for (int i = 0; i < std::min(R, length); i++) sum += a[start + i * stride];
        for (int i = 0; i < length; i++) {
            if (i + R < length) sum += a[start + (i + R) * stride];
            if (i - R - 1 >= 0) sum -= a[start + (i - R - 1) * stride];
            out[start + i * stride] = erode ? sum == 2 * R + 1 : sum > 0;
        }
    }
    void filtered(int cx, int cz, uint8_t flag, bool erode, std::vector<uint8_t> &a, std::vector<uint8_t> &b) {
        padded(cx, cz, flag, a); b.resize(a.size());
        for (int line = 0; line < PAD * PAD; line++) run(a.data(), b.data(), (size_t) line * PADH, 1, PADH, erode);                                   // along y
        for (int pz = 0; pz < PAD; pz++) for (int py = 0; py < PADH; py++) run(b.data(), a.data(), (size_t) pz * PAD * PADH + py, PADH, PAD, erode);    // along x
        for (int px = 0; px < PAD; px++) for (int py = 0; py < PADH; py++) run(a.data(), b.data(), (size_t) px * PADH + py, (size_t) PAD * PADH, PAD, erode); // along z
    }

    // makes sure the chunks in keys have their core: makes them and their neighbours, then erodes
    void ensureCore(const std::vector<uint64_t> &keys) {
        std::vector<uint64_t> toLoad; std::unordered_set<uint64_t> queued;
        for (uint64_t k : keys) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            uint64_t n = chunkKey(keyX(k) + dx, keyZ(k) + dz);
            if (!chunks.count(n) && queued.insert(n).second) toLoad.push_back(n);
        }
        std::vector<std::vector<uint8_t>> made(toLoad.size());
        parallelFor(toLoad.size(), [&](size_t i, int t) {
            std::vector<uint8_t> codes(256 * GEN_H); generators[t].chunk(keyX(toLoad[i]), keyZ(toLoad[i]), codes.data());
            std::vector<uint8_t> &flags = made[i]; flags.assign(256 * GEN_H, 0);
            for (int column = 0; column < 256; column++) {
                bool roof = false;
                for (int y = GEN_H - 1; y >= 0; y--) { if (codes[column * GEN_H + y] != AIR) roof = true; else if (roof) flags[column * GEN_H + y] = F_AIR; }
            }
        });
        for (size_t i = 0; i < toLoad.size(); i++) chunks.emplace(toLoad[i], std::move(made[i]));
        std::vector<uint64_t> todo;
        for (uint64_t k : keys) if (!withCore.count(k)) todo.push_back(k);
        parallelFor(todo.size(), [&](size_t i, int) {
            static thread_local std::vector<uint8_t> a, b;
            int cx = keyX(todo[i]), cz = keyZ(todo[i]); filtered(cx, cz, F_AIR, true, a, b);
            uint8_t *c = cells(cx, cz);
            for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                const uint8_t *from = &b[((size_t) (z + R) * PAD + (x + R)) * PADH + R]; uint8_t *to = c + (z * 16 + x) * GEN_H;
                for (int y = 0; y < GEN_H; y++) if (from[y]) to[y] |= F_CORE;
            }
        });
        for (uint64_t k : todo) withCore.insert(k);
    }

    std::string measure(int startX, int startY, int startZ) {
        auto t0 = std::chrono::steady_clock::now();
        int cx0 = startX >> 4, cz0 = startZ >> 4;
        std::vector<uint64_t> first;
        for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) first.push_back(chunkKey(cx0 + dx, cz0 + dz));
        ensureCore(first);
        std::unordered_set<uint64_t> firstSet(first.begin(), first.end());
        static const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 1, -1, 0, 0}, DZ[6] = {0, 0, 0, 0, 1, -1};
        // where to start: the given block if it is core, else the largest piece of core among the first chunks
        Cell start = {0, 0, 0}; bool found = false;
        if (get(startX, startY, startZ) & F_CORE) { start = {startX, startY, startZ}; found = true; }
        else {
            size_t bestSize = 0; std::vector<Cell> touched; std::deque<Cell> queue;
            for (uint64_t k : first) {
                int bx = keyX(k) << 4, bz = keyZ(k) << 4;
                for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = GEN_MIN_Y; y < GEN_MIN_Y + GEN_H; y++) {
                    if ((get(bx + x, y, bz + z) & (F_CORE | F_SEEN)) != F_CORE) continue;
                    size_t size = 0; Cell from = {bx + x, y, bz + z};
                    ref(from.x, from.y, from.z) |= F_SEEN; queue.push_back(from); touched.push_back(from);
                    while (!queue.empty()) {
                        Cell p = queue.front(); queue.pop_front(); size++;
                        for (int d = 0; d < 6; d++) {
                            Cell n = {p.x + DX[d], p.y + DY[d], p.z + DZ[d]};
                            if (!firstSet.count(chunkKey(n.x >> 4, n.z >> 4)) || (get(n.x, n.y, n.z) & (F_CORE | F_SEEN)) != F_CORE) continue;
                            ref(n.x, n.y, n.z) |= F_SEEN; queue.push_back(n); touched.push_back(n);
                        }
                    }
                    if (size > bestSize) { bestSize = size; start = from; found = true; }
                }
            }
            for (const Cell &c : touched) ref(c.x, c.y, c.z) &= (uint8_t) ~F_SEEN;
        }
        char buf[256];
        auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
        if (!found) { snprintf(buf, sizeof buf, "0 0 0 0 0 0 0 0 at 0 0 0 chunks %zu seconds %.1f", chunks.size(), elapsed()); return buf; }

        // the flood fill; cells whose chunk has no core yet wait until the chunks around them are made
        std::deque<Cell> queue; std::vector<Cell> waiting; std::unordered_set<uint64_t> visited;
        ref(start.x, start.y, start.z) |= F_SEEN; queue.push_back(start);
        size_t core = 0; double sumX = 0, sumY = 0, sumZ = 0;
        while (true) {
            while (!queue.empty()) {
                Cell p = queue.front(); queue.pop_front(); core++;
                sumX += p.x; sumY += p.y; sumZ += p.z; visited.insert(chunkKey(p.x >> 4, p.z >> 4));
                for (int d = 0; d < 6; d++) {
                    Cell n = {p.x + DX[d], p.y + DY[d], p.z + DZ[d]};
                    if (n.y < GEN_MIN_Y || n.y >= GEN_MIN_Y + GEN_H) continue;
                    if (!withCore.count(chunkKey(n.x >> 4, n.z >> 4))) { waiting.push_back(n); continue; }
                    uint8_t &flags = ref(n.x, n.y, n.z);
                    if ((flags & (F_CORE | F_SEEN)) != F_CORE) continue;
                    flags |= F_SEEN; queue.push_back(n);
                }
            }
            if (waiting.empty()) break;
            std::vector<uint64_t> need; std::unordered_set<uint64_t> needSet;
            for (const Cell &c : waiting) for (int dz = -GROW; dz <= GROW; dz++) for (int dx = -GROW; dx <= GROW; dx++) {
                uint64_t k = chunkKey((c.x >> 4) + dx, (c.z >> 4) + dz);
                if (!withCore.count(k) && needSet.insert(k).second) need.push_back(k);
            }
            ensureCore(need);
            std::vector<Cell> again; again.swap(waiting);
            for (const Cell &n : again) {
                uint8_t &flags = ref(n.x, n.y, n.z);
                if ((flags & (F_CORE | F_SEEN)) != F_CORE) continue;
                flags |= F_SEEN; queue.push_back(n);
            }
        }

        // the air the cube sweeps: within R of the core that was reached; the chunks around the visited ones count too
        std::vector<uint64_t> sweep; std::unordered_set<uint64_t> sweepSet;
        for (uint64_t k : visited) for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
            uint64_t n = chunkKey(keyX(k) + dx, keyZ(k) + dz);
            if (chunks.count(n) && sweepSet.insert(n).second) sweep.push_back(n);
        }
        double mx = sumX / core, my = sumY / core, mz = sumZ / core;
        std::mutex lock; size_t volume = 0; int box[6] = {INT_MAX, INT_MIN, INT_MAX, INT_MIN, INT_MAX, INT_MIN}; double nearest = 1e300; int stand[3] = {0, 0, 0};
        parallelFor(sweep.size(), [&](size_t i, int) {
            static thread_local std::vector<uint8_t> a, b;
            int cx = keyX(sweep[i]), cz = keyZ(sweep[i]); filtered(cx, cz, F_SEEN, false, a, b);
            const uint8_t *c = cells(cx, cz);
            size_t count = 0; int bb[6] = {INT_MAX, INT_MIN, INT_MAX, INT_MIN, INT_MAX, INT_MIN}; double near = 1e300; int here[3] = {0, 0, 0};
            for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                const uint8_t *swept = &b[((size_t) (z + R) * PAD + (x + R)) * PADH + R], *flags = c + (z * 16 + x) * GEN_H;
                for (int y = 0; y < GEN_H; y++) {
                    if (!swept[y] || !(flags[y] & F_AIR)) continue;
                    count++;
                    int wx = (cx << 4) + x, wy = GEN_MIN_Y + y, wz = (cz << 4) + z;
                    bb[0] = std::min(bb[0], wx); bb[1] = std::max(bb[1], wx); bb[2] = std::min(bb[2], wy); bb[3] = std::max(bb[3], wy); bb[4] = std::min(bb[4], wz); bb[5] = std::max(bb[5], wz);
                    if (flags[y] & F_SEEN) {
                        double d = (wx - mx) * (wx - mx) + (wy - my) * (wy - my) + (wz - mz) * (wz - mz);
                        if (d < near) { near = d; here[0] = wx; here[1] = wy; here[2] = wz; }
                    }
                }
            }
            std::lock_guard<std::mutex> guard(lock);
            volume += count;
            for (int j = 0; j < 6; j += 2) { box[j] = std::min(box[j], bb[j]); box[j + 1] = std::max(box[j + 1], bb[j + 1]); }
            if (near < nearest) { nearest = near; stand[0] = here[0]; stand[1] = here[1]; stand[2] = here[2]; }
        });
        snprintf(buf, sizeof buf, "%zu %zu %d %d %d %d %d %d at %d %d %d chunks %zu seconds %.1f", volume, core, box[0], box[1], box[2], box[3], box[4], box[5], stand[0], stand[1], stand[2], chunks.size(), elapsed());
        return buf;
    }
};

int main(int argc, char **argv) {
    if (argc > 1) threadCount = atoi(argv[1]);
    std::string line; uint64_t cachedSeed = 0; World *world = NULL;
    while (std::getline(std::cin, line)) {
        long long seed; double x, y, z; int used = 0;
        if (sscanf(line.c_str(), "%lld %lf %lf %lf%n", &seed, &x, &y, &z, &used) < 4) continue;
        if (!world || cachedSeed != (uint64_t) seed) { delete world; world = new World((uint64_t) seed); cachedSeed = (uint64_t) seed; }
        Measure measure(*world);
        std::string result = measure.measure((int) x, (int) y, (int) z);
        std::string rest = line.substr(used); size_t firstChar = rest.find_first_not_of(' ');
        printf("%lld %d %d %s | %s\n", seed, (int) x, (int) z, result.c_str(), firstChar == std::string::npos ? "" : rest.c_str() + firstChar);
        fflush(stdout);
    }
    delete world;
}
