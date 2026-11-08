# Largest cave by volume

A search for the largest cave in Minecraft 26.3 (normal world type), measured as connected cavern volume.

Best found: **55,830,955 blocks**, seed `6008414183811625950`, `/tp 19079170 13 -7883600`, checked block for block
against the game's own terrain code. All 1,637 caverns of 40M+ found are in `results/caverns_40M.txt`.

- `docs/PROGRESSION.pdf`: how the search got there, with the fits.
- `docs/TESTING.md`: how each kind of claim (exact, equivalent, better) is tested.

## Ice caves (26.4-snapshot-3)

`icecaves/` applies the same method to the ice caves biome added in 26.4-snapshot-3: the largest cavern that is ice
caves throughout. Best found: **6,286,232 blocks**, seed `1844768299621927670`, `/tp -17555309 -37 10018737`, checked
on the snapshot's own terrain and biome source. See `icecaves/README.md`, the table in `icecaves/RESULTS.md` and the
write-up `icecaves/docs/ICECAVES.pdf`.

## The measure

A cavern is underground air within 3 blocks of a 6-connected core of 7×7×7 air cubes. Only the noise terrain and
its aquifers count (no carvers, features or structures). `measure.sh` computes it with the game's code and is the
ground truth; `native/cavemeasure` gives the same numbers natively.

## Pipeline

GPU phase scan → coarse looks → first look → CPU full gate → workers (scanner, formula stage, exact measure).

| part | files |
|---|---|
| gate (scan, looks, full gate) | `seedgate14.cpp`, `gpuscan.cu`, `gpulook2.cu`, `gputables.cu`, `cpuprelook2.hpp`, `analyticgate.hpp`, `percgate.hpp`, `tableseed.hpp` |
| workers | `search3.py`, `cavepairs5.cpp` (scanner), `formulacave5.cpp`, `gpusrv.cpp` with `gpupairs.cu`, `gpuformula.cu`, `gputerrain.cu` |
| exact measurer | `native/` (`gamegen.hpp`, `cavemeasure.cpp`, `gen_density.py`), `java/` (the game-code tools) |
| running on a rented box | `fleet/` (`setup5.sh`, `guard4.sh`, `box/start_g14.sh`, `box/rate.sh`) |
| fits and checks | `stats/` (`fit_dollar.py`, `tail_curve.py`, the `*check*.cpp` tools) |
| regression test | `tests/check_measure.sh` |

Files with a number or `_v1` suffix below the latest are earlier versions, kept because the write-up and the
equivalence checks refer to them.

Side studies:

- `presets/`: the same cavities under large-biomes, amplified, redrawn and flat terrain.
- `record/`: map, sections and 3D view of the record cavern.
- `seedcaves/`: the largest caverns of one given seed (large biomes), with an explorer page.
- `deepdark/`: caverns free of the deep dark biome.

## Building

```
vendor/build.sh        # once: the cubiomes objects (vendor/cub_*.o)
```

Each program's build line is in a comment near the top of its source. Lines in the root and in `stats/` are run
from the repository root; lines in `native/`, `presets/native/`, `seedcaves/` and `deepdark/` from that directory.
The `.cu` files need CUDA (`nvcc`); `fleet/setup5.sh NAME` builds everything on a box.

`gpuscan.cu` must be built without `-arch=sm_89`: with it the scan runs at a fifth of the speed.

## Not in the repository

- **The game.** `measure.sh`, the tools in `java/` and `native/gen_density.py` need the Minecraft 26.3 server jar
  unpacked under `mc/` (`mc/bundle/` for the jar and its libraries as listed in `java/classpath.txt`,
  `mc/data/` for its worldgen data) and a Java 25 runtime (`JAVA25=/path/to/java`).
- **The box helper.** The `fleet/` scripts call `box.sh` and read `boxes.txt` from a sibling checkout,
  `../desert-search/deploy/`.
- **Raw data.** Run directories, downloads from the boxes, full hit lists, dumped caverns and built binaries are
  ignored (see `.gitignore`). The scripts in `stats/` that redraw the curves read those run directories.

`vendor/cubiomes` is a copy of [cubiomes](https://github.com/Cubitect/cubiomes) (MIT, see its `LICENSE`).
