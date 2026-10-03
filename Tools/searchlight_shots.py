"""Pictures of the boat's searchlight at night on Ocean_Test: full, dimmed, and the lamp seen from in front.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/searchlight_shots.py"

Pictures: Saved/Screenshots/WindowsEditor/light_*.png. Lines: RiptideLight in Saved/Logs/Riptide.log.
"""
import math

import unreal

state = {"ticks": 0, "handle": None, "phase": "open", "since": 0.0, "shot": 0, "placed": False}

# (name, searchlight level, camera in the boat's frame (forward, left, up) cm, looked-at point in the boat's frame)
SHOTS = [
    ("full_from_astern", 2, (-900.0, 250.0, 420.0), (1500.0, 0.0, 0.0)),
    ("full_beam_side", 2, (300.0, 900.0, 250.0), (900.0, 0.0, 20.0)),
    ("dim_from_astern", 1, (-900.0, 250.0, 420.0), (1500.0, 0.0, 0.0)),
    ("lamp_from_ahead", 2, (700.0, 120.0, 300.0), (26.0, -40.0, 290.0)),
    ("off_from_astern", 0, (-900.0, 250.0, 420.0), (1500.0, 0.0, 0.0)),
]


def log(msg):
    unreal.log("RiptideLight " + msg)


def finish():
    log("RESULT done (%d shots)" % state["shot"])
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    state["ticks"] += 1
    try:
        if state["ticks"] == 30:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Riptide/Maps/Ocean_Test")
        if state["ticks"] == 150:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            if state["ticks"] > 4000:
                log("FAIL the game never started")
                finish()
            return
        t = unreal.GameplayStatics.get_time_seconds(world)
        boats = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBoat)
        if not boats or t < 3.0:
            return
        boat = boats[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        since = t - state["since"]
        if state["phase"] == "open":
            pc.set_time_of_day(unreal.RiptideTimeOfDay.NIGHT)
            try:
                boat.aim_searchlight(0.0, -6.0)
            except Exception as err:  # noqa: BLE001
                log("aim not scriptable: %s" % err)
            pc.set_photo_mode(True)
            pc.set_flying(True)
            state["phase"] = "shots"
            state["since"] = t
        elif state["phase"] == "shots":
            if state["shot"] >= len(SHOTS):
                finish()
                return
            name, level, offset, target = SHOTS[state["shot"]]
            xf = boat.get_actor_transform()

            def frame(o):
                return xf.transform_location(unreal.Vector(o[0], -o[1], o[2]))
            if not state["placed"]:
                boat.set_searchlight_level(level)
                log("%s: level %d, lit %s" % (name, boat.get_searchlight_level(), boat.is_searchlight_on()))
                where, aim = frame(offset), frame(target)
                v = unreal.Vector(aim.x - where.x, aim.y - where.y, aim.z - where.z)
                rot = unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(v.z, math.hypot(v.x, v.y))), yaw=math.degrees(math.atan2(v.y, v.x)))
                cam = pc.get_dev_camera()
                cam.set_actor_location_and_rotation(where, rot, False, True)
                cam.set_mode(unreal.RiptideDevCameraMode.FREE)
                state["placed"] = True
                state["since"] = t
            elif since > 2.5:
                unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=light_%s" % name)
                log("shot " + name)
                state["shot"] += 1
                state["placed"] = False
    except Exception:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
