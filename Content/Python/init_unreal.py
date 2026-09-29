"""Runs automatically when the editor opens. Builds the ocean test map the first time."""

import unreal

MAP_PATH = "/Game/Riptide/Maps/Ocean_Test"


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


# The Water plugin's fluid simulation ripples the ocean surface around the player; boats push their wake into it.
WAKE_SIM_CLASS = "/Water/FluidSimulation/Blueprints/BP_FluidSim_01.BP_FluidSim_01_C"


def _spawn_wake_sim(ocean):
    sim_class = unreal.load_class(None, WAKE_SIM_CLASS)
    if not sim_class:
        unreal.log_warning("Riptide: water fluid simulation not found, boats will leave no wake")
        return
    sim = _spawn(sim_class)
    sim.set_editor_property("WaterBody", ocean)
    sim.set_editor_property("Follow Player ", True)  # the Blueprint's variable name has a trailing space
    # A 40 m patch lets the wake spread out behind the boat. Extra damping fades the wake's side waves
    # before they steepen into dark creases.
    sim.set_editor_property("Simulation World Size", 4096.0)
    sim.set_editor_property("Damping", 0.15)


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
    _spawn_wake_sim(ocean)

    # The boat spawns here and drops onto the water.
    _spawn(unreal.PlayerStart, (0, 0, 150))

    levels.save_current_level()
    levels.load_level(MAP_PATH)
    unreal.log("Riptide: ocean test map ready")


try:
    build_ocean_test_map()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Riptide: could not build ocean test map: {err}")
