// The largest cavern the cheese-cave formula predicts around a site (stage 1.5 of the search).
// Box: SIZE x SIZE blocks around the site, a sample every 16 blocks in x and z and every 8 in y (y -56 .. 200), which are
// corners of the game's density cells. A sample is open if clamp(0.27 + cave_cheese, -1, 1) + 4 cave_layer^2 < 0, the
// aquifer floodedness there is at most 0.4 and it is at least MARGIN blocks under the surface estimate (cubiomes' depth,
// one per 64 x 64 blocks). Open samples that touch (6 neighbours) are one cavern; a passage has to show up on the 16-block
// grid to connect, which stands in for the 7-block rule of the exact measure.
// Output per site: "seed x z | largest cluster (blocks: samples x 2048), all open samples (blocks), x range, y range,
// z range of the cluster, the cluster's sample nearest to its centre (x y z: where to start the exact measurement) |
// rest of the input line"
// build: g++ -O3 -march=native -std=c++17 -I vendor/cubiomes formulacave5.cpp vendor/cub_*.o -lpthread -o formulacave5
// Version 5: with GPU_FORMULA=1 the noise samples (nearly all of the time) are made by gpusrv on the GPU.
// usage: formulacave THREADS [SIZE=1536] [MARGIN=32] [STEP=16] [YSTEP=8] < "seed x z ..."
// STEP 32 and YSTEP 16 is the coarse pass: an eighth of the samples, for ranking many sites before the full grid.
#include "cavenoise.hpp"
#include "gputerrain.h"
#include <cstring>
#include <map>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static bool readAll(int fd, void *buffer, size_t n) { char *p = (char *) buffer; while (n) { ssize_t k = read(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
static bool writeAll(int fd, const void *buffer, size_t n) { const char *p = (const char *) buffer; while (n) { ssize_t k = write(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
static void packLayer(const NoiseLayer &from, FormulaLayer &to) { memset(&to, 0, sizeof to); memcpy(to.d, from.perlin.d, 257); to.a = from.perlin.a; to.b = from.perlin.b; to.c = from.perlin.c; to.frequency = from.frequency; to.amplitude = from.amplitude; }
extern "C" {
#include "biomenoise.h"
}
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
    const int threads = argc > 1 ? atoi(argv[1]) : 8, size = argc > 2 ? atoi(argv[2]) : 1536, margin = argc > 3 ? atoi(argv[3]) : 32, step = argc > 4 ? atoi(argv[4]) : 16, ystep = argc > 5 ? atoi(argv[5]) : 8;
    const int n = size / step, levels = 256 / ystep + 1; const long blocks = (long) step * step * ystep;
    struct Site { uint64_t seed; long x, z; std::string rest; }; std::vector<Site> sites; std::string line;
    while (std::getline(std::cin, line)) {
        Site s; long long seed; double x, z; int used = 0;
        if (sscanf(line.c_str(), "%lld %lf %lf%n", &seed, &x, &z, &used) >= 3) { s.seed = (uint64_t) seed; s.x = (long) x; s.z = (long) z; s.rest = line.substr(used); sites.push_back(s); }
    }
    // GPU_FORMULA=1 (and the default grid): the open samples come from gpusrv (gpuformula.cu), a request per seed; the
    // surface limit and the clusters are made here as before. If the service does not answer, everything is made here.
    std::vector<std::vector<uint64_t>> masks(sites.size());
    if (getenv("GPU_FORMULA") && size == 1536 && step == 16 && ystep == 8 && !sites.empty()) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0); sockaddr_un address; memset(&address, 0, sizeof address); address.sun_family = AF_UNIX; strncpy(address.sun_path, TERRAIN_SOCKET, sizeof address.sun_path - 1);
        if (fd >= 0 && connect(fd, (sockaddr *) &address, sizeof address) == 0) {
            std::map<uint64_t, std::vector<size_t>> bySeed; for (size_t k = 0; k < sites.size(); k++) bySeed[sites[k].seed].push_back(k);
            bool ok = true;
            for (auto &group : bySeed) {
                NormalNoise cheese = makeNoise(group.first, "minecraft:cave_cheese"), layer = makeNoise(group.first, "minecraft:cave_layer"), flood = makeNoise(group.first, "minecraft:aquifer_fluid_level_floodedness");
                if (cheese.layers.size() > 16 || layer.layers.size() != 2 || flood.layers.size() != 2) { ok = false; break; }
                std::unique_ptr<FormulaSeed> packed(new FormulaSeed); memset(packed.get(), 0, sizeof(FormulaSeed)); packed->cheeseCount = (int) cheese.layers.size();
                for (size_t i = 0; i < cheese.layers.size(); i++) packLayer(cheese.layers[i], packed->cheese[i]);
                for (int i = 0; i < 2; i++) { packLayer(layer.layers[i], packed->layer[i]); packLayer(flood.layers[i], packed->flood[i]); }
                for (size_t done = 0; ok && done < group.second.size();) {
                    TerrainRequest request = {FORMULA_MAGIC, (uint32_t) std::min<size_t>(group.second.size() - done, 64), group.first}; std::vector<int32_t> corners;
                    for (uint32_t i = 0; i < request.count; i++) { const Site &s = sites[group.second[done + i]]; corners.push_back((int32_t) (((s.x - size / 2) >> 4) << 4)); corners.push_back((int32_t) (((s.z - size / 2) >> 4) << 4)); }
                    std::vector<uint64_t> bits((size_t) request.count * FORMULA_COLUMNS);
                    ok = writeAll(fd, &request, sizeof request) && writeAll(fd, packed.get(), sizeof(FormulaSeed)) && writeAll(fd, corners.data(), corners.size() * sizeof(int32_t)) && readAll(fd, bits.data(), bits.size() * sizeof(uint64_t));
                    if (ok) for (uint32_t i = 0; i < request.count; i++) masks[group.second[done + i]].assign(bits.begin() + (size_t) i * FORMULA_COLUMNS, bits.begin() + (size_t) (i + 1) * FORMULA_COLUMNS);
                    done += request.count;
                }
                if (!ok) break;
            }
            if (!ok) for (auto &m : masks) m.clear();
        }
        if (fd >= 0) close(fd);
    }
    std::vector<std::string> out(sites.size()); std::atomic<size_t> next(0); std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) pool.emplace_back([&] {
        BiomeNoise bn; initBiomeNoise(&bn, MC_26_3); uint64_t cached = 0; bool have = false; NormalNoise cheese, layer, flood;
        std::vector<uint8_t> open((size_t) n * n * levels); std::vector<int> stack; std::vector<float> surface((size_t) (size / 64 + 1) * (size / 64 + 1));
        for (size_t k; (k = next++) < sites.size();) {
            const Site &s = sites[k];
            if (!have || s.seed != cached) { setBiomeSeed(&bn, s.seed, 0); cheese = makeNoise(s.seed, "minecraft:cave_cheese"); layer = makeNoise(s.seed, "minecraft:cave_layer"); flood = makeNoise(s.seed, "minecraft:aquifer_fluid_level_floodedness"); cached = s.seed; have = true; }
            long x0 = ((s.x - size / 2) >> 4) << 4, z0 = ((s.z - size / 2) >> 4) << 4;
            int ns = size / 64 + 1;
            for (int r = 0; r < ns; r++) for (int c = 0; c < ns; c++) {
                int64_t np[6]; sampleBiomeNoise(&bn, np, (int) ((x0 + 64 * c + 32) >> 2), 0, (int) ((z0 + 64 * r + 32) >> 2), NULL, SAMPLE_NO_BIOME);
                surface[(size_t) r * ns + c] = 128.0f * np[4] / 10000.0f;
            }
            long total = 0;
            for (int r = 0; r < n; r++) for (int c = 0; c < n; c++) {
                double x = x0 + (double) step * c, z = z0 + (double) step * r; float top = surface[(size_t) (r * step / 64) * ns + c * step / 64] - margin;
                uint8_t *cells = open.data() + ((size_t) r * n + c) * levels;
                // the cheese noise down the column (the heights from y -54 up to the surface limit), then the layer and
                // floodedness noises only where the cheese alone would be open
                int first = (-54 + 56 + ystep - 1) / ystep, count = 0;
                while (first + count < levels && -56 + ystep * (first + count) <= top) count++;
                for (int l = 0; l < levels; l++) cells[l] = 0;
                if (count <= 0) continue;
                if (!masks[k].empty()) { const uint64_t bits = masks[k][(size_t) r * n + c]; for (int j = 0; j < count; j++) if (bits >> (first + j) & 1) { cells[first + j] = 1; total++; } continue; }
                float value[64]; for (int k = 0; k < count; k++) value[k] = 0.27f;
                for (const NoiseLayer &layerOf : cheese.layers) { ColumnPerlin column(&layerOf.perlin, x * layerOf.frequency, z * layerOf.frequency); for (int k = 0; k < count; k++) value[k] += layerOf.amplitude * (float) column.at((-56 + ystep * (first + k)) * 0.6666666666666666 * layerOf.frequency); }
                ColumnPerlin la(&layer.layers[0].perlin, x * layer.layers[0].frequency, z * layer.layers[0].frequency), lb(&layer.layers[1].perlin, x * layer.layers[1].frequency, z * layer.layers[1].frequency);
                ColumnPerlin fa(&flood.layers[0].perlin, x * flood.layers[0].frequency, z * flood.layers[0].frequency), fb(&flood.layers[1].perlin, x * flood.layers[1].frequency, z * flood.layers[1].frequency);
                for (int k = 0; k < count; k++) {
                    float v = value[k]; int y = -56 + ystep * (first + k);
                    if (v >= 0) continue;
                    if (v < -1) v = -1;
                    float ly = layer.layers[0].amplitude * (float) la.at(y * 8.0 * layer.layers[0].frequency) + layer.layers[1].amplitude * (float) lb.at(y * 8.0 * layer.layers[1].frequency);
                    if (v + 4 * ly * ly >= 0) continue;
                    if (flood.layers[0].amplitude * (float) fa.at(y * 0.67 * flood.layers[0].frequency) + flood.layers[1].amplitude * (float) fb.at(y * 0.67 * flood.layers[1].frequency) > 0.4f) continue;
                    cells[first + k] = 1; total++;
                }
            }
            // largest cluster
            long best = 0; int box[6] = {0, 0, 0, 0, 0, 0}, centre[3] = {0, 0, 0}; std::vector<int> members;
            for (size_t start = 0; start < open.size(); start++) {
                if (open[start] != 1) continue;
                long count = 0; int lo[3] = {1 << 30, 1 << 30, 1 << 30}, hi[3] = {-1, -1, -1};
                open[start] = 2; stack.push_back((int) start); members.clear();
                while (!stack.empty()) {
                    int i = stack.back(); stack.pop_back(); count++; members.push_back(i);
                    int l = i % levels, c = (i / levels) % n, r = i / (levels * n); int p[3] = {c, l, r};
                    for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], p[a]); hi[a] = std::max(hi[a], p[a]); }
                    auto visit = [&](int j) { if (open[j] == 1) { open[j] = 2; stack.push_back(j); } };
                    if (l > 0) visit(i - 1); if (l + 1 < levels) visit(i + 1);
                    if (c > 0) visit(i - levels); if (c + 1 < n) visit(i + levels);
                    if (r > 0) visit(i - levels * n); if (r + 1 < n) visit(i + levels * n);
                }
                if (count > best) {
                    best = count;
                    for (int a = 0; a < 3; a++) { box[2 * a] = lo[a]; box[2 * a + 1] = hi[a]; }
                    double mean[3] = {0, 0, 0};
                    for (int i : members) { mean[0] += (i / levels) % n; mean[1] += i % levels; mean[2] += i / (levels * n); }
                    for (double &m : mean) m /= count;
                    double nearest = 1e300;
                    for (int i : members) {
                        double dc = (i / levels) % n - mean[0], dl = (i % levels - mean[1]) * ystep / step, dr = i / (levels * n) - mean[2], d = dc * dc + dl * dl + dr * dr;
                        if (d < nearest) { nearest = d; centre[0] = (i / levels) % n; centre[1] = i % levels; centre[2] = i / (levels * n); }
                    }
                }
            }
            char buf[512];
            snprintf(buf, sizeof buf, "%lld %ld %ld | %ld %ld %ld %ld %d %d %ld %ld %ld %d %ld |", (long long) s.seed, s.x, s.z, best * blocks, total * blocks, x0 + (long) step * box[0], x0 + (long) step * box[1], -56 + ystep * box[2],
                     -56 + ystep * box[3], z0 + (long) step * box[4], z0 + (long) step * box[5], x0 + (long) step * centre[0], -56 + ystep * centre[1], z0 + (long) step * centre[2]);
            out[k] = buf + s.rest;
        }
    });
    for (auto &t : pool) t.join();
    for (auto &s : out) puts(s.c_str());
}
