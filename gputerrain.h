// The scanner's terrain step on a GPU (gputerrain.cu), served to the scanner processes by gpusrv.cpp over a socket.
// A place is a window of the pair scanner: grid indices gi, gj (the corner, in steps of 65536 / 1011 blocks), and the
// surface it needs. The kernel makes the rough surface estimate of cavepairs3.cpp (the offset spline of the first 3, 2
// and 2 octaves of continentalness, erosion and ridges, both stacks) at the 16 quarter x quarter points of the window
// and applies the scanner's tests: 4 points first, the other 12 for the places still in.
#pragma once
#include <cstdint>
static const char *const TERRAIN_SOCKET = "/tmp/caves_gpu.sock";
static const uint32_t TERRAIN_MAGIC = 0x43565431;   // "CVT1"
struct TerrainOctave { uint8_t d[260]; int64_t lac, a, c; float amplitude, d2, t2; int32_t h2; };   // lac, a, c: fixed point, 40 fraction bits (cells per quart, offsets); y = 0: d2, t2, h2
struct TerrainSeed { TerrainOctave octave[14]; float scale[3]; int32_t pad; };   // continentalness A 0-2, B 0-2; erosion A 0-1, B 0-1; ridges A 0-1, B 0-1; scale: the three noises' amplitudes
struct TerrainPlace { int32_t gi, gj; float need; };
struct FlatSpline { int32_t len, typ; float loc[12], der[12]; int32_t child[12]; float val; };   // len 1: a constant (val)
static const uint32_t DENSITY_MAGIC = 0x43564431;   // "CVD1": the exact generator's corner columns (native/gpudensity.h): count pairs of int32 block x, z follow; the answer: 205 floats a column
struct TerrainRequest { uint32_t magic, count; uint64_t seed; };   // then count TerrainPlace; the answer: count floats (the mean surface of the 16 points, or a negative number: out)
int gputerrain_init(const FlatSpline *nodes, int count, int maxPlaces);
int gputerrain_run(const TerrainSeed *seed, const TerrainPlace *places, int count, float *surface);

// The formula stage (formulacave) on the GPU: for each site the open samples of the cheese-cave formula on its grid of
// 96 x 96 columns (16 blocks apart) and 33 heights (y -56 + 8 l), as a bit per height; the surface limit and the
// clusters stay with the caller. The request: FORMULA_MAGIC, the number of sites, the seed; then the seed's layers
// (FormulaSeed) and an int32 pair x0, z0 (the grid's corner) per site; the answer: 9,216 uint64 per site (row z, column x).
static const uint32_t FORMULA_MAGIC = 0x43564631;   // "CVF1"
static const int FORMULA_COLUMNS = 96 * 96;
struct FormulaLayer { uint8_t d[260]; double a, b, c, frequency; float amplitude; int32_t pad; };
struct FormulaSeed { FormulaLayer cheese[16], layer[2], flood[2]; int32_t cheeseCount, pad; };
int gpuformula_init(int maxSites);
const uint64_t *gpuformula_run(const FormulaSeed *seed, const int32_t *sites, int count);

// The pair scanner's coarse tables and formula step on the GPU (gpupairs.cu). Two requests on one connection:
//   TABLES_MAGIC, 0, seed; then the seed's layers (FormulaSeed): the tables of both stacks are made on the device and kept
//     for this connection (every column complete: 4 cheese octaves, cave_layer, floodedness, in the scanner's fixed
//     point); the answer: a float per column of stack A (1011 x 1011) then B (993 x 993), the base cheese summed over
//     the 10 lowest heights (what the window means are made of)
//   PAIRS_MAGIC, count, seed; then count pairs of int32 (the window origins a, b): the answer: 16 int16 per pair, the
//     open points of the pair's 16 x 16 window per height. This ends the connection's hold on its tables.
static const uint32_t TABLES_MAGIC = 0x43565331, PAIRS_MAGIC = 0x43565332;   // "CVS1", "CVS2"
static const int PAIR_NA = 1011, PAIR_NB = 993, PAIR_COLUMNS = PAIR_NA * PAIR_NA + PAIR_NB * PAIR_NB;
int gpupairs_init(int slots, int maxPairs);
const float *gpupairs_tables(int slot, const FormulaSeed *seed);                       // PAIR_COLUMNS sums
const int16_t *gpupairs_formula(int slot, const int32_t *pairs, int count);            // 16 counts a pair

