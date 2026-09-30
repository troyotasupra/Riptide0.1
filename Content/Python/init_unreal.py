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


# How far the ocean's collision reaches above its tallest wave. The Water plugin only counts a boat as in the sea
# while it overlaps that collision, which otherwise stops at flat sea level: every swell that lifted the hull above
# it dropped the buoyancy, drag and props for a moment (the "prop out of water" flicker).
OCEAN_COLLISION_ABOVE_WAVES = 300.0


def _cover_waves(ocean):
    """Raises the ocean's collision over the top of its waves. True if it changed."""
    body = ocean.get_water_body_component()
    needed = body.get_max_wave_height() + OCEAN_COLLISION_ABOVE_WAVES
    if body.get_editor_property("collision_height_offset") >= needed - 1.0:
        return False
    body.set_editor_property("collision_height_offset", needed)
    unreal.log(f"Riptide: ocean collision now reaches {needed:.0f} cm above sea level")
    return True


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
    linear = not texture.get_editor_property("srgb")
    if compression == unreal.TextureCompressionSettings.TC_GRAYSCALE and linear:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE
    if compression == unreal.TextureCompressionSettings.TC_DEFAULT and linear:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
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


SPRAY_VERSION = "3"


def make_spray_material():
    """M_Spray: a puff of thrown spray (URiptideSprayComponent draws each droplet cloud as a camera-facing quad). Soft and
    round, broken up by the foam texture (each puff samples a different patch, from its vertex colour's red and green),
    faded by its vertex alpha as it ages, and softened where it meets the water or the hull."""
    path = f"{MATERIALS_PATH}/M_Spray"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        existing = unreal.load_asset(path)
        if unreal.EditorAssetLibrary.get_metadata_tag(existing, "RiptideVersion") == SPRAY_VERSION:
            return
        unreal.EditorAssetLibrary.delete_asset(path)
    unreal.log("Riptide: creating spray material")
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_Spray", MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_DIRECTIONAL)

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    centre = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -1000, 120)
    centre.set_editor_property("r", 0.5)
    centre.set_editor_property("g", 0.5)
    dist = mel.create_material_expression(mat, unreal.MaterialExpressionDistance, -850, 50)
    dist2 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -700, 50)
    dist2.set_editor_property("const_b", 2.0)
    inv = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -580, 50)
    sat = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -460, 50)
    soft = mel.create_material_expression(mat, unreal.MaterialExpressionPower, -340, 50)
    soft.set_editor_property("const_exponent", 2.2)   # a denser core, so droplets read as water, not smoke

    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -1000, 300)
    rg = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -850, 300)
    rg.set_editor_property("r", True)
    rg.set_editor_property("g", True)
    scale_uv = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -850, 180)
    scale_uv.set_editor_property("const_b", 0.9)
    noise_uv = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -700, 220)
    texture = unreal.load_asset(FOAM_TEXTURE)
    noise = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -560, 220)
    noise.set_editor_property("texture", texture)
    noise.set_editor_property("sampler_type", _sampler_type_for(texture))
    breakup = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -400, 220)
    breakup.set_editor_property("const_a", 0.35)
    breakup.set_editor_property("const_b", 1.0)

    shape = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -220, 120)
    faded = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 200)
    depth = mel.create_material_expression(mat, unreal.MaterialExpressionDepthFade, 40, 200)
    depth.set_editor_property("fade_distance_default", 10.0)   # just enough to hide the seam where it meets the water
    white = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 40, -120)
    white.set_editor_property("constant", unreal.LinearColor(0.9, 0.94, 0.97, 1.0))
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 40, -20)
    glow.set_editor_property("constant", unreal.LinearColor(0.22, 0.24, 0.26, 1.0))   # sunlit white even on its shaded side

    mel.connect_material_expressions(uv, "", dist, "A")
    mel.connect_material_expressions(centre, "", dist, "B")
    mel.connect_material_expressions(dist, "", dist2, "A")
    mel.connect_material_expressions(dist2, "", inv, "")
    mel.connect_material_expressions(inv, "", sat, "")
    mel.connect_material_expressions(sat, "", soft, "Base")
    mel.connect_material_expressions(uv, "", scale_uv, "A")
    mel.connect_material_expressions(vc, "", rg, "")
    mel.connect_material_expressions(scale_uv, "", noise_uv, "A")
    mel.connect_material_expressions(rg, "", noise_uv, "B")
    mel.connect_material_expressions(noise_uv, "", noise, "UVs")
    mel.connect_material_expressions(noise, "R", breakup, "Alpha")
    mel.connect_material_expressions(soft, "", shape, "A")
    mel.connect_material_expressions(breakup, "", shape, "B")
    mel.connect_material_expressions(shape, "", faded, "A")
    mel.connect_material_expressions(vc, "A", faded, "B")
    mel.connect_material_expressions(faded, "", depth, "")  # its first input is the opacity to fade
    mel.connect_material_property(depth, "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(white, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.set_metadata_tag(mat, "RiptideVersion", SPRAY_VERSION)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


# The boat model is generated by riptide_boat_mesh.py (our own geometry, no third-party model) and imported here.
BOATS_PATH = "/Game/Riptide/Boats"
BOAT_MODEL_VERSION = "24"
# Material slot -> (base colour, metallic, roughness). "Glass" gets its own see-through material.
BOAT_FINISHES = {
    "Aluminium": ((0.55, 0.57, 0.6), 1.0, 0.38),
    "HullInside": ((0.42, 0.44, 0.46), 1.0, 0.5),
    "Collar": ((0.018, 0.018, 0.02), 0.0, 0.75),
    "Deck": ((0.06, 0.065, 0.07), 0.0, 0.9),
    "Console": ((0.14, 0.15, 0.16), 0.2, 0.5),
    "Cushion": ((0.03, 0.03, 0.035), 0.0, 0.6),
    "Frame": ((0.6, 0.62, 0.65), 1.0, 0.3),
    "Canopy": ((0.05, 0.055, 0.06), 0.0, 0.7),
    "Trim": ((0.03, 0.03, 0.03), 0.3, 0.45),
    "Cowling": ((0.02, 0.02, 0.022), 0.0, 0.22),
    "Prop": ((0.5, 0.5, 0.5), 1.0, 0.3),
    "Safety": ((0.9, 0.18, 0.015), 0.0, 0.6),      # life ring orange
    "Red": ((0.55, 0.015, 0.012), 0.0, 0.3),       # fire extinguisher
    "White": ((0.75, 0.76, 0.74), 0.0, 0.35),      # fibreglass whips, radomes, radar
    "Lamp": ((0.85, 0.87, 0.9), 0.0, 0.05),        # lamp and display glass
}
# How each finish is worn (M_BoatDetail's settings), 0 to 1:
#   NonSkid: the deck's raised speckle, Brushed: metal grain, Grime: dirt and wear, Bottom: antifouling paint
#   below the waterline (hull outside only).
BOAT_DETAIL = {
    "Aluminium": {"Brushed": 0.35, "Grime": 0.35, "Bottom": 1.0},
    "HullInside": {"Brushed": 0.4, "Grime": 0.5},
    "Deck": {"NonSkid": 1.0, "Grime": 0.6},
    "Frame": {"Brushed": 1.0, "Grime": 0.15},
    "Collar": {"Grime": 0.4, "NonSkid": 0.25},
    "Console": {"Grime": 0.35},
    "Canopy": {"Grime": 0.5},
    "Cushion": {"NonSkid": 0.35, "Grime": 0.3},
    "Trim": {"Grime": 0.3},
    "Cowling": {"Grime": 0.15},
    "Prop": {"Brushed": 0.6, "Grime": 0.4},
    "Safety": {"Grime": 0.4},
    "Red": {"Grime": 0.2},
    "White": {"Grime": 0.35},
}
# Antifouling below the waterline (the hull's own frame, cm), in the dark grey the Metal Shark boats carry.
BOTTOM_PAINT = ((0.035, 0.037, 0.04), -13.0)

# Lights: slot -> (colour, glow). The navigation lights are lit.
BOAT_LIGHTS = {
    "NavRed": ((1.0, 0.04, 0.02), 8.0),
    "NavGreen": ((0.04, 1.0, 0.15), 8.0),
    "NavWhite": ((1.0, 0.97, 0.9), 8.0),
    "LampOn": ((1.0, 0.97, 0.9), 80.0),     # the searchlight's lens while it's lit (ARiptideBoat swaps it in)
}


BOAT_DETAIL_VERSION = "4"
NOISE_FINE = "/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq"
NOISE_SOFT = "/Engine/Functions/Engine_MaterialFunctions02/ExampleContent/Textures/LowResBlurredNoise"


def _boat_detail_material():
    """M_BoatDetail: a painted, bare or rubber surface with wear. On top of BaseColor / Metallic / Roughness:
    a non-skid speckle (colour, roughness and bumps from fine noise), brushed metal grain (noise stretched into
    streaks), grime (soft blotches darkening and dulling it), and antifouling paint below the hull's waterline
    (in the mesh's own frame). The mesh's UVs are box-projected at one unit per metre."""
    path = f"{MATERIALS_PATH}/M_BoatDetail"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == BOAT_DETAIL_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_BoatDetail", MATERIALS_PATH, unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    x = [-1800]

    def node(cls, **props):
        e = mel.create_material_expression(mat, cls, x[0], 0)
        x[0] += 40
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def param(name, default):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default)

    def vparam(name, colour):
        return node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*colour, 1.0))

    def op(cls, a, b=None, **props):
        e = node(cls, **props)
        mel.connect_material_expressions(a, "", e, "A")
        if b is not None:
            mel.connect_material_expressions(b, "", e, "B")
        return e

    def unary(cls, a):
        """One-input nodes (1-x, saturate, normalize) take their input unnamed."""
        e = node(cls)
        mel.connect_material_expressions(a, "", e, "")
        return e

    def mul(a, b=None, const=None):
        return op(unreal.MaterialExpressionMultiply, a, b, **({"const_b": const} if const is not None else {}))

    def add(a, b=None, const=None):
        return op(unreal.MaterialExpressionAdd, a, b, **({"const_b": const} if const is not None else {}))

    def lerp(a, b, alpha, ca=None, cb=None):
        e = node(unreal.MaterialExpressionLinearInterpolate, **({"const_a": ca} if ca is not None else {}),
                 **({"const_b": cb} if cb is not None else {}))
        if a is not None:
            mel.connect_material_expressions(a, "", e, "A")
        if b is not None:
            mel.connect_material_expressions(b, "", e, "B")
        mel.connect_material_expressions(alpha, "", e, "Alpha")
        return e

    def sample(texture, uv):
        """One channel of a texture (the noise is grey): its red."""
        e = node(unreal.MaterialExpressionTextureSample, texture=texture, sampler_type=_sampler_type_for(texture))
        mel.connect_material_expressions(uv, "", e, "UVs")
        r = node(unreal.MaterialExpressionComponentMask, r=True)
        mel.connect_material_expressions(e, "", r, "")
        return r

    fine = unreal.load_asset(NOISE_FINE)
    soft = unreal.load_asset(NOISE_SOFT)
    colour, metallic, roughness = vparam("BaseColor", (0.5, 0.5, 0.5)), param("Metallic", 0.0), param("Roughness", 0.5)
    nonskid, brushed, grime, bottom = param("NonSkid", 0.0), param("Brushed", 0.0), param("Grime", 0.0), param("Bottom", 0.0)
    bottom_colour, waterline = vparam("BottomColor", BOTTOM_PAINT[0]), param("WaterlineZ", BOTTOM_PAINT[1])

    def raw_sample(texture, uv_node):
        e = node(unreal.MaterialExpressionTextureSample, texture=texture, sampler_type=_sampler_type_for(texture))
        mel.connect_material_expressions(uv_node, "", e, "UVs")
        return e

    uv = node(unreal.MaterialExpressionTextureCoordinate)
    uv_fine = mul(uv, const=14.0)                                  # a 7 cm tile: the deck's speckle
    h0 = raw_sample(fine, uv_fine)
    hu = raw_sample(fine, add(uv_fine, node(unreal.MaterialExpressionConstant2Vector, r=1.0 / 128.0, g=0.0)))
    hv = raw_sample(fine, add(uv_fine, node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=1.0 / 128.0)))
    streaks = raw_sample(fine, mul(uv, node(unreal.MaterialExpressionConstant2Vector, r=0.6, g=45.0)))   # grain
    blotch = raw_sample(soft, mul(uv, const=0.45))
    world = node(unreal.MaterialExpressionWorldPosition)
    local = node(unreal.MaterialExpressionTransformPosition, transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                 transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    mel.connect_material_expressions(world, "", local, "")

    def custom(code, output_type, inputs):
        """An HLSL node: inputs are (name, node, output) triples."""
        pins = []
        for name, _, _ in inputs:
            pin = unreal.CustomInput()
            pin.set_editor_property("input_name", name)
            pins.append(pin)
        e = node(unreal.MaterialExpressionCustom, code=code, output_type=output_type, inputs=pins)
        for name, src, out_name in inputs:
            mel.connect_material_expressions(src, out_name, e, name)
        return e

    float1, float3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3
    # Antifouling paint below the waterline, in the mesh's own frame.
    painted = custom("return saturate((Waterline - Local.z) * 0.6) * Bottom;", float1,
                     [("Waterline", waterline, ""), ("Local", local, ""), ("Bottom", bottom, "")])
    # Colour: speckle, grain and grime shade it; antifouling replaces it below the waterline.
    final_colour = custom(
        "float speck = lerp(1.0, 0.8, H0 * NonSkid);\n"
        "float grain = lerp(1.0, Streaks, Brushed);\n"
        "float dirt = lerp(1.0, 0.72, (1.0 - Blotch) * Grime);\n"
        "return lerp(Colour.rgb * speck * grain * dirt, BottomColour.rgb, Painted);", float3,
        [("Colour", colour, ""), ("H0", h0, "R"), ("Streaks", streaks, "R"), ("Blotch", blotch, "R"), ("NonSkid", nonskid, ""),
         ("Brushed", brushed, ""), ("Grime", grime, ""), ("BottomColour", bottom_colour, ""), ("Painted", painted, "")])
    # Roughness: speckle and grime dull it, the grain varies it; antifouling is matte.
    final_rough = custom(
        "float r = Rough + H0 * NonSkid * 0.12 + (Streaks - 0.5) * Brushed * 0.18 + (1.0 - Blotch) * Grime * 0.25;\n"
        "return lerp(saturate(r), 0.75, Painted);", float1,
        [("Rough", roughness, ""), ("H0", h0, "R"), ("Streaks", streaks, "R"), ("Blotch", blotch, "R"), ("NonSkid", nonskid, ""),
         ("Brushed", brushed, ""), ("Grime", grime, ""), ("Painted", painted, "")])
    final_metal = custom("return lerp(Metal, 0.0, Painted);", float1, [("Metal", metallic, ""), ("Painted", painted, "")])
    # Bumps: the speckle's slope, by NonSkid (a tangent-space normal from height differences).
    unit = custom("float2 d = float2(HU - H0, HV - H0) * NonSkid * -6.0;\nreturn normalize(float3(d, 1.0));", float3,
                  [("H0", h0, "R"), ("HU", hu, "R"), ("HV", hv, "R"), ("NonSkid", nonskid, "")])

    mel.connect_material_property(final_colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(final_metal, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(final_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(unit, "", unreal.MaterialProperty.MP_NORMAL)
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", BOAT_DETAIL_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def _boat_surface_material():
    """M_BoatSurface: plain opaque surface with BaseColor / Metallic / Roughness parameters."""
    path = f"{MATERIALS_PATH}/M_BoatSurface"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_BoatSurface", MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    colour.set_editor_property("parameter_name", "BaseColor")
    metal = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 200)
    metal.set_editor_property("parameter_name", "Metallic")
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 300)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.5)
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mat


def _boat_light_material():
    """M_BoatLight: a glossy lens that glows, with Colour and Glow parameters."""
    path = f"{MATERIALS_PATH}/M_BoatLight"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_BoatLight", MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    colour.set_editor_property("parameter_name", "Colour")
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 200)
    glow.set_editor_property("parameter_name", "Glow")
    lit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 150)
    mel.connect_material_expressions(colour, "", lit, "A")
    mel.connect_material_expressions(glow, "", lit, "B")
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -250, 300)
    rough.set_editor_property("r", 0.15)
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mat


def _boat_light(slot, light):
    """MI_Boat_<slot>: an instance of M_BoatLight with that light's colour and glow."""
    path = f"{MATERIALS_PATH}/MI_Boat_{slot}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            f"MI_Boat_{slot}", MATERIALS_PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, light)
    (r, g, b), glow = BOAT_LIGHTS[slot]
    mel.set_material_instance_vector_parameter_value(mi, "Colour", unreal.LinearColor(r, g, b, 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Glow", glow)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mi


BOAT_GLASS_VERSION = "3"


def _boat_glass_material():
    """M_BoatGlass: clear, lightly tinted glass that's lit per pixel, so it shows the sky's reflection and sun glints
    (the default translucent lighting made it a flat grey sheet)."""
    path = f"{MATERIALS_PATH}/M_BoatGlass"
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if assets.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == BOAT_GLASS_VERSION:
            return unreal.load_asset(path)
        assets.delete_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_BoatGlass", MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("two_sided", True)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    tint.set_editor_property("constant", unreal.LinearColor(0.02, 0.03, 0.035, 1.0))
    # More see-through looking straight at it, more reflective at a glancing angle, like real glass.
    fresnel = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -500, 150)
    fresnel.set_editor_property("exponent", 4.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.0)
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -300, 150)
    opacity.set_editor_property("const_a", 0.26)
    opacity.set_editor_property("const_b", 0.7)
    mel.connect_material_expressions(fresnel, "", opacity, "Alpha")
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 300)
    spec.set_editor_property("r", 1.0)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 400)
    rough.set_editor_property("r", 0.03)
    mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    assets.set_metadata_tag(mat, "RiptideVersion", BOAT_GLASS_VERSION)
    assets.save_asset(path, only_if_is_dirty=False)
    return mat


def _boat_finish(slot, surface):
    """MI_Boat_<slot>: an instance of M_BoatSurface with that slot's colour, metallic and roughness."""
    path = f"{MATERIALS_PATH}/MI_Boat_{slot}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            f"MI_Boat_{slot}", MATERIALS_PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi = unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, surface)
    (r, g, b), metallic, roughness = BOAT_FINISHES[slot]
    mel.set_material_instance_vector_parameter_value(mi, "BaseColor", unreal.LinearColor(r, g, b, 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Metallic", metallic)
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", roughness)
    for name in ("NonSkid", "Brushed", "Grime", "Bottom"):
        mel.set_material_instance_scalar_parameter_value(mi, name, BOAT_DETAIL.get(slot, {}).get(name, 0.0))
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return mi


def make_boat_assets():
    """The boat's models (hull, outboard, its clamp bracket, a throttle lever), generated and imported when missing or
    when the model's version changes."""
    import importlib
    import riptide_boat_mesh
    importlib.reload(riptide_boat_mesh)

    assets = unreal.EditorAssetLibrary
    skiff_path = f"{BOATS_PATH}/SM_PatrolSkiff"
    models = {
        "SM_PatrolSkiff": riptide_boat_mesh.build_skiff,
        "SM_Outboard": riptide_boat_mesh.build_outboard,
        "SM_OutboardBracket": riptide_boat_mesh.build_outboard_bracket,
        "SM_ThrottleLever": riptide_boat_mesh.build_throttle_lever,
        "SM_RadarArray": riptide_boat_mesh.build_radar_array,
        "SM_Searchlight": riptide_boat_mesh.build_searchlight,
    }
    paths = [f"{BOATS_PATH}/{name}" for name in models]
    if all(assets.does_asset_exist(path) for path in paths) and \
            assets.get_metadata_tag(unreal.load_asset(skiff_path), "RiptideVersion") == BOAT_MODEL_VERSION:
        return
    unreal.log("Riptide: building the boat model")
    if assets.does_directory_exist(BOATS_PATH):
        assets.delete_directory(BOATS_PATH)

    out_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Generated", "Boat")
    os.makedirs(out_dir, exist_ok=True)
    tasks = []
    for name, build in models.items():
        obj = os.path.join(out_dir, name + ".obj")
        build().write_obj(obj)
        task = unreal.AssetImportTask()
        task.filename = obj
        task.destination_path = BOATS_PATH
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for task in tasks:
        unreal.log(f"Riptide: imported {task.filename} -> {list(task.imported_object_paths)}")

    surface = _boat_detail_material()
    glass = _boat_glass_material()
    light = _boat_light_material()
    _boat_light("LampOn", light)
    for path in paths:
        mesh = unreal.load_asset(path)
        if not mesh:
            unreal.log_error(f"Riptide: boat model {path} failed to import")
            continue
        materials = mesh.get_editor_property("static_materials")
        for i, slot in enumerate(materials):
            name = str(slot.get_editor_property("material_slot_name"))
            finish = (glass if name == "Glass" else _boat_light(name, light) if name in BOAT_LIGHTS
                      else _boat_finish(name, surface) if name in BOAT_FINISHES else None)
            if finish:
                slot.set_editor_property("material_interface", finish)
                materials[i] = slot
            else:
                unreal.log_warning(f"Riptide: boat material slot '{name}' has no finish")
        mesh.set_editor_property("static_materials", materials)
        # Not Nanite: the importer turns it on, but Nanite can't draw the see-through glass, and the boat is small
        # enough that it gains nothing.
        mesh_tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        nanite = mesh_tools.get_nanite_settings(mesh)
        nanite.set_editor_property("enabled", False)
        mesh_tools.set_nanite_settings(mesh, nanite, True)
        if path == skiff_path:
            # The crew walks on the hull's own triangles (ARiptideBoat's DeckCollision): the deck, bulwarks, console
            # and T-top legs, exactly as drawn, which no simple box or hull shape can give.
            body = mesh.get_editor_property("body_setup")
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            body.set_editor_property("double_sided_geometry", True)
            complexity = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_collision_complexity(mesh)
            unreal.log(f"Riptide: {path.split('/')[-1]} collision {complexity}")
        assets.set_metadata_tag(mesh, "RiptideVersion", BOAT_MODEL_VERSION)
        assets.save_asset(path, only_if_is_dirty=False)
        box = mesh.get_bounding_box()
        unreal.log(f"Riptide: {path.split('/')[-1]} bounds {box.min} .. {box.max}, slots {[str(s.material_slot_name) for s in materials]}")
    # The importer makes placeholder materials for each slot; ours replace them.
    for leftover in assets.list_assets(BOATS_PATH, recursive=True):
        if not leftover.split(".")[0].endswith(tuple(models)):
            assets.delete_asset(leftover.split(".")[0])


def _fog_for_light_beams(fog):
    """Light volumetric fog, so beams show in the air at night (a searchlight's beam, the masthead's glow), kept thin
    so the day stays clear. True if it changed."""
    comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if comp.get_editor_property("enable_volumetric_fog"):
        return False
    comp.set_editor_property("enable_volumetric_fog", True)
    comp.set_editor_property("volumetric_fog_extinction_scale", 0.35)
    comp.set_editor_property("volumetric_fog_scattering_distribution", 0.7)
    unreal.log("Riptide: volumetric fog on (for light beams)")
    return True


def update_ocean_test_map():
    """Brings a map built by an older version of this script up to date, without rebuilding it."""
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    levels.load_level(MAP_PATH)
    changed = False
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(actor, unreal.WaterBodyOcean):
            changed |= _cover_waves(actor)
        elif isinstance(actor, unreal.ExponentialHeightFog):
            changed |= _fog_for_light_beams(actor)
    if changed:
        levels.save_current_level()


def build_ocean_test_map():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        update_ocean_test_map()
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
    _fog_for_light_beams(_spawn(unreal.ExponentialHeightFog))
    _spawn(unreal.VolumetricCloud)

    zone = _spawn(unreal.WaterZone)
    ocean = _spawn_ocean()
    _open_up_sea(zone, ocean)
    _set_swell(ocean)
    _cover_waves(ocean)
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
    make_spray_material()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not create materials: {err}")

try:
    make_boat_assets()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not build the boat model: {err}")

try:
    build_ocean_test_map()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not build ocean test map: {err}")
