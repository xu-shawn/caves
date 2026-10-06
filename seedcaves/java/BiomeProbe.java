import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.BiomeManager;
import net.minecraft.world.level.biome.BiomeResolver;
import net.minecraft.world.level.biome.MultiNoiseBiomeSource;
import net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterLists;
import net.minecraft.world.level.levelgen.NoiseGeneratorSettings;
import net.minecraft.world.level.levelgen.RandomState;

import java.io.BufferedReader;
import java.io.InputStreamReader;

/**
 * The real game's biome at blocks (Minecraft 26.3 server jar, overworld preset), to check deepdark/ddbox against.
 * usage: BiomeProbe [strict|lenient] < lines "SEED X Y Z" (blocks)
 * prints "SEED X Y Z cell BIOME zoom BIOME": cell = the multi-noise biome source at the block's 4 x 4 x 4 cell
 * (quart position X >> 2, Y >> 2, Z >> 2: what the game stores in the chunk); zoom = BiomeManager.getBiome(block)
 * over the same source (what the game reports at the block: F3, mob spawning).
 *
 * Ties: the game's search for the nearest biome starts from the last biome its thread found (Climate.RTree.lastResult)
 * and only moves to a strictly nearer one, so a cell exactly as near to two biomes gets either, by the order of the
 * lookups. Deep dark ties with the surface biome in the top cell of its layer in about 1 column in 300.
 * strict: every lookup is preceded by one of the cell above (deep dark is reported only where it wins outright);
 * lenient: by one of the cell below (deep dark is reported where it wins or ties). Default: no extra lookups.
 */
public class BiomeProbe {
    static String name(Holder<Biome> biome) { return biome.unwrapKey().map(k -> k.identifier().getPath()).orElse("?"); }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        HolderLookup.Provider registries = VanillaRegistries.createWorldLookup();
        Holder<NoiseGeneratorSettings> settings = registries.lookupOrThrow(Registries.NOISE_SETTINGS).getOrThrow(Boolean.getBoolean("cave.normal") ? NoiseGeneratorSettings.OVERWORLD : NoiseGeneratorSettings.LARGE_BIOMES);
        MultiNoiseBiomeSource biomes = MultiNoiseBiomeSource.createFromPreset(
            registries.lookupOrThrow(Registries.MULTI_NOISE_BIOME_SOURCE_PARAMETER_LIST).getOrThrow(MultiNoiseBiomeSourceParameterLists.OVERWORLD));
        BufferedReader reader = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder out = new StringBuilder();
        final int before = args.length > 0 && args[0].equals("strict") ? 1 : args.length > 0 && args[0].equals("lenient") ? -1 : 0;
        long cached = 0; BiomeResolver resolver = null; BiomeManager manager = null;
        for (String line; (line = reader.readLine()) != null; ) {
            String[] w = line.trim().split("\\s+");
            if (w.length < 4) continue;
            long seed = Long.parseLong(w[0]); int x = Integer.parseInt(w[1]), y = Integer.parseInt(w[2]), z = Integer.parseInt(w[3]);
            if (resolver == null || seed != cached) {
                RandomState randomState = RandomState.create(registries.lookupOrThrow(Registries.NOISE), seed, settings.value());
                BiomeResolver plain = biomes.createUncachedResolver(randomState);
                resolver = before == 0 ? plain : (qx, qy, qz) -> { plain.getNoiseBiome(qx, qy + before, qz); return plain.getNoiseBiome(qx, qy, qz); };
                manager = new BiomeManager(resolver, BiomeManager.obfuscateSeed(seed));
                cached = seed;
            }
            out.append(seed).append(' ').append(x).append(' ').append(y).append(' ').append(z)
               .append(" cell ").append(name(resolver.getNoiseBiome(x >> 2, y >> 2, z >> 2)))
               .append(" zoom ").append(name(manager.getBiome(new BlockPos(x, y, z)))).append('\n');
        }
        System.out.print(out);
    }
}
