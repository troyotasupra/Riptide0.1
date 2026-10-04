"""Checks the chart on the cay: what's round the castaway is charted from the start and the far sea isn't; walking
inland to the pool charts it too; M holds the chart up (a picture of it); reading a sea chart puts the island on the
compass with its bearing (a picture of that).

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/chart_test.py"

(-nullrhi runs the checks without the pictures.) Pictures: Saved/Screenshots/WindowsEditor (the latest two
ScreenShot*.png: "shot showui" names them itself). Results: RiptideChart lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "start", "since": 0.0, "results": []}


def log(msg):
    unreal.log("RiptideChart " + msg)


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
        charts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideChart)
        if not walkers or t < 3.0:
            return
        walker = walkers[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        chart = charts[0] if charts else None
        since = t - state["since"]
        phase = state["phase"]

        if phase == "start":
            check("the game has a chart", chart is not None, str(chart))
            if not chart:
                finish()
                return
            here = walker.get_actor_location()
            far = here + unreal.Vector(60000.0, 0.0, 0.0)
            check("where the castaway washed up is charted, the far sea isn't", chart.is_seen_at(here) and not chart.is_seen_at(far),
                  "%d squares seen" % chart.get_seen_count())
            state["seen0"] = chart.get_seen_count()
            # Inland, to the pool.
            x, y, z, _ = island.pool()
            state["pool"] = unreal.Vector(y * 100.0, x * 100.0, z * 100.0)
            check("the pool wasn't charted yet", not chart.is_seen_at(state["pool"]) or state["pool"].distance(here) < 7000.0, "")
            walker.set_actor_location(unreal.Vector(state["pool"].x - 400.0, state["pool"].y, (island.height(x, y - 4.0) + 1.1) * 100.0), False, True)
            enter("inland", t)
        elif phase == "inland" and since > 1.5:
            check("walking to the pool charts it", chart.is_seen_at(state["pool"]) and chart.get_seen_count() > state["seen0"],
                  "%d -> %d squares" % (state["seen0"], chart.get_seen_count()))
            walker.open_chart()
            enter("open", t)
        elif phase == "open" and since > 6.0:
            check("M holds the chart up", walker.is_chart_open(), "open %s" % walker.is_chart_open())
            unreal.SystemLibrary.execute_console_command(world, "shot showui")
            enter("read", t)
        elif phase == "read" and since > 1.0:
            walker.give_item("sea_chart", 1)
            uid = unreal.RiptideDataLibrary.first_carried_uid(walker, "sea_chart")
            walker.use_item(0, uid)
            walker.use_item(1, uid)
            enter("readout", t)
        elif phase == "readout" and since > 1.0:
            check("reading the sea chart puts the islands on the compass", chart.is_read(), "read %s" % chart.is_read())
            check("and the chart is kept", unreal.RiptideDataLibrary.count_carried(walker, "sea_chart") == 1, "")
            unreal.SystemLibrary.execute_console_command(world, "shot showui")
            enter("close", t)
        elif phase == "close" and since > 1.0:
            walker.close_chart()
            check("M again puts it away", not walker.is_chart_open(), "open %s" % walker.is_chart_open())
            finish()
        if t > 200.0:
            check("finished in time", False, "stuck at " + phase)
            finish()
    except Exception as err:  # noqa: BLE001 - report and stop
        import traceback
        log("ERROR %s" % traceback.format_exc().replace("\n", " | "))
        check("no script error", False, repr(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
