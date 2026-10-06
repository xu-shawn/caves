// interface of gpudensity.cu: the corner values of the exact terrain generator for many columns at once, of several seeds
#pragma once
#include <cstddef>
static const int DENSITY_MIN_Y = -64, DENSITY_LEVELS = 41, DENSITY_VALUES = 5 * DENSITY_LEVELS;   // as GEN_MIN_Y, GEN_H / 8 + 1 and GEN_INTERPOLATED of gamegen.hpp
int gpudensity_init(int maxColumns, int slots);
int gpudensity_world(int slot, const void *world, size_t bytes);              // a seed's device World (gamegen_dev.hpp) into a slot
const float *gpudensity_run(const int *xz, const int *slot, int count);      // xz: block x, z of each column (multiples of 4); slot: its world; the answer: DENSITY_VALUES floats a column, [function][level]
size_t gpudensity_world_size();
