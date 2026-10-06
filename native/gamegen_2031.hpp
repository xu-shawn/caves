// The game's noise terrain, natively: what NoiseBasedChunkGenerator.doFill makes of a chunk (stone, air, water, lava from
// final_density and the aquifer; no structures, carvers or features), for Minecraft 26.3's overworld.
// The density functions come from density_gen.hpp (generated from the datapack); this file adds the cell interpolation
// (op/InterpolatedFunction.fillCell), the preliminary surface level (op/FindTopSurfaceFunction) and the aquifer
// (Aquifer.NoiseBasedAquifer), following the game's code step by step so the blocks come out the same.
#pragma once
#include "density_gen.hpp"
#include <chrono>
#include <immintrin.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <climits>
#include <memory>
#include <unordered_map>

static const int GEN_MIN_Y = -64, GEN_H = 320; // the game has no terrain above y 255
enum : uint8_t { SOLID = 0, AIR = 1, WATER = 2, LAVA = 3 };
static inline int floorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

struct Generator {
    const World &w;
    std::unordered_map<uint64_t, int> surfaceCache; // preliminary surface level of quart columns (x, z multiples of 4)
    explicit Generator(const World &world) : w(world) {}

    // FindTopSurfaceFunction.Sampler.findSurfaceFrom
    int preliminarySurface(int x, int z) {
        Col col; col.clear(); P p; p.w = &w; p.x = x; p.z = z; p.col = &col; p.at(0);
        float upper = surface_upper(p);
        int top = floorIntF(upper / GEN_SURFACE_CELL) * GEN_SURFACE_CELL;
        if (top <= GEN_SURFACE_LOWER) return GEN_SURFACE_LOWER;
        for (int probe = top; probe >= GEN_SURFACE_LOWER; probe -= GEN_SURFACE_CELL) {
            p.at(probe);
            if (surface_density(p) > 0.0f) return probe;
        }
        return GEN_SURFACE_LOWER;
    }
    int surfaceLevel(int blockX, int blockZ) { // Aquifer.surfaceLevel: at the quart position
        int x = (blockX >> 2) << 2, z = (blockZ >> 2) << 2;
        uint64_t key = ((uint64_t) (uint32_t) x << 32) | (uint32_t) z;
        auto found = surfaceCache.find(key);
        if (found != surfaceCache.end()) return found->second;
        int level = preliminarySurface(x, z);
        surfaceCache.emplace(key, level);
        return level;
    }

    // ---------------------------------------------------------------------------------------------------- aquifer
    struct Fluid { int level; uint8_t type; uint8_t at(int y) const { return y < level ? type : AIR; } bool operator==(const Fluid &o) const { return level == o.level && type == o.type; } };
    static constexpr int WAY_BELOW_MIN_Y = -4064; // DimensionType.MIN_Y * 2
    static Fluid globalFluid(int y) { return y < std::min(-54, GEN_SEA_LEVEL) ? Fluid{-54, LAVA} : Fluid{GEN_SEA_LEVEL, WATER}; }

    struct Aquifer {
        Generator &g; int minGridX, minGridY, minGridZ, gridSizeX, gridSizeY, gridSizeZ, skipSamplingAboveY;
        std::vector<int> location; std::vector<uint8_t> hasLocation, hasStatus; std::vector<Fluid> status;
        // scanSurface = false leaves out the "above every surface: plain air or sea" shortcut (it only matters above ground)
        Aquifer(Generator &generator, int minX, int minY, int minZ, int sizeX, int sizeY, int sizeZ, bool scanSurface = true) : g(generator) {
            int maxX = minX + sizeX - 1, maxY = minY + sizeY - 1, maxZ = minZ + sizeZ - 1;
            minGridX = (minX - 5) >> 4; int maxGridX = ((maxX - 5) >> 4) + 1; gridSizeX = maxGridX - minGridX + 1;
            minGridY = floorDiv(minY + 1, 12) - 1; int maxGridY = floorDiv(maxY + 1, 12) + 1; gridSizeY = maxGridY - minGridY + 1;
            minGridZ = (minZ - 5) >> 4; int maxGridZ = ((maxZ - 5) >> 4) + 1; gridSizeZ = maxGridZ - minGridZ + 1;
            int total = gridSizeX * gridSizeY * gridSizeZ;
            location.resize(3 * total); hasLocation.assign(total, 0); hasStatus.assign(total, 0); status.resize(total);
            if (!scanSurface) { skipSamplingAboveY = INT_MAX; return; }
            int highest = INT_MIN; // maxSurfaceLevel over the quart columns of the grid's area
            for (int z = ((minGridZ << 4) >> 2) << 2; z <= ((((maxGridZ << 4) + 9) >> 2) << 2); z += 4)
                for (int x = ((minGridX << 4) >> 2) << 2; x <= ((((maxGridX << 4) + 9) >> 2) << 2); x += 4) highest = std::max(highest, g.surfaceLevel(x, z));
            int skipGridY = floorDiv(highest + 8 + 12, 12) + 1;
            skipSamplingAboveY = skipGridY * 12 + 11 - 1;
        }
        int index(int gx, int gy, int gz) const { return ((gy - minGridY) * gridSizeZ + (gz - minGridZ)) * gridSizeX + (gx - minGridX); }
        static double similarity(int d1, int d2) { return 1.0 - (d2 - d1) / 25.0; }
        float pointNoise(float (*function)(P &), int x, int y, int z) const {
            Col col; col.clear(); P p; p.w = &g.w; p.x = x; p.z = z; p.col = &col; p.at(y);
            return function(p);
        }
        int randomizedLevel(int x, int y, int z, int lowestSurface) const {
            int cellX = floorDiv(x, 16), cellY = floorDiv(y, 40), cellZ = floorDiv(z, 16);
            double spread = pointNoise(aq_spread, cellX, cellY, cellZ) * 10.0f;
            return std::min(lowestSurface, cellY * 40 + 20 + floorInt(spread / 3) * 3);
        }
        Fluid computeFluid(int x, int y, int z) const {
            static const int OFFSETS[13][2] = {{0, 0}, {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {-3, 0}, {-2, 0}, {-1, 0}, {1, 0}, {-2, 1}, {-1, 1}, {0, 1}, {1, 1}};
            Fluid global = globalFluid(y);
            int lowest = INT_MAX, top = y + 12, bottom = y - 12; bool under = false;
            for (const int *offset : OFFSETS) {
                int sampleX = x + 16 * offset[0], sampleZ = z + 16 * offset[1];
                int surface = g.surfaceLevel(sampleX, sampleZ), adjusted = surface + 8;
                bool start = offset[0] == 0 && offset[1] == 0;
                if (start && bottom > adjusted) return global;
                bool pokes = top > adjusted;
                if (pokes || start) {
                    Fluid atSurface = globalFluid(adjusted);
                    if (atSurface.at(adjusted) != AIR) {
                        if (start) under = true;
                        if (pokes) return atSurface;
                    }
                }
                lowest = std::min(lowest, surface);
            }
            double partially, fully;
            if (pointNoise(aq_exclusion, x, y, z) > 0.0) partially = fully = -1.0;
            else {
                int below = lowest + 8 - y;
                double factor = 0.0;
                if (under) { double t = (below - 0.0) / (64.0 - 0.0); factor = t < 0.0 ? 1.0 : (t > 1.0 ? 0.0 : 1.0 + t * (0.0 - 1.0)); }
                double noise = pointNoise(aq_floodedness, x, y, z); noise = noise < -1.0 ? -1.0 : std::min(noise, 1.0);
                double t = (factor - 1.0) / (0.0 - 1.0);
                double fullThreshold = -0.3 + t * (0.8 - -0.3), partThreshold = -0.8 + t * (0.4 - -0.8);
                partially = noise - partThreshold; fully = noise - fullThreshold;
            }
            int level = fully > 0.0 ? global.level : (partially > 0.0 ? randomizedLevel(x, y, z, lowest) : WAY_BELOW_MIN_Y);
            uint8_t type = global.type;
            if (level <= -10 && level != WAY_BELOW_MIN_Y && global.type != LAVA) {
                double lava = pointNoise(aq_lava, floorDiv(x, 64), floorDiv(y, 40), floorDiv(z, 64));
                if (std::fabs(lava) > 0.3) type = LAVA;
            }
            return Fluid{level, type};
        }
        const Fluid &statusOf(int i) {
            if (!hasStatus[i]) { status[i] = computeFluid(location[3 * i], location[3 * i + 1], location[3 * i + 2]); hasStatus[i] = 1; }
            return status[i];
        }
        double pressure(int x, int y, int z, double &barrier, const Fluid &first, const Fluid &second) const {
            uint8_t type1 = first.at(y), type2 = second.at(y);
            if ((type1 == LAVA && type2 == WATER) || (type1 == WATER && type2 == LAVA)) return 2.0;
            int difference = std::abs(first.level - second.level);
            if (difference == 0) return 0.0;
            double average = 0.5 * (first.level + second.level), above = y + 0.5 - average, towardsMiddle = difference / 2.0 - std::fabs(above), slope;
            if (above > 0.0) { double centre = 0.0 + towardsMiddle; slope = centre > 0.0 ? centre / 1.5 : centre / 2.5; }
            else { double centre = 3.0 + towardsMiddle; slope = centre > 0.0 ? centre / 3.0 : centre / 10.0; }
            double noise = 0.0;
            if (!(slope < -2.0) && !(slope > 2.0)) {
                if (std::isnan(barrier)) barrier = pointNoise(aq_barrier, x, y, z);
                noise = barrier;
            }
            return 2.0 * (noise + slope);
        }
        // Aquifer.computeSubstance: what a block with this density is
        uint8_t substance(int x, int y, int z, double density) {
            if (density > 0.0) return SOLID;
            Fluid global = globalFluid(y);
            if (y > skipSamplingAboveY) return global.at(y);
            if (global.at(y) == LAVA) return LAVA;
            int anchorX = (x - 5) >> 4, anchorY = floorDiv(y + 1, 12), anchorZ = (z - 5) >> 4;
            int d1 = INT_MAX, d2 = INT_MAX, d3 = INT_MAX, d4 = INT_MAX, i1 = 0, i2 = 0, i3 = 0;
            for (int x1 = 0; x1 <= 1; x1++) for (int y1 = -1; y1 <= 1; y1++) for (int z1 = 0; z1 <= 1; z1++) {
                int gx = anchorX + x1, gy = anchorY + y1, gz = anchorZ + z1, i = index(gx, gy, gz);
                if (!hasLocation[i]) { // positionalRandomFactory.at(gx, gy, gz)
                    int64_t s = (int64_t) (int32_t) ((uint32_t) gx * 3129871u) ^ ((int64_t) gz * 116129781LL) ^ (int64_t) gy;
                    s = (int64_t) ((uint64_t) s * (uint64_t) s * 42317861ULL + (uint64_t) s * 11ULL); s >>= 16;
                    Xoroshiro random = {(uint64_t) s ^ g.w.aquiferLo, g.w.aquiferHi};
                    location[3 * i] = (gx << 4) + xNextInt(&random, 10); location[3 * i + 1] = gy * 12 + xNextInt(&random, 9); location[3 * i + 2] = (gz << 4) + xNextInt(&random, 10);
                    hasLocation[i] = 1;
                }
                int dx = location[3 * i] - x, dy = location[3 * i + 1] - y, dz = location[3 * i + 2] - z, distance = dx * dx + dy * dy + dz * dz;
                if (d1 >= distance) { i3 = i2; i2 = i1; i1 = i; d4 = d3; d3 = d2; d2 = d1; d1 = distance; }
                else if (d2 >= distance) { i3 = i2; i2 = i; d4 = d3; d3 = d2; d2 = distance; }
                else if (d3 >= distance) { i3 = i; d4 = d3; d3 = distance; }
                else if (d4 >= distance) d4 = distance;
            }
            const Fluid first = statusOf(i1);
            double similarity12 = similarity(d1, d2);
            uint8_t fluid = first.at(y);
            if (similarity12 <= 0.0) return fluid;
            if (fluid == WATER && globalFluid(y - 1).at(y - 1) == LAVA) return fluid;
            double barrier = NAN;
            const Fluid second = statusOf(i2);
            if (density + similarity12 * pressure(x, y, z, barrier, first, second) > 0.0) return SOLID;
            const Fluid third = statusOf(i3);
            double similarity13 = similarity(d1, d3);
            if (similarity13 > 0.0 && density + similarity12 * similarity13 * pressure(x, y, z, barrier, first, third) > 0.0) return SOLID;
            double similarity23 = similarity(d2, d3);
            if (similarity23 > 0.0 && density + similarity12 * similarity23 * pressure(x, y, z, barrier, second, third) > 0.0) return SOLID;
            return fluid;
        }
    };

    // ------------------------------------------------------------------------------------------------------ chunks
    // The blocks of a chunk: codes[(z * 16 + x) * GEN_H + (y - GEN_MIN_Y)] = SOLID, AIR, WATER or LAVA
    void chunk(int chunkX, int chunkZ, uint8_t *codes) {
        const int minX = chunkX << 4, minZ = chunkZ << 4, cornersY = GEN_H / 8 + 1;
        static thread_local std::vector<float> corner, field;
        corner.resize((size_t) GEN_INTERPOLATED * 25 * cornersY); field.resize((size_t) GEN_INTERPOLATED * 256 * GEN_H);
        auto cornerAt = [&](int k, int ix, int iy, int iz) -> float & { return corner[(((size_t) k * 5 + ix) * 5 + iz) * cornersY + iy]; };
        float (*const functions[GEN_INTERPOLATED])(P &) = {corner_0, corner_1, corner_2, corner_3, corner_4};
        for (int ix = 0; ix < 5; ix++) for (int iz = 0; iz < 5; iz++) {
            Col col; col.clear(); P p; p.w = &w; p.x = minX + 4 * ix; p.z = minZ + 4 * iz; p.col = &col;
            for (int iy = 0; iy < cornersY; iy++) {
                p.at(GEN_MIN_Y + 8 * iy);
                for (int k = 0; k < GEN_INTERPOLATED; k++) cornerAt(k, ix, iy, iz) = functions[k](p);
            }
        }
        // InterpolatedFunction.fillCell: along z, then x, then y by adding the step
        for (int k = 0; k < GEN_INTERPOLATED; k++) for (int cellZ = 0; cellZ < 4; cellZ++) for (int cellX = 0; cellX < 4; cellX++) for (int cellY = 0; cellY < cornersY - 1; cellY++) {
            float v000 = cornerAt(k, cellX, cellY, cellZ), v100 = cornerAt(k, cellX + 1, cellY, cellZ), v010 = cornerAt(k, cellX, cellY + 1, cellZ), v110 = cornerAt(k, cellX + 1, cellY + 1, cellZ);
            float v001 = cornerAt(k, cellX, cellY, cellZ + 1), v101 = cornerAt(k, cellX + 1, cellY, cellZ + 1), v011 = cornerAt(k, cellX, cellY + 1, cellZ + 1), v111 = cornerAt(k, cellX + 1, cellY + 1, cellZ + 1);
            for (int z = 0; z < 4; z++) {
                float alphaZ = z * 0.25f;
                float v00 = lerpf(alphaZ, v000, v001), v01 = lerpf(alphaZ, v010, v011), v10 = lerpf(alphaZ, v100, v101), v11 = lerpf(alphaZ, v110, v111);
                for (int x = 0; x < 4; x++) {
                    float alphaX = x * 0.25f;
                    float low = lerpf(alphaX, v00, v10), high = lerpf(alphaX, v01, v11);
                    float step = (high - low) * 0.125f, value = low + step * 0;
                    float *out = &field[((size_t) k * 256 + (cellZ * 4 + z) * 16 + cellX * 4 + x) * GEN_H + cellY * 8];
                    for (int y = 0; y < 8; y++) { out[y] = value; value += step; }
                }
            }
        }
        Aquifer aquifer(*this, minX, GEN_MIN_Y, minZ, 16, GEN_H, 16);
        Col unused; unused.clear(); P p; p.w = &w; p.col = &unused;
        for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
            p.x = minX + x; p.z = minZ + z;
            for (int y = GEN_H - 1; y >= 0; y--) {
                p.at(GEN_MIN_Y + y);
                for (int k = 0; k < GEN_INTERPOLATED; k++) p.interp[k] = field[((size_t) k * 256 + z * 16 + x) * GEN_H + y];
                codes[(z * 16 + x) * GEN_H + y] = aquifer.substance(p.x, p.y, p.z, (double) final_block(p));
            }
        }
    }
};

// -------------------------------------------------------------------------------------- air of chunks, for flood fills
// Which blocks of a chunk are air, as bits: bit (y - GEN_MIN_Y) of the GEN_WORDS words of a column. The same blocks as
// Generator::chunk gives, made faster for many chunks side by side (one thread):
//  - the corner columns of the density cells and the aquifer's centres and fluids are kept, so neighbouring chunks share
//    them (a chunk then costs 16 new corner columns instead of 25);
//  - where every aquifer centre a block could choose is dry below it, the block is air without the search for the nearest
//    centres: with all fluid levels at least 5 blocks below, Aquifer.computeSubstance can only answer air (the nearest
//    centre's fluid is below the block, and every barrier pressure is 2 x slope with slope < -2, noise off).
static const int GEN_WORDS = GEN_H / 64;
struct AirMaker {
    typedef Generator::Fluid Fluid;
    static constexpr int GRID_MIN_Y = -7, GRID_LEVELS = 30, CORNERS_Y = GEN_H / 8 + 1; // aquifer levels floorDiv(-63, 12) - 1 .. floorDiv(256, 12) + 1
    // dryFrom, uniform, common: of the 12 aquifer centres a block of this cell can have as its nearest (this column and the next in x and z, the
    // level below, this one and the one above): 5 over their highest fluid level; whether they all have the same fluid; and that fluid
    struct AquiferColumn { int location[GRID_LEVELS][3]; Fluid status[GRID_LEVELS]; uint8_t hasStatus[GRID_LEVELS]; int dryFrom[GRID_LEVELS]; uint8_t uniform[GRID_LEVELS]; Fluid common[GRID_LEVELS]; };
    Generator g; Generator::Aquifer rules; // rules: for its computeFluid and pressure only
    std::unordered_map<uint64_t, std::unique_ptr<AquiferColumn>> aquiferColumns; // by aquifer grid x, z
    std::unordered_map<uint64_t, std::unique_ptr<float[]>> cornerColumns;        // by block x, z (multiples of 4): [function][level]
    explicit AirMaker(const World &world) : g(world), rules(g, 0, 0, 0, 1, 1, 1, false) {}
    static uint64_t key(int x, int z) { return ((uint64_t) (uint32_t) x << 32) | (uint32_t) z; }

    // The corner columns from the GPU (gpusrv, native/gpudensity.cu: the same values bit for bit): fetch() asks for the
    // ones of a list that are not there yet, in one request. Without a connection, or if it fails, they are made here.
    bool noPlain = getenv("CAVE_NO_PLAIN") != NULL; // (for timing the fast path of chunkAir against the general one)
    int densityFd = -1; double fetchSeconds = 0, skipSeconds = 0, airSeconds = 0, columnSeconds = 0; long fetched = 0, fetchCalls = 0, madeHere = 0;
    static double clockNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    static bool readAll(int fd, void *buffer, size_t n) { char *p = (char *) buffer; while (n) { ssize_t k = read(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
    static bool writeAll(int fd, const void *buffer, size_t n) { const char *p = (const char *) buffer; while (n) { ssize_t k = write(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t) k; } return true; }
    void connectGpu() {
        densityFd = socket(AF_UNIX, SOCK_STREAM, 0); sockaddr_un address; memset(&address, 0, sizeof address); address.sun_family = AF_UNIX; strncpy(address.sun_path, "/tmp/caves_gpu.sock", sizeof address.sun_path - 1);
        if (densityFd >= 0 && connect(densityFd, (sockaddr *) &address, sizeof address)) { close(densityFd); densityFd = -1; }
    }
    bool missingAny(const std::vector<int> &xz) const { for (size_t i = 0; i + 1 < xz.size(); i += 2) if (!cornerColumns.count(key(xz[i], xz[i + 1]))) return true; return false; }
    void fetch(std::vector<int> &xz) { // block x, z pairs (multiples of 4)
        if (densityFd < 0) return;
        const double fetchStart = clockNow(); struct Timer { double &sum, start; ~Timer() { sum += clockNow() - start; } } timer = {fetchSeconds, fetchStart};
        std::vector<int> missing; std::unordered_map<uint64_t, char> asked;
        for (size_t i = 0; i + 1 < xz.size(); i += 2) { uint64_t k = key(xz[i], xz[i + 1]); if (!cornerColumns.count(k) && !asked.count(k)) { asked[k] = 1; missing.push_back(xz[i]); missing.push_back(xz[i + 1]); } }
        const size_t VALUES = (size_t) GEN_INTERPOLATED * CORNERS_Y; std::vector<float> values;
        for (size_t done = 0; done < missing.size() / 2;) {
            struct { uint32_t magic, count; uint64_t seed; } request = {0x43564431u, (uint32_t) std::min<size_t>(missing.size() / 2 - done, 65536), g.w.seed};
            values.resize((size_t) request.count * VALUES);
            if (!writeAll(densityFd, &request, sizeof request) || !writeAll(densityFd, missing.data() + 2 * done, (size_t) request.count * 2 * sizeof(int)) || !readAll(densityFd, values.data(), values.size() * sizeof(float))) { close(densityFd); densityFd = -1; return; }
            fetched += request.count; fetchCalls++;
            for (uint32_t i = 0; i < request.count; i++) { std::unique_ptr<float[]> column(new float[VALUES]); memcpy(column.get(), values.data() + (size_t) i * VALUES, VALUES * sizeof(float)); cornerColumns[key(missing[2 * (done + i)], missing[2 * (done + i) + 1])] = std::move(column); }
            done += request.count;
        }
    }
    const float *cornerColumn(int x, int z) {
        std::unique_ptr<float[]> &column = cornerColumns[key(x, z)];
        if (!column) {
            madeHere++; const double start = clockNow(); struct Timer { double &sum, start; ~Timer() { sum += clockNow() - start; } } timer = {columnSeconds, start};
            column.reset(new float[GEN_INTERPOLATED * CORNERS_Y]);
            float (*const functions[GEN_INTERPOLATED])(P &) = {corner_0, corner_1, corner_2, corner_3, corner_4};
            Col col; col.clear(); P p; p.w = &g.w; p.x = x; p.z = z; p.col = &col;
            for (int iy = 0; iy < CORNERS_Y; iy++) {
                p.at(GEN_MIN_Y + 8 * iy);
                for (int k = 0; k < GEN_INTERPOLATED; k++) column[k * CORNERS_Y + iy] = functions[k](p);
            }
        }
        return column.get();
    }
    AquiferColumn *aquiferColumn(int gx, int gz) {
        std::unique_ptr<AquiferColumn> &column = aquiferColumns[key(gx, gz)];
        if (!column) {
            column.reset(new AquiferColumn);
            for (int level = 0; level < GRID_LEVELS; level++) { // positionalRandomFactory.at(gx, gy, gz)
                int gy = GRID_MIN_Y + level;
                int64_t s = (int64_t) (int32_t) ((uint32_t) gx * 3129871u) ^ ((int64_t) gz * 116129781LL) ^ (int64_t) gy;
                s = (int64_t) ((uint64_t) s * (uint64_t) s * 42317861ULL + (uint64_t) s * 11ULL); s >>= 16;
                Xoroshiro random = {(uint64_t) s ^ g.w.aquiferLo, g.w.aquiferHi};
                column->location[level][0] = (gx << 4) + xNextInt(&random, 10); column->location[level][1] = gy * 12 + xNextInt(&random, 9); column->location[level][2] = (gz << 4) + xNextInt(&random, 10);
                column->hasStatus[level] = 0; column->dryFrom[level] = INT_MIN;
            }
        }
        return column.get();
    }
    const Fluid &statusOf(AquiferColumn *column, int level) {
        if (!column->hasStatus[level]) { column->status[level] = rules.computeFluid(column->location[level][0], column->location[level][1], column->location[level][2]); column->hasStatus[level] = 1; }
        return column->status[level];
    }
    int skipLevel(int chunkX, int chunkZ) { // NoiseBasedAquifer's skipSamplingAboveY for the chunk
        int minX = chunkX << 4, minZ = chunkZ << 4;
        int minGridX = (minX - 5) >> 4, maxGridX = ((minX + 15 - 5) >> 4) + 1, minGridZ = (minZ - 5) >> 4, maxGridZ = ((minZ + 15 - 5) >> 4) + 1;
        int highest = INT_MIN;
        for (int z = ((minGridZ << 4) >> 2) << 2; z <= ((((maxGridZ << 4) + 9) >> 2) << 2); z += 4)
            for (int x = ((minGridX << 4) >> 2) << 2; x <= ((((maxGridX << 4) + 9) >> 2) << 2); x += 4) highest = std::max(highest, g.surfaceLevel(x, z));
        return (floorDiv(highest + 8 + 12, 12) + 1) * 12 + 11 - 1;
    }
    // Aquifer.computeSubstance with the centres of the 3 x 3 aquifer columns around the chunk (near[x][z], from baseX, baseZ)
    static constexpr uint8_t UNKNOWN = 255;
    // what a block with density <= 0 is, where that needs no search for the nearest centres; else UNKNOWN
    uint8_t easySubstance(AquiferColumn *near[3][3], int baseX, int baseZ, int skip, int x, int y, int z) {
        Fluid global = Generator::globalFluid(y);
        if (y > skip) return global.at(y);
        if (global.at(y) == LAVA) return LAVA;
        int ax = ((x - 5) >> 4) - baseX, ay = floorDiv(y + 1, 12) - GRID_MIN_Y, az = ((z - 5) >> 4) - baseZ;
        int &dryFrom = near[ax][az]->dryFrom[ay];
        if (dryFrom == INT_MIN) {
            int highest = INT_MIN; bool same = true; const Fluid one = statusOf(near[ax][az], ay - 1);
            for (int x1 = 0; x1 <= 1; x1++) for (int y1 = -1; y1 <= 1; y1++) for (int z1 = 0; z1 <= 1; z1++) { const Fluid &f = statusOf(near[ax + x1][az + z1], ay + y1); highest = std::max(highest, f.level); same = same && f == one; }
            near[ax][az]->uniform[ay] = same; near[ax][az]->common[ay] = one;
            dryFrom = highest + 5;
        }
        return y >= dryFrom ? (uint8_t) AIR : UNKNOWN;
    }
    uint8_t substance(AquiferColumn *near[3][3], int baseX, int baseZ, int skip, int x, int y, int z, double density) {
        if (density > 0.0) return SOLID;
        uint8_t easy = easySubstance(near, baseX, baseZ, skip, x, y, z);
        if (easy != UNKNOWN) return easy;
        Fluid global = Generator::globalFluid(y);
        int ax = ((x - 5) >> 4) - baseX, ay = floorDiv(y + 1, 12) - GRID_MIN_Y, az = ((z - 5) >> 4) - baseZ;
        int d1 = INT_MAX, d2 = INT_MAX, d3 = INT_MAX, d4 = INT_MAX; AquiferColumn *c1 = NULL, *c2 = NULL, *c3 = NULL; int l1 = 0, l2 = 0, l3 = 0;
        for (int x1 = 0; x1 <= 1; x1++) for (int y1 = -1; y1 <= 1; y1++) for (int z1 = 0; z1 <= 1; z1++) {
            AquiferColumn *c = near[ax + x1][az + z1]; int l = ay + y1;
            int dx = c->location[l][0] - x, dy = c->location[l][1] - y, dz = c->location[l][2] - z, distance = dx * dx + dy * dy + dz * dz;
            if (d1 >= distance) { c3 = c2; l3 = l2; c2 = c1; l2 = l1; c1 = c; l1 = l; d4 = d3; d3 = d2; d2 = d1; d1 = distance; }
            else if (d2 >= distance) { c3 = c2; l3 = l2; c2 = c; l2 = l; d4 = d3; d3 = d2; d2 = distance; }
            else if (d3 >= distance) { c3 = c; l3 = l; d4 = d3; d3 = distance; }
            else if (d4 >= distance) d4 = distance;
        }
        const Fluid first = statusOf(c1, l1);
        double similarity12 = Generator::Aquifer::similarity(d1, d2);
        uint8_t fluid = first.at(y);
        if (similarity12 <= 0.0) return fluid;
        if (fluid == WATER && Generator::globalFluid(y - 1).at(y - 1) == LAVA) return fluid;
        double barrier = NAN;
        const Fluid second = statusOf(c2, l2);
        if (density + similarity12 * rules.pressure(x, y, z, barrier, first, second) > 0.0) return SOLID;
        const Fluid third = statusOf(c3, l3);
        double similarity13 = Generator::Aquifer::similarity(d1, d3);
        if (similarity13 > 0.0 && density + similarity12 * similarity13 * rules.pressure(x, y, z, barrier, first, third) > 0.0) return SOLID;
        double similarity23 = Generator::Aquifer::similarity(d2, d3);
        if (similarity23 > 0.0 && density + similarity12 * similarity23 * rules.pressure(x, y, z, barrier, second, third) > 0.0) return SOLID;
        return fluid;
    }
    // air[(z * 16 + x) * GEN_WORDS + word]: the air blocks of the chunk's columns
    void chunkAir(int chunkX, int chunkZ, uint64_t *air) {
        const int minX = chunkX << 4, minZ = chunkZ << 4;
        const float *corners[5][5]; AquiferColumn *near[3][3];
        for (int ix = 0; ix < 5; ix++) for (int iz = 0; iz < 5; iz++) corners[ix][iz] = cornerColumn(minX + 4 * ix, minZ + 4 * iz);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) near[i][j] = aquiferColumn(chunkX - 1 + i, chunkZ - 1 + j);
        const double skipStart = clockNow(); const int skip = skipLevel(chunkX, chunkZ); const double airStart = clockNow(); skipSeconds += airStart - skipStart;
        struct Timer { double &sum, start; ~Timer() { sum += clockNow() - start; } } timer = {airSeconds, airStart};
        memset(air, 0, 256 * GEN_WORDS * sizeof(uint64_t));
        Col unused; unused.clear(); P p; p.w = &g.w; p.col = &unused;
        float cell[GEN_INTERPOLATED][4][4][8]; // [function][z][x][y], as InterpolatedFunction.fillCell: along z, then x, then y by adding the step
        for (int cellZ = 0; cellZ < 4; cellZ++) for (int cellX = 0; cellX < 4; cellX++) for (int cellY = 0; cellY < CORNERS_Y - 1; cellY++) {
            const float *c00 = corners[cellX][cellZ] + cellY, *c10 = corners[cellX + 1][cellZ] + cellY, *c01 = corners[cellX][cellZ + 1] + cellY, *c11 = corners[cellX + 1][cellZ + 1] + cellY;
            // Cells that are one thing all through need no block by block work. The interpolated values stay between the
            // corner values (but for rounding, hence the margins): with the main density above 0 at the 8 corners and the
            // noodle switch (function 1) below 0 at all of them, every block is rock; with the main density below 0 at
            // all of them every block is open, and only the aquifer decides between air and fluid.
            float lowest = c00[0], highest = c00[0], toggle = c00[CORNERS_Y];
            for (const float *c : {c00, c10, c01, c11}) for (int i = 0; i < 2; i++) { lowest = std::min(lowest, c[i]); highest = std::max(highest, c[i]); toggle = std::max(toggle, c[CORNERS_Y + i]); }
            const float margin = 1e-4f * std::max(1.0f, std::max(-lowest, highest));
            if (lowest > margin && toggle < -1e-3f) continue;
            const bool open = highest < -margin; bool filled = false;
            // With the noodle switch below 0 at the 8 corners the noodle caves are off all through the cell (function 1
            // interpolates between its corner values) and the final density is squeeze(main density) alone: only the main
            // density is interpolated then, and the blocks skip the point context and the noodle function.
            const bool plain = toggle < -1e-3f && !noPlain;
            auto fill = [&] {
                for (int k = 0; k < (plain ? 1 : GEN_INTERPOLATED); k++) {
                    const int o = k * CORNERS_Y;
                    float v000 = c00[o], v100 = c10[o], v010 = c00[o + 1], v110 = c10[o + 1], v001 = c01[o], v101 = c11[o], v011 = c01[o + 1], v111 = c11[o + 1];
                    for (int z = 0; z < 4; z++) {
                        float alphaZ = z * 0.25f;
                        float v00 = lerpf(alphaZ, v000, v001), v01 = lerpf(alphaZ, v010, v011), v10 = lerpf(alphaZ, v100, v101), v11 = lerpf(alphaZ, v110, v111);
                        for (int x = 0; x < 4; x++) {
                            float alphaX = x * 0.25f;
                            float low = lerpf(alphaX, v00, v10), high = lerpf(alphaX, v01, v11);
                            float step = (high - low) * 0.125f, value = low + step * 0;
                            for (int y = 0; y < 8; y++) { cell[k][z][x][y] = value; value += step; }
                        }
                    }
                }
                filled = true;
            };
            if (plain && !open) {
                // The usual mixed cell (no noodle caves): the main density of its 16 columns at the 8 heights with the
                // columns as vector lanes, the same single-precision steps as fill() (no fused multiply-add), and a
                // rock mask per height. Columns that are rock all the way down cost nothing more.
                const float v000 = c00[0], v100 = c10[0], v010 = c00[1], v110 = c10[1], v001 = c01[0], v101 = c11[0], v011 = c01[1], v111 = c11[1];
                alignas(32) float low[16], high[16], value[8][16]; uint32_t rock[8];
                for (int z = 0; z < 4; z++) {
                    const float alphaZ = z * 0.25f, v00 = lerpf(alphaZ, v000, v001), v01 = lerpf(alphaZ, v010, v011), v10 = lerpf(alphaZ, v100, v101), v11 = lerpf(alphaZ, v110, v111);
                    for (int x = 0; x < 4; x++) { const float alphaX = x * 0.25f; low[z * 4 + x] = lerpf(alphaX, v00, v10); high[z * 4 + x] = lerpf(alphaX, v01, v11); }
                }
                {
                    const __m256 eighth = _mm256_set1_ps(0.125f), zero = _mm256_setzero_ps();
                    const __m256 lowA = _mm256_load_ps(low), lowB = _mm256_load_ps(low + 8), stepA = _mm256_mul_ps(_mm256_sub_ps(_mm256_load_ps(high), lowA), eighth), stepB = _mm256_mul_ps(_mm256_sub_ps(_mm256_load_ps(high + 8), lowB), eighth);
                    __m256 a = _mm256_add_ps(lowA, _mm256_mul_ps(stepA, zero)), b = _mm256_add_ps(lowB, _mm256_mul_ps(stepB, zero)); // low + step * 0, as fill()
                    for (int y = 0; y < 8; y++) {
                        _mm256_store_ps(value[y], a); _mm256_store_ps(value[y] + 8, b);
                        rock[y] = (uint32_t) _mm256_movemask_ps(_mm256_cmp_ps(a, zero, _CMP_GT_OQ)) | ((uint32_t) _mm256_movemask_ps(_mm256_cmp_ps(b, zero, _CMP_GT_OQ)) << 8);
                        a = _mm256_add_ps(a, stepA); b = _mm256_add_ps(b, stepB);
                    }
                }
                for (int column = 0; column < 16; column++) {
                    uint32_t left = 0; for (int y = 0; y < 8; y++) left |= ((~rock[y] >> column) & 1u) << y; // the heights that are not rock
                    if (!left) continue;
                    const int z = column >> 2, x = column & 3, blockX = minX + cellX * 4 + x, blockZ = minZ + cellZ * 4 + z; uint64_t bits = 0;
                    AquiferColumn *const aquifer = near[((blockX - 5) >> 4) - (chunkX - 1)][((blockZ - 5) >> 4) - (chunkZ - 1)]; int spanLevel = INT_MIN, spanDry = 0;
                    for (; left; left &= left - 1) {
                        const int y = __builtin_ctz(left), blockY = GEN_MIN_Y + cellY * 8 + y;
                        const uint8_t global = blockY < -54 ? (uint8_t) LAVA : (blockY < GEN_SEA_LEVEL ? (uint8_t) WATER : (uint8_t) AIR); uint8_t easy;
                        if (blockY > skip || global == LAVA) easy = global;
                        else {
                            const int level = floorDiv(blockY + 1, 12) - GRID_MIN_Y;
                            if (level != spanLevel) { easySubstance(near, chunkX - 1, chunkZ - 1, skip, blockX, blockY, blockZ); spanLevel = level; spanDry = aquifer->dryFrom[level]; }
                            easy = blockY >= spanDry ? (uint8_t) AIR : UNKNOWN;
                        }
                        if (easy != UNKNOWN) { if (easy == AIR) bits |= 1ULL << y; continue; }
                        if (aquifer->uniform[spanLevel]) { if (aquifer->common[spanLevel].at(blockY) == AIR) bits |= 1ULL << y; continue; } // see below
                        if (substance(near, chunkX - 1, chunkZ - 1, skip, blockX, blockY, blockZ, (double) (squeeze(value[y][column]) + 0.0f)) == AIR) bits |= 1ULL << y;
                    }
                    air[((cellZ * 4 + z) * 16 + cellX * 4 + x) * GEN_WORDS + (cellY >> 3)] |= bits << ((cellY & 7) * 8);
                }
                continue;
            }
            if (!open) fill();
            for (int z = 0; z < 4; z++) for (int x = 0; x < 4; x++) {
                p.x = minX + cellX * 4 + x; p.z = minZ + cellZ * 4 + z;
                uint64_t bits = 0; // the 8 blocks of the cell's column: one byte of a word
                // A block that is not rock (density <= 0) is mostly decided without the aquifer's search for the nearest
                // centres (easySubstance): by the global fluid above the chunk's highest surface and below the lava
                // level, and else by the height its aquifer cell is dry from. That cell changes every 12 blocks, so
                // the height is looked up per span here and not per block.
                AquiferColumn *const aquifer = near[((p.x - 5) >> 4) - (chunkX - 1)][((p.z - 5) >> 4) - (chunkZ - 1)]; int spanLevel = INT_MIN, spanDry = 0;
                if (!open && !filled) fill();
                for (int y = 0; y < 8; y++) {
                    const int blockY = GEN_MIN_Y + cellY * 8 + y; float density = 0.0f; bool have = false;
                    if (!open) { // rock?
                        if (plain) { const float v = cell[0][z][x][y]; if (v > 0.0f) continue; density = squeeze(v) + 0.0f; } // final_block with the noodle function at its "off" value 64: min(squeeze(v), 64) + 0; squeeze keeps the sign
                        else { p.at(blockY); for (int k = 0; k < GEN_INTERPOLATED; k++) p.interp[k] = cell[k][z][x][y]; density = final_block(p); if ((double) density > 0.0) continue; }
                        have = true;
                    }
                    const uint8_t global = blockY < -54 ? (uint8_t) LAVA : (blockY < GEN_SEA_LEVEL ? (uint8_t) WATER : (uint8_t) AIR); uint8_t easy;
                    if (blockY > skip || global == LAVA) easy = global;
                    else {
                        const int level = floorDiv(blockY + 1, 12) - GRID_MIN_Y;
                        if (level != spanLevel) { easySubstance(near, chunkX - 1, chunkZ - 1, skip, p.x, blockY, p.z); spanLevel = level; spanDry = aquifer->dryFrom[level]; } // (the call makes the height if it is not there yet)
                        easy = blockY >= spanDry ? (uint8_t) AIR : UNKNOWN;
                    }
                    if (easy != UNKNOWN) { if (easy == AIR) bits |= 1ULL << y; continue; }
                    // The full aquifer logic weighs the fluids of the three nearest centres against each other. When all
                    // twelve candidates have the same fluid there is nothing to weigh (the pressure between equal fluids
                    // is 0 and the density is not above 0): the block is that fluid, or air above its level.
                    if (aquifer->uniform[spanLevel]) { if (aquifer->common[spanLevel].at(blockY) == AIR) bits |= 1ULL << y; continue; }
                    if (!have) { // an open cell's block that needs the full aquifer logic: its density now
                        if (!filled) fill();
                        if (plain) density = squeeze(cell[0][z][x][y]) + 0.0f;
                        else { p.at(blockY); for (int k = 0; k < GEN_INTERPOLATED; k++) p.interp[k] = cell[k][z][x][y]; density = final_block(p); }
                    }
                    if (substance(near, chunkX - 1, chunkZ - 1, skip, p.x, blockY, p.z, (double) density) == AIR) bits |= 1ULL << y;
                }
                air[((cellZ * 4 + z) * 16 + cellX * 4 + x) * GEN_WORDS + (cellY >> 3)] |= bits << ((cellY & 7) * 8);
            }
        }
    }
};
