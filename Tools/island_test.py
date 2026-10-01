"""Headless check that the islands load, the boat starts off the start island, and digging works, on Archipelago_Test.

Run it with the editor closed (it quits the editor when done):
  UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/island_test.py"
Then read the "RiptideIslandTest:" lines in Saved/Logs/Riptide.log. The last one says PASS or FAIL.
Use -RenderOffscreen instead of -nullrhi to also save screenshots of the start island and report frame rate.

The island maths itself is tested outside the engine by Tools/worldgen_test.cpp; this checks it's wired up in the game.
"""

import unreal

MAP_PATH = "/Game/Riptide/Maps/Archipelago_Test"
RENDERING = "-nullrhi" not in unreal.SystemLibrary.get_command_line().lower()

# Seconds of play to wait for the start island to finish building before failing.
BUILD_TIMEOUT = 20.0
# Seconds allowed for dug sand to stop sliding.
SETTLE_TIMEOUT = 10.0

state = {"ticks": 0, "stage": "load", "handle": None, "checks": [], "t0": 0.0, "frame_times": [], "shots": 0}


def log(msg):
    unreal.log(f"RiptideIslandTest: {msg}")


def check(name, passed, detail=""):
    state["checks"].append((name, bool(passed), detail))
    log("%s  %s  %s" % ("ok  " if passed else "FAIL", name, detail))


def finish():
    ok = bool(state["checks"]) and all(passed for _, passed, _ in state["checks"])
    if state["frame_times"]:
        ft = sorted(state["frame_times"])
        avg = sum(ft) / len(ft)
        slow = ft[int(len(ft) * 0.99) - 1]
        log("frame rate: average %.0f fps, 1%% low %.0f fps (editor, includes editor overhead)" % (1.0 / avg, 1.0 / slow))
    log("RESULT %s" % ("PASS" if ok else "FAIL"))
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def screenshot(world, name):
    # Queued for the next frame; calling the screenshot API directly re-enters this tick callback.
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=RiptideIslandTest_%s" % name)
    log("screenshot %s" % name)


def find_beach(island, boat):
    """Walks from the boat toward the island until the ground is a metre or more above the sea."""
    start = boat.get_actor_location()
    forward = boat.get_actor_forward_vector()
    for step in range(0, 600):
        point = unreal.Vector(start.x + forward.x * step * 50.0, start.y + forward.y * step * 50.0, 0.0)
        height = island.get_ground_height(point)
        if height > 100.0:
            # A few metres further up the beach.
            return unreal.Vector(point.x + forward.x * 300.0, point.y + forward.y * 300.0, island.get_ground_height(point))
    return None


def tick(dt):
    if state.get("in_tick"):
        return
    state["in_tick"] = True
    try:
        _tick(dt)
    except Exception as err:  # noqa: BLE001 - report and quit instead of hanging the editor
        log("script error: %r" % err)
        check("test ran without errors", False, repr(err))
        finish()
    finally:
        state["in_tick"] = False


def _tick(dt):
    state["ticks"] += 1
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if state["stage"] == "load":
        if state["ticks"] == 30:
            if not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
                check("Archipelago_Test map exists (built by init_unreal.py on first launch)", False)
                finish()
                return
            levels.load_level(MAP_PATH)
        if state["ticks"] == 60:
            log("starting play-in-editor")
            levels.editor_request_begin_play()
            state["stage"] = "play"
        return

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        if state["ticks"] > 3000:
            check("play-in-editor started", False)
            finish()
        return
    t = unreal.GameplayStatics.get_time_seconds(world)
    if RENDERING and t > 2.0:
        state["frame_times"].append(dt)

    directors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideWorldDirector)
    boats = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBoat)

    if state["stage"] == "play":
        if not directors or not boats:
            if t > 5.0:
                check("map has a world director and the boat spawned", False, "%d directors, %d boats" % (len(directors), len(boats)))
                finish()
            return
        director = directors[0]
        island = director.get_start_island()
        if island is None or not island.is_fully_built():
            if t > BUILD_TIMEOUT:
                check("start island loads and builds within %.0f s" % BUILD_TIMEOUT, False)
                finish()
            return

        boat = boats[0]
        state["director"], state["island"], state["boat"] = director, island, boat
        check("start island loads and builds", True, "%s, by %.1f s" % (island.get_description(), t))
        loaded = director.get_loaded_islands()
        check("other islands load around it", len(loaded) >= 3, "%d islands loaded, %d still generating"
              % (len(loaded), director.get_pending_island_count()))

        boat_loc = boat.get_actor_location()
        ground = island.get_ground_height(boat_loc)
        check("boat starts in water at least 1.4 m deep", ground < -140.0, "ground %.0f cm" % ground)
        zone = director.get_zone_at(boat_loc)
        check("boat starts in the home chain", zone == unreal.RiptideZone.HOME_CHAIN, str(zone))

        beach = find_beach(island, boat)
        check("found the beach ahead of the boat", beach is not None, str(beach))
        if beach is None:
            finish()
            return
        state["beach"] = beach
        if RENDERING:
            screenshot(world, "start_island")
        state["stage"] = "dig"
        state["t0"] = t
        return

    island, beach = state["island"], state["beach"]

    if state["stage"] == "dig":
        if t < state["t0"] + 2.0:
            return
        before = island.get_terrain_volume_litres()
        depth_before = island.get_diggable_depth(beach)
        litres = 0.0
        for _ in range(40):
            litres += island.dig(beach, 50.0, 20.0)
        after = island.get_terrain_volume_litres()
        check("digging removes sand", litres > 100.0, "%.0f litres in 40 scoops, %.0f cm of sand was there" % (litres, depth_before))
        check("dug volume matches what the ground lost", abs((before - after) - litres) < max(1.0, litres * 0.001),
              "ground lost %.1f litres" % (before - after))
        check("the hole stops at bedrock", island.get_diggable_depth(beach) < depth_before, "%.0f cm left" % island.get_diggable_depth(beach))
        check("sand starts sliding into the hole", island.is_settling())
        state["dug_volume"] = after
        state["hole_floor"] = island.get_ground_height(beach)
        state["stage"] = "settle"
        state["t0"] = t
        return

    if state["stage"] == "settle":
        if island.is_settling():
            if t > state["t0"] + SETTLE_TIMEOUT:
                check("sand stops sliding within %.0f s" % SETTLE_TIMEOUT, False)
                finish()
            return
        check("sand stops sliding", True, "after %.1f s" % (t - state["t0"]))
        check("sliding keeps the volume", abs(island.get_terrain_volume_litres() - state["dug_volume"]) < 5.0, "")
        floor = island.get_ground_height(beach)
        check("sand ran into the hole (its floor rose)", floor > state["hole_floor"] + 10.0,
              "floor %.0f cm -> %.0f cm" % (state["hole_floor"], floor))
        piled = island.pile(beach, 300.0, 40.0)
        check("piling sand back works", abs(piled - 300.0) < 1.0, "%.0f litres" % piled)
        if RENDERING:
            screenshot(world, "after_digging")
        state["stage"] = "done"
        state["t0"] = t
        return

    if state["stage"] == "done" and t > state["t0"] + 2.0:
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
log("island test registered")
