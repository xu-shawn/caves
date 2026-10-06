// The GPU's services for the worker processes of the cave search, over a Unix socket (gputerrain.h): the pair scanner
// sends a seed and its places, and gets each place's surface back (or "out"). One process holds the CUDA context; a
// thread per connection, the GPU one request at a time.
// The exact measurer (native/cavemeasure) sends the columns whose cell-corner densities it needs and gets them back, bit
// for bit what its own generator makes (native/gpudensity.cu).
// build: nvcc -O3 -c gputerrain.cu -o gputerrain.o && (in native/: see gpudensity.cu) && g++ -O3 -march=native -std=c++17 -ffp-contract=off -I cubiomes gpusrv.cpp gputerrain.o native/gpudensity.o cub_*.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o gpusrv
// usage: gpusrv [MAX_PLACES=4000000]      (stderr: a line every 30 s: requests, places, GPU seconds)
#include "native/density_gen.hpp"
#include "native/densitypack.hpp"
#include "native/gpudensity.h"
#include "terrainseed.hpp"
#include <memory>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
static bool readAll(int fd, void *buffer, size_t n) { char *p = (char *) buffer; while (n) { ssize_t k = read(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
static bool writeAll(int fd, const void *buffer, size_t n) { const char *p = (const char *) buffer; while (n) { ssize_t k = write(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
int main(int argc, char **argv) {
    const int maxPlaces = argc > 1 ? atoi(argv[1]) : 4000000; signal(SIGPIPE, SIG_IGN); const bool verbose = getenv("VERBOSE") != NULL;
    BiomeNoise first; initBiomeNoise(&first, MC_26_3); setBiomeSeed(&first, 0, 0);
    std::vector<FlatSpline> nodes; int depth = flattenSpline(first.sp, nodes);
    if (depth > 6) { fprintf(stderr, "gpusrv: the spline is %d deep, the kernel takes 6\n", depth); return 1; }
    if (gputerrain_init(nodes.data(), (int) nodes.size(), maxPlaces)) return 1;
    const int maxColumns = 65536, SLOTS = 64; const int MAX_SITES = 256; const int TABLE_SLOTS = 24, MAX_PAIRS = 1 << 20;
    if (sizeof(dev::World) != gpudensity_world_size()) { fprintf(stderr, "gpusrv: the world's layout differs between host and device\n"); return 1; }
    if (gpudensity_init(maxColumns, SLOTS)) return 1;
    if (gpuformula_init(MAX_SITES)) return 1;
    if (gpupairs_init(TABLE_SLOTS, MAX_PAIRS)) return 1;
    std::mutex slotLock; std::condition_variable slotCv; std::vector<int> freeSlots; for (int i = 0; i < TABLE_SLOTS; i++) freeSlots.push_back(i);
    std::atomic<uint64_t> tableRequests(0); std::atomic<double> pairSeconds(0);
    int server = socket(AF_UNIX, SOCK_STREAM, 0); sockaddr_un address; memset(&address, 0, sizeof address); address.sun_family = AF_UNIX; strncpy(address.sun_path, TERRAIN_SOCKET, sizeof address.sun_path - 1); unlink(TERRAIN_SOCKET);
    if (server < 0 || bind(server, (sockaddr *) &address, sizeof address) || listen(server, 256)) { perror("gpusrv: socket"); return 1; }
    fprintf(stderr, "gpusrv: %zu spline nodes (depth %d), up to %d places a request, listening on %s\n", nodes.size(), depth, maxPlaces, TERRAIN_SOCKET);
    std::mutex gpu; std::atomic<uint64_t> requests(0), places(0), densityRequests(0), columns(0); std::atomic<double> gpuSeconds(0), densitySeconds(0), formulaSeconds(0); std::atomic<uint64_t> formulaRequests(0), formulaSites(0);
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // Density requests are served together: whatever is waiting when the GPU is free goes in one launch (the measurers
    // ask for a few hundred columns at a time, a hundred and more times a cavern, and a launch has a fixed cost).
    struct Pending { uint64_t seed; const dev::World *packed; const std::vector<int> *xz; std::vector<float> *out; bool done = false, failed = false; };
    std::mutex waitingLock; std::condition_variable waitingCv, doneCv; std::deque<Pending *> waitingList; std::atomic<uint64_t> batches(0);
    std::thread([&] {
        std::vector<uint64_t> slotSeed(SLOTS, 0); std::vector<char> slotHas(SLOTS, 0); std::vector<uint64_t> slotUsed(SLOTS, 0); uint64_t tick = 0;
        std::vector<int> xz, slot; std::vector<Pending *> batch;
        for (;;) {
            batch.clear(); xz.clear(); slot.clear(); tick++;
            {
                std::unique_lock<std::mutex> guard(waitingLock); waitingCv.wait(guard, [&] { return !waitingList.empty(); });
                std::deque<Pending *> later; size_t total = 0; std::vector<char> inBatch(SLOTS, 0);
                while (!waitingList.empty()) {
                    Pending *r = waitingList.front(); waitingList.pop_front(); int s = -1;
                    for (int i = 0; i < SLOTS; i++) if (slotHas[i] && slotSeed[i] == r->seed) s = i;
                    if (total + r->xz->size() / 2 > (size_t) maxColumns) { later.push_back(r); continue; }
                    if (s < 0) { // the seed is not on the device: the slot used longest ago that this batch does not need
                        int oldest = -1; for (int i = 0; i < SLOTS; i++) if (!inBatch[i] && (oldest < 0 || slotUsed[i] < slotUsed[oldest])) oldest = i;
                        if (oldest < 0) { later.push_back(r); continue; }
                        std::lock_guard<std::mutex> g2(gpu);
                        if (gpudensity_world(oldest, r->packed, sizeof(dev::World))) { r->failed = r->done = true; continue; }
                        s = oldest; slotSeed[s] = r->seed; slotHas[s] = 1;
                    }
                    inBatch[s] = 1; slotUsed[s] = tick; batch.push_back(r); total += r->xz->size() / 2;
                    xz.insert(xz.end(), r->xz->begin(), r->xz->end()); slot.insert(slot.end(), r->xz->size() / 2, s);
                }
                waitingList.swap(later);
            }
            const float *values = NULL; double a = now();
            if (!batch.empty()) { std::lock_guard<std::mutex> guard(gpu); values = gpudensity_run(xz.data(), slot.data(), (int) slot.size()); }
            densitySeconds = densitySeconds + (now() - a); batches++;
            size_t at = 0;
            for (Pending *r : batch) { size_t n = r->xz->size() / 2; if (values) r->out->assign(values + at * DENSITY_VALUES, values + (at + n) * DENSITY_VALUES); else r->failed = true; at += n; }
            { std::lock_guard<std::mutex> guard(waitingLock); for (Pending *r : batch) r->done = true; }
            doneCv.notify_all();
        }
    }).detach();
    std::thread([&] { for (;;) { std::this_thread::sleep_for(std::chrono::seconds(30)); fprintf(stderr, "gpusrv: terrain %llu requests, %.3g places, %.1f GPU seconds; density %llu requests in %llu launches, %.3g columns, %.1f GPU seconds; formula %llu requests, %llu sites, %.1f GPU seconds; scanner tables %llu, %.1f GPU seconds with their pairs\n", (unsigned long long) requests.load(), (double) places.load(), gpuSeconds.load(), (unsigned long long) densityRequests.load(), (unsigned long long) batches.load(), (double) columns.load(), densitySeconds.load(), (unsigned long long) formulaRequests.load(), (unsigned long long) formulaSites.load(), formulaSeconds.load(), (unsigned long long) tableRequests.load(), pairSeconds.load()); } }).detach();
    for (;;) {
        int fd = accept(server, NULL, NULL); if (fd < 0) continue;
        std::thread([&, fd] {
            BiomeNoise bn; initBiomeNoise(&bn, MC_26_3); std::vector<TerrainPlace> list; std::vector<float> out; TerrainSeed seed;
            std::unique_ptr<World> world; std::unique_ptr<dev::World> packed(new dev::World); uint64_t worldSeed = 0; std::vector<int> xz; int tableSlot = -1;
            for (;;) {
                TerrainRequest request;
                if (!readAll(fd, &request, sizeof request)) break;
                if (request.magic == DENSITY_MAGIC) { // the exact generator's corner columns
                    if (request.count > (uint32_t) maxColumns) break;
                    xz.resize(2 * (size_t) request.count); out.resize((size_t) request.count * DENSITY_VALUES);
                    if (!readAll(fd, xz.data(), xz.size() * sizeof(int))) break;
                    if (!world || worldSeed != request.seed) { world.reset(new World(request.seed)); worldSeed = request.seed; if (!packWorld(*world, *packed)) break; }
                    {
                        Pending pending; pending.seed = request.seed; pending.packed = packed.get(); pending.xz = &xz; pending.out = &out;
                        std::unique_lock<std::mutex> guard(waitingLock); waitingList.push_back(&pending); waitingCv.notify_one();
                        doneCv.wait(guard, [&] { return pending.done; });
                        if (pending.failed) { fprintf(stderr, "gpusrv: a density request failed\n"); break; }
                    }
                    densityRequests++; columns += request.count;
                    if (!writeAll(fd, out.data(), out.size() * sizeof(float))) break;
                    continue;
                }
                if (request.magic == FORMULA_MAGIC) { // the formula stage's open samples
                    if (request.count == 0 || request.count > (uint32_t) MAX_SITES) break;
                    std::unique_ptr<FormulaSeed> formula(new FormulaSeed); std::vector<int32_t> sites(2 * (size_t) request.count);
                    if (!readAll(fd, formula.get(), sizeof(FormulaSeed)) || !readAll(fd, sites.data(), sites.size() * sizeof(int32_t))) break;
                    bool sent = false;
                    { std::lock_guard<std::mutex> guard(gpu); double a = now(); const uint64_t *bits = gpuformula_run(formula.get(), sites.data(), (int) request.count); formulaSeconds = formulaSeconds + (now() - a);
                      if (bits) sent = writeAll(fd, bits, (size_t) request.count * FORMULA_COLUMNS * sizeof(uint64_t)); }
                    if (!sent) break;
                    formulaRequests++; formulaSites += request.count;
                    continue;
                }
                if (request.magic == TABLES_MAGIC) { // the pair scanner's tables, kept on the device for this connection
                    std::unique_ptr<FormulaSeed> formula(new FormulaSeed);
                    if (!readAll(fd, formula.get(), sizeof(FormulaSeed))) break;
                    if (tableSlot < 0) { std::unique_lock<std::mutex> guard(slotLock); slotCv.wait(guard, [&] { return !freeSlots.empty(); }); tableSlot = freeSlots.back(); freeSlots.pop_back(); }
                    bool sent = false;
                    { std::lock_guard<std::mutex> guard(gpu); double a = now(); const float *sums = gpupairs_tables(tableSlot, formula.get()); pairSeconds = pairSeconds + (now() - a);
                      if (sums) sent = writeAll(fd, sums, (size_t) PAIR_COLUMNS * sizeof(float)); }
                    if (!sent) break;
                    tableRequests++;
                    continue;
                }
                if (request.magic == PAIRS_MAGIC) { // the formula for the scanner's pairs, from this connection's tables
                    if (tableSlot < 0 || request.count == 0 || request.count > (uint32_t) MAX_PAIRS) break;
                    std::vector<int32_t> pairList(2 * (size_t) request.count);
                    if (!readAll(fd, pairList.data(), pairList.size() * sizeof(int32_t))) break;
                    bool sent = false;
                    { std::lock_guard<std::mutex> guard(gpu); double a = now(); const int16_t *counts = gpupairs_formula(tableSlot, pairList.data(), (int) request.count); pairSeconds = pairSeconds + (now() - a);
                      if (counts) sent = writeAll(fd, counts, (size_t) request.count * 16 * sizeof(int16_t)); }
                    { std::lock_guard<std::mutex> guard(slotLock); freeSlots.push_back(tableSlot); tableSlot = -1; } slotCv.notify_one();
                    if (!sent) break;
                    continue;
                }
                if (request.magic != TERRAIN_MAGIC || request.count > (uint32_t) maxPlaces) break;
                list.resize(request.count); out.resize(request.count);
                if (!readAll(fd, list.data(), (size_t) request.count * sizeof(TerrainPlace))) break;
                double t0 = now(); setBiomeSeed(&bn, request.seed, 0); terrainSeed(bn, seed); double t1 = now(), t2, t3;
                { std::lock_guard<std::mutex> guard(gpu); t2 = now(); if (gputerrain_run(&seed, list.data(), (int) request.count, out.data())) { fprintf(stderr, "gpusrv: a request failed\n"); break; } t3 = now(); gpuSeconds = gpuSeconds + (t3 - t2); }
                if (verbose) fprintf(stderr, "gpusrv: %u places: seed setup %.4f s, waited %.4f s, GPU %.4f s\n", request.count, t1 - t0, t2 - t1, t3 - t2);
                requests++; places += request.count;
                if (!writeAll(fd, out.data(), (size_t) request.count * sizeof(float))) break;
            }
            if (tableSlot >= 0) { { std::lock_guard<std::mutex> guard(slotLock); freeSlots.push_back(tableSlot); } slotCv.notify_one(); }
            close(fd);
        }).detach();
    }
}
