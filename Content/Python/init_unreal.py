"""Runs automatically when the editor opens. Imports the game's sounds and builds the ocean test map and the main menu
map the first time."""

import os

import unreal

MAP_PATH = "/Game/Riptide/Maps/Ocean_Test"
MENU_MAP_PATH = "/Game/Riptide/Maps/MainMenu"
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

# Each sound's class, for the settings screen's volumes (URiptideSettingsSave): the sea's ambience on its own, everything
# else (the boat, the water) under effects.
SOUND_CLASSES = ("SC_Effects", "SC_Ambient")
AMBIENT_SOUNDS = ("S_Ocean_Ambience",)


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

    classes = {}
    for name in SOUND_CLASSES:
        path = f"{AUDIO_PATH}/{name}"
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, AUDIO_PATH, unreal.SoundClass, unreal.SoundClassFactory())
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        classes[name] = unreal.load_asset(path)

    # Levels (and classes) are applied every launch, so retuning a target above takes effect without a reimport.
    for name, (_filename, loops, lufs, peak, target) in SOUNDS.items():
        path = f"{AUDIO_PATH}/{name}"
        sound = unreal.load_asset(path)
        if not sound:
            unreal.log_error(f"Riptide: sound {name} failed to import")
            continue
        volume = round(_sound_volume(lufs, peak, target), 4)
        sound_class = classes.get("SC_Ambient" if name in AMBIENT_SOUNDS else "SC_Effects")
        if sound.get_editor_property("looping") != loops or abs(sound.get_editor_property("volume") - volume) > 1e-4                 or sound.get_editor_property("sound_class_object") != sound_class:
            sound.set_editor_property("looping", loops)
            sound.set_editor_property("volume", volume)
            sound.set_editor_property("sound_class_object", sound_class)
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)


RADIO_SOUNDS_VERSION = "1"
FISHING_SOUNDS_VERSION = "1"


def _squelch(open_click, seconds, seed):
    """A radio's squelch, made rather than recorded: a click, then a burst of band-limited hiss that dies away fast
    (the carrier dropping out). 16-bit mono samples at 22.05 kHz."""
    import math
    import random
    rate = 22050
    rng = random.Random(seed)
    out = []
    lp = bp = 0.0
    n = int(rate * seconds)
    for i in range(n):
        t = i / rate
        noise = rng.uniform(-1.0, 1.0)
        # Two one-pole filters make a rough band-pass round 2-4 kHz: a radio's hiss, not a white roar.
        lp += 0.55 * (noise - lp)
        bp += 0.18 * (lp - bp)
        hiss = (lp - bp) * 1.8
        env = math.exp(-t / (seconds * (0.22 if open_click else 0.35)))
        click = math.exp(-t / 0.002) * (1.0 if open_click else 0.5) * (1.0 if (i // 9) % 2 else -1.0)
        out.append(max(-1.0, min(1.0, 0.55 * hiss * env + 0.6 * click)))
    return rate, out


def _made_sound(name, version, make, volume=0.5):
    """Imports a sound made in code (make() returns (rate, samples in -1..1)) as AUDIO_PATH/name, unless the one there
    is already this version: written out as a 16-bit mono WAV and put in the effects sound class."""
    import struct
    import wave
    path = f"{AUDIO_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path) and \
            unreal.EditorAssetLibrary.get_metadata_tag(unreal.load_asset(path), "RiptideVersion") == version:
        return
    out_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Generated", "Audio")
    os.makedirs(out_dir, exist_ok=True)
    rate, samples = make()
    filename = os.path.join(out_dir, f"{name}.wav")
    with wave.open(filename, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, v)) * 32000)) for v in samples))
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = AUDIO_PATH
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    sound = unreal.load_asset(path)
    if not sound:
        unreal.log_error(f"Riptide: sound {name} failed to import")
        return
    sound.set_editor_property("volume", volume)
    effects = f"{AUDIO_PATH}/SC_Effects"
    if unreal.EditorAssetLibrary.does_asset_exist(effects):
        sound.set_editor_property("sound_class_object", unreal.load_asset(effects))
    unreal.EditorAssetLibrary.set_metadata_tag(sound, "RiptideVersion", version)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"Riptide: made the sound {name}")


def make_radio_sounds():
    """The radio's squelch opening and closing (S_RadioSquelchOpen and S_RadioSquelchClose): generated, written out as
    WAVs and imported."""
    for name, open_click, seconds, seed in (("S_RadioSquelchOpen", True, 0.16, 7), ("S_RadioSquelchClose", False, 0.32, 11)):
        _made_sound(name, RADIO_SOUNDS_VERSION, lambda o=open_click, s=seconds, r=seed: _squelch(o, s, r))


def _fishing_sound(kind):
    """Fishing's sounds, made rather than recorded (22.05 kHz mono): the rod swishing through a cast, the bobber's
    plop, the splash of a bite, one click of the reel's ratchet, and the line snapping."""
    import math
    import random
    rate = 22050
    rng = random.Random({"cast": 3, "plop": 5, "splash": 9, "reel": 13, "snap": 17}[kind])
    out = []
    lp = lp2 = 0.0
    if kind == "cast":
        # Air past a thin rod: hiss swept up and down in pitch as the tip whips through.
        n = int(rate * 0.42)
        for i in range(n):
            t = i / n
            k = 0.05 + 0.5 * math.sin(math.pi * t) ** 2
            lp += k * (rng.uniform(-1.0, 1.0) - lp)
            lp2 += 0.5 * k * (lp - lp2)
            out.append((lp - lp2) * 2.2 * math.sin(math.pi * t) ** 1.5)
    elif kind == "plop":
        # A small thing into water: a falling bubble tone and a short wash.
        n = int(rate * 0.35)
        phase = 0.0
        for i in range(n):
            t = i / rate
            phase += 2.0 * math.pi * (900.0 * math.exp(-t / 0.03) + 260.0) / rate
            lp += 0.25 * (rng.uniform(-1.0, 1.0) - lp)
            out.append(0.55 * math.sin(phase) * math.exp(-t / 0.05) + 0.35 * lp * math.exp(-t / 0.09))
    elif kind == "splash":
        # A fish breaking the surface: a broadband burst with a few bubble tones in it.
        n = int(rate * 0.7)
        bubbles = [(rng.uniform(0.0, 0.25), rng.uniform(500.0, 1400.0)) for _ in range(6)]
        for i in range(n):
            t = i / rate
            lp += 0.45 * (rng.uniform(-1.0, 1.0) - lp)
            v = lp * (1.0 - math.exp(-t / 0.004)) * math.exp(-t / 0.16) * 0.9
            for start, f in bubbles:
                if t > start:
                    u = t - start
                    v += 0.18 * math.sin(2.0 * math.pi * f * (1.0 + 2.0 * u) * u) * math.exp(-u / 0.04)
            out.append(v)
    elif kind == "reel":
        # One tick of the ratchet: a hard click and a short metallic ring.
        n = int(rate * 0.05)
        for i in range(n):
            t = i / rate
            out.append(0.8 * math.exp(-t / 0.0015) * (1.0 if (i // 3) % 2 else -1.0)
                       + 0.25 * math.sin(2.0 * math.pi * 3400.0 * t) * math.exp(-t / 0.012))
    elif kind == "snap":
        # The line going: a sharp crack and a falling twang.
        n = int(rate * 0.45)
        phase = 0.0
        for i in range(n):
            t = i / rate
            phase += 2.0 * math.pi * (1800.0 * math.exp(-t / 0.08) + 180.0) / rate
            crack = rng.uniform(-1.0, 1.0) * math.exp(-t / 0.006)
            out.append(0.7 * crack + 0.35 * math.sin(phase) * math.exp(-t / 0.12))
    return rate, out


def make_fishing_sounds():
    """S_FishCast, S_FishPlop, S_FishSplash, S_FishReel, S_FishSnap (URiptideAnglerComponent plays them where they happen)."""
    for name, kind, volume in (("S_FishCast", "cast", 0.45), ("S_FishPlop", "plop", 0.6), ("S_FishSplash", "splash", 0.7),
                               ("S_FishReel", "reel", 0.3), ("S_FishSnap", "snap", 0.6)):
        _made_sound(name, FISHING_SOUNDS_VERSION, lambda k=kind: _fishing_sound(k), volume=volume)


def _spawn(actor_class, location=(0.0, 0.0, 0.0), yaw=0.0, pitch=0.0):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return actors.spawn_actor_from_class(
        actor_class,
        unreal.Vector(*location),
        unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw),
    )


# Open water the maps cover, in cm (24 km square): about 13 minutes flat out from the start to any edge. Past the
# edge there's no sea at all, and the boat drops through the world (a 2 km sea was reached in a minute at 30 kn).
SEA_SIZE = 2400000.0

# The ocean treats the inside of its shoreline spline as dry land for an island. There's no island yet,
# so the shoreline is shrunk to a 4 m loop parked in a far corner, leaving the spawn point in open water.
SHORE_CENTRE = (-SEA_SIZE * 0.45, -SEA_SIZE * 0.45)
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


# Past the edge of the detailed sea the Water plugin can draw a flat "far" ocean out to the horizon. Without it the
# water simply stopped at the zone's edge and the boat sailed off into nothing. 500 km is past any horizon.
FAR_SEA_EXTENT = 50000000.0
FAR_SEA_MATERIAL = "/Water/Materials/WaterSurface/Water_FarMesh"


def _far_sea(zone):
    """Draws the ocean on past the water zone to the horizon. True if it changed."""
    mesh = zone.get_component_by_class(unreal.WaterMeshComponent)
    if mesh.get_editor_property("far_distance_mesh_extent") >= FAR_SEA_EXTENT - 1.0:
        return False
    mesh.set_editor_property("far_distance_material", unreal.load_asset(FAR_SEA_MATERIAL))
    mesh.set_editor_property("far_distance_mesh_extent", FAR_SEA_EXTENT)
    unreal.log("Riptide: the sea is drawn on to the horizon past the water zone")
    return True


# How deep the water must be before waves reach full size. The drawn waves shrink with the depth the renderer
# measures down to the ground, but the height the game reads (buoyancy, swimmers, the underwater view) uses full
# waves, so wherever the two disagreed the camera could be under the drawn surface yet "above" the read one, or the
# other way round (the sky tinted as if underwater). A tiny mask depth keeps the drawn waves full size everywhere.
OCEAN_WAVE_MASK_DEPTH = 1.0


def _full_waves(ocean):
    """Keeps the drawn waves the same size as the ones the game reads. True if it changed."""
    body = ocean.get_water_body_component()
    if abs(body.get_editor_property("target_wave_mask_depth") - OCEAN_WAVE_MASK_DEPTH) < 0.01:
        return False
    body.set_editor_property("target_wave_mask_depth", OCEAN_WAVE_MASK_DEPTH)
    unreal.log("Riptide: drawn waves now full size at any depth")
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


SPRAY_VERSION = "5"


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
BOAT_MODEL_VERSION = "31"
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
        "SM_OutboardSwivel": riptide_boat_mesh.build_outboard_swivel,
        "SM_ThrottleLever": riptide_boat_mesh.build_throttle_lever,
        "SM_RadarArray": riptide_boat_mesh.build_radar_array,
        "SM_Searchlight": riptide_boat_mesh.build_searchlight,
        "SM_Propeller": riptide_boat_mesh.build_propeller,
        "SM_HelmWheel": riptide_boat_mesh.build_wheel,
        "SM_RadioMic": riptide_boat_mesh.build_mic,
        "SM_CompassCard": riptide_boat_mesh.build_compass_card,
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


# --- The crew --------------------------------------------------------------------------------------------------------
# Quaternius's CC0 bodies, hair and animation libraries (SourceAssets/Characters/Quaternius, credited in
# Docs/CREDITS.md), and the uniforms and gear riptide_crew_mesh.py generates from them, imported onto one shared
# skeleton under /Game/Riptide/Characters. URiptideCrewBodyComponent loads them by path. Bump CREW_VERSION when
# riptide_crew_mesh.py or the recipe here changes, so every machine rebuilds them on its next launch.
CHARACTERS_PATH = "/Game/Riptide/Characters"
CREW_VERSION = "14"
CREW_MATERIALS = f"{CHARACTERS_PATH}/Materials"

# Textures: (asset name, file under SourceAssets/Characters, kind). The bodies' are MakeHuman's (CC0, see
# Docs/CREDITS.md); the beard's are Quaternius's.
CREW_HAIR_STYLES = ("short04", "short01", "long01", "braid01")
CREW_TEXTURES = [
    ("T_CrewMale_Base", "MakeHuman/Male/middleage_lightskinned_male_diffuse.png", "colour"),
    ("T_CrewFemale_Base", "MakeHuman/Female/middleage_lightskinned_female_diffuse.png", "colour"),
    ("T_CrewBrows_Male", "MakeHuman/Male/eyebrow001.png", "colour"),
    ("T_CrewBrows_Female", "MakeHuman/Female/eyebrow006.png", "colour"),
    ("T_CrewLashes", "MakeHuman/Male/eyelashes01.png", "colour"),
    ("T_CrewTeeth", "MakeHuman/Male/teeth.png", "colour"),
    ("T_CrewEye", "MakeHuman/Male/brown_eye.png", "colour"),
    ("T_CrewHair1_Base", "Quaternius/Hair/T_Hair_1_BaseColor.png", "colour"),
    ("T_CrewHair1_Normal", "Quaternius/Hair/T_Hair_1_Normal.png", "normal"),
    ("T_CrewHair2_Base", "Quaternius/Hair/T_Hair_2_BaseColor.png", "colour"),
    ("T_CrewHair2_Normal", "Quaternius/Hair/T_Hair_2_Normal.png", "normal"),
] + [("T_CrewHair_" + style, f"MakeHuman/Male/{style}_diffuse.png", "colour") for style in CREW_HAIR_STYLES]

# A tiny noise library for the crew's materials' HLSL (value noise and fBm, self-contained so it compiles the same
# on every platform), wrapped in a struct: a Custom node's code can't declare functions, but it can declare a struct
# with methods.
_NOISE_HLSL = (
    "struct FNoise {\n"
    "  float h(float3 p) { p = frac(p * 0.3183099 + 0.1); p *= 17.0; return frac(p.x * p.y * p.z * (p.x + p.y + p.z)); }\n"
    "  float n(float3 x) { float3 i = floor(x); float3 f = frac(x); f = f * f * (3.0 - 2.0 * f);\n"
    "    return lerp(lerp(lerp(h(i), h(i + float3(1, 0, 0)), f.x), lerp(h(i + float3(0, 1, 0)), h(i + float3(1, 1, 0)), f.x), f.y),\n"
    "                lerp(lerp(h(i + float3(0, 0, 1)), h(i + float3(1, 0, 1)), f.x), lerp(h(i + float3(0, 1, 1)), h(i + float3(1, 1, 1)), f.x), f.y), f.z); }\n"
    "  float fbm(float3 p) { float a = 0.5, s = 0.0; for (int k = 0; k < 4; k++) { s += a * n(p); p = p * 2.03 + 17.1; a *= 0.5; } return s / 0.9375; }\n"
    "};\n"
    "FNoise nz;\n"
)

# The uniform's camouflage, from the cloth's own (pre-skinning) position so the pattern stays printed on it as the
# body moves. Style 0 is soft-edged blotches in three layers (MultiCam-like, desert), 1 hard-edged shapes stretched
# sideways (woodland, urban), 2 plain cloth. Then the ripstop grid, a little fading, and darker reinforced panels
# (the mesh's red vertex colour, at the knees and elbows).
_CAMO_HLSL = _NOISE_HLSL + (
    "float3 p = Pos / (13.0 * max(Size, 0.1));\n"
    "float3 c = C1.rgb;\n"
    "if (Style < 0.5) {\n"
    "  c = lerp(c, C2.rgb, smoothstep(0.52, 0.535, nz.fbm(p * 0.8 + 3.1)));\n"
    "  c = lerp(c, C3.rgb, smoothstep(0.57, 0.585, nz.fbm(p * 1.3 + 11.7)));\n"
    "  c = lerp(c, C4.rgb, smoothstep(0.62, 0.635, nz.fbm(p * 2.6 + 29.3)) * step(0.45, nz.n(p * 1.7 + 5.0)));\n"
    "} else if (Style < 1.5) {\n"
    "  float3 q = p * float3(1.0, 1.0, 1.7);\n"
    "  c = lerp(c, C2.rgb, step(0.52, nz.fbm(q * 0.8 + 5.0)));\n"
    "  c = lerp(c, C4.rgb, step(0.63, nz.fbm(q * 1.1 + 47.0)));\n"
    "  c = lerp(c, C3.rgb, step(0.60, nz.fbm(q * 1.3 + 13.0)) * step(0.45, nz.fbm(q * 1.9 + 31.0)));\n"
    "}\n"
    "float3 g = abs(frac(Pos / 0.6) - 0.5);\n"
    "float grid = smoothstep(0.42, 0.5, max(max(g.x, g.y), g.z));\n"
    "c *= 1.0 - 0.06 * grid;\n"
    "c *= 0.95 + 0.08 * nz.n(Pos * 0.06) + 0.02 * nz.n(Pos * 2.5);\n"
    "c *= lerp(1.0, 0.82, Panel);\n"
    "return c;\n"
)
# Creases in the cloth: a soft bump from the same noise (tangent-space normal).
_CLOTH_NORMAL_HLSL = _NOISE_HLSL + (
    "float3 q = Pos * 0.35; float e = 0.12;\n"
    "float h0 = nz.fbm(q);\n"
    "float hx = nz.fbm(q + float3(e, 0, 0)); float hy = nz.fbm(q + float3(0, e, 0)); float hz = nz.fbm(q + float3(0, 0, e));\n"
    "return normalize(float3((h0 - hx) + 0.5 * (h0 - hz), (h0 - hy) + 0.5 * (h0 - hz), 0.18 / Strength));\n"
)

# The gear's surfaces: one material for webbing, nylon, knit, rubber, leather and metal, picked by Weave. The colour
# is the gear colour (UseGear), the boot leather colour (UseBoot) or a fixed one, times Tint. Boots and gloves mark
# their parts in vertex colour: boots red for the rubber sole, green the laces, blue the toe and heel caps; gloves
# red for the palm.
#   Weave 0 nylon (Cordura), 1 webbing, 2 knit ribs, 3 velcro loop, 4 smooth (rubber, plastic, metal), 5 shemagh
#   check, 6 boot leather, 7 glove
_GEAR_HLSL = _NOISE_HLSL + (
    "float3 base = lerp(Fixed.rgb, Gear.rgb, UseGear); base = lerp(base, Boot.rgb, UseBoot); base = lerp(base, Cloth.rgb, UseCloth); base *= Tint;\n"
    "float3 P = float3(UV * 10.0, 0.0);\n"
    "float w = Weave;\n"
    "if (w < 0.5) {\n"
    "  float2 f = abs(frac(UV * 10.0 / 0.32) - 0.5);\n"
    "  base *= (0.93 + 0.1 * (f.x + f.y)) * (0.9 + 0.18 * nz.n(P * 0.6));\n"
    "} else if (w < 1.5) {\n"
    "  base *= (0.9 + 0.1 * abs(sin(UV.x * 10.0 * 18.0))) * (0.92 + 0.12 * nz.n(P * 0.5));\n"
    "} else if (w < 2.5) {\n"
    "  base *= 0.78 + 0.22 * abs(sin(UV.x * 3.14159)) + 0.06 * nz.n(P * 3.0);\n"
    "} else if (w < 3.5) {\n"
    "  base *= 0.8 + 0.3 * nz.n(P * 9.0);\n"
    "} else if (w < 4.5) {\n"
    "  base *= 0.97 + 0.06 * nz.n(P * 0.4);\n"
    "} else if (w < 5.5) {\n"
    "  float3 sand = lerp(float3(0.42, 0.37, 0.27), Gear.rgb, 0.3);\n"
    "  float2 f = frac(UV * float2(1.4, 1.4));\n"
    "  float band = max(step(f.x, 0.12), step(f.y, 0.12)) * 0.7 + step(frac((UV.x - UV.y) * 4.0), 0.25) * 0.12;\n"
    "  base = lerp(sand, Gear.rgb * 0.3, band) * (0.9 + 0.15 * nz.n(P * 1.5));\n"
    "} else if (w < 6.5) {\n"
    "  base *= 0.88 + 0.16 * nz.n(P * 1.2);\n"
    "  base = lerp(base, base * 0.6, VC.b);\n"
    "  base = lerp(base, float3(0.03, 0.03, 0.03), VC.g);\n"
    "  base = lerp(base, float3(0.018, 0.018, 0.018), VC.r);\n"
    "} else {\n"
    "  float2 f = abs(frac(UV * 10.0 / 0.25) - 0.5);\n"
    "  base *= (0.9 + 0.14 * (f.x + f.y)) * (0.92 + 0.12 * nz.n(P * 0.8));\n"
    "  base = lerp(base, float3(0.045, 0.045, 0.047), VC.r);\n"
    "}\n"
    "return base;\n"
)
_GEAR_ROUGH_HLSL = (
    "float r = Rough;\n"
    "if (Weave > 5.5 && Weave < 6.5) r = lerp(lerp(r, 0.5, VC.b), 0.9, VC.r);\n"
    "if (Weave > 6.5) r = lerp(r, 0.95, VC.r);\n"
    "return r;\n"
)
_SKIN_HLSL = (
    "float lum = dot(Tex.rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "float3 rel = Tex.rgb / float3(0.58, 0.28, 0.16);\n"
    "return saturate(Tone.rgb * lerp(lum.xxx / 0.34, rel, 0.3));\n"
)
_HAIR_HLSL = (
    "float lum = dot(Tex.rgb, float3(0.2126, 0.7152, 0.0722));\n"
    "return saturate(Colour.rgb * pow(saturate(lum / 0.42), 1.3) * 1.15);\n"
)

# Each material slot the generated meshes use: (material, settings). Gear slots are instances of M_CrewGear:
#   (UseGear, UseBoot, Tint, Fixed colour, Weave, Roughness, Metallic)
CREW_GEAR_SLOTS = {
    "Gear": (1, 0, 1.0, (0.1, 0.1, 0.1), 0, 0.85, 0.0),
    "GearDark": (1, 0, 0.55, (0.1, 0.1, 0.1), 0, 0.85, 0.0),
    "Strap": (1, 0, 0.75, (0.1, 0.1, 0.1), 1, 0.8, 0.0),
    "Belt": (1, 0, 0.7, (0.1, 0.1, 0.1), 1, 0.8, 0.0),
    "Velcro": (1, 0, 0.9, (0.1, 0.1, 0.1), 3, 0.95, 0.0),
    "Knit": (1, 0, 0.85, (0.1, 0.1, 0.1), 2, 0.95, 0.0),
    "Shemagh": (1, 0, 1.0, (0.1, 0.1, 0.1), 5, 0.9, 0.0),
    "Glove": (1, 0, 0.42, (0.1, 0.1, 0.1), 7, 0.75, 0.0),
    "Lace": (0, 0, 1.0, (0.02, 0.02, 0.02), 1, 0.8, 0.0),
    "Sole": (0, 0, 1.0, (0.018, 0.018, 0.018), 4, 0.9, 0.0),
    "GloveGuard": (1, 0, 0.45, (0.1, 0.1, 0.1), 4, 0.5, 0.0),
    "Furniture": (1, 0, 0.9, (0.1, 0.1, 0.1), 4, 0.6, 0.0),
    "Boot": (0, 1, 1.0, (0.1, 0.1, 0.1), 6, 0.62, 0.0),
    # The castaway clothes: cloth in the look's colour (UseCloth, set below), wood for the clogs.
    "Shirt": (0, 0, 1.0, (0.8, 0.8, 0.8), 0, 0.9, 0.0),
    "Shorts": (0, 0, 1.0, (0.5, 0.45, 0.3), 0, 0.88, 0.0),
    "Clog": (0, 0, 1.0, (0.36, 0.24, 0.12), 4, 0.55, 0.0),
    "Mount": (0, 0, 1.0, (0.035, 0.036, 0.038), 4, 0.42, 0.5),
    "Trim": (0, 0, 1.0, (0.02, 0.02, 0.02), 4, 0.75, 0.0),
    "Polymer": (0, 0, 1.0, (0.03, 0.03, 0.032), 4, 0.55, 0.0),
    "Pad": (0, 0, 1.0, (0.045, 0.045, 0.045), 0, 0.95, 0.0),
    "Metal": (0, 0, 1.0, (0.2, 0.19, 0.17), 4, 0.35, 0.9),
    "Frame": (0, 0, 1.0, (0.02, 0.02, 0.02), 4, 0.35, 0.0),
    "GunMetal": (0, 0, 1.0, (0.022, 0.022, 0.024), 4, 0.42, 0.35),
}
# Every other slot: the material asset it's drawn with.
CREW_OTHER_SLOTS = {
    "Uniform": "MI_CrewCamo", "Camo": "MI_CrewCamo", "Lens": "MI_CrewLens_Sun", "ClearLens": "MI_CrewLens_Clear",
    "Hair1": "MI_CrewHair1", "Hair2": "MI_CrewHair2", "MI_Hair_1": "MI_CrewHair1", "MI_Hair_2": "MI_CrewHair2",
    "MI_Eyes": "M_CrewEyes", "MI_Superhero_Male": "MI_CrewSkin_Male", "MI_Superhero_Female": "MI_CrewSkin_Female",
    # MakeHuman's bodies (Tools/mh_export.py names the slots).
    "Skin_Male": "MI_CrewSkin_Male", "Skin_Female": "MI_CrewSkin_Female", "Eyes": "M_CrewEyes",
    "Brows_Male": "MI_CrewBrows_Male", "Brows_Female": "MI_CrewBrows_Female", "Lashes": "MI_CrewLashes", "Teeth": "MI_CrewTeeth",
    "Hair_short04": "MI_CrewHair_short04", "Hair_short01": "MI_CrewHair_short01", "Hair_long01": "MI_CrewHair_long01", "Hair_braid01": "MI_CrewHair_braid01",
}


class _Graph:
    """Builds a material's node graph from Python (the same helpers as M_BoatDetail's, reusable)."""

    def __init__(self, mat):
        self.mat = mat
        self.mel = unreal.MaterialEditingLibrary
        self.x = -1600

    def node(self, cls, **props):
        e = self.mel.create_material_expression(self.mat, cls, self.x, 0)
        self.x += 40
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def scalar(self, name, default):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default)

    def vector(self, name, colour):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*colour, 1.0))

    def texture(self, name, texture, uv=None):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name, texture=texture,
                      sampler_type=_sampler_type_for(texture))
        if uv is not None:
            self.mel.connect_material_expressions(uv, "", e, "UVs")
        return e

    def custom(self, code, output_type, inputs):
        """An HLSL node: inputs are (name, node, output name) triples."""
        pins = []
        for name, _, _ in inputs:
            pin = unreal.CustomInput()
            pin.set_editor_property("input_name", name)
            pins.append(pin)
        output_type = getattr(unreal.CustomMaterialOutputType, output_type)
        e = self.node(unreal.MaterialExpressionCustom, code=code, output_type=output_type, inputs=pins)
        for name, src, out in inputs:
            self.mel.connect_material_expressions(src, out, e, name)
        return e

    def out(self, node, prop, output=""):
        self.mel.connect_material_property(node, output, prop)


# Names, looked up when a material is built: the enum isn't there in every launch (a packaged game has no editor).
_F1, _F3 = "CMOT_FLOAT1", "CMOT_FLOAT3"


def _new_material(name, skinned=True):
    path = f"{CREW_MATERIALS}/{name}"
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, CREW_MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if skinned:
        mat.set_editor_property("used_with_skeletal_mesh", True)
    return mat, _Graph(mat)


def _finish_material(mat):
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(mat.get_path_name().split(".")[0], only_if_is_dirty=False)


def _make_crew_materials(textures):
    """The crew's materials: skin and hair tinted by the look's colours, the eyes, the camouflage uniform, the gear
    (one master with an instance per slot) and lenses. URiptideCrewBodyComponent sets SkinTone, HairColour,
    GearColour, BootColour and the camouflage (CamoStyle, CamoSize, Camo1-4) on dynamic instances."""
    mp = unreal.MaterialProperty
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary

    mat, g = _new_material("M_CrewSkin")
    # MakeHuman's skin photos have no normal or roughness maps: skin is smooth-ish and a little shiny.
    base = g.texture("BaseTex", textures["T_CrewMale_Base"])
    tone = g.vector("SkinTone", (0.56, 0.32, 0.2))
    g.out(g.custom(_SKIN_HLSL, _F3, [("Tex", base, "RGB"), ("Tone", tone, "")]), mp.MP_BASE_COLOR)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.55), mp.MP_ROUGHNESS)
    spec = g.node(unreal.MaterialExpressionConstant, r=0.35)
    g.out(spec, mp.MP_SPECULAR)
    _finish_material(mat)

    mat, g = _new_material("M_CrewHair")
    # Hair cards: the photo's alpha cuts the strands out.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    base = g.texture("BaseTex", textures["T_CrewHair1_Base"])
    colour = g.vector("HairColour", (0.03, 0.02, 0.015))
    g.out(g.custom(_HAIR_HLSL, _F3, [("Tex", base, "RGB"), ("Colour", colour, "")]), mp.MP_BASE_COLOR)
    g.out(base, mp.MP_OPACITY_MASK, "A")
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.6), mp.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.3), mp.MP_SPECULAR)
    _finish_material(mat)

    mat, g = _new_material("M_CrewCutout")
    # Eyebrows, eyelashes, teeth: a photo with its alpha, as it comes.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    base = g.texture("BaseTex", textures["T_CrewLashes"])
    g.out(base, mp.MP_BASE_COLOR, "RGB")
    g.out(base, mp.MP_OPACITY_MASK, "A")
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.7), mp.MP_ROUGHNESS)
    _finish_material(mat)

    mat, g = _new_material("M_CrewEyes")
    g.out(g.texture("BaseTex", textures["T_CrewEye"]), mp.MP_BASE_COLOR, "RGB")
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.08), mp.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.7), mp.MP_SPECULAR)
    _finish_material(mat)

    mat, g = _new_material("M_CrewCamo")
    mat.set_editor_property("used_with_static_lighting", False)
    # The pre-skinning position is only known per vertex: interpolated across each triangle for the pixel shader.
    pos = g.node(unreal.MaterialExpressionVertexInterpolator)
    unreal.MaterialEditingLibrary.connect_material_expressions(g.node(unreal.MaterialExpressionPreSkinnedPosition), "", pos, "")
    vc = g.node(unreal.MaterialExpressionVertexColor)
    ins = [("Pos", pos, ""), ("Style", g.scalar("CamoStyle", 0.0), ""), ("Size", g.scalar("CamoSize", 1.0), ""), ("Panel", vc, "R")]
    for i, c in enumerate(((0.3, 0.25, 0.15), (0.15, 0.15, 0.07), (0.17, 0.1, 0.05), (0.05, 0.035, 0.02))):
        ins.append(("C%d" % (i + 1), g.vector("Camo%d" % (i + 1), c), ""))
    g.out(g.custom(_CAMO_HLSL, _F3, ins), mp.MP_BASE_COLOR)
    g.out(g.custom(_CLOTH_NORMAL_HLSL, _F3, [("Pos", pos, ""), ("Strength", g.scalar("Creases", 1.0), "")]), mp.MP_NORMAL)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.88), mp.MP_ROUGHNESS)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.3), mp.MP_SPECULAR)
    _finish_material(mat)

    mat, g = _new_material("M_CrewGear")
    mat.set_editor_property("used_with_static_lighting", False)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    weave = g.scalar("Weave", 0.0)
    ins = [("Gear", g.vector("GearColour", (0.22, 0.12, 0.05)), ""), ("Boot", g.vector("BootColour", (0.1, 0.06, 0.03)), ""),
           ("Fixed", g.vector("FixedColour", (0.1, 0.1, 0.1)), ""), ("UseGear", g.scalar("UseGear", 1.0), ""),
           ("UseBoot", g.scalar("UseBoot", 0.0), ""), ("Tint", g.scalar("Tint", 1.0), ""), ("Weave", weave, ""),
           ("Cloth", g.vector("ClothColour", (0.8, 0.8, 0.8)), ""), ("UseCloth", g.scalar("UseCloth", 0.0), ""),
           ("UV", uv, ""), ("VC", vc, "")]
    g.out(g.custom(_GEAR_HLSL, _F3, ins), mp.MP_BASE_COLOR)
    g.out(g.custom(_GEAR_ROUGH_HLSL, _F1, [("Rough", g.scalar("Roughness", 0.8), ""), ("Weave", weave, ""), ("VC", vc, "")]), mp.MP_ROUGHNESS)
    g.out(g.scalar("Metallic", 0.0), mp.MP_METALLIC)
    _finish_material(mat)

    mat, g = _new_material("M_CrewLens")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("two_sided", True)
    g.out(g.vector("Tint", (0.01, 0.012, 0.014)), mp.MP_BASE_COLOR)
    # More see-through looking straight through it, more mirror at a glancing angle.
    fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=3.0, base_reflect_fraction=0.0)
    opacity = g.node(unreal.MaterialExpressionLinearInterpolate)
    mel.connect_material_expressions(g.scalar("Opacity", 0.85), "", opacity, "A")
    mel.connect_material_expressions(g.node(unreal.MaterialExpressionConstant, r=0.97), "", opacity, "B")
    mel.connect_material_expressions(fresnel, "", opacity, "Alpha")
    g.out(opacity, mp.MP_OPACITY)
    g.out(g.node(unreal.MaterialExpressionConstant, r=1.0), mp.MP_SPECULAR)
    g.out(g.node(unreal.MaterialExpressionConstant, r=0.04), mp.MP_ROUGHNESS)
    _finish_material(mat)

    def instance(name, parent, scalars=None, vectors=None, textures_=None):
        path = f"{CREW_MATERIALS}/{name}"
        mi = tools.create_asset(name, CREW_MATERIALS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, unreal.load_asset(f"{CREW_MATERIALS}/{parent}"))
        for k, v in (scalars or {}).items():
            mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
        for k, v in (vectors or {}).items():
            mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1.0))
        for k, v in (textures_ or {}).items():
            mel.set_material_instance_texture_parameter_value(mi, k, v)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        return mi

    instance("MI_CrewSkin_Male", "M_CrewSkin", textures_={"BaseTex": textures["T_CrewMale_Base"]})
    instance("MI_CrewSkin_Female", "M_CrewSkin", textures_={"BaseTex": textures["T_CrewFemale_Base"]})
    for k in ("1", "2"):
        instance("MI_CrewHair" + k, "M_CrewHair", textures_={"BaseTex": textures[f"T_CrewHair{k}_Base"]})
    for style in CREW_HAIR_STYLES:
        instance("MI_CrewHair_" + style, "M_CrewHair", textures_={"BaseTex": textures["T_CrewHair_" + style]})
    instance("MI_CrewBrows_Male", "M_CrewCutout", textures_={"BaseTex": textures["T_CrewBrows_Male"]})
    instance("MI_CrewBrows_Female", "M_CrewCutout", textures_={"BaseTex": textures["T_CrewBrows_Female"]})
    instance("MI_CrewLashes", "M_CrewCutout", textures_={"BaseTex": textures["T_CrewLashes"]})
    instance("MI_CrewTeeth", "M_CrewCutout", textures_={"BaseTex": textures["T_CrewTeeth"]})
    instance("MI_CrewCamo", "M_CrewCamo")
    instance("MI_CrewLens_Sun", "M_CrewLens", scalars={"Opacity": 0.88}, vectors={"Tint": (0.012, 0.01, 0.008)})
    instance("MI_CrewLens_Clear", "M_CrewLens", scalars={"Opacity": 0.3}, vectors={"Tint": (0.05, 0.045, 0.035)})
    for slot, (use_gear, use_boot, tint, fixed, weave, rough, metal) in CREW_GEAR_SLOTS.items():
        instance("MI_CrewGear_" + slot, "M_CrewGear", scalars={"UseGear": use_gear, "UseBoot": use_boot, "Tint": tint, "Weave": weave,
                                                              "Roughness": rough, "Metallic": metal, "UseCloth": 1.0 if slot in ("Shirt", "Shorts") else 0.0},
                 vectors={"FixedColour": fixed})


def _crew_material_for(slot):
    name = ("MI_CrewGear_" + slot) if slot in CREW_GEAR_SLOTS else CREW_OTHER_SLOTS.get(slot)
    return unreal.load_asset(f"{CREW_MATERIALS}/{name}") if name else None


def _interchange_import(filename, dest, skeleton=None, animations=False):
    """Imports a glTF/glb with Interchange: skeletal meshes (onto the given skeleton) or only its animations, with
    no materials, textures or physics assets of its own (ours are assigned after). Returns the imported paths."""
    p = unreal.InterchangeGenericAssetsPipeline()
    materials = p.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_static_meshes", False)
    meshes.set_editor_property("import_skeletal_meshes", not animations)
    meshes.set_editor_property("create_physics_asset", False)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("recompute_normals", False)       # the files' own normals: smooth across the welds
    shared = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    shared.set_editor_property("import_only_animations", animations)
    if skeleton:
        shared.set_editor_property("skeleton", skeleton)
    p.get_editor_property("animation_pipeline").set_editor_property("import_animations", animations)
    params = unreal.ImportAssetParameters()
    params.is_automated = True
    params.replace_existing = True
    params.override_pipelines.append(unreal.SoftObjectPath(p.get_path_name()))
    source = unreal.InterchangeManager.create_source_data(filename)
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(dest, source, params)
    return unreal.EditorAssetLibrary.list_assets(dest, recursive=False)


def _assign_crew_materials(mesh, path):
    """Each of a skeletal mesh's slots drawn with its crew material (by the slot's name)."""
    materials = mesh.get_editor_property("materials")
    for i, slot in enumerate(materials):
        name = str(slot.get_editor_property("material_slot_name"))
        mat = _crew_material_for(name)
        if mat:
            slot.set_editor_property("material_interface", mat)
            materials[i] = slot
        else:
            unreal.log_warning(f"Riptide: crew material slot '{name}' on {path} has no material")
    mesh.set_editor_property("materials", materials)


def make_crew_assets():
    """The crew: bodies, generated uniforms and gear, hair, animations, materials and the rifle, imported when
    missing or when CREW_VERSION changes."""
    import importlib
    import riptide_crew_mesh
    importlib.reload(riptide_crew_mesh)

    assets = unreal.EditorAssetLibrary
    marker = f"{CHARACTERS_PATH}/Male/SK_CrewMale"
    clips = [c.split("=") for c in unreal.RiptideCrewLibrary.get_crew_clips()]
    expected = [marker, f"{CHARACTERS_PATH}/Female/SK_CrewFemale", f"{CHARACTERS_PATH}/SM_Rifle"] + \
               [f"{CHARACTERS_PATH}/Animations/{name}" for name, _ in clips]
    if all(assets.does_asset_exist(p) for p in expected) and \
            assets.get_metadata_tag(unreal.load_asset(marker), "RiptideVersion") == CREW_VERSION:
        return
    unreal.log("Riptide: building the crew")
    if assets.does_directory_exist(CHARACTERS_PATH):
        assets.delete_directory(CHARACTERS_PATH)

    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    source = os.path.join(project, "SourceAssets", "Characters")
    quaternius = os.path.join(source, "Quaternius")
    out_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Generated", "Crew")
    generated, rifle_obj = riptide_crew_mesh.build_all(source, out_dir, log=unreal.log)

    # Textures, then the materials that use them.
    tasks = []
    for name, rel, _kind in CREW_TEXTURES:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(source, rel)
        task.destination_path = f"{CHARACTERS_PATH}/Textures"
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    textures = {}
    for name, _rel, kind in CREW_TEXTURES:
        path = f"{CHARACTERS_PATH}/Textures/{name}"
        tex = unreal.load_asset(path)
        if kind == "normal":
            # The packs' normal maps are OpenGL-style (green up): flipped for Unreal.
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("flip_green_channel", True)
        elif kind == "data":
            tex.set_editor_property("srgb", False)
        assets.save_asset(path, only_if_is_dirty=False)
        textures[name] = tex
    _make_crew_materials(textures)

    # The bodies (the male one first: its skeleton becomes the crew's), copied under the asset names they get.
    import shutil
    skeleton = None
    for body, src in (("Male", "Male_FullBody"), ("Female", "Female_FullBody")):
        folder = os.path.join(out_dir, body)
        name = "SK_Crew" + body
        with open(os.path.join(source, "MakeHuman", body, src + ".gltf")) as f:
            doc = f.read()
        shutil.copyfile(os.path.join(source, "MakeHuman", body, src + ".bin"), os.path.join(folder, src + ".bin"))
        with open(os.path.join(folder, name + ".gltf"), "w") as f:
            f.write(doc)
        _interchange_import(os.path.join(folder, name + ".gltf"), f"{CHARACTERS_PATH}/{body}", skeleton)
        if skeleton is None:
            skeleton = unreal.load_asset(f"{CHARACTERS_PATH}/{body}/{name}_Skeleton")
            if not unreal.RiptideCrewLibrary.set_up_crew_skeleton(skeleton):
                unreal.log_error("Riptide: could not set up the crew skeleton's retargeting")
            assets.save_asset(skeleton.get_path_name().split(".")[0], only_if_is_dirty=False)
        # The same body without its head, for the player's own view (URiptideCrewBodyComponent::ApplyFirstPerson).
        with open(os.path.join(source, "MakeHuman", body, src + "_FP.gltf")) as f:
            doc = f.read()
        shutil.copyfile(os.path.join(source, "MakeHuman", body, src + "_FP.bin"), os.path.join(folder, src + "_FP.bin"))
        with open(os.path.join(folder, name + "_FP.gltf"), "w") as f:
            f.write(doc)
        _interchange_import(os.path.join(folder, name + "_FP.gltf"), f"{CHARACTERS_PATH}/{body}", skeleton)
        for k, (mesh_name, gltf) in enumerate(generated[body].items()):
            _interchange_import(gltf, f"{CHARACTERS_PATH}/{body}", skeleton)
            if k % 8 == 7:
                # Each import keeps its working data until a collection: freed as it goes, so the build's memory
                # stays low.
                unreal.SystemLibrary.collect_garbage()
        for path in assets.list_assets(f"{CHARACTERS_PATH}/{body}", recursive=False):
            obj = unreal.load_asset(path)
            if isinstance(obj, unreal.SkeletalMesh):
                _assign_crew_materials(obj, path)
                if obj.get_editor_property("skeleton") != skeleton:
                    unreal.log_error(f"Riptide: {path} is not on the crew skeleton")
                assets.save_asset(path.split(".")[0], only_if_is_dirty=False)

    # The animations the crew uses (GetCrewClips), renamed; the rest of the libraries' clips are dropped.
    unreal.SystemLibrary.collect_garbage()
    import_dir = f"{CHARACTERS_PATH}/Animations/Import"
    for lib in ("UAL1", "UAL2"):
        _interchange_import(os.path.join(quaternius, "Animations", lib + "_Standard.glb"), import_dir, skeleton, animations=True)
        unreal.SystemLibrary.collect_garbage()
    for name, origin in clips:
        lib, clip = origin.split("/")
        src = f"{import_dir}/{lib}_Standard{clip}"
        if not assets.does_asset_exist(src) or not assets.rename_asset(src, f"{CHARACTERS_PATH}/Animations/{name}"):
            unreal.log_error(f"Riptide: crew animation {origin} did not import")
            continue
        assets.save_asset(f"{CHARACTERS_PATH}/Animations/{name}", only_if_is_dirty=False)
    assets.delete_directory(import_dir)

    # The rifle (a static mesh, like the boat's parts).
    task = unreal.AssetImportTask()
    task.filename = rifle_obj
    task.destination_path = CHARACTERS_PATH
    task.destination_name = "SM_Rifle"
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    rifle_path = f"{CHARACTERS_PATH}/SM_Rifle"
    rifle = unreal.load_asset(rifle_path)
    if rifle:
        materials = rifle.get_editor_property("static_materials")
        for i, slot in enumerate(materials):
            mat = _crew_material_for(str(slot.get_editor_property("material_slot_name")))
            if mat:
                slot.set_editor_property("material_interface", mat)
                materials[i] = slot
        rifle.set_editor_property("static_materials", materials)
        tools = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        nanite = tools.get_nanite_settings(rifle)
        nanite.set_editor_property("enabled", False)
        tools.set_nanite_settings(rifle, nanite, True)
        assets.save_asset(rifle_path, only_if_is_dirty=False)
    for leftover in assets.list_assets(CHARACTERS_PATH, recursive=False):
        # The OBJ importer's placeholder materials.
        if isinstance(unreal.load_asset(leftover), unreal.MaterialInterface):
            assets.delete_asset(leftover.split(".")[0])

    body = unreal.load_asset(marker)
    assets.set_metadata_tag(body, "RiptideVersion", CREW_VERSION)
    assets.save_asset(marker, only_if_is_dirty=False)
    unreal.log("Riptide: crew ready")


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
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    zone = next((a for a in actors if isinstance(a, unreal.WaterZone)), None)
    if zone:
        changed |= _far_sea(zone)
    for actor in actors:
        if isinstance(actor, unreal.WaterBodyOcean):
            # A map built with a smaller sea grows to the current one.
            extents = actor.get_water_body_component().get_editor_property("collision_extents")
            if zone and extents.x < SEA_SIZE / 2.0 - 1.0:
                _open_up_sea(zone, actor)
                unreal.log(f"Riptide: the sea now reaches {SEA_SIZE / 200000.0:.0f} km each way from the start")
                changed = True
            changed |= _cover_waves(actor)
            changed |= _full_waves(actor)
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
    _far_sea(zone)
    _set_swell(ocean)
    _cover_waves(ocean)
    _full_waves(ocean)
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


# Bump when the main menu map's recipe below changes, so every machine rebuilds it on its next launch.
MENU_MAP_VERSION = "6"

# The menu's night: a low moon ahead of the camera (which looks across the boat from its starboard side), laying a
# path of light on the sea behind the boat, so the boat and its crew stand dark against it.
# The menu camera (ARiptideMenuCamera) sets its own exposure and grade; the scene itself (boat, searchlight sweep,
# crew member) is set up by ARiptideMenuGameMode when the map starts.
MOON = {"yaw": 110.0, "pitch": -14.0, "intensity": 0.6, "colour": (0.62, 0.72, 1.0)}


def _night_fog(fog):
    """Thicker volumetric fog than Ocean_Test's, a sea haze at night, so the searchlight's beam stands out in the air."""
    comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    comp.set_editor_property("fog_density", 0.025)
    comp.set_editor_property("fog_height_falloff", 0.25)
    comp.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.004, 0.007, 0.014, 1.0))
    comp.set_editor_property("enable_volumetric_fog", True)
    comp.set_editor_property("volumetric_fog_extinction_scale", 0.35)
    comp.set_editor_property("volumetric_fog_scattering_distribution", 0.75)
    comp.set_editor_property("volumetric_fog_albedo", unreal.Color(r=230, g=235, b=245, a=255))


def build_main_menu_map():
    """MainMenu: the same open sea and swell as Ocean_Test, at night, run by ARiptideMenuGameMode."""
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if assets.does_asset_exist(MENU_MAP_PATH):
        if assets.get_metadata_tag(unreal.load_asset(MENU_MAP_PATH), "RiptideVersion") == MENU_MAP_VERSION:
            return
        assets.delete_asset(MENU_MAP_PATH)

    unreal.log("Riptide: building the main menu map")
    levels.new_level(MENU_MAP_PATH)

    moon = _spawn(unreal.DirectionalLight, (0, 0, 5000), yaw=MOON["yaw"], pitch=MOON["pitch"])
    moon_light = moon.get_component_by_class(unreal.DirectionalLightComponent)
    moon_light.set_editor_property("atmosphere_sun_light", True)
    moon_light.set_editor_property("intensity", MOON["intensity"])
    moon_light.set_editor_property("light_color", unreal.Color(r=int(MOON["colour"][0] * 255), g=int(MOON["colour"][1] * 255),
                                                                   b=int(MOON["colour"][2] * 255), a=255))
    moon_light.set_editor_property("volumetric_scattering_intensity", 0.3)

    _spawn(unreal.SkyAtmosphere)
    sky_light = _spawn(unreal.SkyLight, (0, 0, 1000))
    sky_light.get_component_by_class(unreal.SkyLightComponent).set_editor_property("real_time_capture", True)
    _night_fog(_spawn(unreal.ExponentialHeightFog))

    zone = _spawn(unreal.WaterZone)
    ocean = _spawn_ocean()
    _open_up_sea(zone, ocean)
    _far_sea(zone)
    _set_swell(ocean)
    _cover_waves(ocean)
    _full_waves(ocean)

    # The sea all around, quieter than in the game: under the menu.
    ambience = _spawn(unreal.AmbientSound)
    ambience_audio = ambience.get_component_by_class(unreal.AudioComponent)
    ambience_audio.set_editor_property("sound", unreal.load_asset(f"{AUDIO_PATH}/S_Ocean_Ambience"))
    ambience_audio.set_editor_property("allow_spatialization", False)
    ambience_audio.set_editor_property("volume_multiplier", 0.6)

    # The boat is launched here.
    _spawn(unreal.PlayerStart, (0, 0, 150))

    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.RiptideMenuGameMode)
    levels.save_current_level()
    assets.set_metadata_tag(unreal.load_asset(MENU_MAP_PATH), "RiptideVersion", MENU_MAP_VERSION)
    assets.save_asset(MENU_MAP_PATH, only_if_is_dirty=False)
    unreal.log("Riptide: main menu map ready")


def _running_editor():
    """True when the full editor is open. The project also runs as a standalone game (-game) or server, where this
    script still starts but the editor's tools it uses don't exist (calling them crashes)."""
    args = unreal.SystemLibrary.get_command_line().lower().split()
    return not any(a in ("-game", "-server", "-dedicatedserver") or a.startswith(("-game=", "-server=")) for a in args)


def _set_up_project():
    try:
        import_sounds()
        make_radio_sounds()
        make_fishing_sounds()
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

    # The items' models (riptide_item_models.py), from the C++ item table.
    try:
        import riptide_item_models
        riptide_item_models.make_item_assets()
    except Exception as err:  # noqa: BLE001 - never block the editor from opening
        unreal.log_error(f"Riptide: could not build the item models: {err}")

    # The islands and their test map (riptide_islands.py), using the sea and sky built here.
    try:
        import riptide_islands
        riptide_islands.build(globals())
    except Exception as err:  # noqa: BLE001 - never block the editor from opening
        unreal.log_error(f"Riptide: could not build the islands: {err}")

    # Before the ocean test map, which the editor is left on.
    try:
        build_main_menu_map()
    except Exception as err:  # noqa: BLE001 - never block the editor from opening
        unreal.log_error(f"Riptide: could not build the main menu map: {err}")

    try:
        make_crew_assets()
    except Exception as err:  # noqa: BLE001 - never block the editor from opening
        unreal.log_error(f"Riptide: could not build the crew: {err}")

    try:
        build_ocean_test_map()
    except Exception as err:  # noqa: BLE001 - never block the editor from opening
        unreal.log_error(f"Riptide: could not build ocean test map: {err}")


if _running_editor():
    _set_up_project()
else:
    unreal.log("Riptide: running as a game: the editor-only project setup is skipped")
