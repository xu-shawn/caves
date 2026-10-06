import net.minecraft.SharedConstants;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.levelgen.NoiseGeneratorSettings;
import net.minecraft.world.level.levelgen.RandomState;
import net.minecraft.world.level.levelgen.densityfunction.DensityFunction;
import net.minecraft.world.level.levelgen.synth.Noise;

import java.io.BufferedReader;
import java.io.InputStreamReader;

/**
 * Values of the game's own noises and density functions, for checking other implementations against.
 * usage: NoiseProbe SEED < lines        a line is "noise NAME X Y Z" (the raw noise at those noise coordinates) or
 * "density NAME X Y Z" (a registered density function at a block, uncached and uninterpolated). Prints the value per line.
 */
public class NoiseProbe {
    public static void main(String[] args) throws Exception {
        long seed = Long.parseLong(args[0]);
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        HolderLookup.Provider registries = VanillaRegistries.createWorldLookup();
        Holder<NoiseGeneratorSettings> settings = registries.lookupOrThrow(Registries.NOISE_SETTINGS).getOrThrow(NoiseGeneratorSettings.OVERWORLD);
        RandomState randomState = RandomState.create(registries.lookupOrThrow(Registries.NOISE), seed, settings.value());
        BufferedReader reader = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder out = new StringBuilder();
        for (String line; (line = reader.readLine()) != null; ) {
            String[] w = line.trim().split("\\s+");
            if (w.length < 5) continue;
            if (w[0].equals("noise")) {
                Noise noise = randomState.getOrCreateNoise(ResourceKey.create(Registries.NOISE, Identifier.parse(w[1])));
                out.append(noise.get(Double.parseDouble(w[2]), Double.parseDouble(w[3]), Double.parseDouble(w[4]))).append('\n');
            } else {
                DensityFunction function = registries.lookupOrThrow(Registries.DENSITY_FUNCTION).getOrThrow(ResourceKey.create(Registries.DENSITY_FUNCTION, Identifier.parse(w[1]))).value();
                out.append(randomState.sampleBlockValueUncached(function, Integer.parseInt(w[2]), Integer.parseInt(w[3]), Integer.parseInt(w[4]))).append('\n');
            }
        }
        System.out.print(out);
    }
}
