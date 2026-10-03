"""Photographs every palm and scanned model up close on the Props_Review map, so each can be checked before it's
planted on an island. Run it with the editor closed:

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/prop_shots.py"

Pictures go to Saved/Screenshots/WindowsEditor/prop_*.png. Palms get four views each (whole, the crown from the
side, from underneath looking up, and the foot of the trunk); everything else one.
"""
import json
import math
import os

import unreal

REVIEW_MAP = "/Game/Riptide/Maps/Props_Review"


def log(msg):
    unreal.log("RiptideProps " + msg)


layout_file = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Generated", "props_review.json")
with open(layout_file) as f:
    LAYOUT = json.load(f)


def _views():
    views = []
    for item in LAYOUT:
        cx, cy, cz = item["centre"]
        sx, sy, sz = item["size"]
        big = max(sx, sy, sz)
        name = item["name"]
        away = big * 1.25 + 250.0
        views.append((name + "_whole", (cx - away * 0.75, cy - away * 0.66, cz + sz * 0.15 + 120.0), (cx, cy, cz)))
        if "Palm" in name:
            top = sz
            views.append((name + "_crown", (cx - 650.0, cy - 520.0, top - 130.0), (cx + 80.0, cy, top - 150.0)))
            views.append((name + "_from_below", (cx - sx * 0.25 - 260.0, cy - 150.0, 170.0), (cx + sx * 0.3, cy, top - 60.0)))
            views.append((name + "_foot", (cx - sx * 0.5 - 230.0, cy - 190.0, 150.0), (cx - sx * 0.5 + 20.0, cy, 90.0)))
        elif big > 800.0:
            # Big rock: from all four sides and from above, to see which way it faces.
            for label, dx, dy in (("north", 1, 0), ("south", -1, 0), ("east", 0, 1), ("west", 0, -1)):
                views.append((name + "_from_" + label, (cx + dx * away, cy + dy * away, cz + sz * 0.3), (cx, cy, cz)))
            views.append((name + "_from_above", (cx - 10.0, cy, cz + away * 1.2), (cx, cy, cz)))
        elif big < 400.0:
            near = big * 1.6 + 25.0
            views.append((name + "_close", (cx - near * 0.75, cy - near * 0.66, cz + near * 0.45), (cx, cy, cz)))
    return views


VIEWS = _views()
state = {"ticks": 0, "handle": None, "view": 0, "placed": False, "since": 0.0}


def look(a, b):
    v = unreal.Vector(b[0] - a[0], b[1] - a[1], b[2] - a[2])
    return unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(v.z, math.hypot(v.x, v.y))), yaw=math.degrees(math.atan2(v.y, v.x)))


def finish():
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    state["ticks"] += 1
    try:
        if state["ticks"] == 20:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(REVIEW_MAP)
            return
        if state["ticks"] == 60:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            if state["ticks"] > 3000:
                log("FAIL the game never started")
                finish()
            return
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        pawn = pc.get_controlled_pawn() if pc else None
        if not pawn:
            return
        t = unreal.GameplayStatics.get_time_seconds(world)
        if t < 3.0:
            return
        if state["view"] >= len(VIEWS):
            log("RESULT %d pictures" % len(VIEWS))
            finish()
            return
        name, where, target = VIEWS[state["view"]]
        if not state["placed"]:
            rot = look(where, target)
            pawn.set_actor_location(unreal.Vector(*where), False, True)
            pc.set_control_rotation(rot)
            state["placed"], state["since"] = True, t
        elif t - state["since"] > 1.2:
            unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=prop_%s" % name)
            state["view"] += 1
            state["placed"] = False
    except Exception as err:  # noqa: BLE001 - always quit, so a broken run doesn't leave the editor open
        log("FAIL error %r" % err)
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
