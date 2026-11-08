// icecaves/iceformula.cpp: a quick look at the cave space under a place, to rank places before the exact measure.
// On a lattice (every STEP blocks across, every 8 up from y -53, the lowest air over the lava, to TOP, within REACH
// blocks of the place) a sample is air when it is open (the game's main density there is not positive) and dry (the
// floodedness noise under the threshold the aquifers use: 0.4 under land, falling to -0.8 towards the sea floor). Air
// samples joined by lattice edges make clusters; each is counted whole and by its samples in ice caves.
// A sample stands for the 8 blocks around its height, and counts as ice caves only if the cells at both ends of those
// are: three caverns in four that leave the ice caves do so through the floor (the biome's box ends at depth 0.9, and
// near it only weirdness over 0.3 is still ice caves) and most others through the roof.
// Output per line of input "SEED X Y Z ...":
//   "SEED X Y Z | ice N of M at X Y Z pure N at X Y Z open N | rest"
//   ice N of M: the cluster with the most samples in ice caves (N of its M), and one of them: a start for icemeasure
//   pure N: the largest cluster that is in ice caves throughout, and a sample of it
//   edge A B: of the pure cluster and of the ice cluster, the sideways lattice neighbours that are rock or water outside
//   the ice caves (0: the ice caves reach a lattice step past the cluster all around)
// Multiply by STEP * STEP * 8 for blocks.
// build: g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../native -I ../vendor/cubiomes iceformula.cpp icebiome.o ../vendor/cub_*.o -lpthread -o iceformula
// usage: iceformula [THREADS=12] [REACH=224] [STEP=32] [TOP=64] < lines
#include "gamegen.hpp"
#include "icebiome.hpp"
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

struct Formula { int ice = 0, iceOf = 0, ix = 0, iy = 0, iz = 0, pure = 0, px = 0, py = 0, pz = 0, open = 0, pureEdge = 0, iceEdge = 0; };
// top: the highest level sampled (64 for the whole height a cavern there can have; a quick look can start lower)
static Formula formula(const World &w, const IceBiome *ice, int x0, int z0, int reach, int step, int FORMULA_TOP = 67) {
    const int LOWEST = -53, FORMULA_LEVELS = (FORMULA_TOP - LOWEST) / 8 + 1;   // level k: y = LOWEST + 8 (LEVELS - 1 - k), from the top down
    const int n = 2 * (reach / step) + 1, baseX = ((x0 >> 2) << 2) - (reach / step) * step, baseZ = ((z0 >> 2) << 2) - (reach / step) * step;
    std::vector<uint8_t> state((size_t) n * n * FORMULA_LEVELS, 0), outside(state.size(), 1); Formula f;   // state: 1 air, 3 air in ice caves; outside: the sample's 8 blocks are not all in ice caves cells (air or not)
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        const int x = baseX + i * step, z = baseZ + j * step;
        IceClimate climate; iceClimate(ice, x >> 2, z >> 2, &climate, true, true); uint64_t cells[2]; iceColumn(&climate, cells, 0);
        Col col; col.clear(); P p; p.w = &w; p.x = x; p.z = z; p.col = &col; p.at(64); const bool land = corner_0(p) > 0.0f;   // ground at y 64: not under the sea
        const double floor_ = 128.0 * (1.0 - 83.0 / 160.0 + climate.off);          // the rough sea floor of the column
        for (int k = 0; k < FORMULA_LEVELS; k++) {
            const int y = LOWEST + 8 * (FORMULA_LEVELS - 1 - k); p.at(y);
            { const int low = (std::max(y - 4, -56) >> 2) + 16, high = ((y + 3) >> 2) + 16; outside[((size_t) j * n + i) * FORMULA_LEVELS + k] = !((cells[low >> 6] >> (low & 63) & 1) && (cells[high >> 6] >> (high & 63) & 1)); }
            if (corner_0(p) > 0.0f) continue;
            f.open++;
            // Aquifer.computeSurfaceLevel: no fluid where the floodedness noise is under a threshold, which under a sea
            // floor falls from 0.4 at 64 blocks below it to -0.8 at the floor; under land it is 0.4
            const double factor = land ? 0.0 : std::min(1.0, std::max(0.0, 1.0 - (floor_ + 8 - y) / 64.0)), threshold = 0.4 - 1.2 * factor;
            if (std::min(1.0f, std::max(-1.0f, aq_floodedness(p))) > threshold) continue;
            state[((size_t) j * n + i) * FORMULA_LEVELS + k] = outside[((size_t) j * n + i) * FORMULA_LEVELS + k] ? 1 : 3;
        }
    }
    // clusters: air samples joined by lattice edges
    std::vector<int> label(state.size(), -1), stack; std::vector<int> sizes, inIce, edge;   // edge: a cluster's sideways neighbours that are rock or water outside the ice caves (or past the lattice): where the cavern between the samples can leave them
    for (size_t s = 0; s < state.size(); s++) if (state[s] && label[s] < 0) {
        const int id = (int) sizes.size(); sizes.push_back(0); inIce.push_back(0); edge.push_back(0); stack.push_back((int) s); label[s] = id;
        while (!stack.empty()) {
            const int at = stack.back(); stack.pop_back(); sizes[id]++; inIce[id] += state[at] == 3;
            const int k = at % FORMULA_LEVELS, i = (at / FORMULA_LEVELS) % n, j = at / FORMULA_LEVELS / n;
            auto visit = [&](int jj, int ii, int kk) {
                if (kk < 0 || kk >= FORMULA_LEVELS) return;
                if (jj < 0 || jj >= n || ii < 0 || ii >= n) { edge[id]++; return; }
                const size_t o = ((size_t) jj * n + ii) * FORMULA_LEVELS + kk;
                if (!state[o]) { if (kk == k && outside[o]) edge[id]++; return; }
                if (label[o] < 0) { label[o] = id; stack.push_back((int) o); } };
            visit(j, i, k - 1); visit(j, i, k + 1); visit(j, i - 1, k); visit(j, i + 1, k); visit(j - 1, i, k); visit(j + 1, i, k);
        }
    }
    int bestIce = -1, bestPure = -1;
    for (int id = 0; id < (int) sizes.size(); id++) {
        if (inIce[id] > (bestIce < 0 ? 0 : inIce[bestIce])) bestIce = id;
        if (inIce[id] == sizes[id] && sizes[id] > (bestPure < 0 ? 0 : sizes[bestPure])) bestPure = id;
    }
    // a start for the exact measure: the ice sample of the cluster with the most of its six neighbours in the cluster,
    // nearest to the cluster's middle among those (a sample at the rim can sit in a side pocket)
    auto start = [&](int id, int &sx, int &sy, int &sz) {
        double mi = 0, mj = 0, mk = 0; int count = 0;
        for (size_t s = 0; s < state.size(); s++) if (label[s] == id) { mk += s % FORMULA_LEVELS; mi += (s / FORMULA_LEVELS) % n; mj += s / FORMULA_LEVELS / n; count++; }
        mi /= count; mj /= count; mk /= count; int bestNear = -1; double bestD = 1e300;
        for (size_t s = 0; s < state.size(); s++) if (label[s] == id && state[s] == 3) {
            const int k = (int) (s % FORMULA_LEVELS), i = (int) ((s / FORMULA_LEVELS) % n), j = (int) (s / FORMULA_LEVELS / n); int near = 0;
            auto same = [&](int jj, int ii, int kk) { return jj >= 0 && jj < n && ii >= 0 && ii < n && kk >= 0 && kk < FORMULA_LEVELS && label[((size_t) jj * n + ii) * FORMULA_LEVELS + kk] == id; };
            near = same(j, i, k - 1) + same(j, i, k + 1) + same(j, i - 1, k) + same(j, i + 1, k) + same(j - 1, i, k) + same(j + 1, i, k);
            const double d = (i - mi) * (i - mi) + (j - mj) * (j - mj) + (k - mk) * (k - mk);
            if (near > bestNear || (near == bestNear && d < bestD)) { bestNear = near; bestD = d; sx = baseX + i * step; sy = LOWEST + 8 * (FORMULA_LEVELS - 1 - k); sz = baseZ + j * step; }
        }
    };
    if (bestIce >= 0) { f.ice = inIce[bestIce]; f.iceOf = sizes[bestIce]; f.iceEdge = edge[bestIce]; start(bestIce, f.ix, f.iy, f.iz); }
    if (bestPure >= 0) { f.pure = sizes[bestPure]; f.pureEdge = edge[bestPure]; start(bestPure, f.px, f.py, f.pz); }
    return f;
}

#ifndef ICEFORMULA_NO_MAIN
int main(int argc, char **argv) {
    const int threads = argc > 1 ? atoi(argv[1]) : 12, reach = argc > 2 ? atoi(argv[2]) : 224, step = argc > 3 ? atoi(argv[3]) : 32, top = argc > 4 ? atoi(argv[4]) : 67;
    std::vector<std::string> lines; for (std::string l; std::getline(std::cin, l);) if (!l.empty()) lines.push_back(l);
    std::atomic<size_t> next{0}; std::mutex lock; std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        uint64_t cached = 0; std::unique_ptr<World> world; IceBiome *ice = NULL;
        for (size_t i; (i = next.fetch_add(1)) < lines.size();) {
            long long seed; int x, y, z, used = 0; if (sscanf(lines[i].c_str(), "%lld %d %d %d%n", &seed, &x, &y, &z, &used) < 4) continue;
            if (!world || cached != (uint64_t) seed) { world.reset(new World((uint64_t) seed)); if (ice) iceFree(ice); ice = iceNew((uint64_t) seed, false); cached = (uint64_t) seed; }
            const Formula f = formula(*world, ice, x, z, reach, step, top);
            std::lock_guard<std::mutex> hold(lock);
            printf("%lld %d %d %d | ice %d of %d at %d %d %d pure %d at %d %d %d open %d edge %d %d |%s\n", seed, x, y, z, f.ice, f.iceOf, f.ix, f.iy, f.iz, f.pure, f.px, f.py, f.pz, f.open, f.pureEdge, f.iceEdge, lines[i].c_str() + used);
        }
    });
    for (auto &t : pool) t.join();
}
#endif
