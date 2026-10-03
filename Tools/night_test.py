"""Checks the day and night: the sky clock runs; out at night a castaway gets cold, by a shelter they warm up; a
shelter can be slept in after dark (not by day), and with everyone asleep the night passes to morning, hungrier and
rested.

    UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/night_test.py"

Results: RiptideNight lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "start", "since": 0.0, "results": []}


def log(msg):
    unreal.log("RiptideNight " + msg)


def check(name, ok, detail):
    state["results"].append(ok)
    log(("PASS " if ok else "FAIL ") + name + " (" + detail + ")")


def finish(world=None):
    if world:
        unreal.GameplayStatics.set_global_time_dilation(world, 1.0)
    passed = state["results"] and all(state["results"])
    log("RESULT %s (%d checks)" % ("PASS" if passed else "FAIL", len(state["results"])))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def enter(phase, t):
    state["phase"] = phase
    state["since"] = t


def look_at(pc, where, height):
    eye = pc.player_camera_manager.get_camera_location()
    to = unreal.Vector(where.x - eye.x, where.y - eye.y, where.z + height - eye.z)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))


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
        clocks = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideSkyClock)
        if not walkers or t < 2.0:
            return
        walker = walkers[0]
        clock = clocks[0] if clocks else None
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        survival = walker.get_survival()
        since = t - state["since"]
        phase = state["phase"]
        shelters = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideStructure)
        shelter = shelters[0] if shelters else None

        if phase == "start":
            check("the game has a sky clock", clock is not None, str(clock))
            if not clock:
                finish(world)
                return
            state["hours0"] = clock.get_hours()
            # Up the wash-up beach, facing inland, with a lean-to kit placed two metres ahead.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            x, y = bx - ox / length * 14.0, by - oy / length * 14.0
            walker.set_actor_location(unreal.Vector(y * 100.0, x * 100.0, (max(island.height(x, y), 0.0) + 1.1) * 100.0), False, True)
            yaw = math.degrees(math.atan2(-ox, -oy))
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
            walker.give_item("lean_to_kit", 1)
            walker.use_item(0, unreal.RiptideDataLibrary.first_carried_uid(walker, "lean_to_kit"))
            enter("placed", t)
        elif phase == "placed" and since > 3.0:
            check("the clock runs", clock.get_hours() > state["hours0"], "%.3f -> %.3f h" % (state["hours0"], clock.get_hours()))
            check("a lean-to is placed and finished", shelter is not None and shelter.is_finished(), str(shelter))
            if not shelter:
                finish(world)
                return
            # By day it can't be slept in.
            look_at(pc, shelter.get_actor_location(), 60.0)
            state["day_prompt"] = str(walker.get_interaction().peek_prompt())
            walker.get_interaction().use_focused()
            enter("day try", t)
        elif phase == "day try" and since > 0.5:
            check("by day the shelter can't be slept in", not walker.is_sleeping(), "prompt '%s'" % state["day_prompt"])
            # Night, out on the beach 15 m from the shelter: time sped up so the cold shows.
            clock.set_hours(23.0)
            here = shelter.get_actor_location()
            walker.set_actor_location(unreal.Vector(here.x - 1500.0, here.y, here.z + 150.0), False, True)
            state["cold0"] = survival.get_cold()
            unreal.GameplayStatics.set_global_time_dilation(world, 20.0)
            enter("outside", t)
        elif phase == "outside" and since > 60.0:          # game seconds (sped up 20 times)
            cold = survival.get_cold()
            check("out at night away from shelter, it gets cold", clock.is_night() and cold > state["cold0"] + 10.0,
                  "cold %.1f -> %.1f at %.1f h" % (state["cold0"], cold, clock.get_hours()))
            state["cold1"] = cold
            here = shelter.get_actor_location()
            walker.set_actor_location(unreal.Vector(here.x + 120.0, here.y, here.z + 150.0), False, True)
            enter("inside", t)
        elif phase == "inside" and since > 30.0:
            cold = survival.get_cold()
            check("by the shelter it warms up again", cold < state["cold1"] - 5.0, "cold %.1f -> %.1f" % (state["cold1"], cold))
            unreal.GameplayStatics.set_global_time_dilation(world, 1.0)
            clock.set_hours(23.0)
            state["hunger0"] = survival.get_hunger()
            survival.set_vitals(60.0, survival.get_hunger(), survival.get_thirst())
            look_at(pc, shelter.get_actor_location(), 60.0)
            state["night_prompt"] = str(walker.get_interaction().peek_prompt())
            walker.get_interaction().use_focused()
            enter("asleep", t)
        elif phase == "asleep" and since > 2.0:
            hours = clock.get_hours()
            check("at night the shelter is slept in, and with everyone asleep the night passes to morning",
                  6.0 <= hours < 8.0 and not walker.is_sleeping(), "prompt '%s', now %.2f h, sleeping %s" % (state["night_prompt"], hours, walker.is_sleeping()))
            check("a night later: hungrier, and rested", survival.get_hunger() < state["hunger0"] - 5.0 and survival.get_health() > 70.0,
                  "hunger %.1f -> %.1f, health %.1f" % (state["hunger0"], survival.get_hunger(), survival.get_health()))
            finish(world)
        if t > 400.0:
            check("finished in time", False, "stuck at " + phase)
            finish(world)
    except Exception as err:  # noqa: BLE001 - report and stop
        import traceback
        log("ERROR %s" % traceback.format_exc().replace("\n", " | "))
        check("no script error", False, repr(err))
        finish(world)


state["handle"] = unreal.register_slate_post_tick_callback(tick)
