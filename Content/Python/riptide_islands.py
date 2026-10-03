"""Brings Riptide's designed islands into the editor: generates each island's ground from its design
(riptide_island_shape.py, riptide_island_mesh.py), imports it under /Game/Riptide/Islands, and builds the island
test map. Called by init_unreal.py when the editor opens; it does nothing when everything is up to date.

Bump an island's version here when its design changes, so every machine rebuilds it on its next launch.
"""
import math
import os

import unreal

ISLANDS_PATH = "/Game/Riptide/Islands"
MATERIALS_PATH = "/Game/Riptide/Materials"
ISLAND_MAP_PATH = "/Game/Riptide/Maps/Island_Test"

ISLAND_VERSIONS = {"StartCay": "12"}
GREY_VERSION = "3"
ISLAND_MAP_VERSION = "15"

# Waves reach full size in water this deep, in cm, and die away toward the shore (the plugin's fall-off: about a
# tenth of full size in half a metre of water, a fifth in 1 m, two fifths in 2 m, two thirds in 4 m, nearly all in
# 15 m). Both the drawn waves and the ones the game reads (ARiptideOcean) follow it, so the bay is calm and the
# beach isn't swamped.
SHALLOWS_WAVE_DEPTH = 800.0

# The Water plugin's depth map (where the seabed is, for the drawn waves) follows the camera on island maps, covering
# this far across, in cm, at this many pixels: 1.5 m a pixel. Spread over the whole 24 km sea it would be 47 m a
# pixel, far too coarse to find a beach.
DEPTH_MAP_SPAN = 300000.0
DEPTH_MAP_PIXELS = 2048

# How far off the wash-up beach's waterline the boat starts on the island test map, metres.
BOAT_START_OFF_BEACH = 45.0


def _grey_material():
    """M_IslandGrey: the bare shape of an island before it has surfaces. Matte grey with a line every metre and a
    stronger one every 10 m, drawn from world position, so slopes and sizes can be read by eye."""
    path = f"{MATERIALS_PATH}/M_IslandGrey"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == GREY_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_IslandGrey", MATERIALS_PATH, unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -700, 0)
    pin = unreal.CustomInput()
    pin.set_editor_property("input_name", "P")
    grid = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -400, 0)
    grid.set_editor_property("inputs", [pin])
    grid.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    grid.set_editor_property("code", """
float2 m = P.xy / 100.0;
float2 w = max(fwidth(m), 1e-4);
float2 a = abs(frac(m - 0.5) - 0.5) / w;
float fine = 1.0 - saturate(min(a.x, a.y) - 0.2);
fine *= saturate(1.0 - max(w.x, w.y) * 2.5);
float2 m10 = m / 10.0;
float2 w10 = max(fwidth(m10), 1e-5);
float2 b = abs(frac(m10 - 0.5) - 0.5) / w10;
float coarse = 1.0 - saturate(min(b.x, b.y) - 0.6);
float shade = 0.2 - 0.05 * fine - 0.1 * coarse;
return float3(shade, shade, shade);""")
    mel.connect_material_expressions(world, "", grid, "P")
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    rough.set_editor_property("r", 0.92)
    mel.connect_material_property(grid, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NANITE)
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", GREY_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


# --- Surfaces ----------------------------------------------------------------------------------------------------
# The ground's photo surfaces (SourceAssets/Textures, credited in Docs/CREDITS.md) and the material that lays them
# over an island by its surface map. Bump SURFACE_VERSION when the material's recipe changes.
TEXTURES_PATH = f"{ISLANDS_PATH}/Textures"
SURFACE_VERSION = "10"
GROUND_TEXTURES = ["dense_sand", "aerial_beach_01", "shell_floor_01", "coral_mud_01", "seaside_rock", "forrest_sand_01",
                   "low_tide_rocks"]
TEXTURE_MAPS = ["diff", "nor_dx", "arm", "disp"]

# The ground material's inputs: (name in the code below, texture, map).
GROUND_INPUTS = [
    ("SandD", "dense_sand", "diff"), ("SandN", "dense_sand", "nor_dx"), ("SandA", "dense_sand", "arm"), ("SandH", "dense_sand", "disp"),
    ("FarD", "aerial_beach_01", "diff"), ("FarN", "aerial_beach_01", "nor_dx"), ("FarH", "aerial_beach_01", "disp"),
    ("ShellD", "shell_floor_01", "diff"), ("ShellN", "shell_floor_01", "nor_dx"), ("ShellA", "shell_floor_01", "arm"),
    ("CoralD", "coral_mud_01", "diff"), ("CoralN", "coral_mud_01", "nor_dx"), ("CoralA", "coral_mud_01", "arm"), ("CoralH", "coral_mud_01", "disp"),
    ("RockD", "seaside_rock", "diff"), ("RockN", "seaside_rock", "nor_dx"), ("RockA", "seaside_rock", "arm"), ("RockH", "seaside_rock", "disp"),
    ("GroveD", "forrest_sand_01", "diff"), ("GroveN", "forrest_sand_01", "nor_dx"), ("GroveA", "forrest_sand_01", "arm"), ("GroveH", "forrest_sand_01", "disp"),
    ("ReefD", "low_tide_rocks", "diff"), ("ReefN", "low_tide_rocks", "nor_dx"), ("ReefA", "low_tide_rocks", "arm"),
]

# The whole ground surface in one piece of shader code. UV is the mesh's own, in metres (east, north); every
# photo is laid at its real size. Each surface is only read where the surface map says it's present.
GROUND_CODE = """
float2 dx = ddx(UV), dy = ddy(UV);
#define RT_TEX(T, sc) T.SampleGrad(Material.Wrap_WorldGroupSettings, UV / (sc), dx / (sc), dy / (sc))
// A photo's colour pulled toward grey by d, then tinted: the photos are of browner ground than a coral cay's.
#define RT_TONE(c, d, t) (lerp((c), dot((c), float3(0.3, 0.59, 0.11)).xxx, (d)) * (t))
// A random number per cell of a grid (an integer hash: the sine trick lines up into a visible lattice), and a
// smooth noise read from four of them.
#define RT_MIX(h) ((((h) ^ ((h) >> 13u)) * 1103515245u) ^ ((((h) ^ ((h) >> 13u)) * 1103515245u) >> 16u))
#define RT_HASH(p) (float(RT_MIX((asuint(int(floor((p).x))) * 374761393u) ^ (asuint(int(floor((p).y))) * 668265263u)) >> 8u) * (1.0 / 16777216.0))
#define RT_VNOISE(g, q) lerp(lerp(RT_HASH(g), RT_HASH((g) + float2(1, 0)), (q).x), lerp(RT_HASH((g) + float2(0, 1)), RT_HASH((g) + float2(1, 1)), (q).x), (q).y)
float zm = WorldPos.z / 100.0;

// Broad, soft variation (the far beach photo's height, tens of metres across): breaks up edges and repeats.
float broad = RT_TEX(FarH, 47.0).r;
float mid = RT_TEX(FarH, 11.0).r;

// The island's surface map, read by world position (u east, v north), nudged so its edges wander.
float2 suv = (WorldPos.yx / 100.0 + SurfaceHalf + (float2(mid, broad) - 0.5) * 3.0) / (2.0 * SurfaceHalf);
float4 sm = Surface.SampleLevel(Material.Clamp_WorldGroupSettings, suv, 0);
float wRock = sm.r, wCoral = sm.g, wGrove = sm.b, wReef = sm.a;
// Any face too steep to hold sand is bare rock, read from the ground's own slope (finer than the map).
float steep = 1.0 - smoothstep(0.72, 0.84, Up.z);
wRock = max(wRock, steep);
wCoral *= 1.0 - steep; wGrove *= 1.0 - steep; wReef *= 1.0 - steep;
float wSand = saturate(1.0 - wRock - wCoral - wGrove - wReef);

// Where two surfaces meet, the one standing higher in its photo wins: sand lies in the rock's hollows.
float hSand = RT_TEX(SandH, 1.8).r;
float hCoral = wCoral > 0.004 ? RT_TEX(CoralH, 2.0).r : 0.5;
float hRock = wRock > 0.004 ? RT_TEX(RockH, 2.0).r : 0.5;
float hGrove = wGrove > 0.004 ? RT_TEX(GroveH, 2.0).r : 0.5;
float aSand = wSand * (0.25 + hSand * 0.6);
float aCoral = wCoral * (0.3 + hCoral);
float aRock = wRock * (0.35 + hRock);
float aGrove = wGrove * (0.3 + hGrove);
float aReef = wReef * 0.8;
float top = max(max(max(aSand, aCoral), max(aRock, aGrove)), aReef);
float cut = top * 0.7;
aSand = max(aSand - cut, 0); aCoral = max(aCoral - cut, 0); aRock = max(aRock - cut, 0);
aGrove = max(aGrove - cut, 0); aReef = max(aReef - cut, 0);
float total = aSand + aCoral + aRock + aGrove + aReef + 1e-5;
aSand /= total; aCoral /= total; aRock /= total; aGrove /= total; aReef /= total;

float3 col = 0; float3 nrm = 0; float rough = 0; float ao = 0;

if (aSand > 0.004)
{
    // Coral sand, made here rather than read from a photo: the sand photos we have are of coarser, pebbly beaches,
    // and underfoot they read as gravel. Coral sand is fine, pale and nearly featureless: a grain you only see close
    // up, flecks of broken shell, wind ripples over the dry upper beach, and smooth packed sand where the sea reaches.
    // Everything fine fades out before it is small enough to shimmer.
    float2 fw = fwidth(UV);                                   // metres per pixel
    float fine = saturate(1.0 - fw.x * 180.0);                // the 2 mm grain: gone by 5 mm a pixel
    float coarse = saturate(1.0 - fw.x * 45.0);               // the 7 mm grain, flecks and ripples: gone by 2 cm a pixel
    // The second grain is turned 37 degrees from the first, so neither lines up with the other or the world.
    float2 UVr = float2(UV.x * 0.7986 - UV.y * 0.6018, UV.x * 0.6018 + UV.y * 0.7986);
    float2 g1 = UV / 0.002; float2 q1 = frac(g1); q1 = q1 * q1 * (3.0 - 2.0 * q1);
    float2 g2 = UVr / 0.007; float2 q2 = frac(g2); q2 = q2 * q2 * (3.0 - 2.0 * q2);
    float2 g2x = (UVr + float2(0.0025, 0.0)) / 0.007; float2 q2x = frac(g2x); q2x = q2x * q2x * (3.0 - 2.0 * q2x);
    float2 g2y = (UVr + float2(0.0, 0.0025)) / 0.007; float2 q2y = frac(g2y); q2y = q2y * q2y * (3.0 - 2.0 * q2y);
    float h2 = RT_VNOISE(g2, q2);
    float grain = (RT_VNOISE(g1, q1) - 0.5) * 0.5 * fine + (h2 - 0.5) * coarse;
    // The grain's slope, as a bump about half a millimetre high (in the turned frame, turned back).
    float2 slopeR = float2(RT_VNOISE(g2x, q2x) - h2, RT_VNOISE(g2y, q2y) - h2) / 0.0025 * 0.0006 * coarse;
    float2 slope = float2(slopeR.x * 0.7986 + slopeR.y * 0.6018, -slopeR.x * 0.6018 + slopeR.y * 0.7986);

    // Patches of broad light and shade over tens of metres, mottling over a metre, and the far beach photo's wind
    // marks from a distance.
    float far = saturate((Dist / 100.0 - 8.0) / 40.0);
    float3 c = float3(0.80, 0.755, 0.64) * (1.0 + grain * 0.22) * lerp(0.93, 1.07, mid) * lerp(0.95, 1.05, RT_TEX(FarH, 0.9).r);
    float3 fc = RT_TONE(RT_TEX(FarD, 30.0).rgb, 0.6, float3(1.3, 1.28, 1.2));
    c = lerp(c, fc, far * 0.5);
    float3 fn = UnpackNormalMap(RT_TEX(FarN, 30.0)).xyz;
    float rgh = 0.86 - grain * 0.08;

    // Wind ripples: ridges across the wind, 8-9 cm apart, steeper on the lee side, wandering with the beach, only
    // on the dry sand above the wash and only in patches.
    float dry = smoothstep(1.2, 1.55, zm + (mid - 0.5) * 0.4);
    float ripples = dry * smoothstep(0.35, 0.6, RT_TEX(FarH, 9.0).r) * coarse;
    float2 wind = normalize(float2(0.78, 0.62));
    float spacing = 0.085 * (0.85 + 0.3 * RT_TEX(FarH, 5.0).r);
    float ph = (dot(UV, wind) + (RT_TEX(FarH, 2.6).r - 0.5) * 0.3) / spacing;
    float saw = frac(ph);
    float ridge = saw < 0.68 ? saw / 0.68 : (1.0 - saw) / 0.32;
    float dridge = (saw < 0.68 ? 1.0 / 0.68 : -1.0 / 0.32) / spacing * 0.0025;    // 2.5 mm high
    slope += wind * dridge * ripples;
    c *= 1.0 - ripples * (ridge - 0.5) * 0.06;

    // Flecks of broken shell, one in every dozen 2.5 cm cells: white, pink or grey, a few millimetres across.
    float2 cell = floor(UV / 0.025);
    float pick = RT_HASH(cell * 1.37 + 3.1);
    if (pick > 0.92 && coarse > 0.01)
    {
        float2 at = frac(UV / 0.025) - 0.5 - (float2(RT_HASH(cell + 7.3), RT_HASH(cell + 11.9)) - 0.5) * 0.5;
        float rad = 0.09 + RT_HASH(cell + 5.5) * 0.14;
        float fleck = (1.0 - smoothstep(0.6, 1.0, length(at) / rad)) * coarse;
        float tint = RT_HASH(cell + 2.2);
        float3 fcol = tint < 0.6 ? float3(0.9, 0.88, 0.82) : tint < 0.85 ? float3(0.86, 0.66, 0.6) : float3(0.5, 0.5, 0.47);
        c = lerp(c, fcol, fleck);
        rgh = lerp(rgh, 0.55, fleck);
        slope += at / max(rad, 1e-3) * fleck * 0.25;
    }

    // Broken shell gathers thicker along the high-tide line.
    float tide = zm + (mid - 0.5) * 0.5;
    float shell = smoothstep(1.0, 1.2, tide) * (1.0 - smoothstep(1.45, 1.8, tide)) * smoothstep(0.4, 0.65, RT_TEX(FarH, 3.7).r) * 0.6;
    float3 n = normalize(float3(-slope.x, -slope.y, 1.0));
    n = normalize(lerp(n, fn, far * 0.6));
    if (shell > 0.004)
    {
        c = lerp(c, RT_TONE(RT_TEX(ShellD, 3.0).rgb, 0.3, float3(1.1, 1.08, 1.04)), shell);
        n = normalize(lerp(n, UnpackNormalMap(RT_TEX(ShellN, 3.0)).xyz, shell));
        rgh = lerp(rgh, RT_TEX(ShellA, 3.0).g, shell);
    }
    // Packed smooth where the sea has been: the grain and ripples flatten out.
    float packed = 1.0 - smoothstep(0.3, 0.9, zm);
    n = normalize(lerp(n, float3(0.0, 0.0, 1.0), packed * 0.7));
    col += aSand * c * SandTint.rgb; nrm += aSand * n; rough += aSand * rgh; ao += aSand;
}
if (aCoral > 0.004)
{
    float3 arm = RT_TEX(CoralA, 2.0).rgb;
    // Weathered limestone: grey-white, not the photo's orange.
    col += aCoral * RT_TONE(RT_TEX(CoralD, 2.0).rgb, 0.75, float3(0.6, 0.6, 0.57)); nrm += aCoral * UnpackNormalMap(RT_TEX(CoralN, 2.0)).xyz;
    rough += aCoral * arm.g; ao += aCoral * arm.r;
}
if (aRock > 0.004)
{
    // Two sizes of the same rock mixed, so cliffs don't show the photo repeating.
    float3 arm = RT_TEX(RockA, 2.0).rgb;
    float3 c = lerp(RT_TEX(RockD, 2.0).rgb, RT_TEX(RockD, 7.3).rgb, 0.45);
    float3 n = normalize(lerp(UnpackNormalMap(RT_TEX(RockN, 2.0)).xyz, UnpackNormalMap(RT_TEX(RockN, 7.3)).xyz, 0.45));
    c = RT_TONE(c, 0.3, float3(1.25, 1.22, 1.15));
    n = normalize(n * float3(1.6, 1.6, 1.0));
    col += aRock * c; nrm += aRock * n; rough += aRock * arm.g; ao += aRock * arm.r;
}
if (aGrove > 0.004)
{
    float3 arm = RT_TEX(GroveA, 2.0).rgb;
    // Darker, browner soil under the trees, with fallen leaves lying on it: dark brown and tan flakes 2-5 cm
    // across, thicker where the ground dips.
    float3 c = RT_TONE(RT_TEX(GroveD, 2.0).rgb, 0.15, float3(0.52, 0.47, 0.37));
    float3 n = UnpackNormalMap(RT_TEX(GroveN, 2.0)).xyz;
    float leafy = saturate(1.0 - fwidth(UV).x * 25.0) * smoothstep(0.3, 0.7, RT_TEX(FarH, 6.0).r);
    if (leafy > 0.01)
    {
        float2 cell = floor(UV / 0.045);
        float pick = RT_HASH(cell * 1.91 + 0.7);
        float2 at = frac(UV / 0.045) - 0.5 - (float2(RT_HASH(cell + 3.3), RT_HASH(cell + 9.1)) - 0.5) * 0.4;
        float2 stretch = float2(1.0 + RT_HASH(cell + 1.3), 1.0);
        float leaf = (1.0 - smoothstep(0.55, 0.9, length(at * stretch) / 0.3)) * step(0.45, pick) * leafy;
        float tint = RT_HASH(cell + 4.4);
        float3 lcol = tint < 0.5 ? float3(0.30, 0.19, 0.10) : tint < 0.8 ? float3(0.52, 0.36, 0.18) : float3(0.62, 0.5, 0.3);
        c = lerp(c, lcol, leaf);
        n = normalize(lerp(n, float3(at.x, at.y, 1.2), leaf * 0.5));
    }
    col += aGrove * c; nrm += aGrove * n; rough += aGrove * lerp(arm.g, 0.75, leafy * 0.5); ao += aGrove * arm.r;
}
if (aReef > 0.004)
{
    float3 arm = RT_TEX(ReefA, 2.17).rgb;
    col += aReef * RT_TEX(ReefD, 2.17).rgb; nrm += aReef * UnpackNormalMap(RT_TEX(ReefN, 2.17)).xyz;
    rough += aReef * arm.g; ao += aReef * arm.r;
}

// Lighter and darker over tens of metres, as real ground is.
col *= lerp(0.86, 1.1, broad);

// Wet where the sea reaches: darker and glossier up to the top of the wash, and everything below the waterline.
float wet = 1.0 - smoothstep(-0.05, 0.55, zm + (mid - 0.5) * 0.2);
col *= lerp(1.0, 0.58, wet);
rough = lerp(rough, 0.28, wet * 0.85);

Normal = normalize(nrm);
Rough = rough;
AO = ao;
#undef RT_TEX
#undef RT_TONE
#undef RT_HASH
#undef RT_VNOISE
return col;
"""


def import_ground_textures():
    """The ground photos, imported when missing. Colour stays sRGB; normals, the packed roughness maps and heights
    are plain data."""
    assets = unreal.EditorAssetLibrary
    source = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceAssets", "Textures")
    tasks, wanted = [], []
    for name in GROUND_TEXTURES:
        for kind in TEXTURE_MAPS:
            asset = f"T_{name}_{kind}"
            wanted.append((asset, kind))
            if assets.does_asset_exist(f"{TEXTURES_PATH}/{asset}"):
                continue
            task = unreal.AssetImportTask()
            task.filename = os.path.join(source, name, f"{name}_{kind}.jpg")
            task.destination_path = TEXTURES_PATH
            task.destination_name = asset
            task.automated = True
            task.replace_existing = True
            task.save = False
            tasks.append(task)
    if not tasks:
        return
    unreal.log(f"Riptide: importing {len(tasks)} ground textures")
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for asset, kind in wanted:
        path = f"{TEXTURES_PATH}/{asset}"
        tex = unreal.load_asset(path)
        if not tex:
            unreal.log_error(f"Riptide: ground texture {asset} failed to import")
            continue
        if kind == "nor_dx":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif kind != "diff":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        assets.save_asset(path, only_if_is_dirty=False)


def _ground_material():
    """M_IslandGround: sand, shell, coral rock, dark rock, grove floor and reef, laid by an island's surface map
    (the Surface texture and SurfaceHalf, set per island in its material instance), wet near the waterline."""
    path = f"{MATERIALS_PATH}/M_IslandGround"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == SURFACE_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    import_ground_textures()
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_IslandGround", MATERIALS_PATH, unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    sources = []

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -300)
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, -200)
    camera = mel.create_material_expression(mat, unreal.MaterialExpressionCameraPositionWS, -1100, -100)
    dist = mel.create_material_expression(mat, unreal.MaterialExpressionDistance, -900, -100)
    mel.connect_material_expressions(world, "", dist, "A")
    mel.connect_material_expressions(camera, "", dist, "B")
    half = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 0)
    half.set_editor_property("parameter_name", "SurfaceHalf")
    half.set_editor_property("default_value", 256.0)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, 100)
    tint.set_editor_property("parameter_name", "SandTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    surface = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -900, 250)
    surface.set_editor_property("parameter_name", "Surface")
    surface.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    up = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, 400)
    sources += [("Up", up), ("UV", uv), ("WorldPos", world), ("Dist", dist), ("SurfaceHalf", half), ("SandTint", tint), ("Surface", surface)]
    for i, (name, texture, kind) in enumerate(GROUND_INPUTS):
        obj = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObject, -1500, i * 120)
        tex = unreal.load_asset(f"{TEXTURES_PATH}/T_{texture}_{kind}")
        obj.set_editor_property("texture", tex)
        obj.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind == "diff"
                                else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind == "nor_dx"
                                else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        sources.append((name, obj))

    pins = []
    for name, _ in sources:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    outs = []
    for name, kind in (("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                       ("AO", unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        out = unreal.CustomOutput()
        out.set_editor_property("output_name", name)
        out.set_editor_property("output_type", kind)
        outs.append(out)
    node.set_editor_property("inputs", pins)
    node.set_editor_property("additional_outputs", outs)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("code", GROUND_CODE)
    for name, src in sources:
        mel.connect_material_expressions(src, "", node, name)
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(node, "Normal", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(node, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(node, "AO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # The ground is Nanite; without this the game (not the editor) draws it with the grey default material.
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NANITE)
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", SURFACE_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def _island_ground(name, island, out_dir):
    """An island's own ground material: M_IslandGround with the island's surface map."""
    import riptide_island_shape
    assets = unreal.EditorAssetLibrary
    parent = _ground_material()
    png = os.path.join(out_dir, f"T_{name}_Surface.png")
    riptide_island_shape.write_surface_map(island, png)
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = f"{ISLANDS_PATH}/{name}"
    task.destination_name = f"T_{name}_Surface"
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex_path = f"{ISLANDS_PATH}/{name}/T_{name}_Surface"
    tex = unreal.load_asset(tex_path)
    # Plain numbers, kept exactly as written: no colour curve, no lossy compression.
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    tex.set_editor_property("srgb", False)
    assets.save_asset(tex_path, only_if_is_dirty=False)

    mi_path = f"{ISLANDS_PATH}/{name}/MI_{name}_Ground"
    if assets.does_asset_exist(mi_path):
        assets.delete_asset(mi_path)
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"MI_{name}_Ground", f"{ISLANDS_PATH}/{name}",
                                                                 unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, parent)
    mel.set_material_instance_texture_parameter_value(mi, "Surface", tex)
    mel.set_material_instance_scalar_parameter_value(mi, "SurfaceHalf", float(riptide_island_shape.SURFACE_MAP_HALF))
    mel.update_material_instance(mi)
    assets.save_asset(mi_path, only_if_is_dirty=False)
    return mi


# The cliff's rock, laid on by position from three directions (it has faces every way, undercuts included), so it
# never stretches: dark weathered rock on its faces, pale limestone where it lies flat, darker and green-tinged
# with weed where the sea reaches.
CLIFF_CODE = """
float3 n = normalize(VNormal);
float3 w = pow(abs(n), 4.0);
w /= (w.x + w.y + w.z);
float3 p = WorldPos / 100.0;
#define RT_PLANE(T, uv) T.Sample(Material.Wrap_WorldGroupSettings, (uv))
float2 ux = p.yz / 3.4, uy = p.xz / 3.4, uz = p.xy / 3.4;
float3 rock = RT_PLANE(RockD, ux).rgb * w.x + RT_PLANE(RockD, uy).rgb * w.y + RT_PLANE(RockD, uz).rgb * w.z;
float3 big = RT_PLANE(RockD, ux / 3.7).rgb * w.x + RT_PLANE(RockD, uy / 3.7).rgb * w.y + RT_PLANE(RockD, uz / 3.7).rgb * w.z;
rock = lerp(rock, big, 0.35);
float lum = dot(rock, float3(0.3, 0.59, 0.11));
// Sun-bleached grey-tan limestone: the photo is of redder rock.
rock = lerp(rock, lum.xxx, 0.55) * float3(1.08, 1.04, 0.96);
float3 arm = RT_PLANE(RockA, ux).rgb * w.x + RT_PLANE(RockA, uy).rgb * w.y + RT_PLANE(RockA, uz).rgb * w.z;
arm = float3(1.0, arm.r, 0.0);

// Bumps from the same three directions, straight into world space.
float3 tx = UnpackNormalMap(RT_PLANE(RockN, ux)).xyz;
float3 ty = UnpackNormalMap(RT_PLANE(RockN, uy)).xyz;
float3 tz = UnpackNormalMap(RT_PLANE(RockN, uz)).xyz;
// The photo's normals are OpenGL-style: green is flipped for Unreal.
tx.y = -tx.y; ty.y = -ty.y; tz.y = -tz.y;
tx = float3(tx.xy * 1.3 + n.yz, abs(tx.z) * n.x);
ty = float3(ty.xy * 1.3 + n.xz, abs(ty.z) * n.y);
tz = float3(tz.xy * 1.3 + n.xy, abs(tz.z) * n.z);
float3 bumped = normalize(tx.zxy * w.x + ty.xzy * w.y + tz.xyz * w.z);

// Flat tops are the same pale limestone as the ground behind the cliff.
float flat = smoothstep(0.62, 0.9, n.z);
float3 top = RT_PLANE(CoralD, p.xy / 2.0).rgb;
top = lerp(top, dot(top, float3(0.3, 0.59, 0.11)).xxx, 0.75) * float3(0.6, 0.6, 0.57);
float3 col = lerp(rock, top, flat * 0.85);

// Beds a little lighter and darker than their neighbours, and long stains running down the face.
float bed = frac(sin(floor(p.z / 0.72) * 12.9898) * 43758.5453);
col *= lerp(0.88, 1.08, bed);
col *= lerp(0.82, 1.0, RT_PLANE(RockA, float2(dot(p.xy, float2(0.31, 0.27)), p.z / 9.0)).r);

// Dark in the joints and under the ledges, where light and weather don't reach.
float recess = saturate(UV.x);
col *= lerp(1.0, 0.42, recess * (1.0 - flat));

// Where the sea reaches: a dark wet band at the waterline, weed-green just above it.
float wet = 1.0 - smoothstep(0.05, 0.55, p.z);
float weed = (1.0 - smoothstep(0.3, 0.95, p.z)) * (1.0 - flat);
col = lerp(col, col * float3(0.5, 0.62, 0.36), weed * 0.7);
col *= lerp(1.0, 0.6, wet);
Rough = lerp(arm.g, 0.3, wet * 0.8);
AO = arm.r * lerp(1.0, 0.55, recess);
#undef RT_PLANE
Normal = bumped;
return col;
"""


def _cliff_material():
    """M_IslandCliff: the rock of the sea cliffs (riptide_island_cliff.py), with its normal given in world space."""
    path = f"{MATERIALS_PATH}/M_IslandCliff"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == SURFACE_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    import_ground_textures()
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_IslandCliff", MATERIALS_PATH, unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    mat.set_editor_property("tangent_space_normal", False)
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, -200)
    normal = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -900, -100)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -300)
    sources = [("WorldPos", world), ("VNormal", normal), ("UV", uv)]
    # The cliff-face photo (rock_face_03: colour, OpenGL-style normal, roughness), and the ground's limestone.
    source = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceAssets", "Textures", "rock_face_03")
    face = {}
    for kind in ("diff", "nor_gl", "rough"):
        asset = f"T_rock_face_03_{kind}"
        if not assets.does_asset_exist(f"{TEXTURES_PATH}/{asset}"):
            task = unreal.AssetImportTask()
            task.filename = os.path.join(source, f"rock_face_03_{kind}.jpg")
            task.destination_path = TEXTURES_PATH
            task.destination_name = asset
            task.automated = True
            task.replace_existing = True
            task.save = False
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.load_asset(f"{TEXTURES_PATH}/{asset}")
        if kind == "nor_gl":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif kind == "rough":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        assets.save_asset(f"{TEXTURES_PATH}/{asset}", only_if_is_dirty=False)
        face[kind] = tex
    for i, (name, tex, kind) in enumerate((("RockD", face["diff"], "diff"), ("RockN", face["nor_gl"], "normal"),
                                            ("RockA", face["rough"], "data"),
                                            ("CoralD", unreal.load_asset(f"{TEXTURES_PATH}/T_coral_mud_01_diff"), "diff"))):
        obj = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObject, -1500, i * 120)
        obj.set_editor_property("texture", tex)
        obj.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind == "diff"
                                else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind == "normal"
                                else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        sources.append((name, obj))
    pins = []
    for name, _ in sources:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    outs = []
    for name, kind in (("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                       ("AO", unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        out = unreal.CustomOutput()
        out.set_editor_property("output_name", name)
        out.set_editor_property("output_type", kind)
        outs.append(out)
    node.set_editor_property("inputs", pins)
    node.set_editor_property("additional_outputs", outs)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("code", CLIFF_CODE)
    for name, src in sources:
        mel.connect_material_expressions(src, "", node, name)
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(node, "Normal", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(node, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(node, "AO", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NANITE)
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", SURFACE_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def _island_cliffs(name, island, out_dir, version):
    """An island's sea cliffs, generated (and checked sealed) and imported beside its ground."""
    import importlib
    import riptide_palm_mesh
    import riptide_island_cliff
    importlib.reload(riptide_palm_mesh)
    importlib.reload(riptide_island_cliff)
    assets = unreal.EditorAssetLibrary
    folder = f"{ISLANDS_PATH}/{name}"
    written = riptide_island_cliff.write_cliffs(island, out_dir)
    tasks = []
    for mesh_name, obj, _ in written:
        task = unreal.AssetImportTask()
        task.filename = obj
        task.destination_path = folder
        task.destination_name = mesh_name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    rock = _cliff_material()
    mesh_tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for mesh_name, _, tris in written:
        path = f"{folder}/{mesh_name}"
        mesh = unreal.load_asset(path)
        if not mesh:
            unreal.log_error(f"Riptide: cliff {path} failed to import")
            continue
        materials = mesh.get_editor_property("static_materials")
        for i, slot in enumerate(materials):
            slot.set_editor_property("material_interface", rock)
            materials[i] = slot
        mesh.set_editor_property("static_materials", materials)
        nanite = mesh_tools.get_nanite_settings(mesh)
        nanite.set_editor_property("enabled", True)
        nanite.set_editor_property("fallback_target", unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
        nanite.set_editor_property("fallback_percent_triangles", 1.0)
        mesh_tools.set_nanite_settings(mesh, nanite, True)
        body = mesh.get_editor_property("body_setup")
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        assets.set_metadata_tag(mesh, "RiptideVersion", version)
        assets.save_asset(path, only_if_is_dirty=False)
        unreal.log(f"Riptide: {mesh_name}: {tris} triangles, sealed into the ground")
    return [m for m, _, _ in written]


def _load_island(name):
    import importlib
    import riptide_island_shape
    importlib.reload(riptide_island_shape)
    return getattr(riptide_island_shape, name)()


def island_chunk_paths(name):
    """The asset paths of an island's ground meshes that exist."""
    folder = f"{ISLANDS_PATH}/{name}"
    assets = unreal.EditorAssetLibrary
    if not assets.does_directory_exist(folder):
        return []
    return sorted(p.split(".")[0] for p in assets.list_assets(folder, recursive=False) if "/SM_" in p and "_Cliff_" not in p)


def island_cliff_paths(name):
    """The asset paths of an island's sea cliffs."""
    folder = f"{ISLANDS_PATH}/{name}"
    assets = unreal.EditorAssetLibrary
    if not assets.does_directory_exist(folder):
        return []
    return sorted(p.split(".")[0] for p in assets.list_assets(folder, recursive=False) if "_Cliff_" in p)


def make_island_assets(name):
    """An island's ground meshes, generated and imported when missing or when its version changes. True if rebuilt."""
    import importlib
    import riptide_island_mesh
    importlib.reload(riptide_island_mesh)

    assets = unreal.EditorAssetLibrary
    folder = f"{ISLANDS_PATH}/{name}"
    version = ISLAND_VERSIONS[name]
    existing = island_chunk_paths(name)
    ground_path = f"{MATERIALS_PATH}/M_IslandGround"
    surfaces_current = (assets.does_asset_exist(ground_path)
                        and assets.get_metadata_tag(unreal.load_asset(ground_path), "RiptideVersion") == SURFACE_VERSION)
    if existing and surfaces_current and assets.get_metadata_tag(unreal.load_asset(existing[0]), "RiptideVersion") == version:
        return False
    unreal.log(f"Riptide: building {name}'s ground")
    if assets.does_directory_exist(folder):
        assets.delete_directory(folder)

    island = _load_island(name)
    out_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Generated", "Islands", name)
    written = riptide_island_mesh.write_island(island, out_dir)
    tasks = []
    for mesh_name, obj, _ in written:
        task = unreal.AssetImportTask()
        task.filename = obj
        task.destination_path = folder
        task.destination_name = mesh_name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    ground = _island_ground(name, island, out_dir)
    mesh_tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    made = 0
    for mesh_name, _, _ in written:
        path = f"{folder}/{mesh_name}"
        mesh = unreal.load_asset(path)
        if not mesh:
            unreal.log_error(f"Riptide: island ground {path} failed to import")
            continue
        materials = mesh.get_editor_property("static_materials")
        for i, slot in enumerate(materials):
            slot.set_editor_property("material_interface", ground)
            materials[i] = slot
        mesh.set_editor_property("static_materials", materials)
        # Nanite: 144 squares of ordinary mesh overflow the shadow maps' budget for them (the engine's "Non-Nanite
        # Marking Job Queue overflow" warning). The stand-in mesh the collision is made from keeps every triangle,
        # so the crew still walks on exactly the ground that's drawn.
        nanite = mesh_tools.get_nanite_settings(mesh)
        nanite.set_editor_property("enabled", True)
        nanite.set_editor_property("fallback_target", unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
        nanite.set_editor_property("fallback_percent_triangles", 1.0)
        nanite.set_editor_property("fallback_relative_error", 0.0)
        mesh_tools.set_nanite_settings(mesh, nanite, True)
        # The crew, the boat's hull and everything else stand on the ground's own triangles.
        body = mesh.get_editor_property("body_setup")
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        assets.set_metadata_tag(mesh, "RiptideVersion", version)
        assets.save_asset(path, only_if_is_dirty=False)
        made += 1
        if made % 24 == 0:
            unreal.SystemLibrary.collect_garbage()
    # The importer makes a placeholder material per mesh; ours replaces them.
    cliffs = _island_cliffs(name, island, out_dir, version)
    wanted = {m for m, _, _ in written} | {f"T_{name}_Surface", f"MI_{name}_Ground"} | set(cliffs)
    for leftover in assets.list_assets(folder, recursive=True):
        if leftover.split(".")[0].split("/")[-1] not in wanted:
            assets.delete_asset(leftover.split(".")[0])
    unreal.log(f"Riptide: {name}'s ground ready ({made} squares)")
    return True


def _place_island(name, origin=(0.0, 0.0, 0.0)):
    """Puts an island's ground into the open level, its centre at `origin` (cm), sea level at the origin's height."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for path in island_chunk_paths(name):
        # ARiptideIslandGround: ground the sea knows the depth over (RiptideSea.h).
        actor = actors.spawn_actor_from_class(unreal.RiptideIslandGround, unreal.Vector(*origin))
        actor.static_mesh_component.set_static_mesh(unreal.load_asset(path))
        actor.set_actor_label(path.split("/")[-1])
        actor.set_folder_path(f"Islands/{name}")
        actor.tags = [unreal.Name("RiptideIsland"), unreal.Name(name)]
    # The sea cliffs: part of the ground, standing on it and sealed into it.
    for path in island_cliff_paths(name):
        actor = actors.spawn_actor_from_object(unreal.load_asset(path), unreal.Vector(*origin))
        actor.set_actor_label(path.split("/")[-1])
        actor.set_folder_path(f"Islands/{name}")
        actor.tags = [unreal.Name("RiptideIsland"), unreal.Name(name)]


# What the Water plugin's own ocean factory gives a new ocean, and a plain spawn doesn't: its materials.
OCEAN_MATERIALS = ["water_material", "water_static_mesh_material", "underwater_post_process_material",
                   "water_info_material"]


# Which mesh each kind of prop in an island's design uses: a palm by name, or one of a scanned model's meshes
# (picked by the prop's place in the list, so the same prop always gets the same mesh). Solid things block the crew.
PROP_MESHES = {
    "palm_tall": ("palm", "SM_Palm_Tall", True), "palm_leaning": ("palm", "SM_Palm_Leaning", True),
    "palm_sweeping": ("palm", "SM_Palm_Sweeping", True), "palm_medium": ("palm", "SM_Palm_Medium", True),
    "palm_young": ("palm", "SM_Palm_Young", True),
    "tree": ("model", "island_tree_02", True), "tree_small": ("model", "tree_small_02", True), "tree_big": ("model", "island_tree_01", True),
    "shrub": ("model", "searsia_lucida", False), "sorrel": ("model", "shrub_sorrel_01", False), "lowshrub": ("model", "shrub_04", False),
    "fern": ("model", "fern_02", False), "grass": ("model", "grass_medium_01", False), "shell": ("model", "lambis_shell", False),
    "outcrop": ("model", "coast_rocks_05", True), "boulder": ("model", "boulder_01", True),
    "log": ("model", "dead_tree_trunk_02", True), "branch": ("model", "dry_branches_medium_01", False),
}

# Things that lie on the ground rather than grow from one point: they're laid to the ground's slope, then bedded in
# until no part of their underside is above it. (kind: how much of its footprint must be bedded, the most of its
# height that may be buried.) A piece that can't be bedded within that isn't placed.
LYING = {"outcrop": (0.9, 0.7), "boulder": (0.7, 0.65), "log": (0.9, 0.6), "branch": (0.85, 0.8), "shell": (0.5, 0.35)}


def _bed_into_ground(island, mesh, prop, origin):
    """The transform that lays a lying prop on the ground and beds it in, or None if it can't be bedded."""
    x, y, scale = prop["x"], prop["y"], prop["scale"]
    footprint, most = LYING[prop["kind"]]
    e = 0.75
    # The ground's slope here, as its upward normal in Unreal's axes (X north = the design's y, Y east = its x).
    dx = (island.height(x + e, y) - island.height(x - e, y)) / (2 * e)
    dy = (island.height(x, y + e) - island.height(x, y - e)) / (2 * e)
    up = unreal.Vector(-dy, -dx, 1.0).normal()
    yaw = math.radians(prop["yaw"])
    rotation = unreal.MathLibrary.make_rot_from_zx(up, unreal.Vector(math.cos(yaw), math.sin(yaw), 0.0))
    box = mesh.get_bounding_box()
    height = (box.max.z - box.min.z) * scale / 100.0
    z = island.height(x, y) - prop["sink"]

    def transform(at_z):
        return unreal.Transform(location=unreal.Vector(origin[0] + y * 100.0, origin[1] + x * 100.0, origin[2] + at_z * 100.0),
                                rotation=rotation, scale=unreal.Vector(scale, scale, scale))

    # How far any point low on its sides stands above the ground under it: it's lowered by the worst of them.
    cx, cy = (box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0
    hx, hy = (box.max.x - box.min.x) / 2.0 * footprint, (box.max.y - box.min.y) / 2.0 * footprint
    low = box.min.z + (box.max.z - box.min.z) * 0.18
    placed = transform(z)
    worst = 0.0
    for px, py in ((-1, -1), (1, -1), (-1, 1), (1, 1), (0, -1), (0, 1), (-1, 0), (1, 0), (0, 0)):
        world = unreal.MathLibrary.transform_location(placed, unreal.Vector(cx + px * hx, cy + py * hy, low))
        ground = island.height((world.y - origin[1]) / 100.0, (world.x - origin[0]) / 100.0)
        worst = max(worst, (world.z - origin[2]) / 100.0 - ground)
    # Settled into the sand a little as well, in proportion for small things like shells.
    z -= worst + min(0.03, 0.08 * height)
    if island.height(x, y) - z > most * height:
        return None
    return transform(z)



def _place_props(name, island, origin=(0.0, 0.0, 0.0)):
    """Plants and places everything in an island's design (Island.props) as one ARiptideIslandProps actor."""
    import riptide_island_props
    catalogue = riptide_island_props.model_catalogue()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    holder = actors.spawn_actor_from_class(unreal.RiptideIslandProps, unreal.Vector(*origin))
    holder.set_actor_label(f"{name}_Props")
    holder.set_folder_path(f"Islands/{name}")
    loaded = {}
    counts = {}
    for n, prop in enumerate(island.props()):
        source, which, solid = PROP_MESHES[prop["kind"]]
        if source == "palm":
            path = f"{riptide_island_props.PALMS_PATH}/{which}"
        else:
            entries = catalogue.get(which, [])
            if not entries:
                continue
            # Grass: only the upright tufts, not the dead or flattened scraps.
            if which == "grass_bermuda_01":
                entries = [e for e in entries if "medium" in e["path"] or "seedling" in e["path"] or "small" in e["path"]] or entries
            path = entries[n % len(entries)]["path"]
        mesh = loaded.get(path) or unreal.load_asset(path)
        loaded[path] = mesh
        if not mesh:
            continue
        if prop["kind"] in LYING:
            transform = _bed_into_ground(island, mesh, prop, origin)
            if transform is None:
                counts["(not placed: would float)"] = counts.get("(not placed: would float)", 0) + 1
                continue
            holder.add_prop(mesh, transform, solid)
            counts[prop["kind"]] = counts.get(prop["kind"], 0) + 1
            continue
        x, y = prop["x"], prop["y"]
        z = island.height(x, y) - prop["sink"]
        # Unreal's X is north (the design's y), Y is east (the design's x). The design's yaw is a compass bearing,
        # which is Unreal's yaw as it stands. Scans whose face is their +Y side (the cliff) turn a quarter less.
        yaw = prop["yaw"] - (90.0 if prop["kind"] == "cliff" else 0.0)
        tilt = prop["tilt"]
        rotation = unreal.Rotator(roll=tilt * math.sin(n * 2.4), pitch=tilt * math.cos(n * 2.4), yaw=yaw)
        scale = prop["scale"]
        transform = unreal.Transform(location=unreal.Vector(origin[0] + y * 100.0, origin[1] + x * 100.0, origin[2] + z * 100.0),
                                     rotation=rotation, scale=unreal.Vector(scale, scale, scale))
        holder.add_prop(mesh, transform, solid)
        counts[prop["kind"]] = counts.get(prop["kind"], 0) + 1
    unreal.log(f"Riptide: {name} planted: " + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())))


# --- Marks on the sand -------------------------------------------------------------------------------------------
# Decals laid over the beach from the island's design (Island.decals): drifts of broken shell, lines of dried weed
# and litter the tide left, and damp patches. Each is a soft-edged patch of a photo laid on by world position.
DECAL_CODE = {
    "shells": """
float2 c = UV - 0.5;
float edge = 1.0 - smoothstep(0.35, 1.0, length(c) * 2.0);
float4 pic = Picture.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 300.0);
float4 nrm = Bumps.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 300.0);
float lum = dot(pic.rgb, float3(0.3, 0.59, 0.11));
// Only the shells themselves show, scattered thicker in the middle of the drift.
float grain = Grain.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 1100.0).r;
// The shells, lighter than the sand of their photo, scattered thicker toward the middle of the drift.
Opacity = edge * smoothstep(0.28, 0.5, lum + (grain - 0.5) * 0.35 + edge * 0.1) * 0.95;
Normal = UnpackNormalMap(nrm).xyz;
Rough = 0.5;
return pic.rgb * float3(1.3, 1.26, 1.18);
""",
    "wrack": """
// A line of dried weed and litter the last high tide left: separate pieces strung along the line with sand
// showing between them, not one brown streak.
float2 c = UV - 0.5;
float edge = (1.0 - smoothstep(0.1, 1.0, abs(c.x) * 2.0)) * (1.0 - smoothstep(0.15, 1.0, abs(c.y) * 2.0));
float4 pic = Picture.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 140.0);
float4 nrm = Bumps.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 140.0);
float lum = dot(pic.rgb, float3(0.3, 0.59, 0.11));
float grain = Grain.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 700.0).r;
// Pieces: cells 9 cm across, most of them empty; a piece is an uneven blob set anywhere in its cell, of any size,
// with the photo's dark litter cut out of it, so no two look alike and they never line up.
float2 cell = floor(WorldPos.xy / 9.0);
float pick = frac(sin(dot(cell, float2(127.1, 311.7))) * 43758.5453);
float2 off = float2(frac(sin(dot(cell, float2(269.5, 183.3))) * 43758.5453), frac(sin(dot(cell, float2(419.2, 371.9))) * 43758.5453)) - 0.5;
float size = 0.18 + 0.32 * frac(sin(dot(cell, float2(97.3, 233.7))) * 43758.5453);
float2 at = (frac(WorldPos.xy / 9.0) - 0.5 - off * 0.9) * float2(0.7, 1.0);
float piece = (1.0 - smoothstep(size * 0.5, size, length(at))) * step(0.72, pick + edge * 0.2);
float litter = smoothstep(0.62, 0.28, lum + (grain - 0.5) * 0.3);
Opacity = edge * piece * litter * 0.85;
Normal = UnpackNormalMap(nrm).xyz;
Rough = 0.85;
return pic.rgb * float3(0.62, 0.52, 0.38);
""",
    "damp": """
float2 c = UV - 0.5;
float edge = 1.0 - smoothstep(0.3, 1.0, length(c) * 2.0);
float grain = Grain.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 900.0).r;
float4 pic = Picture.Sample(Material.Wrap_WorldGroupSettings, WorldPos.xy / 180.0);
Opacity = edge * saturate(0.35 + grain * 0.5) * 0.6;
Normal = float3(0.0, 0.0, 1.0);
Rough = 0.3;
return pic.rgb * float3(0.5, 0.47, 0.42);
""",
}
DECAL_PICTURES = {"shells": "shell_floor_01", "wrack": "forrest_sand_01", "damp": "dense_sand"}


def _decal_material(kind):
    """M_SandDecal_<kind>: a mark laid over the sand (see DECAL_CODE)."""
    name = f"M_SandDecal_{kind}"
    path = f"{MATERIALS_PATH}/{name}"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == SURFACE_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    import_ground_textures()
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    node = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -300)
    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, -200)
    sources = [("UV", uv), ("WorldPos", world)]
    picture = DECAL_PICTURES[kind]
    for i, (pin_name, texture, kind_of) in enumerate(((("Picture", f"T_{picture}_diff", "diff")), ("Bumps", f"T_{picture}_nor_dx", "nor"),
                                                      ("Grain", "T_aerial_beach_01_disp", "data"))):
        obj = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObject, -1500, i * 120)
        obj.set_editor_property("texture", unreal.load_asset(f"{TEXTURES_PATH}/{texture}"))
        obj.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if kind_of == "diff"
                                else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if kind_of == "nor"
                                else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        sources.append((pin_name, obj))
    pins = []
    for pin_name, _ in sources:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", pin_name)
        pins.append(pin)
    outs = []
    for out_name, out_kind in (("Opacity", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("Normal", unreal.CustomMaterialOutputType.CMOT_FLOAT3),
                               ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        out = unreal.CustomOutput()
        out.set_editor_property("output_name", out_name)
        out.set_editor_property("output_type", out_kind)
        outs.append(out)
    node.set_editor_property("inputs", pins)
    node.set_editor_property("additional_outputs", outs)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("code", DECAL_CODE[kind])
    for pin_name, src in sources:
        mel.connect_material_expressions(src, "", node, pin_name)
    mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(node, "Opacity", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(node, "Normal", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(node, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", SURFACE_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def _place_decals(name, island, origin=(0.0, 0.0, 0.0)):
    """Lays the island's sand marks (Island.decals) as decal actors, projecting down onto the ground."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    materials = {kind: _decal_material(kind) for kind in DECAL_CODE}
    counts = {}
    for mark in island.decals():
        x, y = mark["x"], mark["y"]
        z = island.height(x, y)
        # Unreal's X is north (the design's y), Y is east (the design's x). Pitched to look straight down, the
        # decal's own Y runs across its yaw and Z along it.
        actor = actors.spawn_actor_from_class(unreal.DecalActor, unreal.Vector(origin[0] + y * 100.0, origin[1] + x * 100.0, origin[2] + z * 100.0 + 60.0),
                                              unreal.Rotator(roll=0.0, pitch=-90.0, yaw=mark["yaw"]))
        along, across = mark["size"]
        decal = actor.get_component_by_class(unreal.DecalComponent)
        decal.set_decal_material(materials[mark["kind"]])
        decal.set_editor_property("decal_size", unreal.Vector(120.0, across * 50.0, along * 50.0))
        actor.set_actor_label(f"{name}_{mark['kind']}")
        actor.set_folder_path(f"Islands/{name}/Marks")
        counts[mark["kind"]] = counts.get(mark["kind"], 0) + 1
    unreal.log(f"Riptide: {name} marked: " + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())))


def _spawn_island_ocean(ns):
    """The ocean for a map with islands: ARiptideOcean, whose waves die away in the shallows for the game as well
    as on screen. The plugin's factory only dresses its own ocean class, so a stock ocean is placed first and its
    materials handed over."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    stock = ns["_spawn_ocean"]()
    stock_body = stock.get_water_body_component()
    ocean = ns["_spawn"](unreal.RiptideOcean)
    body = ocean.get_water_body_component()
    for name in OCEAN_MATERIALS:
        try:
            body.set_editor_property(name, stock_body.get_editor_property(name))
        except Exception as err:  # noqa: BLE001 - report it; the ocean still works without a far or HLOD material
            unreal.log_warning(f"Riptide: island ocean has no {name} ({err})")
    actors.destroy_actor(stock)
    unreal.log(f"Riptide: island ocean {ocean.get_class().get_name()} with {body.get_class().get_name()}, "
               f"water material {body.get_editor_property('water_material')}")
    return ocean


def build_island_test_map(ns, rebuilt):
    """Island_Test: the start cay in the same sea, sky and swell as Ocean_Test, with the boat starting just off the
    wash-up beach, facing it. `ns` is init_unreal's namespace, for the sea and sky it already knows how to build."""
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if assets.does_asset_exist(ISLAND_MAP_PATH):
        if not rebuilt and assets.get_metadata_tag(unreal.load_asset(ISLAND_MAP_PATH), "RiptideVersion") == ISLAND_MAP_VERSION:
            return
        assets.delete_asset(ISLAND_MAP_PATH)

    unreal.log("Riptide: building the island test map")
    levels.new_level(ISLAND_MAP_PATH)
    spawn = ns["_spawn"]

    sun = spawn(unreal.DirectionalLight, (0, 0, 5000), yaw=-40.0, pitch=-35.0)
    sun_light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_light.set_editor_property("atmosphere_sun_light", True)
    sun_light.set_editor_property("intensity", 10.0)
    spawn(unreal.SkyAtmosphere)
    sky_light = spawn(unreal.SkyLight, (0, 0, 1000))
    sky_light.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    ns["_fog_for_light_beams"](spawn(unreal.ExponentialHeightFog))
    spawn(unreal.VolumetricCloud)

    zone = spawn(unreal.WaterZone)
    ocean = _spawn_island_ocean(ns)
    ns["_open_up_sea"](zone, ocean)
    ns["_far_sea"](zone)
    ns["_set_swell"](ocean)
    ns["_cover_waves"](ocean)
    ocean.get_water_body_component().set_editor_property("target_wave_mask_depth", SHALLOWS_WAVE_DEPTH)
    zone.set_editor_property("render_target_resolution", unreal.IntPoint(DEPTH_MAP_PIXELS, DEPTH_MAP_PIXELS))
    zone.set_editor_property("local_tessellation_extent", unreal.Vector(DEPTH_MAP_SPAN, DEPTH_MAP_SPAN, 10000.0))
    zone.set_editor_property("enable_local_only_tessellation", True)
    # Beyond the window that follows the camera the far-distance mesh (_far_sea) carries the sea to the horizon. The
    # ocean's own static mesh is not drawn as well: it lies on the same plane as the tessellated water and fought it
    # for every pixel, which showed from the air as a lattice of pale triangles and stripes.
    ocean.get_water_body_component().set_water_body_static_mesh_enabled(False)

    ambience = spawn(unreal.AmbientSound)
    ambience_audio = ambience.get_component_by_class(unreal.AudioComponent)
    ambience_audio.set_editor_property("sound", unreal.load_asset(f"{ns['AUDIO_PATH']}/S_Ocean_Ambience"))
    ambience_audio.set_editor_property("allow_spatialization", False)

    _place_island("StartCay")
    island = _load_island("StartCay")
    _place_props("StartCay", island)
    _place_decals("StartCay", island)

    # The boat is launched here: off the wash-up beach, bow toward it.
    bx, by = island.beach
    ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
    length = math.hypot(ox, oy)
    ox, oy = ox / length, oy / length
    sx, sy = bx + ox * BOAT_START_OFF_BEACH, by + oy * BOAT_START_OFF_BEACH
    # Unreal's X is north (the design's y), Y is east (the design's x).
    spawn(unreal.PlayerStart, (sy * 100.0, sx * 100.0, 150.0), yaw=math.degrees(math.atan2(-ox, -oy)))

    levels.save_current_level()
    assets.set_metadata_tag(unreal.load_asset(ISLAND_MAP_PATH), "RiptideVersion", ISLAND_MAP_VERSION)
    assets.save_asset(ISLAND_MAP_PATH, only_if_is_dirty=False)
    unreal.log("Riptide: island test map ready")


def build(ns):
    # What stands on the islands first (palms, rocks, plants): the islands are planted with them.
    import importlib
    import riptide_island_props
    importlib.reload(riptide_island_props)
    rebuilt = False
    try:
        rebuilt = riptide_island_props.build(ns)
    except Exception as err:  # noqa: BLE001 - the islands' ground is still worth building without them
        unreal.log_error(f"Riptide: could not build the islands' props: {err}")
    for name in ISLAND_VERSIONS:
        rebuilt |= make_island_assets(name)
    build_island_test_map(ns, rebuilt)
