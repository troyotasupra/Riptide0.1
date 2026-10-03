"""Pictures of a crew member's gear up close on the cay (the boots, first), and a note of the microphones found.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/crew_gear_shots.py"

Pictures: Saved/Screenshots/WindowsEditor/gear_*.png. Lines: RiptideGear in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "open", "since": 0.0, "shot": 0, "placed": False}

# Looks (RiptideAppearance choices): Body, Skin, Hair, HairColour, Beard, Headgear, Face, Camo, Vest, GearColour, Pack, Gloves
LOOK = [0, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0]

# (name, camera offset from the mannequin's feet (right, forward, up) cm, what it looks at (same frame))
SHOTS = [
    ("boots_front", (0.0, 120.0, 38.0), (0.0, 0.0, 16.0)),
    ("boots_side", (115.0, 10.0, 30.0), (0.0, 0.0, 14.0)),
    ("boots_quarter", (85.0, 85.0, 45.0), (0.0, 0.0, 15.0)),
    ("boots_back", (30.0, -110.0, 40.0), (0.0, 0.0, 16.0)),
    ("full_body", (160.0, 260.0, 120.0), (0.0, 0.0, 95.0)),
]


def log(msg):
    unreal.log("RiptideGear " + msg)


def finish():
    log("RESULT done (%d shots)" % state["shot"])
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    state["ticks"] += 1
    try:
        if state["ticks"] == 30:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Riptide/Maps/Island_Test")
        if state["ticks"] == 120:
            # A mannequin on the wash-up beach, a little way up the sand, facing the sea.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            d = math.hypot(ox, oy)
            x, y = bx - ox / d * 9.0, by - oy / d * 9.0
            at = unreal.Vector(y * 100.0, x * 100.0, max(island.height(x, y), 0.0) * 100.0 + 1.0)
            actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
            mannequin = actors.spawn_actor_from_class(unreal.RiptideCrewMannequin, at, unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(math.atan2(ox, oy))))
            mannequin.set_actor_label("GearMannequin")
        if state["ticks"] == 150:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            if state["ticks"] > 4000:
                log("FAIL the game never started")
                finish()
            return
        t = unreal.GameplayStatics.get_time_seconds(world)
        walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
        if not walkers or t < 2.0:
            return
        walker = walkers[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        since = t - state["since"]
        if state["phase"] == "open":
            mics = [str(m) for m in unreal.RiptideVoiceComponent.list_microphones()]
            log("microphones: %s" % (", ".join(mics) if mics else "none found"))
            # The mannequin was stood on the beach before play began (below); it is made up here.
            mannequins = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCrewMannequin)
            if not mannequins:
                log("FAIL no mannequin in the play world")
                finish()
                return
            mannequin = mannequins[0]
            at = mannequin.get_actor_location()
            yaw = mannequin.get_actor_rotation().yaw
            look = unreal.RiptideAppearance()
            look.set_editor_property("choices", LOOK)
            mannequin.set_appearance(look)
            state["mannequin"] = mannequin
            state["feet"] = at
            state["yaw"] = yaw + 180.0
            pc.set_photo_mode(True)
            pc.set_flying(True)
            state["phase"] = "shots"
            state["since"] = t
        elif state["phase"] == "shots":
            if state["shot"] >= len(SHOTS):
                finish()
                return
            name, offset, target = SHOTS[state["shot"]]
            feet, yaw = state["feet"], math.radians(state["yaw"])
            fwd = unreal.Vector(math.cos(yaw), math.sin(yaw), 0.0)
            right = unreal.Vector(math.sin(yaw), -math.cos(yaw), 0.0)

            def frame(o):
                return unreal.Vector(feet.x + right.x * o[0] + fwd.x * o[1], feet.y + right.y * o[0] + fwd.y * o[1], feet.z + o[2])
            if not state["placed"]:
                where, aim = frame(offset), frame(target)
                v = unreal.Vector(aim.x - where.x, aim.y - where.y, aim.z - where.z)
                rot = unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(v.z, math.hypot(v.x, v.y))), yaw=math.degrees(math.atan2(v.y, v.x)))
                cam = pc.get_dev_camera()
                cam.set_actor_location_and_rotation(where, rot, False, True)
                cam.set_mode(unreal.RiptideDevCameraMode.FREE)
                state["placed"] = True
                state["since"] = t
            elif since > 1.5:
                unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=gear_%s" % name)
                log("shot " + name)
                state["shot"] += 1
                state["placed"] = False
    except Exception:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
