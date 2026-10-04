"""Checks dropping and picking things up on the cay: a crew member is given coconuts, drops them (they land as a
coconut on the sand), looks at them and takes them back; then several things dropped together make a bag.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/pickup_test.py"

Pictures: Saved/Screenshots/WindowsEditor/pickup_*.png. Results: RiptidePickup lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "open", "since": 0.0, "results": []}


def log(msg):
    unreal.log("RiptidePickup " + msg)


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


def count_items(walker, item_id):
    # Through the data library: it reads the character's grids on any machine.
    return unreal.RiptideDataLibrary.count_carried(walker, item_id)


def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=pickup_%s" % name)


def enter(phase, t):
    state["phase"] = phase
    state["since"] = t


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
        if not walkers or t < 2.0:
            return
        walker = walkers[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        since = t - state["since"]
        phase = state["phase"]
        if phase == "open":
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            x, y = bx - ox / length * 12.0, by - oy / length * 12.0
            walker.set_actor_location(ue(x, y, max(island.height(x, y), 0.0) + 1.1), False, True)
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(math.atan2(-oy, -ox)) + 90.0))
            left = walker.give_item("coconut", 3)
            check("three coconuts go into the pockets", left == 0 and count_items(walker, "coconut") == 3, "%d left over, carrying %d" % (left, count_items(walker, "coconut")))
            enter("drop", t)
        elif phase == "drop" and since > 0.5:
            uid = unreal.RiptideDataLibrary.first_carried_uid(walker, "coconut")
            walker.drop_item(0, uid, 0)
            enter("dropped", t)
        elif phase == "dropped" and since > 3.5:
            items = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideWorldItem)
            check("the coconuts lie on the ground as coconuts", len(items) == 1 and str(items[0].get_item_id()) == "coconut" and items[0].get_count() == 3,
                  "%d things on the ground: %s" % (len(items), ", ".join("%s x%d" % (i.get_item_id(), i.get_count()) for i in items)))
            check("nothing is left in the pockets", count_items(walker, "coconut") == 0, "carrying %d" % count_items(walker, "coconut"))
            if items:
                here = items[0].get_actor_location()
                ground = unreal.RiptideSeaSubsystem.ground_height_at(world, here)
                check("they rest on the sand, not in it or above it", -12.0 < here.z - ground < 25.0, "%.0f cm above the ground" % (here.z - ground))
                # Look at them.
                eye = pc.player_camera_manager.get_camera_location()
                to = here - eye
                pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))
            enter("look", t)
        elif phase == "look" and since > 1.0:
            shot(world, "01_coconuts_on_the_sand")
            prompt = walker.get_interaction().get_focused_prompt()
            check("looking at them offers to take them", "Take" in str(prompt) and "oconut" in str(prompt), "prompt: '%s'" % prompt)
            used = walker.get_interaction().use_focused()
            check("E takes them", used, "used %s" % used)
            enter("took", t)
        elif phase == "took" and since > 1.0:
            items = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideWorldItem)
            check("the coconuts are back in the pockets and gone from the ground", count_items(walker, "coconut") == 3 and len(items) == 0,
                  "carrying %d, %d on the ground" % (count_items(walker, "coconut"), len(items)))
            # Several different things dropped on one spot become a bag.
            walker.give_item("rope", 2)
            walker.give_item("stone", 1)
            for item_id in ("coconut", "rope", "stone"):
                uid = unreal.RiptideDataLibrary.first_carried_uid(walker, item_id)
                if uid > 0:
                    walker.drop_item(0, uid, 0)
            enter("bagged", t)
        elif phase == "bagged" and since > 3.5:
            items = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideWorldItem)
            bags = [i for i in items if i.is_bag()]
            check("three things dropped together make one bag", len(items) >= 1 and len(bags) >= 1, "%d things, %d bags" % (len(items), len(bags)))
            if items:
                here = items[0].get_actor_location()
                eye = pc.player_camera_manager.get_camera_location()
                to = here - eye
                pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))
            enter("bag_look", t)
        elif phase == "bag_look" and since > 1.0:
            shot(world, "02_bag_on_the_sand")
            prompt = walker.get_interaction().get_focused_prompt()
            log("bag prompt: '%s'" % prompt)
            enter("done", t)
        elif phase == "done" and since > 1.0:
            finish()
    except Exception as err:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        check("the run completed", False, str(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
