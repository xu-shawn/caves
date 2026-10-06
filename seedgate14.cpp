// Seed gate of the cave search, version 14: version 13 with
//   the CPU's looks from cpuprelook2.hpp (the z filter made once per layer: the same numbers at a third to a quarter of
//   the time) and the octave -7 tables made only for the seeds past the first look;
//   the GPU's coarse look from gpulook2.cu (the same idea: link gpulook2.o in place of gpulook.o);
//   the scan along the chain (gpuscan.cu's scanChain, one seeding mix per seed instead of two): FIRST and LAST are then
//   indexes of the chain, seed = (index x 0x9e3779b97f4a7c15) ^ 0x6a09e667f3bcc909, and the seeds printed are anywhere
//   in the 64 bits (signed: half are negative). CHAIN=0 scans seeds in order as before.
// Version 13: version 12 with CPU_LOOKS=n: n of the threads give every chunk of survivors they
// take the three looks themselves. With the workers' scanner and exact measure on the card too, the card is what the box
// runs out of and CPU threads are idle; each look a thread takes is card time for the scan.
// Version 12: version 11 for a box whose GPU also serves the workers (gpusrv: the scanner's
// tables and terrain, the formula stage, the exact measurer's densities). Two additions:
//   GPU_SHARE=0.6   the gate leaves the card alone for the rest of the time (it sleeps after each of its launches)
//   the coarse looks the GPU does not get to are done by the CPU threads (as in version 10): a chunk of scan survivors
//   goes to the CPU's looks when more than CPU_AT (default 3 batches) seeds are already waiting for the GPU's
// So the gate's looks are the GPU's share plus whatever CPU threads are free, and the card has room for the workers.
// Version 11: for a box whose GPU has time to spare, the first coarse look on the GPU.
//   GPU   the phase scan with the terrain score in it (gpuscan.cu: the host used to do the terrain score for 5 million
//         seeds a second); the first coarse look (gpulook.cu: octave -8, y components, the lowest 1280-block window of
//         each stack); the first look at the tables for the few seeds past the CPU's looks (gputables.cu)
//   CPU   per scan survivor: the exact phases and terrain score and the two octave -8 tables for the GPU (2 us); for the
//         15% past the GPU's coarse look: the middle look (both octaves, y components, 16-bit) and the last one (every
//         component), as in seedgate12; then the full gate with the percolation term for the seeds past the first look.
// build: nvcc -O3 -c gpuscan.cu -o gpuscan.o && nvcc -O3 -c gputables.cu -o gputables.o && nvcc -O3 -c gpulook.cu -o gpulook.o && g++ -O3 -march=native -std=c++17 -I vendor/cubiomes seedgate12.cpp gpuscan.o gputables.o gpulook.o vendor/cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o seedgate12
// usage: seedgate12 FIRST LAST [THREADS=4] [PRINT=-0.556] [C8=0.542 W8=0.045 C7=0.584 W7=0.10] [TERRAIN=0.45] [TDIV=100] [ALPHA=0.75]     (stderr: a line of rates every 30 s; env PRE=-0.4365: the first look's score a seed needs for the full gate; PERC=0.0912: the weight of the percolation volume; CUTY=-0.1748, CUTM=-0.202, CUT2=-0.2076: the coarse looks' cuts; SKIP=-0.300: the 512-block number above which the first look stops early; TABLE_BATCH=1024; LOOK_BATCH=8192; PER_THREAD=4096: seeds per GPU thread in a scan batch)
// prints "seed final gate512 0 phases(4) terrain wide score volume": final = score - PERC x volume, score = wide + ALPHA x gate512 + terrain / TDIV
#include "cpuprelook2.hpp"
#include "percgate.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
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
    if (argc < 3) { fprintf(stderr, "usage: seedgate12 FIRST LAST [THREADS=4] [PRINT=-0.585] [C8=0.55 W8=0.03 C7=0.595 W7=0.06] [TERRAIN=0.55] [TDIV=100] [ALPHA=0.75]\n"); return 1; }
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
        if (lo < 1e-6 || hi > 1 - 1e-6) { fprintf(stderr, "seedgate14: the window of layer %d wraps around a lattice plane: not supported\n", i); return 1; }
        w.low[i] = (int64_t) (lo * 35184372088832.0) - (1 << 20); w.high[i] = (int64_t) (hi * 35184372088832.0) + (1 << 20);
    }
    { // the terrain score in the scan kernel (single precision there: a cut a little loose, the host redoes it)
        const char *noises[3] = {"minecraft:erosion", "minecraft:continentalness", "minecraft:ridge"}, *octaves[5] = {"octave_-9", "octave_-8", "octave_-7", "octave_-6", "octave_-5"};
        for (int i = 0; i < 3; i++) { NameHash h = nameHash(noises[i]); w.noiseLo[i] = h.lo; w.noiseHi[i] = h.hi; }
        for (int i = 0; i < 5; i++) { NameHash h = nameHash(octaves[i]); w.octLo[i] = h.lo; w.octHi[i] = h.hi; }
        w.terrainCut = (float) (-terrain + 5e-4); w.terrain = terrain > -5 && !getenv("CPU_TERRAIN");
    }
    const uint32_t GPU_THREADS = 1 << 20, CAPACITY = 1 << 20; const int PER_THREAD = getenv("PER_THREAD") ? atoi(getenv("PER_THREAD")) : 4096; const uint64_t BATCH = (uint64_t) GPU_THREADS * PER_THREAD;
    const size_t TABLE_BATCH = getenv("TABLE_BATCH") ? atoi(getenv("TABLE_BATCH")) : 1024, CHUNK = 1024, LONG = 256, LOOK_BATCH = getenv("LOOK_BATCH") ? atoi(getenv("LOOK_BATCH")) : 8192;
    const float cutY = getenv("CUTY") ? (float) atof(getenv("CUTY")) : -0.1748f, cutM = getenv("CUTM") ? (float) atof(getenv("CUTM")) : -0.204f, cut2 = getenv("CUT2") ? (float) atof(getenv("CUT2")) : -0.2076f;
    if (gpuscan_init(&w, CAPACITY) || gputables_init((int) TABLE_BATCH, 0) || gpulook_init((int) LOOK_BATCH)) return 1;
    const double gpuShare = getenv("GPU_SHARE") ? atof(getenv("GPU_SHARE")) : 1.0; const size_t cpuAt = getenv("CPU_AT") ? (size_t) atol(getenv("CPU_AT")) : 3 * LOOK_BATCH;
    const bool chain = !getenv("CHAIN") || atoi(getenv("CHAIN")) != 0; // the scan along the chain of gpuscan.cu's scanChain (FIRST and LAST are then indexes of the chain, not seeds); CHAIN=0: seeds in order, as before
    const size_t gpuLookAt = std::max<long>(1, getenv("GPU_LOOK_AT") ? atol(getenv("GPU_LOOK_AT")) : 1); // chunks of scan survivors waiting before a thread that feeds the card's look takes one (1: as before; with CPU_LOOKS threads, 16 or so leaves the chunks to them first)
    const int cpuLookThreads = getenv("CPU_LOOKS") ? atoi(getenv("CPU_LOOKS")) : 0; // that many of the threads give the survivors they take all three looks themselves: card time for the scan, from CPU threads that have none of the workers' work
    gputables_skip(getenv("SKIP") ? (float) atof(getenv("SKIP")) : -0.300f); // the first look stops at the 512-block number for seeds above this (of the seeds that reach the full gate, 1 in 200 has one)
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // three stores between the GPU (this thread) and the CPU threads: the scan's survivors in chunks; the seeds past
    // the terrain score with their layers ready for the GPU tables; the seeds past the first look, for the full gate
    struct Ready { uint64_t seed; double land, p[4]; };
    struct Prepared { std::vector<Ready> seeds; std::vector<TableSeed> tables; };
    std::deque<std::vector<uint64_t>> scanQueue; std::deque<Prepared> ready; size_t readyCount = 0; std::deque<Ready> exactQueue; // ready: in batches, one per chunk of survivors (swapped in and out whole)
    struct LookBatch { std::vector<Ready> seeds; std::vector<LookSeed> tables; };
    std::deque<LookBatch> forLook; size_t forLookCount = 0; std::deque<std::vector<Ready>> pastLook; size_t pastLookCount = 0; // for the GPU's coarse look, and the seeds past it for the CPU's looks
    std::mutex lock, printLock; std::condition_variable work; bool finished = false;
    std::atomic<uint64_t> phased(0), coarse(0), coarseM(0), coarse2(0), exactDone(0), printed(0), cpuLooks(0);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&, t] {
        AnalyticGate gate; Percolation perc; std::unique_ptr<CoarseLook2> look(new CoarseLook2); std::vector<uint64_t> chunk; std::vector<Ready> made; std::vector<TableSeed> madeTables; std::vector<Ready> second2; std::vector<LookSeed> lookTables;
        for (;;) {
            Ready candidate; bool full = false, looks = false, onCpu = false;
            {
                // A chunk of survivors goes to a CPU-look thread if one is free. The other threads, which only prepare
                // seeds for the card's look, take chunks when `gpuLookAt` of them are waiting: the card then looks at
                // what the CPU-look threads do not get to, instead of racing them for every chunk (they were half idle).
                const size_t need = t < cpuLookThreads ? 1 : gpuLookAt;
                std::unique_lock<std::mutex> guard(lock); work.wait(guard, [&] { return !exactQueue.empty() || !pastLook.empty() || scanQueue.size() >= need || finished; });
                if (!exactQueue.empty()) { candidate = exactQueue.front(); exactQueue.pop_front(); full = true; }
                else if (!pastLook.empty()) { second2.swap(pastLook.front()); pastLook.pop_front(); pastLookCount -= second2.size(); looks = true; }
                else if (scanQueue.size() >= need) { chunk.swap(scanQueue.front()); scanQueue.pop_front(); onCpu = t < cpuLookThreads || forLookCount >= cpuAt; }
                else if (finished) return;
                else continue;
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
            if (looks) { // the seeds past the GPU's coarse look: the middle and the last look here
                made.clear(); madeTables.clear(); size_t looks2 = 0; TableSeed tables;
                for (const Ready &r : second2) {
                    tableSeedOctave(r.seed, 0, tables); tableSeedOctave(r.seed, 1, tables);
                    look->low8y(tables); // (the GPU has judged this number; here it leaves its windows for the middle look)
                    if (look->lowBothY(tables) > cutM) continue; // the middle look: both octaves, y components
                    looks2++; look->low8(tables);
                    if (look->lowBoth(tables) > cut2) continue; // the last: both octaves, every component
                    made.push_back(r); madeTables.push_back(tables);
                }
                coarseM += second2.size(); coarse2 += looks2; second2.clear();
                if (!made.empty()) { Prepared p; p.seeds.swap(made); p.tables.swap(madeTables); std::lock_guard<std::mutex> guard(lock); readyCount += p.seeds.size(); ready.push_back(std::move(p)); }
                continue;
            }
            made.clear(); lookTables.clear(); size_t passed = 0;
            if (onCpu) { // the GPU has enough waiting: all three looks here
                madeTables.clear(); size_t n = 0, nM = 0, n2 = 0; TableSeed tables;
                for (uint64_t seed : chunk) {
                    Ready r; r.seed = seed;
                    if ((!chain && seed > last) || !phases(seed, cheeseHash, o8, o7, c8, w8, c7, w7, r.p)) continue;
                    passed++; r.land = terrainScore(seed);
                    if (r.land > -terrain) continue;
                    n++; tableSeedOctave(seed, 0, tables); // the octave -8 tables alone: six seeds in seven stop here
                    if (look->low8y(tables) > cutY) continue;
                    nM++; tableSeedOctave(seed, 1, tables);
                    if (look->lowBothY(tables) > cutM) continue;
                    n2++; look->low8(tables);
                    if (look->lowBoth(tables) > cut2) continue;
                    made.push_back(r); madeTables.push_back(tables);
                }
                phased += passed; coarse += n; cpuLooks += n; coarseM += nM; coarse2 += n2;
                if (!made.empty()) { Prepared p; p.seeds.swap(made); p.tables.swap(madeTables); std::lock_guard<std::mutex> guard(lock); readyCount += p.seeds.size(); ready.push_back(std::move(p)); }
                continue;
            }
            for (uint64_t seed : chunk) {
                Ready r; r.seed = seed;
                if ((!chain && seed > last) || !phases(seed, cheeseHash, o8, o7, c8, w8, c7, w7, r.p)) continue;
                passed++; r.land = terrainScore(seed);
                if (r.land > -terrain) continue;
                made.push_back(r); lookTables.emplace_back(); lookSeed(seed, lookTables.back());
            }
            phased += passed; coarse += made.size();
            if (!made.empty()) { LookBatch p; p.seeds.swap(made); p.tables.swap(lookTables); std::lock_guard<std::mutex> guard(lock); forLookCount += p.seeds.size(); forLook.push_back(std::move(p)); }
            continue;
            if (!made.empty()) { Prepared p; p.seeds.swap(made); p.tables.swap(madeTables); std::lock_guard<std::mutex> guard(lock); readyCount += p.seeds.size(); ready.push_back(std::move(p)); }
        }
    });
    std::vector<uint64_t> found(CAPACITY); std::vector<Ready> second; std::vector<TableSeed> secondTables; std::vector<float> numbers(2 * TABLE_BATCH); std::vector<Ready> lookSeeds; std::vector<LookSeed> lookBuffer; std::vector<float> lows(LOOK_BATCH); double lookTime = 0, lastLook = 0; uint64_t lookedCoarse = 0;
    double t0 = now(), shown = t0, lastTables = t0, scanTime = 0, tableTime = 0, idle = 0; uint64_t scanned = 0, survivors = 0, looked = 0, kept = 0; uint64_t base = first; size_t secondStart = 0;
    double resting = 0; auto rest = [&](double took) { if (gpuShare < 1.0) { double a = now(); std::this_thread::sleep_for(std::chrono::duration<double>(took * (1.0 / gpuShare - 1.0))); resting += now() - a; } }; // the card's time for others
    while (base <= last && base >= first) {
        size_t pending, queued, waitingLook, behind;
        {
            std::vector<Prepared> taken;
            { std::lock_guard<std::mutex> guard(lock); queued = scanQueue.size(); waitingLook = forLookCount; behind = pastLookCount; while (!ready.empty()) { taken.push_back(std::move(ready.front())); ready.pop_front(); } readyCount = 0; }
            for (Prepared &p : taken) { second.insert(second.end(), p.seeds.begin(), p.seeds.end()); secondTables.insert(secondTables.end(), p.tables.begin(), p.tables.end()); } // the seeds past the CPU's looks
            pending = second.size() - secondStart;
        }
        if (pending >= TABLE_BATCH || (pending > 0 && now() - lastTables > 2)) { // the first look at the tables, for the seeds past the CPU's looks
            size_t n = std::min(second.size() - secondStart, TABLE_BATCH); const Ready *sec = second.data() + secondStart;
            double a = now(); if (gputables_batch(secondTables.data() + secondStart, (int) n, numbers.data())) break; lastTables = now(); tableTime += lastTables - a; looked += n; rest(lastTables - a);
            if (dump) for (size_t i = 0; i < n; i++) printf("%lld %.4f %.4f %.4f %.4f %.3f %.4f %.4f\n", (long long) sec[i].seed, sec[i].p[0], sec[i].p[1], sec[i].p[2], sec[i].p[3], sec[i].land, numbers[2 * i], numbers[2 * i + 1]);
            else {
                size_t good = 0;
                { std::lock_guard<std::mutex> guard(lock); for (size_t i = 0; i < n; i++) if (numbers[2 * i + 1] + alpha * numbers[2 * i] + sec[i].land / tdiv <= pre) { exactQueue.push_back(sec[i]); good++; } }
                kept += good; if (good) work.notify_all();
            }
            secondStart += n;
            if (secondStart == second.size()) { second.clear(); secondTables.clear(); secondStart = 0; }
            else if (secondStart >= 4 * TABLE_BATCH) { second.erase(second.begin(), second.begin() + secondStart); secondTables.erase(secondTables.begin(), secondTables.begin() + secondStart); secondStart = 0; }
        } else if ((waitingLook >= LOOK_BATCH && (gpuShare >= 1.0 || queued >= LONG)) || (waitingLook > 0 && (queued >= LONG || now() - lastLook > (gpuShare >= 1.0 ? 0.05 : 0.25)))) { // the coarse look, the oldest batches first; with a capped share the scan comes first (only the GPU can scan, and the CPU threads take the looks left over)
            std::vector<LookBatch> taken; size_t n = 0;
            { std::lock_guard<std::mutex> guard(lock); while (!forLook.empty() && n + forLook.front().seeds.size() <= LOOK_BATCH) { n += forLook.front().seeds.size(); taken.push_back(std::move(forLook.front())); forLook.pop_front(); } forLookCount -= n; }
            lookSeeds.clear(); lookBuffer.clear();
            for (LookBatch &p : taken) { lookSeeds.insert(lookSeeds.end(), p.seeds.begin(), p.seeds.end()); lookBuffer.insert(lookBuffer.end(), p.tables.begin(), p.tables.end()); }
            if (n == 0) continue;
            double a = now(); if (gpulook_batch(lookBuffer.data(), (int) n, lows.data())) break; lastLook = now(); lookTime += lastLook - a; lookedCoarse += n; rest(lastLook - a);
            std::vector<Ready> past; for (size_t i = 0; i < n; i++) if (lows[i] <= cutY) past.push_back(lookSeeds[i]);
            if (!past.empty()) { { std::lock_guard<std::mutex> guard(lock); pastLookCount += past.size(); pastLook.push_back(std::move(past)); } work.notify_all(); }
        } else if (queued >= LONG || behind >= 4 * LOOK_BATCH) { double a = now(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); idle += now() - a; } // the CPU threads are behind
        else {
            double a = now(); long n = chain ? gpuscan_chain_batch(base, GPU_THREADS, PER_THREAD, found.data()) : gpuscan_batch(base, GPU_THREADS, PER_THREAD, found.data()); { double took = now() - a; scanTime += took; rest(took); }
            if (n < 0) break;
            if ((uint64_t) n > CAPACITY) { fprintf(stderr, "seedgate14: %ld survivors in a batch, %u kept\n", n, CAPACITY); n = CAPACITY; }
            scanned += BATCH; survivors += n; base += BATCH;
            { std::lock_guard<std::mutex> guard(lock); for (size_t i = 0; i < (size_t) n; i += CHUNK) scanQueue.emplace_back(found.begin() + i, found.begin() + std::min((size_t) n, i + CHUNK)); }
            work.notify_all();
        }
        if (now() - shown >= 30) {
            double s = now() - t0; shown = now();
            fprintf(stderr, "%.0f s: %.3g seeds/s scanned (GPU: %.0f%% scan, %.0f%% coarse look, %.0f%% first look, %.0f%% left to others, %.0f%% waiting for the CPU), %.0f/s from the scan, %.0f/s coarse look on the GPU and %.0f/s on the CPU, %.0f/s middle look (%.1f%%), %.0f/s last look (%.2f%%), %.0f/s first look on the GPU (%.2f%%), %.1f/s past it, %.1f/s full gate, %.2f/s printed; per billion scanned %.2f from the scan and %.2f past the host's exact tests; next %s %llu\n", s, scanned / s,
                    100 * scanTime / s, 100 * lookTime / s, 100 * tableTime / s, 100 * resting / s, 100 * idle / s, survivors / s, lookedCoarse / s, cpuLooks / s, coarseM / s, 100.0 * coarseM / std::max<uint64_t>(lookedCoarse + cpuLooks, 1), coarse2 / s, 100.0 * coarse2 / std::max<uint64_t>(lookedCoarse + cpuLooks, 1), looked / s, 100.0 * looked / std::max<uint64_t>(lookedCoarse + cpuLooks, 1), kept / s, exactDone / s, printed / s, 1e9 * survivors / std::max<double>((double) scanned, 1), 1e9 * coarse.load() / std::max<double>((double) scanned, 1), "seed", (unsigned long long) base); // ("next seed": the start script reads it; with the chain scan it is the next index)
        }
    }
    { std::lock_guard<std::mutex> guard(lock); finished = true; }
    work.notify_all();
    for (auto &t : pool) t.join();
    return 0;
}
