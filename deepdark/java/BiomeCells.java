import net.minecraft.SharedConstants;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.biome.Biome;
import net.minecraft.world.level.biome.BiomeResolver;
import net.minecraft.world.level.biome.Biomes;
import net.minecraft.world.level.biome.MultiNoiseBiomeSource;
import net.minecraft.world.level.biome.MultiNoiseBiomeSourceParameterLists;
import net.minecraft.world.level.levelgen.NoiseGeneratorSettings;
import net.minecraft.world.level.levelgen.RandomState;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.file.Paths;
import java.util.Map;
import java.util.TreeMap;

/**
 * Asks the real game (Minecraft 26.3 server jar, overworld preset) for the biome of every biome cell of a cavern and of
 * the cells touching them, from a file written by deepdark/ddmeasure with DD_EXPORT_DIR (48-byte records: int32 qx, qz;
 * uint64 own[2]; uint64 near[2]; int32 qtop; int32 0; little endian; bit k of a mask is cell qy = k - 16).
 * usage: BiomeCells SEED FILE [uncached|strict|lenient]
 * strict / lenient: see BiomeProbe (every lookup preceded by one of the cell above / below: deep dark only where it
 * wins outright / also where it ties). Default: the cells of a column from the bottom up, which is lenient in effect.
 * prints the number of cells, how many the game says are deep dark (own cells and near cells), how many differ from
 * cubiomes' answer (deep dark iff qy <= qtop), the first few of each, and the biomes of the own cells.
 */
public class BiomeCells {
    public static void main(String[] args) throws Exception {
        long seed = Long.parseLong(args[0]); boolean uncached = args.length > 2 && args[2].equals("uncached");
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        HolderLookup.Provider registries = VanillaRegistries.createWorldLookup();
        Holder<NoiseGeneratorSettings> settings = registries.lookupOrThrow(Registries.NOISE_SETTINGS).getOrThrow(NoiseGeneratorSettings.OVERWORLD);
        MultiNoiseBiomeSource biomes = MultiNoiseBiomeSource.createFromPreset(
            registries.lookupOrThrow(Registries.MULTI_NOISE_BIOME_SOURCE_PARAMETER_LIST).getOrThrow(MultiNoiseBiomeSourceParameterLists.OVERWORLD));
        RandomState randomState = RandomState.create(registries.lookupOrThrow(Registries.NOISE), seed, settings.value());
        final int before = args.length > 2 && args[2].equals("strict") ? 1 : args.length > 2 && args[2].equals("lenient") ? -1 : 0;
        BiomeResolver plain = uncached ? biomes.createUncachedResolver(randomState) : biomes.createCachingResolver(randomState);
        BiomeResolver resolver = before == 0 ? plain : (x, y, z) -> { plain.getNoiseBiome(x, y + before, z); return plain.getNoiseBiome(x, y, z); };

        long columns = 0, own = 0, near = 0, ownDeep = 0, nearDeep = 0, ownDeepCubiomes = 0, nearDeepCubiomes = 0, differ = 0, shownDiffer = 0, shownDeep = 0;
        Map<String, Long> ownBiomes = new TreeMap<>();
        long t0 = System.nanoTime();
        try (FileChannel channel = FileChannel.open(Paths.get(args[1]))) {
            ByteBuffer buffer = ByteBuffer.allocate(48 * 4096).order(ByteOrder.LITTLE_ENDIAN);
            while (true) {
                buffer.clear();
                while (buffer.hasRemaining() && channel.read(buffer) >= 0) { }
                buffer.flip();
                if (buffer.remaining() < 48) break;
                while (buffer.remaining() >= 48) {
                    int qx = buffer.getInt(), qz = buffer.getInt();
                    long[] ownMask = {buffer.getLong(), buffer.getLong()}, nearMask = {buffer.getLong(), buffer.getLong()};
                    int qtop = buffer.getInt(); buffer.getInt();
                    columns++;
                    for (int k = 0; k < 80; k++) {
                        if ((nearMask[k >> 6] >>> (k & 63) & 1) == 0) continue;
                        boolean isOwn = (ownMask[k >> 6] >>> (k & 63) & 1) != 0;
                        int qy = k - 16;
                        Holder<Biome> biome = resolver.getNoiseBiome(qx, qy, qz);
                        boolean deep = biome.is(Biomes.DEEP_DARK), deepCubiomes = qtop != Integer.MIN_VALUE && qy <= qtop;
                        near++; if (deep) nearDeep++; if (deepCubiomes) nearDeepCubiomes++;
                        if (isOwn) {
                            own++; if (deep) ownDeep++; if (deepCubiomes) ownDeepCubiomes++;
                            ownBiomes.merge(biome.unwrapKey().map(key -> key.identifier().getPath()).orElse("?"), 1L, Long::sum);
                            if (deep && shownDeep++ < 5) System.out.println("deep dark cell of the cavern (game): quart " + qx + " " + qy + " " + qz + " = blocks from " + (qx * 4) + " " + (qy * 4) + " " + (qz * 4));
                        }
                        if (deep != deepCubiomes) {
                            differ++;
                            if (shownDiffer++ < 20) System.out.println("DIFFERS: quart " + qx + " " + qy + " " + qz + " game " + biome.getRegisteredName() + " cubiomes deep dark " + deepCubiomes + (isOwn ? " (own)" : " (near)"));
                        }
                    }
                }
            }
        }
        System.out.println("seed " + seed + " file " + args[1] + " resolver " + (uncached ? "uncached" : "caching") + (before > 0 ? " strict" : before < 0 ? " lenient" : ""));
        System.out.println("columns " + columns + " seconds " + String.format("%.1f", (System.nanoTime() - t0) / 1e9));
        System.out.println("own cells " + own + " deep dark by the game " + ownDeep + " by cubiomes " + ownDeepCubiomes);
        System.out.println("own and touching cells " + near + " deep dark by the game " + nearDeep + " by cubiomes " + nearDeepCubiomes);
        System.out.println("cells where the game and cubiomes differ on deep dark: " + differ);
        System.out.println("biomes of the own cells: " + ownBiomes);
    }
}
