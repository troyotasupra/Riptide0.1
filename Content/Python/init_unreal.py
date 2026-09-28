"""Runs automatically when the editor opens. Builds the ocean test map the first time."""

import unreal

MAP_PATH = "/Game/Deadwater/Maps/Ocean_Test"


def _spawn(actor_class, location=(0.0, 0.0, 0.0), yaw=0.0, pitch=0.0):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return actors.spawn_actor_from_class(
        actor_class,
        unreal.Vector(*location),
        unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw),
    )


def _spawn_ocean():
    # Placing through the actor factory gives the ocean its default shoreline spline and mesh,
    # the same as dragging it in from the Place Actors panel.
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    try:
        ocean = actors.spawn_actor_from_object(unreal.WaterBodyOcean.static_class(), unreal.Vector(0, 0, 0))
        if ocean:
            return ocean
    except Exception as err:  # noqa: BLE001 - fall back to a plain spawn
        unreal.log_warning(f"Deadwater: ocean factory spawn failed ({err}), using plain spawn")
    return _spawn(unreal.WaterBodyOcean)


def build_ocean_test_map():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        return

    unreal.log("Deadwater: building ocean test map")
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

    _spawn(unreal.WaterZone)
    _spawn_ocean()

    # The boat spawns here and drops onto the water.
    _spawn(unreal.PlayerStart, (0, 0, 150))

    levels.save_current_level()
    levels.load_level(MAP_PATH)
    unreal.log("Deadwater: ocean test map ready")


try:
    build_ocean_test_map()
except Exception as err:  # noqa: BLE001 - never block the editor from opening
    unreal.log_error(f"Deadwater: could not build ocean test map: {err}")
