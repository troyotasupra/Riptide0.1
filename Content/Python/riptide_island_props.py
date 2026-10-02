"""The things that stand on Riptide's islands: our own coconut palms (riptide_palm_mesh.py) and the photo-scanned
rocks, trees, shrubs, grass and driftwood (SourceAssets/Models, credited in Docs/CREDITS.md). Imported under
/Game/Riptide/Islands when the editor opens, by riptide_islands.py. Bump PROPS_VERSION when anything here changes.
"""
import json
import os

import unreal

ISLANDS_PATH = "/Game/Riptide/Islands"
MATERIALS_PATH = "/Game/Riptide/Materials"
PALMS_PATH = f"{ISLANDS_PATH}/Palms"
MODELS_PATH = f"{ISLANDS_PATH}/Models"
REVIEW_MAP_PATH = "/Game/Riptide/Maps/Props_Review"
PROPS_VERSION = "3"

# The scanned models: name -> how its meshes are treated. "rock": solid, drawn with Nanite. "plant": leaves cut out
# by their alpha picture, lit from both sides. "wood": solid, plain.
MODELS = {
    "coastal_cliff_02": "rock", "coast_rocks_05": "rock", "boulder_01": "rock", "sand_rocks_small_01": "rock",
    "island_tree_02": "plant", "searsia_lucida": "plant", "fern_02": "plant", "grass_bermuda_01": "plant",
    "dead_tree_trunk_02": "wood", "dry_branches_medium_01": "wood",
}


def _project_path(*parts):
    return os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), *parts)


def _saved_path(*parts):
    return os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), *parts)


def _import_files(files, dest):
    tasks = []
    for filename, name in files:
        task = unreal.AssetImportTask()
        task.filename = filename
        task.destination_path = dest
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)


def _set_texture_kind(path, kind):
    """Colour stays sRGB; normals, roughness and cut-out pictures are plain data."""
    tex = unreal.load_asset(path)
    if not tex:
        unreal.log_error(f"Riptide: texture {path} failed to import")
        return None
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
    elif kind != "colour":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return tex


# --- Materials ---------------------------------------------------------------------------------------------------

def _versioned(path):
    """The asset at path if it's the current version, else None (an out-of-date one is deleted)."""
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == PROPS_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    return None


def _finish(mat, path):
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.set_metadata_tag(mat, "RiptideVersion", PROPS_VERSION)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mat


def _photo_material(name, foliage):
    """M_IslandProp / M_IslandFoliage: a photo-scanned surface from its Diffuse, Normal (OpenGL-style, as Poly Haven
    supplies: green is flipped here) and Rough pictures, tinted by Tint. The foliage one is cut out by its Alpha
    picture, drawn from both sides, and lets light through its leaves."""
    path = f"{MATERIALS_PATH}/{name}"
    existing = _versioned(path)
    if existing:
        return existing
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())

    def sample(param, y, sampler):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
        node.set_editor_property("parameter_name", param)
        node.set_editor_property("sampler_type", sampler)
        return node

    diffuse = sample("Diffuse", 0, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal = sample("Normal", 300, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    normal.set_editor_property("texture", unreal.load_asset("/Engine/EngineMaterials/DefaultNormal"))
    rough = sample("Rough", 600, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    rough.set_editor_property("texture", unreal.load_asset("/Engine/EngineMaterials/DefaultWhiteGrid"))
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, -200)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, -100)
    mel.connect_material_expressions(diffuse, "RGB", colour, "A")
    mel.connect_material_expressions(tint, "", colour, "B")
    flip = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, 450)
    flip.set_editor_property("constant", unreal.LinearColor(1.0, -1.0, 1.0, 1.0))
    bumps = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 350)
    mel.connect_material_expressions(normal, "RGB", bumps, "A")
    mel.connect_material_expressions(flip, "", bumps, "B")
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(bumps, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    # Scans of cliffs and outcrops are open shells, seen from behind as well as in front.
    mat.set_editor_property("two_sided", True)
    if foliage:
        alpha = sample("Alpha", 900, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        alpha.set_editor_property("texture", unreal.load_asset("/Engine/EngineMaterials/DefaultWhiteGrid"))
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        mat.set_editor_property("opacity_mask_clip_value", 0.4)
        through = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 100)
        mel.connect_material_expressions(colour, "", through, "A")
        half = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -700, 150)
        half.set_editor_property("constant", unreal.LinearColor(0.55, 0.6, 0.3, 1.0))
        mel.connect_material_expressions(half, "", through, "B")
        mel.connect_material_property(alpha, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
        mel.connect_material_property(through, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    return _finish(mat, path)


def _instance(name, folder, parent, textures, tint=None):
    assets = unreal.EditorAssetLibrary
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.MaterialInstanceConstant,
                                                                 unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, parent)
    for param, tex in textures.items():
        if tex:
            mel.set_material_instance_texture_parameter_value(mi, param, tex)
    if tint:
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    mel.update_material_instance(mi)
    assets.save_asset(path, only_if_is_dirty=False)
    return mi


# The palm's leaves, drawn from nothing but each leaflet's own shape (see riptide_palm_mesh.py for its UVs): four
# shades of green, a paler midrib, darker toward the base, yellowing at the tip, glossy, with light coming through.
FROND_CODE = """
float shade = floor(UV.x);
float across = frac(UV.x);
float along = saturate(UV.y);
float3 greens[4] = { float3(0.022, 0.066, 0.012), float3(0.030, 0.082, 0.015), float3(0.040, 0.092, 0.018), float3(0.055, 0.100, 0.024) };
float3 col = greens[(int)clamp(shade, 0.0, 3.0)];
col *= lerp(0.72, 1.12, along);
col = lerp(col, float3(0.13, 0.13, 0.035), smoothstep(0.86, 1.0, along) * 0.55);
float rib = 1.0 - smoothstep(0.0, 0.11, abs(across - 0.5));
col = lerp(col, float3(0.10, 0.13, 0.04), rib * 0.6);
Through = col * float3(0.9, 1.25, 0.4);
return col;
"""

# Leaflets stir in the breeze: the further along the leaflet, the more it moves.
FROND_SWAY_CODE = """
float along = saturate(UV.y);
float t = Time * 1.7;
float3 p = WorldPos / 100.0;
float a = sin(t + p.x * 0.9 + p.y * 0.7) + 0.5 * sin(t * 2.3 + p.y * 1.7 + p.z);
float b = sin(t * 0.8 + p.x * 0.35 - p.y * 0.4);
return float3(b * 1.2, a * 0.9, a * 1.6) * along * along * 2.4 + float3(b, b * 0.6, 0.0) * 1.5;
"""


def _palm_materials(bark):
    """The palm's own materials, by slot name."""
    mel = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    photo = _photo_material("M_IslandProp", False)
    made = {
        # Weathered grey, as coconut trunks are; the crown's leaf bases browner and rougher.
        "PalmTrunk": _instance("MI_PalmTrunk", PALMS_PATH, photo, bark, tint=(0.5, 0.48, 0.44)),
        "PalmCrown": _instance("MI_PalmCrown", PALMS_PATH, photo, bark, tint=(0.4, 0.3, 0.18)),
    }

    def plain(name, colour, roughness, two_sided=False):
        path = f"{MATERIALS_PATH}/{name}"
        existing = _versioned(path)
        if existing:
            return existing
        mat = tools.create_asset(name, MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
        col = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
        col.set_editor_property("constant", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
        rough.set_editor_property("r", roughness)
        mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mat.set_editor_property("two_sided", two_sided)
        return _finish(mat, path)

    made["PalmStem"] = plain("M_PalmStem", (0.1, 0.125, 0.035), 0.55)
    made["PalmFrondDry"] = plain("M_PalmFrondDry", (0.2, 0.135, 0.065), 0.85, two_sided=True)
    made["Coconut"] = plain("M_Coconut", (0.09, 0.12, 0.035), 0.5)

    path = f"{MATERIALS_PATH}/M_PalmFrond"
    frond = _versioned(path)
    if not frond:
        frond = tools.create_asset("M_PalmFrond", MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
        frond.set_editor_property("two_sided", True)
        frond.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        uv = mel.create_material_expression(frond, unreal.MaterialExpressionTextureCoordinate, -900, 0)
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", "UV")
        through = unreal.CustomOutput()
        through.set_editor_property("output_name", "Through")
        through.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        leaf = mel.create_material_expression(frond, unreal.MaterialExpressionCustom, -500, 0)
        leaf.set_editor_property("inputs", [pin])
        leaf.set_editor_property("additional_outputs", [through])
        leaf.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        leaf.set_editor_property("code", FROND_CODE)
        mel.connect_material_expressions(uv, "", leaf, "UV")
        rough = mel.create_material_expression(frond, unreal.MaterialExpressionConstant, -500, 300)
        rough.set_editor_property("r", 0.38)
        world = mel.create_material_expression(frond, unreal.MaterialExpressionWorldPosition, -900, 500)
        time = mel.create_material_expression(frond, unreal.MaterialExpressionTime, -900, 600)
        pins = []
        for input_name in ("UV", "WorldPos", "Time"):
            p = unreal.CustomInput()
            p.set_editor_property("input_name", input_name)
            pins.append(p)
        sway = mel.create_material_expression(frond, unreal.MaterialExpressionCustom, -500, 500)
        sway.set_editor_property("inputs", pins)
        sway.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        sway.set_editor_property("code", FROND_SWAY_CODE)
        mel.connect_material_expressions(uv, "", sway, "UV")
        mel.connect_material_expressions(world, "", sway, "WorldPos")
        mel.connect_material_expressions(time, "", sway, "Time")
        mel.connect_material_property(leaf, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.connect_material_property(leaf, "Through", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(sway, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        _finish(frond, path)
    made["PalmFrond"] = frond
    return made


# --- Meshes ------------------------------------------------------------------------------------------------------

def _dress(mesh, path, materials, nanite, solid):
    """A mesh's slots given their materials, Nanite on or off, and (for solid things) its own triangles to stand on."""
    slots = mesh.get_editor_property("static_materials")
    for i, slot in enumerate(slots):
        name = str(slot.get_editor_property("material_slot_name"))
        mat = materials(name) if callable(materials) else materials.get(name)
        if mat:
            slot.set_editor_property("material_interface", mat)
            slots[i] = slot
        else:
            unreal.log_warning(f"Riptide: {path.split('/')[-1]} slot '{name}' has no material")
    mesh.set_editor_property("static_materials", slots)
    tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    settings = tools.get_nanite_settings(mesh)
    settings.set_editor_property("enabled", nanite)
    tools.set_nanite_settings(mesh, settings, True)
    if solid:
        body = mesh.get_editor_property("body_setup")
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    unreal.EditorAssetLibrary.set_metadata_tag(mesh, "RiptideVersion", PROPS_VERSION)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def _folder_current(folder, marker):
    assets = unreal.EditorAssetLibrary
    path = f"{folder}/{marker}"
    return assets.does_asset_exist(path) and assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == PROPS_VERSION


def make_palm_assets():
    """Our coconut palms, generated and imported when missing or out of date. True if rebuilt."""
    import importlib
    import riptide_palm_mesh
    importlib.reload(riptide_palm_mesh)
    assets = unreal.EditorAssetLibrary
    if _folder_current(PALMS_PATH, next(iter(riptide_palm_mesh.PALMS))):
        return False
    unreal.log("Riptide: growing the palms")
    if assets.does_directory_exist(PALMS_PATH):
        assets.delete_directory(PALMS_PATH)

    source = _project_path("SourceAssets", "Textures", "palm_tree_bark")
    kinds = {"diff": "colour", "nor_gl": "normal", "rough": "data"}
    _import_files([(os.path.join(source, f"palm_tree_bark_{k}.jpg"), f"T_palm_tree_bark_{k}") for k in kinds], PALMS_PATH)
    bark = {"Diffuse": _set_texture_kind(f"{PALMS_PATH}/T_palm_tree_bark_diff", "colour"),
            "Normal": _set_texture_kind(f"{PALMS_PATH}/T_palm_tree_bark_nor_gl", "normal"),
            "Rough": _set_texture_kind(f"{PALMS_PATH}/T_palm_tree_bark_rough", "data")}
    materials = _palm_materials(bark)

    written = riptide_palm_mesh.write_palms(_saved_path("Generated", "Palms"))
    _import_files([(obj, name) for name, obj, _ in written], PALMS_PATH)
    keep = {name for name, _, _ in written} | {"T_palm_tree_bark_diff", "T_palm_tree_bark_nor_gl", "T_palm_tree_bark_rough",
                                              "MI_PalmTrunk", "MI_PalmCrown"}
    for name, _, tris in written:
        path = f"{PALMS_PATH}/{name}"
        mesh = unreal.load_asset(path)
        if not mesh:
            unreal.log_error(f"Riptide: palm {name} failed to import")
            continue
        # Not Nanite: it can't draw leaves this thin from both sides. The trunk is what blocks the crew.
        _dress(mesh, path, materials, nanite=False, solid=True)
        unreal.log(f"Riptide: {name}: {tris} triangles")
    for leftover in assets.list_assets(PALMS_PATH, recursive=True):
        if leftover.split(".")[0].split("/")[-1] not in keep:
            assets.delete_asset(leftover.split(".")[0])
    return True


def _import_fbx_meshes(filename, dest):
    """Imports an FBX's meshes with Interchange, each object as its own static mesh, with none of the file's own
    materials or textures (ours are assigned after)."""
    p = unreal.InterchangeGenericAssetsPipeline()
    materials = p.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_static_meshes", True)
    meshes.set_editor_property("import_skeletal_meshes", False)
    meshes.set_editor_property("combine_static_meshes", False)
    p.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    params.override_pipelines.append(unreal.SoftObjectPath(p.get_path_name()))
    source = unreal.InterchangeManager.create_source_data(filename)
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(dest, source, params)


def make_model_assets():
    """The scanned rocks, plants and driftwood, imported when missing or out of date. True if rebuilt."""
    assets = unreal.EditorAssetLibrary
    marker = f"{MODELS_PATH}/Catalogue"
    catalogue_file = _saved_path("Generated", "island_models.json")
    if _versioned_folder_marker() and os.path.exists(catalogue_file):
        return False
    unreal.log("Riptide: importing the islands' rocks and plants")
    if assets.does_directory_exist(MODELS_PATH):
        assets.delete_directory(MODELS_PATH)
    solid_parent = _photo_material("M_IslandProp", False)
    leaf_parent = _photo_material("M_IslandFoliage", True)
    catalogue = {}
    for model, kind in MODELS.items():
        folder = f"{MODELS_PATH}/{model}"
        source = _project_path("SourceAssets", "Models", model)
        files = sorted(os.listdir(os.path.join(source, "textures")))
        _import_files([(os.path.join(source, "textures", f), "T_" + os.path.splitext(f)[0].replace("_2k", "")) for f in files], folder)
        textures = {}
        for f in files:
            stem = os.path.splitext(f)[0].replace("_2k", "")
            which = "normal" if "_nor_" in stem else "colour" if stem.endswith("_diff") else "data"
            textures[stem] = _set_texture_kind(f"{folder}/T_{stem}", which)

        def material_for(slot, model=model, kind=kind, textures=textures, folder=folder):
            # A slot's pictures are the model's own, or a named part's (island_tree_02_leaves, ..._branches).
            part = next((p for p in ("leaves", "branches") if p in slot.lower() and f"{model}_{p}_diff" in textures), None)
            stem = f"{model}_{part}" if part else model
            alpha = textures.get(f"{stem}_alpha")
            pictures = {"Diffuse": textures.get(f"{stem}_diff"), "Normal": textures.get(f"{stem}_nor_gl"),
                        "Rough": textures.get(f"{stem}_rough"), "Alpha": alpha}
            parent = leaf_parent if (alpha or (kind == "plant" and part != "branches" and model == "grass_bermuda_01")) else solid_parent
            if parent == leaf_parent and not alpha:
                pictures.pop("Alpha")
            name = f"MI_{stem}" + ("" if parent == solid_parent or alpha else "_blades")
            path = f"{folder}/{name}"
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                return unreal.load_asset(path)
            return _instance(name, folder, parent, pictures)

        before = set(assets.list_assets(folder, recursive=True))
        _import_fbx_meshes(os.path.join(source, model + ".fbx"), folder)
        unreal.SystemLibrary.collect_garbage()
        entries = []
        for path in sorted(set(assets.list_assets(folder, recursive=True)) - before):
            obj = unreal.load_asset(path.split(".")[0])
            if not isinstance(obj, unreal.StaticMesh):
                continue
            if path.split(".")[0][-5:] in ("_LOD1", "_LOD2", "_LOD3"):
                assets.delete_asset(path.split(".")[0])
                continue
            slots = [str(s.get_editor_property("material_slot_name")) for s in obj.get_editor_property("static_materials")]
            _dress(obj, path.split(".")[0], material_for, nanite=(kind == "rock"), solid=(kind != "plant" or model == "island_tree_02"))
            box = obj.get_bounding_box()
            entry = {"path": path.split(".")[0], "kind": kind, "slots": slots,
                     "min": [box.min.x, box.min.y, box.min.z], "max": [box.max.x, box.max.y, box.max.z],
                     "triangles": obj.get_num_triangles(0)}
            entries.append(entry)
            unreal.log(f"Riptide: model {entry['path'].split('/')[-1]}: {entry['triangles']} triangles, "
                       f"{(box.max.x - box.min.x) / 100:.1f} x {(box.max.y - box.min.y) / 100:.1f} x {(box.max.z - box.min.z) / 100:.1f} m, "
                       f"lowest point {box.min.z / 100:.2f} m, slots {slots}")
        catalogue[model] = entries
    os.makedirs(os.path.dirname(catalogue_file), exist_ok=True)
    with open(catalogue_file, "w") as f:
        json.dump(catalogue, f, indent=1)
    _versioned_folder_marker(write=True)
    return True


def _versioned_folder_marker(write=False):
    """The models' version is kept on the shared prop material (every model uses it)."""
    path = f"{MATERIALS_PATH}/M_IslandFoliage"
    assets = unreal.EditorAssetLibrary
    if write:
        return True
    return assets.does_asset_exist(path) and assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == PROPS_VERSION \
        and assets.does_directory_exist(MODELS_PATH)


def model_catalogue():
    """{model: [{path, kind, slots, min, max, triangles}]} for the imported scans."""
    with open(_saved_path("Generated", "island_models.json")) as f:
        return json.load(f)


# --- The review map ------------------------------------------------------------------------------------------------

def build_review_map(ns, rebuilt):
    """Props_Review: every palm and scanned model stood in rows on a plain floor, 15 m apart, for looking at up close
    (Tools/prop_shots.py). Where each one stands is written to Saved/Generated/props_review.json."""
    import riptide_palm_mesh
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    layout_file = _saved_path("Generated", "props_review.json")
    if assets.does_asset_exist(REVIEW_MAP_PATH):
        if not rebuilt and os.path.exists(layout_file):
            return
        assets.delete_asset(REVIEW_MAP_PATH)
    unreal.log("Riptide: building the props review map")
    levels.new_level(REVIEW_MAP_PATH)
    spawn = ns["_spawn"]
    sun = spawn(unreal.DirectionalLight, (0, 0, 5000), yaw=-40.0, pitch=-42.0)
    sun_light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_light.set_editor_property("atmosphere_sun_light", True)
    sun_light.set_editor_property("intensity", 10.0)
    spawn(unreal.SkyAtmosphere)
    sky_light = spawn(unreal.SkyLight, (0, 0, 1000))
    sky_light.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    spawn(unreal.ExponentialHeightFog)

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    floor = actors.spawn_actor_from_object(unreal.load_asset("/Engine/BasicShapes/Plane"), unreal.Vector(0, 0, 0))
    floor.set_actor_scale3d(unreal.Vector(400.0, 400.0, 1.0))
    grey = unreal.load_asset(f"{MATERIALS_PATH}/M_IslandGrey")
    if grey:
        floor.static_mesh_component.set_material(0, grey)

    layout = []
    rows = [[f"{PALMS_PATH}/{name}" for name in riptide_palm_mesh.PALMS]]
    catalogue = model_catalogue()
    for model in MODELS:
        rows.append([e["path"] for e in catalogue.get(model, [])])
    y = 0.0
    for row in rows:
        x = 0.0
        deepest = 0.0
        for path in row:
            mesh = unreal.load_asset(path)
            if not mesh:
                continue
            box = mesh.get_bounding_box()
            width, depth = box.max.x - box.min.x, box.max.y - box.min.y
            # Stood on the floor, centred on its spot.
            at = unreal.Vector(x + width / 2.0 - (box.min.x + box.max.x) / 2.0, y - (box.min.y + box.max.y) / 2.0, -min(box.min.z, 0.0))
            actor = actors.spawn_actor_from_object(mesh, at)
            actor.set_actor_label(path.split("/")[-1])
            layout.append({"name": path.split("/")[-1], "path": path, "centre": [x + width / 2.0, y, (box.max.z - box.min.z) / 2.0],
                           "size": [width, depth, box.max.z - box.min.z]})
            x += width + 1500.0
            deepest = max(deepest, depth)
        y += deepest + 2500.0
    spawn(unreal.PlayerStart, (-1000.0, -1000.0, 300.0))
    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)
    levels.save_current_level()
    with open(layout_file, "w") as f:
        json.dump(layout, f, indent=1)
    unreal.log(f"Riptide: props review map ready ({len(layout)} things)")


def build(ns):
    rebuilt = make_palm_assets()
    rebuilt |= make_model_assets()
    build_review_map(ns, rebuilt)
    return rebuilt
