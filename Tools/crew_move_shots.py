"""Photographs a crew member moving on the cay: the first-person view looking down at their own body, and from the
side while sprinting, crouching, punching and reaching. Checks the animation states and the body's eye height.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/crew_move_shots.py"

Pictures: Saved/Screenshots/WindowsEditor/move_*.png. Results: RiptideMove lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "open", "since": 0.0, "results": [], "shots": 0}
SHOTS = [
    # (name, seconds into the phase, what the crew member does, where the camera is relative to them (cm) or "fp")
    ("01_first_person_down", 1.0, "stand_down", "fp"),
    ("02_first_person_ahead", 2.0, "stand", "fp"),
    ("03_walking_side", 4.0, "walk", (0.0, -320.0, 110.0)),
    ("04_sprinting_side", 7.0, "sprint", (0.0, -320.0, 110.0)),
    ("05_crouching_side", 10.0, "crouch", (0.0, -300.0, 80.0)),
    ("06_punching_front", 12.5, "punch", (260.0, 40.0, 120.0)),
    ("07_reaching_front", 14.5, "reach", (260.0, -40.0, 120.0)),
    ("08_crouch_first_person_down", 16.5, "crouch_down", "fp"),
]


def log(msg):
    unreal.log("RiptideMove " + msg)


def check(name, ok, detail):
    state["results"].append(ok)
    log(("PASS " if ok else "FAIL ") + name + " (" + detail + ")")


def ue(x, y, z):
    return unreal.Vector(y * 100.0, x * 100.0, z * 100.0)


def finish():
    log("RESULT %s (%d checks)" % ("PASS" if all(state["results"]) else "FAIL", len(state["results"])))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    state["ticks"] += 1
    try:
        if state["ticks"] == 30:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Riptide/Maps/Island_Test")
        if state["ticks"] == 150:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            if state["ticks"] > 4000:
                check("the game started", False, "never")
                finish()
            return
        t = unreal.GameplayStatics.get_time_seconds(world)
        walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
        cams = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideDevCamera)
        if not walkers or t < 2.0:
            return
        walker = walkers[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        if state["phase"] == "open":
            # Put down on the dry upper beach, facing along it.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            ox, oy = ox / length, oy / length
            x, y = bx - ox * 14.0, by - oy * 14.0
            walker.set_actor_location(ue(x, y, max(island.height(x, y), 0.0) + 1.1), False, True)
            state["face"] = math.degrees(math.atan2(-ox, -oy))     # along the beach (Unreal yaw of the design's -out turned 90)
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(math.atan2(-oy, -ox)) + 90.0))
            state["phase"] = "go"
            state["since"] = t
            return
        since = t - state["since"]
        if state["shots"] >= len(SHOTS):
            if since > SHOTS[-1][1] + 1.0:
                finish()
            return
        name, at, action, cam = SHOTS[state["shots"]]
        if pc.is_flying() and since < at - 0.05:
            pc.set_flying(False)      # back in the body for the run-up to the next shot
        yaw = pc.get_control_rotation().yaw
        forward = unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0.0)
        # What the crew member is doing in the run-up to this shot.
        if action in ("walk", "sprint"):
            walker.set_sprinting(action == "sprint")
            walker.add_movement_input(forward, 1.0)
        else:
            walker.set_sprinting(False)
        if action in ("crouch", "crouch_down") and not walker.get_editor_property("is_crouched") if hasattr(walker, "get_editor_property") else False:
            walker.crouch(False)
        if action not in ("crouch", "crouch_down") and since > 0.2:
            try:
                if walker.get_editor_property("is_crouched"):
                    walker.un_crouch(False)
            except Exception:  # noqa: BLE001
                pass
        if action == "punch" and since > at - 0.35 and "punched" not in state:
            walker.punch()
            state["punched"] = True
        if action == "reach" and since > at - 0.35 and "reached" not in state:
            walker.start_action(unreal.RiptideCrewAction.REACH)
            state["reached"] = True
        pitch = -75.0 if action in ("stand_down", "crouch_down") else 0.0
        pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))
        if since >= at:
            if cam == "fp":
                if pc.is_flying():
                    pc.set_flying(False)
                pc.set_view_target_with_blend(walker)
            else:
                # From the side: the dev mode's fly camera, placed beside the crew member (who keeps moving without us).
                here = walker.get_actor_location()
                right = unreal.Vector(-forward.y, forward.x, 0.0)
                where = here + forward * cam[0] + right * cam[1] + unreal.Vector(0.0, 0.0, cam[2] - 60.0)
                to = here + unreal.Vector(0.0, 0.0, 20.0) - where
                pc.set_flying(True)
                camera = pc.get_dev_camera()
                camera.set_actor_location_and_rotation(where, unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))),
                                                                              yaw=math.degrees(math.atan2(to.y, to.x))), False, True)
                camera.set_mode(unreal.RiptideDevCameraMode.FREE)
            unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=move_%s" % name)
            body = walker.get_crew_body()
            anim = str(body.get_anim_state()).split(".")[-1] if body else "?"
            speed = walker.get_velocity().length()
            log("shot %s: action %s, anim %s, speed %.0f cm/s, sprinting %s, crouched %s" % (
                name, action, anim, speed, walker.is_sprinting(), walker.get_editor_property("is_crouched")))
            if action == "sprint":
                check("sprinting is faster than walking", speed > 450.0, "%.0f cm/s" % speed)
            if action == "walk":
                check("walking is a walk", 250.0 < speed < 420.0, "%.0f cm/s" % speed)
            if action == "crouch":
                check("crouched when asked", bool(walker.get_editor_property("is_crouched")), "crouched")
            if action == "punch":
                check("the punch is under way", str(walker.get_action()).split(".")[-1].startswith("PUNCH"), str(walker.get_action()))
            if action == "stand_down":
                cam_z = pc.player_camera_manager.get_camera_location().z
                feet = walker.get_actor_location().z - walker.get_editor_property("capsule_component").get_scaled_capsule_half_height()
                check("the eyes are at head height", 140.0 < cam_z - feet < 180.0, "%.0f cm above the feet" % (cam_z - feet))
            state["shots"] += 1
    except Exception as err:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        check("the run completed", False, str(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
