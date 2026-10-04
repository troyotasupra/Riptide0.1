"""Checks saving and continuing: on the cay a castaway builds a lean-to, drops a stone, picks one up off the beach
and carries a coconut; the world is saved, then hosted again with ?Continue. Everything has to come back: the day and
hour, the lean-to, the stone on the ground, the gap where the stone was taken, and the castaway where they stood,
in the same condition, carrying the same things (the coconut no nearer going off than when it was saved).

    UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/save_test.py"

Playing in the editor saves to its own slot (WorldEditor), never the game's. Results: RiptideSave lines in
Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "start", "since": 0.0, "results": [], "travel_t": 0.0}
CARRIED = ("stone", "coconut", "fiber")


def log(msg):
    unreal.log("RiptideSave " + msg)


def check(name, ok, detail):
    state["results"].append(ok)
    log(("PASS " if ok else "FAIL ") + name + " (" + detail + ")")


def finish():
    passed = state["results"] and all(state["results"])
    log("RESULT %s (%d checks)" % ("PASS" if passed else "FAIL", len(state["results"])))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def enter(phase, t):
    state["phase"] = phase
    state["since"] = t


def look_at(pc, where):
    eye = pc.player_camera_manager.get_camera_location()
    to = unreal.Vector(where.x - eye.x, where.y - eye.y, where.z - eye.z)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))


def counts(walker):
    return {item: unreal.RiptideDataLibrary.count_carried(walker, item) for item in CARRIED}


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
        if state["phase"] == "travelling" and t >= state["travel_t"]:
            return          # still the first world, on its way out (the new one's clock starts again from 0)
        walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
        if not walkers or t < 2.5:
            return
        walker = walkers[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        mode = unreal.GameplayStatics.get_game_mode(world)
        clock = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideSkyClock)[0]
        props = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideIslandProps)[0]
        survival = walker.get_survival()
        since = t - state["since"]
        phase = state["phase"]

        if phase == "start":
            check("a new game isn't a continued one", not mode.is_continued(), "continued %s" % mode.is_continued())
            # Up the beach, facing inland, with a lean-to kit placed ahead and things in the pockets.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            x, y = bx - ox / length * 14.0, by - oy / length * 14.0
            walker.set_actor_location(unreal.Vector(y * 100.0, x * 100.0, (max(island.height(x, y), 0.0) + 1.1) * 100.0), False, True)
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=math.degrees(math.atan2(-ox, -oy))))
            walker.give_item("lean_to_kit", 1)
            walker.give_item("stone", 3)
            walker.give_item("coconut", 2)
            walker.give_item("fiber", 4)
            walker.use_item(0, unreal.RiptideDataLibrary.first_carried_uid(walker, "lean_to_kit"))
            enter("placed", t)
        elif phase == "placed" and since > 1.5:
            # One stone dropped on the sand; then off to pick another off the beach.
            walker.drop_item(0, unreal.RiptideDataLibrary.first_carried_uid(walker, "stone"), 1)
            found = props.find_nearest("stone", walker.get_actor_location(), 1.0e7)
            where = found[1] if isinstance(found, tuple) else found
            ground = unreal.RiptideSeaSubsystem.ground_height_at(world, where)
            away = unreal.Vector(where.x - walker.get_actor_location().x, where.y - walker.get_actor_location().y, 0.0).normal()
            stand = unreal.Vector(where.x - away.x * 170.0, where.y - away.y * 170.0, 0.0)
            stand.z = unreal.RiptideSeaSubsystem.ground_height_at(world, stand) + 100.0
            state["stone_aim"] = unreal.Vector(where.x, where.y, ground + 12.0)
            state["stone_at"] = stand
            enter("to stone", t)
        elif phase == "to stone" and since > 2.0:      # the dropped stone settles first
            walker.set_actor_location(state["stone_at"], False, True)
            enter("look stone", t)
        elif phase == "look stone":
            look_at(pc, state["stone_aim"])
            if since > 0.8:
                check("a stone is picked up off the beach", walker.get_interaction().use_focused(), "prompt '%s'" % walker.get_interaction().get_focused_prompt())
                enter("taken", t)
        elif phase == "taken" and since > 1.5:
            # Late afternoon of the third day, a little worn, then saved.
            clock.set_day(3)
            clock.set_hours(16.5)
            survival.set_vitals(63.0, 55.0, 44.0)
            leantos = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideStructure)
            items = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideWorldItem)
            saved = {
                "day": clock.get_day(), "hours": clock.get_hours(), "where": walker.get_actor_location(), "yaw": pc.get_control_rotation().yaw,
                "health": survival.get_health(), "hunger": survival.get_hunger(), "thirst": survival.get_thirst(),
                "carried": counts(walker), "spoils": unreal.RiptideDataLibrary.carried_spoils_in(walker, "coconut"),
                "structures": [(str(s.get_type()), s.get_stage(), s.is_finished()) for s in leantos],
                "items": sorted(str(i.get_item_id()) for i in items), "depleted": props.get_depleted_count(),
            }
            state["saved"] = saved
            log("saving: %r" % saved)
            check("there's something to save", len(leantos) == 1 and leantos[0].is_finished() and "stone" in saved["items"] and saved["depleted"] >= 1
                  and saved["carried"]["coconut"] == 2 and saved["spoils"] > 0, "%r" % saved)
            check("the world saves", mode.save_world(), "save_world")
            state["travel_t"] = t
            # Hosted again, continuing (as the host screen's Continue does).
            unreal.GameplayStatics.open_level(world, "/Game/Riptide/Maps/Island_Test", True, "Continue")
            enter("travelling", t)
        elif phase == "travelling":
            enter("continued", t)
        elif phase == "continued" and since > 1.5:
            saved = state["saved"]
            check("the game continued the saved world", mode.is_continued(), "continued %s" % mode.is_continued())
            check("the same day and hour", clock.get_day() == saved["day"] and abs(clock.get_hours() - saved["hours"]) < 0.1,
                  "day %d %.2f h, saved day %d %.2f h" % (clock.get_day(), clock.get_hours(), saved["day"], saved["hours"]))
            leantos = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideStructure)
            built = [(str(s.get_type()), s.get_stage(), s.is_finished()) for s in leantos]
            check("the lean-to is standing where it was built", built == saved["structures"], "%r, saved %r" % (built, saved["structures"]))
            items = sorted(str(i.get_item_id()) for i in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideWorldItem))
            check("the dropped stone is still lying there", items == saved["items"], "%r, saved %r" % (items, saved["items"]))
            check("what was harvested is still gone", props.get_depleted_count() == saved["depleted"], "%d, saved %d" % (props.get_depleted_count(), saved["depleted"]))
            where = walker.get_actor_location()
            moved = math.hypot(where.x - saved["where"].x, where.y - saved["where"].y)
            check("the castaway is back where they stood", moved < 30.0 and abs(where.z - saved["where"].z) < 60.0,
                  "%.0f cm away (%s, saved %s)" % (moved, where, saved["where"]))
            check("in the same condition", abs(survival.get_health() - saved["health"]) < 2.0 and abs(survival.get_hunger() - saved["hunger"]) < 2.0
                  and abs(survival.get_thirst() - saved["thirst"]) < 2.0,
                  "health %.1f hunger %.1f thirst %.1f, saved %.1f %.1f %.1f" % (survival.get_health(), survival.get_hunger(), survival.get_thirst(),
                                                                               saved["health"], saved["hunger"], saved["thirst"]))
            carried = counts(walker)
            check("carrying the same things", carried == saved["carried"], "%r, saved %r" % (carried, saved["carried"]))
            spoils = unreal.RiptideDataLibrary.carried_spoils_in(walker, "coconut")
            check("the coconut carries on spoiling from where it was", 0.0 < spoils <= saved["spoils"] and spoils > saved["spoils"] - 30.0,
                  "%.0f s left, saved %.0f s" % (spoils, saved["spoils"]))
            finish()
        if state["ticks"] > 20000:
            check("finished in time", False, "stuck at " + phase)
            finish()
    except Exception as err:  # noqa: BLE001 - report and stop
        import traceback
        log("ERROR %s" % traceback.format_exc().replace("\n", " | "))
        check("no script error", False, repr(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
