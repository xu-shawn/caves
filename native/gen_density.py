#!/usr/bin/env python3
"""Turns the game's overworld density functions (the datapack JSON of Minecraft 26.3) into C++ that computes them with the
same float arithmetic as the game's samplers (net.minecraft.world.level.levelgen.densityfunction.*, point evaluation).

usage: gen_density.py DATA_DIR OUT.hpp      (DATA_DIR = .../data/minecraft/worldgen)

What comes out, all taking a point context P (block x, y, z, the column's cache, the point's cache):
  corner_0 .. corner_4   the inputs of the five "interpolated" nodes of final_density, to be sampled at cell corners
  final_block            final_density from the five interpolated values in p.interp
  surface_upper, surface_density   the two parts of preliminary_surface_level (find_top_surface)
  aq_barrier, aq_floodedness, aq_spread, aq_lava, aq_exclusion   the aquifer's functions
Functions of x and z only are cached per column, cached functions of x, y, z per point."""
import json, os, sys
DATA, OUT = sys.argv[1], sys.argv[2]

def lit(v): return f'((float) {float(v)!r})'

class Generator:
    def __init__(self):
        self.noises = {}; self.defs = []; self.named = {}; self.counter = 0; self.slots2d = 0; self.slots3d = 0; self.interpolated = []; self.json = {}
    def uid(self): self.counter += 1; return self.counter
    def load(self, name):
        name = name.replace('minecraft:', '')
        if name not in self.json: self.json[name] = json.load(open(os.path.join(DATA, 'density_function', name + '.json')))
        return self.json[name]
    def noise(self, name):
        if name not in self.noises: self.noises[name] = len(self.noises)
        return self.noises[name]
    # does the node depend on y?
    def dep(self, node):
        if isinstance(node, (int, float)): return False
        if isinstance(node, str): return self.dep(self.load(node))
        t = node['type'].replace('minecraft:', '')
        if t == 'gradient': return node['axis'] == 'y'
        if t == 'noise': return node['y_scale'] != 0.0 or any(self.dep(node[k]) for k in ('shift_x', 'shift_y', 'shift_z') if k in node)
        if t in ('shift_a', 'blend_alpha', 'blend_offset', 'beardifier', 'shift_b'): return False
        if t in ('old_blended_noise', 'interpolated'): return True
        if t == 'spline': return self.dep_spline(node['spline'])
        return any(self.dep(v) for k, v in node.items() if k not in ('type',) and isinstance(v, (dict, str, int, float)) and k not in ('axis', 'noise', 'min', 'max', 'min_inclusive', 'max_exclusive', 'cell_size_xz', 'cell_size_y')) \
            or any(self.dep(x) for v in node.values() if isinstance(v, list) for x in v if isinstance(x, (dict, str)))
    def dep_spline(self, sp):
        if isinstance(sp, (int, float)): return False
        return self.dep(sp['coordinate']) or any(self.dep_spline(pt['value']) for pt in sp['points'])
    def memo(self, node, body_fn, label):
        """A function with a cache slot (per column if the node doesn't depend on y)."""
        three = self.dep(node); n = self.uid(); name = f'df_{n}_{label}'
        if three: slot = self.slots3d; self.slots3d += 1; store = 'p'
        else: slot = self.slots2d; self.slots2d += 1; store = '(*p.col)'
        self.defs.append((name, None))            # reserve the declaration order
        body = body_fn()
        self.defs.append((name, f'static float {name}(P &p) {{\n    if (!{store}.has[{slot}]) {{ {store}.memo[{slot}] = {body}; {store}.has[{slot}] = 1; }}\n    return {store}.memo[{slot}];\n}}\n'))
        return name
    def ref(self, name):
        key = name.replace('minecraft:', '')
        if key not in self.named:
            self.named[key] = None
            self.named[key] = self.memo(self.load(key), lambda: self.expr(self.load(key)), ''.join(c if c.isalnum() else '_' for c in key))
        return f'{self.named[key]}(p)'
    def spline(self, sp):
        if isinstance(sp, (int, float)): return lit(sp)
        n = self.uid(); pts = sp['points']; values = [self.spline(pt['value']) for pt in pts]
        coordinate = self.expr(sp['coordinate'])
        cases = ' '.join(f'case {i}: return {v};' for i, v in enumerate(values))
        name = f'spline_{n}'
        self.defs.append((name, f'static float {name}(P &p) {{\n    static const float loc[] = {{{", ".join(lit(pt["location"]) for pt in pts)}}}, der[] = {{{", ".join(lit(pt["derivative"]) for pt in pts)}}};\n'
                                f'    return splineSample({coordinate}, {len(pts)}, loc, der, [&](int i) -> float {{ switch (i) {{ {cases} }} return 0.0f; }});\n}}\n'))
        return f'{name}(p)'
    def expr(self, node):
        if isinstance(node, (int, float)): return lit(node)
        if isinstance(node, str): return self.ref(node)
        t = node['type'].replace('minecraft:', ''); u = self.uid()
        const = lambda v: isinstance(v, (int, float))
        if t in ('add', 'sub', 'mul', 'div', 'min', 'max'):
            l, r = node['left'], node['right']; L, R = self.expr(l), self.expr(r)
            if t == 'add': return f'({L} + {R})'
            if t == 'sub': return f'({L} - {R})'
            if t == 'min': return f'std::min({L}, {R})'
            if t == 'max': return f'std::max({L}, {R})'
            if t == 'mul':
                if const(l) or const(r): return f'({L} * {R})'
                return f'({{ float l{u} = {L}; l{u} == 0.0f ? 0.0f : l{u} * {R}; }})'
            if const(l): return f'({L} / {R})'
            if const(r): return f'({L} * (1.0f / {R}))'
            return f'({{ float l{u} = {L}; l{u} == 0.0f ? 0.0f : l{u} / {R}; }})'
        if t == 'abs': return f'std::fabs({self.expr(node["input"])})'
        if t == 'square': return f'({{ float v{u} = {self.expr(node["input"])}; v{u} * v{u}; }})'
        if t == 'cube': return f'({{ float v{u} = {self.expr(node["input"])}; v{u} * v{u} * v{u}; }})'
        if t == 'half_negative': return f'({{ float v{u} = {self.expr(node["input"])}; v{u} > 0.0f ? v{u} : v{u} * 0.5f; }})'
        if t == 'quarter_negative': return f'({{ float v{u} = {self.expr(node["input"])}; v{u} > 0.0f ? v{u} : v{u} * 0.25f; }})'
        if t == 'squeeze': return f'squeeze({self.expr(node["input"])})'
        if t == 'clamp': return f'clampf({self.expr(node["input"])}, {lit(node["min"])}, {lit(node["max"])})'
        if t == 'lerp':
            A, F, S = self.expr(node['alpha']), self.expr(node['first']), self.expr(node['second'])
            return f'({{ float a{u} = {A}; a{u} == 0.0f ? {F} : (a{u} == 1.0f ? {S} : ({{ float f{u} = {F}; f{u} + a{u} * ({S} - f{u}); }})); }})'
        if t == 'gradient':
            axis = node['axis']; assert node.get('tiling', 'clamp') in ('clamp', 'clamped'), node
            return f'gradient(p.{axis}, {int(node["from_coordinate"])}, {int(node["to_coordinate"])}, {lit(node["from_value"])}, {lit(node["to_value"])})'
        if t == 'range_choice':
            return f'({{ float in{u} = {self.expr(node["input"])}; (in{u} >= {lit(node["min_inclusive"])} && in{u} < {lit(node["max_exclusive"])}) ? {self.expr(node["when_in_range"])} : {self.expr(node["when_out_of_range"])}; }})'
        if t == 'interval_select':
            fs = [self.expr(f) for f in node['functions']]; th = node['thresholds']; code = fs[-1]
            for i in reversed(range(len(th))): code = f'(in{u} < {lit(th[i])} ? {fs[i]} : {code})'
            return f'({{ float in{u} = {self.expr(node["input"])}; {code}; }})'
        if t == 'noise':
            i = self.noise(node['noise']); xz, ys = float(node['xz_scale']), float(node['y_scale'])
            assert 'shift_y' not in node
            sx = f' + {self.expr(node["shift_x"])}' if 'shift_x' in node else ''; sz = f' + {self.expr(node["shift_z"])}' if 'shift_z' in node else ''
            return f'p.w->noise[{i}].get(p.x * {xz!r}{sx}, p.y * {ys!r}, p.z * {xz!r}{sz})'
        if t == 'shift_a': return f'(p.w->noise[{self.noise(node["noise"])}].get(p.x * 0.25, p.y * 0.0, p.z * 0.25) * 4.0f)'
        if t == 'shift_b': return f'(p.w->noise[{self.noise(node["noise"])}].get(p.z * 0.25, p.x * 0.25, 0.0) * 4.0f)'
        if t == 'spline': return self.spline(node['spline'])
        if t == 'old_blended_noise':
            self.blended = node; return 'p.w->blended.get(p.x, p.y, p.z)'
        if t == 'blend_alpha': return '1.0f'
        if t == 'blend_offset': return '0.0f'
        if t == 'beardifier': return '0.0f'
        if t == 'blend_density': return self.expr(node['input'])
        if t == 'cache': return f'{self.memo(node, lambda: self.expr(node["input"]), "cache")}(p)'
        if t == 'interpolated':
            assert (node['cell_size_xz'], node['cell_size_y']) == (4, 8)
            k = len(self.interpolated); self.interpolated.append(None)
            self.interpolated[k] = self.expr(node['input'])
            return f'p.interp[{k}]'
        raise SystemExit('no rule for ' + t)

g = Generator()
settings = json.load(open(os.path.join(DATA, 'noise_settings', 'overworld.json')))
final = g.expr(settings['noise_router']['final_density'])
surface = g.load('overworld/preliminary_surface_level'); assert surface['type'] == 'minecraft:find_top_surface'
upper, sdensity = g.expr(surface['upper_bound']), g.expr(surface['density'])
aq = settings['aquifers']; assert aq['surface_level'] == 'minecraft:overworld/preliminary_surface_level'
aquifer = {k: g.expr(aq[k]) for k in ('barrier', 'fluid_level_floodedness', 'fluid_level_spread', 'lava', 'exclusion')}
b = g.blended
out = ['// Generated by gen_density.py from the Minecraft 26.3 datapack: the overworld density functions. Do not edit.',
       f'#define GEN_NOISES {len(g.noises)}', f'#define GEN_SLOTS_2D {max(1, g.slots2d)}', f'#define GEN_SLOTS_3D {max(1, g.slots3d)}', f'#define GEN_INTERPOLATED {len(g.interpolated)}',
       f'#define GEN_SURFACE_CELL {int(surface["cell_height"])}', f'#define GEN_SURFACE_LOWER {int(surface["lower_bound"])}',
       f'#define GEN_SEA_LEVEL {int(settings["sea_level"])}',
       f'static const double GEN_BLENDED[5] = {{{float(b["xz_scale"])!r}, {float(b["y_scale"])!r}, {float(b["xz_factor"])!r}, {float(b["y_factor"])!r}, {float(b["smear_scale_multiplier"])!r}}}; // xz_scale, y_scale, xz_factor, y_factor, smear',
       'static const char *const GEN_NOISE_NAMES[GEN_NOISES] = {' + ', '.join(f'"{n}"' for n in g.noises) + '};',
       '#include "gamegen_types.hpp"', '']
seen = set()
for name, _ in g.defs:                          # declarations first: the functions call each other
    if name not in seen: seen.add(name); out.append(f'static float {name}(P &p);')
out.append('')
out += [code for _, code in g.defs if code]
for k, code in enumerate(g.interpolated): out.append(f'static float corner_{k}(P &p) {{ return {code}; }}')
out.append(f'static float final_block(P &p) {{ return {final}; }}')
out.append(f'static float surface_upper(P &p) {{ return {upper}; }}')
out.append(f'static float surface_density(P &p) {{ return {sdensity}; }}')
for k, code in aquifer.items(): out.append(f'static float aq_{k.replace("fluid_level_", "")}(P &p) {{ return {code}; }}')
out.append('static const struct { const char *name; float (*function)(P &); } GEN_NAMED[] = {' + ', '.join(f'{{"minecraft:{k}", {v}}}' for k, v in g.named.items() if 'noodle' not in k and 'final_density' not in k) + '};')
open(OUT, 'w').write('\n'.join(out) + '\n')
print(f'{len(g.noises)} noises, {g.slots2d} column slots, {g.slots3d} point slots, {len(g.interpolated)} interpolated, {len(g.defs)} functions -> {OUT}')
