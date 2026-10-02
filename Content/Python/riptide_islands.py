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

ISLAND_VERSIONS = {"StartCay": "3"}
GREY_VERSION = "2"
ISLAND_MAP_VERSION = "7"

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
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", GREY_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


# --- Surfaces ----------------------------------------------------------------------------------------------------
# The ground's photo surfaces (SourceAssets/Textures, credited in Docs/CREDITS.md) and the material that lays them
# over an island by its surface map. Bump SURFACE_VERSION when the material's recipe changes.
TEXTURES_PATH = f"{ISLANDS_PATH}/Textures"
SURFACE_VERSION = "3"
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
    // Close up, the fine sand photo; further off, the wind-rippled one, so the beach never looks tiled.
    float far = saturate((Dist / 100.0 - 6.0) / 30.0);
    float3 c = RT_TEX(SandD, 1.8).rgb;
    float3 n = UnpackNormalMap(RT_TEX(SandN, 1.8)).xyz;
    float3 arm = RT_TEX(SandA, 1.8).rgb;
    float3 fc = RT_TEX(FarD, 30.0).rgb;
    float3 fn = UnpackNormalMap(RT_TEX(FarN, 30.0)).xyz;
    c = lerp(c, fc, far * 0.85);
    n = normalize(lerp(n, fn, far * 0.7));
    // Broken shell gathers along the high-tide line.
    float tide = zm + (mid - 0.5) * 0.5;
    float shell = smoothstep(1.0, 1.2, tide) * (1.0 - smoothstep(1.45, 1.8, tide)) * smoothstep(0.35, 0.6, RT_TEX(FarH, 3.7).r) * 0.85;
    if (shell > 0.004)
    {
        c = lerp(c, RT_TEX(ShellD, 3.0).rgb, shell);
        n = normalize(lerp(n, UnpackNormalMap(RT_TEX(ShellN, 3.0)).xyz, shell));
        arm = lerp(arm, RT_TEX(ShellA, 3.0).rgb, shell);
    }
    // Pale coral sand.
    col += aSand * RT_TONE(c, 0.45, float3(1.32, 1.3, 1.24)) * SandTint.rgb; nrm += aSand * n; rough += aSand * arm.g; ao += aSand * arm.r;
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
    // Darker, browner soil under the trees.
    col += aGrove * RT_TONE(RT_TEX(GroveD, 2.0).rgb, 0.15, float3(0.66, 0.6, 0.5)); nrm += aGrove * UnpackNormalMap(RT_TEX(GroveN, 2.0)).xyz;
    rough += aGrove * arm.g; ao += aGrove * arm.r;
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
    return sorted(p.split(".")[0] for p in assets.list_assets(folder, recursive=False) if "/SM_" in p)


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
        # Plain meshes for now: the ground is only a few thousand triangles a square.
        nanite = mesh_tools.get_nanite_settings(mesh)
        nanite.set_editor_property("enabled", False)
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
    wanted = {m for m, _, _ in written} | {f"T_{name}_Surface", f"MI_{name}_Ground"}
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


# What the Water plugin's own ocean factory gives a new ocean, and a plain spawn doesn't: its materials.
OCEAN_MATERIALS = ["water_material", "water_static_mesh_material", "underwater_post_process_material",
                   "water_info_material"]


# Which mesh each kind of prop in an island's design uses: a palm by name, or one of a scanned model's meshes
# (picked by the prop's place in the list, so the same prop always gets the same mesh). Solid things block the crew.
PROP_MESHES = {
    "palm_tall": ("palm", "SM_Palm_Tall", True), "palm_leaning": ("palm", "SM_Palm_Leaning", True),
    "palm_sweeping": ("palm", "SM_Palm_Sweeping", True), "palm_medium": ("palm", "SM_Palm_Medium", True),
    "palm_young": ("palm", "SM_Palm_Young", True),
    "tree": ("model", "island_tree_02", True), "shrub": ("model", "searsia_lucida", False), "fern": ("model", "fern_02", False),
    "grass": ("model", "grass_bermuda_01", False), "cliff": ("model", "coastal_cliff_02", True),
    "outcrop": ("model", "coast_rocks_05", True), "boulder": ("model", "boulder_01", True),
    "rubble": ("model", "sand_rocks_small_01", True), "log": ("model", "dead_tree_trunk_02", True),
    "branch": ("model", "dry_branches_medium_01", False),
}


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
                entries = [e for e in entries if "medium" in e["path"] or "seedling" in e["path"]] or entries
            path = entries[n % len(entries)]["path"]
        mesh = loaded.get(path) or unreal.load_asset(path)
        loaded[path] = mesh
        if not mesh:
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
    # Beyond the window that follows the camera, the sea is drawn as plain meshes (as the plugin's factory sets up).
    ocean.get_water_body_component().set_water_body_static_mesh_enabled(True)

    ambience = spawn(unreal.AmbientSound)
    ambience_audio = ambience.get_component_by_class(unreal.AudioComponent)
    ambience_audio.set_editor_property("sound", unreal.load_asset(f"{ns['AUDIO_PATH']}/S_Ocean_Ambience"))
    ambience_audio.set_editor_property("allow_spatialization", False)

    _place_island("StartCay")
    island = _load_island("StartCay")
    _place_props("StartCay", island)

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
