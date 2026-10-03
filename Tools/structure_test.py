"""Checks building: a crew member places a campfire kit, builds it up with stone and wood, feeds and lights it,
cooks a fish and boils a canteen on it.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/structure_test.py"

Pictures: Saved/Screenshots/WindowsEditor/structure_*.png. Results: RiptideStructure lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "kit", "since": 0.0, "results": []}


def log(msg):
    unreal.log("RiptideStructure " + msg)


def check(name, ok, detail):
    state["results"].append(ok)
    log(("PASS " if ok else "FAIL ") + name + " (" + detail + ")")


def finish(world=None):
    if world:
        unreal.GameplayStatics.set_global_time_dilation(world, 1.0)
    log("RESULT %s (%d checks)" % ("PASS" if all(state["results"]) else "FAIL", len(state["results"])))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def count(walker, item_id):
    return unreal.RiptideDataLibrary.count_carried(walker, item_id)


def enter(phase, t):
    state["phase"] = phase
    state["since"] = t


def look_at(pc, walker, where, height):
    """Turns the crew member's eyes to a point on the thing."""
    eye = pc.player_camera_manager.get_camera_location()
    to = unreal.Vector(where.x - eye.x, where.y - eye.y, where.z + height - eye.z)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))


def use(walker, what):
    prompt = str(walker.get_interaction().peek_prompt())
    used = walker.get_interaction().use_focused()
    log("%s: prompt '%s', used %s" % (what, prompt, used))
    return prompt, used


def tick(dt):
    state["ticks"] += 1
    world = None
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
        fires = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideStructure)
        fire = fires[0] if fires else None
        if phase == "kit":
            # Level ground, looking inland (towards the cay's middle): the kit goes down two metres ahead.
            # Castaways: the crew start on the sand with no boat.
            start = walker.get_actor_location()
            boats = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBoat)
            under = unreal.RiptideSeaSubsystem.ground_height_at(world, start)
            check("the crew start on the beach with no boat", len(boats) == 0 and under > 0.0 and start.z < under + 200.0,
                  "%d boats, start %s, sand at %.0f" % (len(boats), start, under))
            # Then stand a known way up the wash-up beach, facing inland, for the rest.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            x, y = bx - ox / length * 14.0, by - oy / length * 14.0
            walker.set_actor_location(unreal.Vector(y * 100.0, x * 100.0, (max(island.height(x, y), 0.0) + 1.1) * 100.0), False, True)
            yaw = math.degrees(math.atan2(-ox, -oy))
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
            here = walker.get_actor_location()
            ahead = unreal.Vector(here.x + math.cos(math.radians(yaw)) * 210.0, here.y + math.sin(math.radians(yaw)) * 210.0, 0.0)
            log("walker at %s facing %.0f, ground ahead %.0f" % (here, yaw, unreal.RiptideSeaSubsystem.ground_height_at(world, ahead)))
            walker.give_item("campfire_kit", 1)
            walker.use_item(0, unreal.RiptideDataLibrary.first_carried_uid(walker, "campfire_kit"))
            enter("placed", t)
        elif phase == "placed" and since > 1.0:
            check("the campfire kit placed a campfire", fire is not None and str(fire.get_type()) == "campfire" and count(walker, "campfire_kit") == 0,
                  "%d structures, kits left %d" % (len(fires), count(walker, "campfire_kit")))
            if not fire:
                finish(world)
                return
            check("it starts as the first stage", fire.get_stage() == 0 and not fire.is_finished(), "stage %d" % fire.get_stage())
            walker.give_item("stone", 4)
            look_at(pc, walker, fire.get_actor_location(), 15.0)
            enter("stones", t)
        elif phase == "stones" and since > 0.5:
            prompt, used = use(walker, "stones")
            check("looking at it says what it needs", "Build" in prompt and "tone" in prompt, "prompt '%s'" % prompt)
            check("E puts the stones in", used, "used %s" % used)
            enter("stoned", t)
        elif phase == "stoned" and since > 0.5:
            check("four stones finish the ring", fire.get_stage() == 1 and count(walker, "stone") == 0, "stage %d, stones left %d" % (fire.get_stage(), count(walker, "stone")))
            walker.give_item("driftwood", 3)
            look_at(pc, walker, fire.get_actor_location(), 15.0)
            use(walker, "wood")
            enter("wooded", t)
        elif phase == "wooded" and since > 0.5:
            check("three wood finish the campfire", fire.is_finished() and count(walker, "driftwood") == 0, "finished %s, wood left %d" % (fire.is_finished(), count(walker, "driftwood")))
            unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=structure_1_campfire")
            walker.give_item("driftwood", 2)
            look_at(pc, walker, fire.get_actor_location(), 25.0)
            use(walker, "fuel")
            enter("fuelled", t)
        elif phase == "fuelled" and since > 0.5:
            check("wood on the fire is fuel", fire.get_fuel_seconds() > 60.0 and count(walker, "driftwood") == 1, "fuel %.0f s, wood left %d" % (fire.get_fuel_seconds(), count(walker, "driftwood")))
            walker.give_item("lighter", 1)
            look_at(pc, walker, fire.get_actor_location(), 25.0)
            prompt, used = use(walker, "light")
            check("a lighter offers to light it", "Light" in prompt and used, "prompt '%s', used %s" % (prompt, used))
            enter("lit", t)
        elif phase == "lit" and since > 0.5:
            check("the fire is lit", fire.is_lit(), "lit %s" % fire.is_lit())
            unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=structure_2_lit")
            walker.give_item("raw_fish", 1)
            walker.give_item("canteen_dirty", 1)
            look_at(pc, walker, fire.get_actor_location(), 25.0)
            prompt, used = use(walker, "cook")
            check("raw fish goes on to cook", "Cook" in prompt and used, "prompt '%s', used %s" % (prompt, used))
            use(walker, "boil")
            unreal.GameplayStatics.set_global_time_dilation(world, 10.0)
            enter("cooking", t)
        elif phase == "cooking" and since > 35.0:
            unreal.GameplayStatics.set_global_time_dilation(world, 1.0)
            check("the raw things left the pockets", count(walker, "raw_fish") == 0 and count(walker, "canteen_dirty") == 0,
                  "raw fish %d, dirty canteens %d" % (count(walker, "raw_fish"), count(walker, "canteen_dirty")))
            look_at(pc, walker, fire.get_actor_location(), 25.0)
            prompt, used = use(walker, "take")
            check("the cooked thing can be taken", "Take" in prompt and used, "prompt '%s', used %s" % (prompt, used))
            enter("took", t)
        elif phase == "took" and since > 0.5:
            look_at(pc, walker, fire.get_actor_location(), 25.0)
            use(walker, "take again")
            enter("took2", t)
        elif phase == "took2" and since > 0.5:
            check("the fish is cooked and the water boiled", count(walker, "cooked_fish") == 1 and count(walker, "canteen_clean") == 1,
                  "cooked fish %d, clean canteens %d" % (count(walker, "cooked_fish"), count(walker, "canteen_clean")))
            finish(world)
    except Exception as err:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        check("the run completed", False, str(err))
        finish(world)


state["handle"] = unreal.register_slate_post_tick_callback(tick)
