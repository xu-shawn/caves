import net.minecraft.core.registries.Registries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.Aquifer;
import net.minecraft.world.level.levelgen.NoiseChunk;
import net.minecraft.world.level.levelgen.RandomState;
import net.minecraft.world.level.levelgen.blending.Blender;
import net.minecraft.world.level.levelgen.densityfunction.DensitySampler;
import net.minecraft.world.level.levelgen.densityfunction.DensityVolume;
import net.minecraft.world.level.levelgen.densityfunction.ScopedDensityBuffer;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * A cheap exact estimate of how much underground air a region has: the real game's noise terrain (as CaveDump) for one
 * chunk in every STEP x STEP, scaled up. Underground air = air with a non-air block above it in its column.
 *
 * usage: CaveAir THREADS STEP < lines "SEED X Z SIZE [anything]"     (X, Z is the centre of a SIZE x SIZE block region)
 * prints "SEED X Z AIR WATER TOP | rest of the line": the estimates of underground air and of water under a roof (blocks),
 * and the mean height of the highest non-air block.
 */
public class CaveAir {
    public static void main(String[] args) throws Exception {
        int threads = Integer.parseInt(args[0]), step = Integer.parseInt(args[1]);
        CaveDump.setup();
        List<String> lines = new ArrayList<>();
        BufferedReader reader = new BufferedReader(new InputStreamReader(System.in));
        for (String line; (line = reader.readLine()) != null; ) if (line.trim().split("\\s+").length >= 4) lines.add(line.trim());
        String[] results = new String[lines.size()];
        AtomicInteger next = new AtomicInteger();
        Thread[] pool = new Thread[threads];
        for (int t = 0; t < threads; t++) {
            pool[t] = new Thread(() -> {
                long cachedSeed = 0; RandomState randomState = null;
                for (int index; (index = next.getAndIncrement()) < lines.size(); ) {
                    String[] w = lines.get(index).split("\\s+", 5);
                    long seed = Long.parseLong(w[0]);
                    int x = (int) Double.parseDouble(w[1]), z = (int) Double.parseDouble(w[2]), size = Integer.parseInt(w[3]);
                    if (randomState == null || seed != cachedSeed) {
                        randomState = RandomState.create(CaveDump.registries.lookupOrThrow(Registries.NOISE), seed, CaveDump.noiseSettings);
                        cachedSeed = seed;
                    }
                    int chunkX0 = (x - size / 2) >> 4, chunkZ0 = (z - size / 2) >> 4, chunks = size / 16;
                    long air = 0, water = 0, top = 0; int sampled = 0;
                    for (int cz = 0; cz < chunks; cz++) for (int cx = 0; cx < chunks; cx++) {
                        if (Math.floorMod(chunkX0 + cx, step) != 0 || Math.floorMod(chunkZ0 + cz, step) != 0) continue;
                        int minX = 16 * (chunkX0 + cx), minZ = 16 * (chunkZ0 + cz);
                        DensityVolume volume = new DensityVolume(16, CaveDump.HEIGHT, 16, minX, CaveDump.MIN_Y, minZ);
                        try (NoiseChunk noiseChunk = new NoiseChunk(randomState, null, CaveDump.noiseSettings, CaveDump.fluidPicker, Blender.empty(), volume)) {
                            Aquifer aquifer = noiseChunk.aquifer();
                            DensitySampler.Bound finalDensity = noiseChunk.cachingSamplers().get(CaveDump.noiseSettings.noiseRouter().finalDensity());
                            try (ScopedDensityBuffer densities = finalDensity.sampleVolume(noiseChunk.volume())) {
                                for (int bz = 0; bz < 16; bz++) for (int bx = 0; bx < 16; bx++) {
                                    boolean roof = false;
                                    for (int y = CaveDump.HEIGHT - 1; y >= 0; y--) {
                                        BlockState state = aquifer.computeSubstance(minX + bx, CaveDump.MIN_Y + y, minZ + bz, densities.get(volume.indexUnchecked(bx, y, bz)));
                                        byte code = state == null ? 0 : CaveDump.code(state);
                                        if (code != 1) {
                                            if (!roof) top += CaveDump.MIN_Y + y;
                                            else if (code == 2) water++;
                                            roof = true;
                                        } else if (roof) air++;
                                    }
                                }
                            }
                        }
                        sampled++;
                    }
                    double scale = sampled == 0 ? 0 : (double) chunks * chunks / sampled;
                    results[index] = w[0] + " " + x + " " + z + " " + Math.round(air * scale) + " " + Math.round(water * scale) + " " + (sampled == 0 ? 0 : top / (256L * sampled))
                        + " | " + (w.length > 4 ? w[4] : "");
                }
            });
            pool[t].start();
        }
        for (Thread thread : pool) thread.join();
        StringBuilder out = new StringBuilder();
        for (String result : results) out.append(result).append('\n');
        Bootstrap.STDOUT.print(out);
        Bootstrap.STDOUT.flush();
    }
}
