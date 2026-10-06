import net.minecraft.SharedConstants;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.level.biome.MultiNoiseBiomeSource;
import net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterLists;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.Aquifer;
import net.minecraft.world.level.levelgen.NoiseBasedChunkGenerator;
import net.minecraft.world.level.levelgen.NoiseChunk;
import net.minecraft.world.level.levelgen.NoiseGeneratorSettings;
import net.minecraft.world.level.levelgen.RandomState;
import net.minecraft.world.level.levelgen.blending.Blender;
import net.minecraft.world.level.levelgen.densityfunction.DensitySampler;
import net.minecraft.world.level.levelgen.densityfunction.DensityVolume;
import net.minecraft.world.level.levelgen.densityfunction.ScopedDensityBuffer;

import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

/**
 * Dumps the terrain the real game makes from noise (stone, air, water, lava: the output of NoiseBasedChunkGenerator's
 * noise fill with its aquifers; no structures, carvers or features) for a block region, with the game's own classes
 * (Minecraft 26.3 server jar).
 *
 * usage: CaveDump SEED X0 Z0 SIZE_X SIZE_Z OUT [THREADS]      (the region has to be whole chunks)
 *        CaveDump --batch [THREADS] < lines of "SEED X0 Z0 SIZE_X SIZE_Z OUT"      (prints "done OUT" after each)
 * OUT gets SIZE_Z * SIZE_X * 384 bytes: for z, for x, for y from -64 to 319: 0 solid, 1 air, 2 water, 3 lava.
 */
public class CaveDump {
    static final int MIN_Y = -64, HEIGHT = 384;
    static HolderLookup.Provider registries;
    static NoiseGeneratorSettings noiseSettings;
    static Aquifer.FluidPicker fluidPicker;

    static byte code(BlockState state) {
        if (state.isAir()) return 1;
        if (state.is(Blocks.LAVA)) return 3; // the fluid tags are not bound without a loaded datapack: go by the block
        return (byte) (state.getFluidState().isEmpty() ? 0 : 2);
    }

    @SuppressWarnings("unchecked")
    static void setup() throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        registries = VanillaRegistries.createWorldLookup();
        Holder<NoiseGeneratorSettings> settings = registries.lookupOrThrow(Registries.NOISE_SETTINGS).getOrThrow(NoiseGeneratorSettings.OVERWORLD);
        noiseSettings = settings.value();
        MultiNoiseBiomeSource biomes = MultiNoiseBiomeSource.createFromPreset(
            registries.lookupOrThrow(Registries.MULTI_NOISE_BIOME_SOURCE_PARAMETER_LIST).getOrThrow(MultiNoiseBiomeSourceParameterLists.OVERWORLD));
        NoiseBasedChunkGenerator generator = new NoiseBasedChunkGenerator(biomes, settings);
        java.lang.reflect.Field pickerField = NoiseBasedChunkGenerator.class.getDeclaredField("globalFluidPicker");
        pickerField.setAccessible(true);
        fluidPicker = ((Supplier<Aquifer.FluidPicker>) pickerField.get(generator)).get();
    }

    static void dump(long seed, int x0, int z0, int sizeX, int sizeZ, String out, int threads) throws Exception {
        if ((x0 & 15) != 0 || (z0 & 15) != 0 || (sizeX & 15) != 0 || (sizeZ & 15) != 0) throw new IllegalArgumentException("the region has to be whole chunks");
        int chunksX = sizeX / 16, chunksZ = sizeZ / 16;
        byte[] grid = new byte[sizeZ * sizeX * HEIGHT];
        AtomicInteger next = new AtomicInteger();
        Thread[] pool = new Thread[threads];
        for (int t = 0; t < threads; t++) {
            pool[t] = new Thread(() -> {
                RandomState randomState = RandomState.create(registries.lookupOrThrow(Registries.NOISE), seed, noiseSettings);
                for (int chunk; (chunk = next.getAndIncrement()) < chunksX * chunksZ; ) {
                    int minX = x0 + 16 * (chunk % chunksX), minZ = z0 + 16 * (chunk / chunksX);
                    // the same steps as NoiseBasedChunkGenerator.doFill, without the structures' beardifier
                    DensityVolume volume = new DensityVolume(16, HEIGHT, 16, minX, MIN_Y, minZ);
                    try (NoiseChunk noiseChunk = new NoiseChunk(randomState, null, noiseSettings, fluidPicker, Blender.empty(), volume)) {
                        Aquifer aquifer = noiseChunk.aquifer();
                        DensitySampler.Bound finalDensity = noiseChunk.cachingSamplers().get(noiseSettings.noiseRouter().finalDensity());
                        try (ScopedDensityBuffer densities = finalDensity.sampleVolume(noiseChunk.volume())) {
                            for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
                                int base = ((minZ + z - z0) * sizeX + (minX + x - x0)) * HEIGHT;
                                for (int y = HEIGHT - 1; y >= 0; y--) {
                                    BlockState state = aquifer.computeSubstance(minX + x, MIN_Y + y, minZ + z, densities.get(volume.indexUnchecked(x, y, z)));
                                    grid[base + y] = state == null ? 0 : code(state);
                                }
                            }
                        }
                    }
                }
            });
            pool[t].start();
        }
        for (Thread thread : pool) thread.join();
        try (OutputStream stream = new BufferedOutputStream(new FileOutputStream(out), 1 << 20)) {
            stream.write(grid);
        }
    }

    public static void main(String[] args) throws Exception {
        setup();
        if (args[0].equals("--batch")) {
            int threads = args.length > 1 ? Integer.parseInt(args[1]) : Runtime.getRuntime().availableProcessors();
            BufferedReader reader = new BufferedReader(new InputStreamReader(System.in));
            for (String line; (line = reader.readLine()) != null; ) {
                String[] w = line.trim().split("\\s+");
                if (w.length < 6) continue;
                dump(Long.parseLong(w[0]), Integer.parseInt(w[1]), Integer.parseInt(w[2]), Integer.parseInt(w[3]), Integer.parseInt(w[4]), w[5], threads);
                Bootstrap.STDOUT.println("done " + w[5]); // System.out goes through the game's logger after the bootstrap
                Bootstrap.STDOUT.flush();
            }
            return;
        }
        int sizeX = Integer.parseInt(args[3]), sizeZ = Integer.parseInt(args[4]);
        int threads = args.length > 6 ? Integer.parseInt(args[6]) : Runtime.getRuntime().availableProcessors();
        long start = System.nanoTime();
        dump(Long.parseLong(args[0]), Integer.parseInt(args[1]), Integer.parseInt(args[2]), sizeX, sizeZ, args[5], threads);
        double seconds = (System.nanoTime() - start) / 1e9;
        Bootstrap.STDOUT.printf("%d x %d columns in %.1f s (%.0f columns/s)%n", sizeX, sizeZ, seconds, sizeX * (double) sizeZ / seconds);
    }
}
