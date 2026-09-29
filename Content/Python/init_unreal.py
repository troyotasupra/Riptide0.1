"""Runs automatically when the editor opens. Imports the game's sounds and builds the ocean test map the first time."""

import os

import unreal

MAP_PATH = "/Game/Riptide/Maps/Ocean_Test"
AUDIO_PATH = "/Game/Riptide/Audio"

# Sounds, imported from SourceAssets/Audio (credited in Docs/CREDITS.md). Each is levelled so that at volume 1
# it plays at its target loudness, measured with Tools/measure_loudness.py:
#   asset name: (source file, loops, measured LUFS, measured peak dBFS, target LUFS)
# Targets are for the loudest moment in play: each engine layer at full volume, the wash at top speed, the hardest
# hull slap. The ocean is a quiet bed under everything. No sound's peak goes above PEAK_CEILING_DBFS.
SOUNDS = {
    "S_Engine_Low": ("engine_low.wav", True, -24.7, -13.4, -24.0),
    "S_Engine_High": ("engine_high.wav", True, -17.9, -7.0, -24.0),
    "S_Hull_Wash": ("hull_wash.ogg", True, -22.1, -13.8, -26.0),
    "S_Ocean_Ambience": ("ocean_waves.mp3", True, -9.3, 0.0, -28.0),
    "S_Hull_Slap_04": ("hull_slap_04.ogg", False, -16.4, -1.0, -22.0),
    "S_Hull_Slap_06": ("hull_slap_06.ogg", False, -15.5, -1.6, -22.0),
    "S_Hull_Slap_08": ("hull_slap_08.ogg", False, -14.5, -0.5, -22.0),
    "S_Hull_Slap_13": ("hull_slap_13.ogg", False, -18.5, -3.1, -22.0),
    "S_Hull_Slap_15": ("hull_slap_15.ogg", False, -17.8, -1.3, -22.0),
}
PEAK_CEILING_DBFS = -8.0


def _sound_volume(measured_lufs, measured_peak, target_lufs):
    """Linear volume that brings a sound to its target loudness without its peak passing the ceiling."""
    gain_db = min(target_lufs - measured_lufs, PEAK_CEILING_DBFS - measured_peak)
    return 10.0 ** (gain_db / 20.0)


def import_sounds():
    source_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceAssets", "Audio")
    tasks = []
    for name, (filename, _loops, _lufs, _peak, _target) in SOUNDS.items():
        if not unreal.EditorAssetLibrary.does_asset_exist(f"{AUDIO_PATH}/{name}"):
            task = unreal.AssetImportTask()
            task.filename = os.path.join(source_dir, filename)
            task.destination_path = AUDIO_PATH
            task.destination_name = name
            task.automated = True
            task.save = False
            tasks.append(task)
    if tasks:
        unreal.log(f"Riptide: importing {len(tasks)} sounds")
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    # Levels are applied every launch, so retuning a target above takes effect without a reimport.
    for name, (_filename, loops, lufs, peak, target) in SOUNDS.items():
        path = f"{AUDIO_PATH}/{name}"
        sound = unreal.load_asset(path)
        if not sound:
            unreal.log_error(f"Riptide: sound {name} failed to import")
            continue
        volume = round(_sound_volume(lufs, peak, target), 4)
        if sound.get_editor_property("looping") != loops or abs(sound.get_editor_property("volume") - volume) > 1e-4:
            sound.set_editor_property("looping", loops)
            sound.set_editor_property("volume", volume)
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def _spawn(actor_class, location=(0.0, 0.0, 0.0), yaw=0.0, pitch=0.0):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return actors.spawn_actor_from_class(
        actor_class,
        unreal.Vector(*location),
        unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw),
    )


# Open water the test map covers, in cm (2 km square). The boat does ~15 kn, so this is a few minutes of driving.
SEA_SIZE = 200000.0

# The ocean treats the inside of its shoreline spline as dry land for an island. There's no island yet,
# so the shoreline is shrunk to a 4 m loop parked in a far corner, leaving the spawn point in open water.
SHORE_CENTRE = (-90000.0, -90000.0)
SHORE_HALF_SIZE = 200.0

# A moderate swell, about 1-1.5 m trough to crest: the engine's default ocean waves (up to 5 m) are storm
# seas for a small skiff. Same long wavelengths as the default, so it rolls rather than chops. Heights in cm.
SWELL = {
    "num_waves": 16,
    "min_wavelength": 521.0,
    "max_wavelength": 6000.0,
    "min_amplitude": 2.0,
    "max_amplitude": 30.0,
    "wind_angle_deg": -30.0,
    # A wave's sideways pinch depends on steepness alone, not height, so steepness is scaled down with the
    # amplitudes (30/80 of the default 0.4 / 0.2). Left at the default, the smaller waves fold over where
    # they cross and render as dark spots.
    "small_wave_steepness": 0.15,
    "large_wave_steepness": 0.075,
}


def _spawn_ocean():
    # Placing through the actor factory gives the ocean its default shoreline spline and mesh,
    # the same as dragging it in from the Place Actors panel.
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    try:
        ocean = actors.spawn_actor_from_object(unreal.WaterBodyOcean.static_class(), unreal.Vector(0, 0, 0))
        if ocean:
            return ocean
    except Exception as err:  # noqa: BLE001 - fall back to a plain spawn
        unreal.log_warning(f"Riptide: ocean factory spawn failed ({err}), using plain spawn")
    return _spawn(unreal.WaterBodyOcean)


def _open_up_sea(zone, ocean):
    zone.set_editor_property("zone_extent", unreal.Vector2D(SEA_SIZE, SEA_SIZE))

    shore = ocean.get_component_by_class(unreal.WaterSplineComponent)
    cx, cy = SHORE_CENTRE
    h = SHORE_HALF_SIZE
    shore.clear_spline_points(False)
    for x, y in ((cx + h, cy - h), (cx + h, cy + h), (cx - h, cy + h), (cx - h, cy - h)):
        shore.add_spline_point(unreal.Vector(x, y, 0.0), unreal.SplineCoordinateSpace.WORLD, False)
    shore.update_spline()

    body = ocean.get_water_body_component()
    body.set_editor_property("collision_extents", unreal.Vector(SEA_SIZE / 2.0, SEA_SIZE / 2.0, 10000.0))
    # Set last: changing the extents rebuilds the ocean mesh, picking up the new shoreline too.
    body.set_editor_property("ocean_extents", unreal.Vector2D(SEA_SIZE, SEA_SIZE))


def _set_swell(ocean):
    # The ocean gets its own waves, saved inside the map, instead of the shared engine wave asset.
    waves = unreal.new_object(unreal.GerstnerWaterWaves, outer=ocean)
    generator = unreal.new_object(unreal.GerstnerWaterWaveGeneratorSimple, outer=waves)
    for name, value in SWELL.items():
        generator.set_editor_property(name, value)
    waves.set_editor_property("gerstner_wave_generator", generator)
    ocean.set_water_waves(waves)
    unreal.log(f"Riptide: swell up to {ocean.get_water_body_component().get_max_wave_height():.0f} cm")


MATERIALS_PATH = "/Game/Riptide/Materials"
FOAM_TEXTURE = "/Water/Textures/Foam/T_WaterFlow_01_Foam_Tiled"


def _sampler_type_for(texture):
    """Texture samplers must match the texture's compression or the material won't compile."""
    compression = texture.get_editor_property("compression_settings")
    return {
        unreal.TextureCompressionSettings.TC_GRAYSCALE: unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE,
        unreal.TextureCompressionSettings.TC_MASKS: unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
        unreal.TextureCompressionSettings.TC_NORMALMAP: unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
        unreal.TextureCompressionSettings.TC_ALPHA: unreal.MaterialSamplerType.SAMPLERTYPE_ALPHA,
    }.get(compression, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)


# Bump when the material recipe below changes, so every machine rebuilds it on its next launch.
WAKE_FOAM_VERSION = "2"

# The hull's push on the Water plugin's wake simulation. Epic's boat force material draws a hull-shaped push
# (from T_BoatForceFoam, turned to the boat's heading) into the simulation's red channel and foam into green,
# scaled by F, which the simulation sets from the hull's speed. For a boat this size that piles the water into
# metre-high hills, so M_WakeForce is a copy of Epic's material with the output scaled by HeightScale (push)
# and FoamScale (foam), and MI_WakeForce sets those two values.
WAKE_FORCE_SOURCE = "/Water/FluidSimulation/Materials/Forces/M_Fluid_Sim_Force_Boat_Component"
WAKE_FORCE_VERSION = "2"
WAKE_FORCE_SETTINGS = {"HeightScale": 0.025, "FoamScale": 1.0}


def make_wake_force_material():
    mel = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    base_path = f"{MATERIALS_PATH}/M_WakeForce"
    if assets.does_asset_exist(base_path) and \
            assets.get_metadata_tag(unreal.load_asset(base_path), "RiptideVersion") != WAKE_FORCE_VERSION:
        assets.delete_asset(f"{MATERIALS_PATH}/MI_WakeForce")
        assets.delete_asset(base_path)
    if not assets.does_asset_exist(base_path):
        unreal.log("Riptide: creating wake force material")
        base = assets.duplicate_asset(WAKE_FORCE_SOURCE, base_path)
        output = mel.get_material_property_input_node(base, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        height = mel.create_material_expression(base, unreal.MaterialExpressionScalarParameter, 400, 300)
        height.set_editor_property("parameter_name", "HeightScale")
        height.set_editor_property("default_value", 1.0)
        foam = mel.create_material_expression(base, unreal.MaterialExpressionScalarParameter, 400, 420)
        foam.set_editor_property("parameter_name", "FoamScale")
        foam.set_editor_property("default_value", 1.0)
        # (HeightScale, FoamScale, 1): the output is RGB with the push in red and foam in green.
        scales_rg = mel.create_material_expression(base, unreal.MaterialExpressionAppendVector, 600, 350)
        one = mel.create_material_expression(base, unreal.MaterialExpressionConstant, 600, 480)
        one.set_editor_property("r", 1.0)
        scales = mel.create_material_expression(base, unreal.MaterialExpressionAppendVector, 700, 400)
        scaled = mel.create_material_expression(base, unreal.MaterialExpressionMultiply, 850, 200)
        mel.connect_material_expressions(height, "", scales_rg, "A")
        mel.connect_material_expressions(foam, "", scales_rg, "B")
        mel.connect_material_expressions(scales_rg, "", scales, "A")
        mel.connect_material_expressions(one, "", scales, "B")
        mel.connect_material_expressions(output, "", scaled, "A")
        mel.connect_material_expressions(scales, "", scaled, "B")
        mel.connect_material_property(scaled, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.recompile_material(base)
        assets.set_metadata_tag(base, "RiptideVersion", WAKE_FORCE_VERSION)
        assets.save_asset(base_path, only_if_is_dirty=False)

    path = f"{MATERIALS_PATH}/MI_WakeForce"
    if not assets.does_asset_exist(path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_WakeForce", MATERIALS_PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = unreal.load_asset(path)
    changed = mi.get_editor_property("parent") != unreal.load_asset(base_path)
    if changed:
        mel.set_material_instance_parent(mi, unreal.load_asset(base_path))
    for name, value in WAKE_FORCE_SETTINGS.items():
        if abs(mel.get_material_instance_scalar_parameter_value(mi, name) - value) > 1e-5:
            mel.set_material_instance_scalar_parameter_value(mi, name, value)
            changed = True
    if changed:
        assets.save_asset(path, only_if_is_dirty=False)


def make_materials():
    """M_WakeFoam: white foam for boat wakes. The foam texture is mapped in world space (so it doesn't stretch
    along a trail), each vertex's alpha fades it out as the foam ages, and it softens toward the trail's edges
    (the mesh's U runs 0 to 1 across the trail)."""
    path = f"{MATERIALS_PATH}/M_WakeFoam"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        existing = unreal.load_asset(path)
        if unreal.EditorAssetLibrary.get_metadata_tag(existing, "RiptideVersion") == WAKE_FOAM_VERSION:
            return
        unreal.EditorAssetLibrary.delete_asset(path)
    unreal.log("Riptide: creating wake foam material")
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_WakeFoam", MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)

    world = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -900, 0)
    xy = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -750, 0)
    xy.set_editor_property("r", True)
    xy.set_editor_property("g", True)
    tile = mel.create_material_expression(mat, unreal.MaterialExpressionDivide, -600, 0)
    tile.set_editor_property("const_b", 400.0)  # one texture tile per 4 m
    texture = unreal.load_asset(FOAM_TEXTURE)
    foam = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -450, 0)
    foam.set_editor_property("texture", texture)
    foam.set_editor_property("sampler_type", _sampler_type_for(texture))
    fade = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -250, 200)
    foam_fade = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 80)
    # Soft sides: sin(pi * u) is 0 at both edges of the trail and 1 down its middle.
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -750, 350)
    across = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -600, 350)
    across.set_editor_property("r", True)
    to_angle = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, 350)
    to_angle.set_editor_property("const_b", 3.14159)
    sides = mel.create_material_expression(mat, unreal.MaterialExpressionSine, -300, 350)
    sides.set_editor_property("period", 6.28318)  # period in the input's units: plain sin(x)
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -30, 150)
    white = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -80, -150)
    white.set_editor_property("constant", unreal.LinearColor(0.92, 0.96, 1.0, 1.0))
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -80, -250)
    rough.set_editor_property("r", 0.8)

    mel.connect_material_expressions(world, "", xy, "")
    mel.connect_material_expressions(xy, "", tile, "A")
    mel.connect_material_expressions(tile, "", foam, "UVs")
    mel.connect_material_expressions(foam, "R", foam_fade, "A")
    mel.connect_material_expressions(fade, "A", foam_fade, "B")
    mel.connect_material_expressions(uv, "", across, "")
    mel.connect_material_expressions(across, "", to_angle, "A")
    mel.connect_material_expressions(to_angle, "", sides, "")
    mel.connect_material_expressions(foam_fade, "", opacity, "A")
    mel.connect_material_expressions(sides, "", opacity, "B")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(white, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.set_metadata_tag(mat, "RiptideVersion", WAKE_FOAM_VERSION)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


def build_ocean_test_map():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        return

    unreal.log("Riptide: building ocean test map")
    levels.new_level(MAP_PATH)

    sun = _spawn(unreal.DirectionalLight, (0, 0, 5000), yaw=-40.0, pitch=-35.0)
    sun_light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_light.set_editor_property("atmosphere_sun_light", True)
    sun_light.set_editor_property("intensity", 10.0)

    _spawn(unreal.SkyAtmosphere)
    sky_light = _spawn(unreal.SkyLight, (0, 0, 1000))
    sky_light.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    _spawn(unreal.ExponentialHeightFog)
    _spawn(unreal.VolumetricCloud)

    zone = _spawn(unreal.WaterZone)
    ocean = _spawn_ocean()
    _open_up_sea(zone, ocean)
    _set_swell(ocean)
    # The wake simulation isn't placed here: each boat creates it at runtime (see ARiptideBoat).

    # The sea all around: plays everywhere at the same level, not from a point.
    ambience = _spawn(unreal.AmbientSound)
    ambience_audio = ambience.get_component_by_class(unreal.AudioComponent)
    ambience_audio.set_editor_property("sound", unreal.load_asset(f"{AUDIO_PATH}/S_Ocean_Ambience"))
    ambience_audio.set_editor_property("allow_spatialization", False)

    # The boat spawns here and drops onto the water.
    _spawn(unreal.PlayerStart, (0, 0, 150))

    levels.save_current_level()
    levels.load_level(MAP_PATH)
    unreal.log("Riptide: ocean test map ready")


try:
    import_sounds()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not import sounds: {err}")

try:
    make_materials()
    make_wake_force_material()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not create materials: {err}")

try:
    build_ocean_test_map()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not build ocean test map: {err}")
