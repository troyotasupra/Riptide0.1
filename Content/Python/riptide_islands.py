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

ISLAND_VERSIONS = {"StartCay": "2"}
GREY_VERSION = "2"
ISLAND_MAP_VERSION = "1"

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
    return sorted(p.split(".")[0] for p in assets.list_assets(folder, recursive=False))


def make_island_assets(name):
    """An island's ground meshes, generated and imported when missing or when its version changes. True if rebuilt."""
    import importlib
    import riptide_island_mesh
    importlib.reload(riptide_island_mesh)

    assets = unreal.EditorAssetLibrary
    folder = f"{ISLANDS_PATH}/{name}"
    version = ISLAND_VERSIONS[name]
    existing = island_chunk_paths(name)
    if existing and assets.get_metadata_tag(unreal.load_asset(existing[0]), "RiptideVersion") == version:
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

    grey = _grey_material()
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
            slot.set_editor_property("material_interface", grey)
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
    wanted = {m for m, _, _ in written}
    for leftover in assets.list_assets(folder, recursive=True):
        if leftover.split(".")[0].split("/")[-1] not in wanted:
            assets.delete_asset(leftover.split(".")[0])
    unreal.log(f"Riptide: {name}'s ground ready ({made} squares)")
    return True


def _place_island(name, origin=(0.0, 0.0, 0.0)):
    """Puts an island's ground into the open level, its centre at `origin` (cm), sea level at the origin's height."""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for path in island_chunk_paths(name):
        actor = actors.spawn_actor_from_object(unreal.load_asset(path), unreal.Vector(*origin))
        actor.set_actor_label(path.split("/")[-1])
        actor.set_folder_path(f"Islands/{name}")
        actor.tags = [unreal.Name("RiptideIsland"), unreal.Name(name)]


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
    ocean = ns["_spawn_ocean"]()
    ns["_open_up_sea"](zone, ocean)
    ns["_far_sea"](zone)
    ns["_set_swell"](ocean)
    ns["_cover_waves"](ocean)
    ns["_full_waves"](ocean)

    ambience = spawn(unreal.AmbientSound)
    ambience_audio = ambience.get_component_by_class(unreal.AudioComponent)
    ambience_audio.set_editor_property("sound", unreal.load_asset(f"{ns['AUDIO_PATH']}/S_Ocean_Ambience"))
    ambience_audio.set_editor_property("allow_spatialization", False)

    _place_island("StartCay")

    # The boat is launched here: off the wash-up beach, bow toward it.
    island = _load_island("StartCay")
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
    rebuilt = False
    for name in ISLAND_VERSIONS:
        rebuilt |= make_island_assets(name)
    build_island_test_map(ns, rebuilt)
