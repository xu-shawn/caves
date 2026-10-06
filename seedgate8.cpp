// The seed-level filters of the cave search, cheapest first.
//
// 1. The phase gate (7 ns a seed). The cave_cheese noise is a sum of Perlin layers, and a Perlin layer is weakest on its
//    lattice planes and strongest half way between them. The two lowest octaves have cells 384 and 192 blocks tall, so
//    where their lattice planes fall in y decides how hard the noise can swing at the heights caves live at - and that is
//    one number per layer, its y offset, the second double out of the layer's random source. No noise table is needed: 14
//    xoroshiro steps give the four offsets (octaves -8 and -7, stacks A and B). Over 620,000 seeds they predict the table
//    gate below with r = 0.986, and over 700 seeds scanned in full, the number of big-cavern sites of a seed with r 0.84.
//    A seed passes if the phase of y = 24 (0 and 1 = lattice planes) of each of the four layers is within W8 / W7 of C8 / C7.
//    The terrain has the same handle. Continentalness, erosion and ridges are 3D Perlin layers read at y = 0, so a layer's
//    y offset says how far that slice is from a lattice plane, and with it how strongly the terrain of the whole seed
//    varies. Big caverns need tall terrain above them: over 830 seeds searched in full, the seeds whose lowest erosion
//    and continentalness layers are read mid-cell have their biggest formula cavern 0.9M blocks bigger and the terrain
//    above their top caverns 6 blocks higher. A seed passes if the weighted sum of cos(2 pi phase) over the 12 lowest
//    terrain layers (weights below; -1 = mid-cell) is at most -TERRAIN.
// 2. The table gate: what the offsets do not say is in the permutation tables ("table luck": worth a factor 1.7 in
//    big-cavern sites per standard deviation). How low does cave_cheese get, averaged over a 512-block window and the
//    heights -56 .. 72, for its A stack and its B stack? The gate value is the summed window mean (A + B) at the pair
//    count that matches cavepairs' cut. It needs the two lowest octaves only, and no noise samples: analyticgate.hpp
//    computes every window mean exactly from the gradients (14 ms a seed; the sampled version in seedgate3_sampled.cpp
//    took 90 ms with a coarse look in front that lost one seed in seven).
// 3. The wide gate: the same window-mean sum for 1280-block windows at pair number 1,000 (analyticgate.hpp,
//    wideValue). Big caverns are 1,500 to 2,400 blocks long; this number predicts a seed's big sites far better than
//    the 512-block one (0.17 of log yield per 0.001, 1.5 per standard deviation). The 512-block gate still counts
//    beside it, the more the bigger the cavern (exact caverns of 30M+: 0.14 per 0.001 against 0.16 for the wide one).
// The test: score = wide + ALPHA x gate + terrainScore / TDIV at most PRINT. A seed whose 512-block gate
// is above SKIP is dropped before the wide one is computed (no seed that passes has one).
// Prints "seed score gate 0 phase8A phase8B phase7A phase7B terrainScore wide".
//
// seedgate7: both heavy steps run on a GPU. Step 1's four cheese phases (gpuscan.cu): 18 billion seeds a second on an
// RTX A2000; the survivors (1 seed in 19,000) come back to a CPU thread for the exact phases and the terrain score.
// The tables (gputables.cu): for the seeds past the terrain score the GPU computes the two gate numbers on a grid of
// every second window (r = 0.983 with the full score), several thousand seeds a second; the best few percent by that
// first look get the full CPU gate, which decides. The GPU alternates between the two jobs as the queues ask, so the
// table gate sees ten times the seeds twelve CPU threads could show it and keeps a ten times smaller share of them.
// seedgate8: the full CPU gate adds the percolation term (percgate.hpp): the connected volume of low smooth noise over
// the best alignments of the two stacks. The test is score - PERC x volume at most PRINT, for the seeds whose first-look
// score is at most PRE.
// build: nvcc -O3 -c gpuscan.cu -o gpuscan.o && nvcc -O3 -c gputables.cu -o gputables.o && g++ -O3 -march=native -std=c++17 -I vendor/cubiomes seedgate8.cpp gpuscan.o gputables.o vendor/cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o seedgate8
// usage: seedgate8 FIRST LAST [THREADS=4] [PRINT=-0.443] [C8=0.55 W8=0.03 C7=0.595 W7=0.06] [TERRAIN=0.55] [TDIV=100] [ALPHA=0.75] [SKIP=-0.300]     (stderr: a line of rates every 30 s; env PRE=-0.4365: the first look's score a seed needs for the full gate; PERC=0.0912: the weight of the percolation volume; SKIP=-0.300: the 512-block number above which the first look stops early; TABLE_BATCH=1024; DUMP=1: the first look's numbers of every seed)
// prints "seed final gate512 0 phases(4) terrain wide score volume": final = score - PERC x volume, score = wide + ALPHA x gate512 + terrain / TDIV       (PRINT 9: every seed that passes 1; W 1 and TERRAIN -9: no phase gate)
#include "tableseed.hpp"
#include "percgate.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
// the phase of y = 24 in the cells of the four lowest cheese layers (octave -8 A, -7 A, -8 B, -7 B in phase[0..3]), from the
// seed alone; stops at the first layer outside its window (most seeds: after 6 of the 14 random steps) and returns false
static inline bool phases(uint64_t seed, const NameHash &cheese, const NameHash &o8, const NameHash &o7, double c8, double w8, double c7, double w7, double *phase) {
    static const double AT[4] = {16.0 / 256, 16.0 / 128, 16.0 / 256 * 1.0181268882175227, 16.0 / 128 * 1.0181268882175227}; // 24 x 2/3 in cells
    Xoroshiro world; xSetSeed(&world, seed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world);
    Xoroshiro random = {worldLo ^ cheese.lo, worldHi ^ cheese.hi};
    for (int half = 0; half < 2; half++) {
        uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
        for (int octave = 0; octave < 2; octave++) {
            const NameHash &h = octave ? o7 : o8; Xoroshiro r = {forkLo ^ h.lo, forkHi ^ h.hi};
            xNextDouble(&r); double v = xNextDouble(&r) * 256.0 + AT[half * 2 + octave];
            v -= std::floor(v); phase[half * 2 + octave] = v;
            if (std::fabs(v - (octave ? c7 : c8)) > (octave ? w7 : w8)) return false;
        }
    }
    return true;
}
// sum of weight x cos(2 pi phase at y = 0) over the lowest terrain layers, stacks A and B each: erosion octaves -9, -8,
// continentalness octave -9, ridges octaves -7, -6, -5 (the offsets as cubiomes' setBiomeSeed draws them). The weights
// are the layers' measured effects on the scanner's cut over 2,463 seeds scanned in full (every optimum is mid-cell),
// in units of 92 x the gate value.
static inline double terrainScore(uint64_t seed) {
    static const NameHash noises[3] = {nameHash("minecraft:erosion"), nameHash("minecraft:continentalness"), nameHash("minecraft:ridge")};
    static const NameHash o9 = nameHash("octave_-9"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7"), o6 = nameHash("octave_-6"), o5 = nameHash("octave_-5");
    static const double TWO_PI = 6.283185307179586;
    Xoroshiro world; xSetSeed(&world, seed);
    uint64_t worldLo = xNextLong(&world), worldHi = xNextLong(&world); double score = 0;
    for (int n = 0; n < 3; n++) {
        Xoroshiro random = {worldLo ^ noises[n].lo, worldHi ^ noises[n].hi};
        for (int half = 0; half < 2; half++) {
            uint64_t forkLo = xNextLong(&random), forkHi = xNextLong(&random);
            auto offset = [&](const NameHash &h) { Xoroshiro r = {forkLo ^ h.lo, forkHi ^ h.hi}; xNextDouble(&r); return xNextDouble(&r) * 256.0; };
            if (n == 0) score += 0.21 * std::cos(TWO_PI * offset(o9)) + 0.065 * std::cos(TWO_PI * offset(o8));
            else if (n == 1) score += 0.10 * std::cos(TWO_PI * offset(o9));
            else score += 0.09 * std::cos(TWO_PI * offset(o7)) + 0.125 * std::cos(TWO_PI * offset(o6)) + 0.05 * std::cos(TWO_PI * offset(o5));
        }
    }
    return score;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: seedgate8 FIRST LAST [THREADS=4] [PRINT=-0.585] [C8=0.55 W8=0.03 C7=0.595 W7=0.06] [TERRAIN=0.55] [TDIV=100] [ALPHA=0.75]\n"); return 1; }
    uint64_t first = strtoull(argv[1], 0, 10), last = strtoull(argv[2], 0, 10);
    int threads = argc > 3 ? atoi(argv[3]) : 4; double print = argc > 4 ? atof(argv[4]) : -0.585; const double tdiv = argc > 10 ? atof(argv[10]) : 100, alpha = argc > 11 ? atof(argv[11]) : 0.75;
    const double c8 = argc > 5 ? atof(argv[5]) : 0.55, w8 = argc > 6 ? atof(argv[6]) : 0.03, c7 = argc > 7 ? atof(argv[7]) : 0.595, w7 = argc > 8 ? atof(argv[8]) : 0.06, terrain = argc > 9 ? atof(argv[9]) : 0.55;
    const bool dump = getenv("DUMP") != nullptr; // DUMP=1: print "seed phase8A phase7A phase8B phase7B terrain first512 firstWide" for every seed the GPU looks at, nothing else
    const double pre = getenv("PRE") ? atof(getenv("PRE")) : -0.4365, percWeight = getenv("PERC") ? atof(getenv("PERC")) : 0.0912;
    const NameHash cheeseHash = nameHash("minecraft:cave_cheese"), o8 = nameHash("octave_-8"), o7 = nameHash("octave_-7");
    // the integer windows: the phase of y = 24 is frac(double x 256 + at), at = 24 x 2/3 in cells of the layer
    static const double AT[4] = {16.0 / 256, 16.0 / 128, 16.0 / 256 * 1.0181268882175227, 16.0 / 128 * 1.0181268882175227};
    GpuScanWindows w = {cheeseHash.lo, cheeseHash.hi, o8.lo, o8.hi, o7.lo, o7.hi, {0, 0, 0, 0}, {0, 0, 0, 0}};
    for (int i = 0; i < 4; i++) {
        double centre = i % 2 ? c7 : c8, width = i % 2 ? w7 : w8, lo = centre - width - AT[i], hi = centre + width - AT[i];
        if (lo < 1e-6 || hi > 1 - 1e-6) { fprintf(stderr, "seedgate8: the window of layer %d wraps around a lattice plane: not supported\n", i); return 1; }
        w.low[i] = (int64_t) (lo * 35184372088832.0) - (1 << 20); w.high[i] = (int64_t) (hi * 35184372088832.0) + (1 << 20);
    }
    const uint32_t GPU_THREADS = 1 << 20, CAPACITY = 1 << 20; const int PER_THREAD = getenv("PER_THREAD") ? atoi(getenv("PER_THREAD")) : 1024; const uint64_t BATCH = (uint64_t) GPU_THREADS * PER_THREAD;
    const size_t TABLE_BATCH = getenv("TABLE_BATCH") ? atoi(getenv("TABLE_BATCH")) : 1024, CHUNK = 4096, LONG = 256;
    if (gpuscan_init(&w, CAPACITY) || gputables_init((int) TABLE_BATCH)) return 1;
    gputables_skip(getenv("SKIP") ? (float) atof(getenv("SKIP")) : -0.300f); // the first look stops at the 512-block number for seeds above this (of the seeds that reach the full gate, 1 in 200 has one)
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // three stores between the GPU (this thread) and the CPU threads: the scan's survivors in chunks; the seeds past
    // the terrain score with their layers ready for the GPU tables; the seeds past the first look, for the full gate
    struct Ready { uint64_t seed; double land, p[4]; };
    std::deque<std::vector<uint64_t>> scanQueue; std::vector<Ready> ready; std::vector<TableSeed> readyTables; std::deque<Ready> exactQueue;
    std::mutex lock, printLock; std::condition_variable work; bool finished = false;
    std::atomic<uint64_t> phased(0), exactDone(0), printed(0);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        AnalyticGate gate; Percolation perc; std::vector<uint64_t> chunk; std::vector<Ready> made; std::vector<TableSeed> madeTables;
        for (;;) {
            Ready candidate; bool full = false;
            {
                std::unique_lock<std::mutex> guard(lock); work.wait(guard, [&] { return !exactQueue.empty() || !scanQueue.empty() || finished; });
                if (!exactQueue.empty()) { candidate = exactQueue.front(); exactQueue.pop_front(); full = true; }
                else if (!scanQueue.empty()) { chunk.swap(scanQueue.front()); scanQueue.pop_front(); }
                else return;
            }
            if (full) { // the full gate decides
                NormalNoise cheese = makeNoise(candidate.seed, "minecraft:cave_cheese");
                float lowest, value = gate.value(cheese, lowest), wide = gate.wideValue(), volume = perc.volume(gate); double score = wide + alpha * value + candidate.land / tdiv, final = score - percWeight * volume; exactDone++;
                if (final > print) continue;
                printed++;
                std::lock_guard<std::mutex> guard(printLock);
                printf("%lld %.4f %.4f 0 %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f\n", (long long) candidate.seed, final, value, candidate.p[0], candidate.p[2], candidate.p[1], candidate.p[3], candidate.land, wide, score, volume); fflush(stdout);
                continue;
            }
            made.clear(); madeTables.clear(); size_t passed = 0;
            for (uint64_t seed : chunk) {
                Ready r; r.seed = seed;
                if (seed > last || !phases(seed, cheeseHash, o8, o7, c8, w8, c7, w7, r.p)) continue;
                passed++; r.land = terrainScore(seed);
                if (r.land > -terrain) continue;
                made.push_back(r); madeTables.emplace_back(); tableSeed(makeNoise(seed, "minecraft:cave_cheese"), madeTables.back());
            }
            phased += passed;
            if (!made.empty()) { std::lock_guard<std::mutex> guard(lock); ready.insert(ready.end(), made.begin(), made.end()); readyTables.insert(readyTables.end(), madeTables.begin(), madeTables.end()); }
        }
    });
    std::vector<uint64_t> found(CAPACITY); std::vector<Ready> batch; std::vector<TableSeed> batchTables; std::vector<float> numbers(2 * TABLE_BATCH);
    double t0 = now(), shown = t0, lastTables = t0, scanTime = 0, tableTime = 0, idle = 0; uint64_t scanned = 0, survivors = 0, looked = 0, kept = 0; uint64_t base = first;
    while (base <= last && base >= first) {
        size_t pending, queued;
        { std::lock_guard<std::mutex> guard(lock); pending = ready.size(); queued = scanQueue.size(); }
        if (pending >= TABLE_BATCH || (pending > 0 && (queued >= LONG || now() - lastTables > 2))) { // the first look at the tables of up to a batch of seeds (a part batch if none ran for 2 s)
            size_t n = std::min(pending, TABLE_BATCH);
            { std::lock_guard<std::mutex> guard(lock); batch.assign(ready.begin(), ready.begin() + n); batchTables.assign(readyTables.begin(), readyTables.begin() + n); ready.erase(ready.begin(), ready.begin() + n); readyTables.erase(readyTables.begin(), readyTables.begin() + n); } // the oldest first
            double a = now(); if (gputables_batch(batchTables.data(), (int) n, numbers.data())) break; lastTables = now(); tableTime += lastTables - a; looked += n;
            if (dump) { // every seed with its phases, terrain score and the two first-look numbers: for fitting the windows
                for (size_t i = 0; i < n; i++) printf("%lld %.4f %.4f %.4f %.4f %.3f %.4f %.4f\n", (long long) batch[i].seed, batch[i].p[0], batch[i].p[1], batch[i].p[2], batch[i].p[3], batch[i].land, numbers[2 * i], numbers[2 * i + 1]);
                continue;
            }
            size_t good = 0;
            { std::lock_guard<std::mutex> guard(lock); for (size_t i = 0; i < n; i++) if (numbers[2 * i + 1] + alpha * numbers[2 * i] + batch[i].land / tdiv <= pre) { exactQueue.push_back(batch[i]); good++; } }
            kept += good; if (good) work.notify_all();
        } else if (queued >= LONG) { double a = now(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); idle += now() - a; } // the CPU threads are behind
        else {
            double a = now(); long n = gpuscan_batch(base, GPU_THREADS, PER_THREAD, found.data()); scanTime += now() - a;
            if (n < 0) break;
            if ((uint64_t) n > CAPACITY) { fprintf(stderr, "seedgate8: %ld survivors in a batch, %u kept\n", n, CAPACITY); n = CAPACITY; }
            scanned += BATCH; survivors += n; base += BATCH;
            { std::lock_guard<std::mutex> guard(lock); for (size_t i = 0; i < (size_t) n; i += CHUNK) scanQueue.emplace_back(found.begin() + i, found.begin() + std::min((size_t) n, i + CHUNK)); }
            work.notify_all();
        }
        if (now() - shown >= 30) {
            double s = now() - t0; shown = now();
            fprintf(stderr, "%.0f s: %.3g seeds/s scanned (GPU: %.0f%% scan, %.0f%% tables, %.0f%% waiting for the CPU), %.0f/s past the phases, %.0f/s first look at the tables, %.1f/s past it, %.1f/s full gate, %.2f/s printed; next seed %llu\n", s, scanned / s,
                    100 * scanTime / s, 100 * tableTime / s, 100 * idle / s, phased / s, looked / s, kept / s, exactDone / s, printed / s, (unsigned long long) base);
        }
    }
    { std::lock_guard<std::mutex> guard(lock); finished = true; }
    work.notify_all();
    for (auto &t : pool) t.join();
    return 0;
}
