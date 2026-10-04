"""Checks harvesting on the cay: a crew member picks coconuts from a palm, takes a stone and a flint off the ground,
strips fiber from a fern, and is refused a tree without a hatchet (then chops it with one).

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/harvest_test.py"

Pictures: Saved/Screenshots/WindowsEditor/harvest_*.png. Results: RiptideHarvest lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

state = {"ticks": 0, "handle": None, "step": 0, "since": 0.0, "results": [], "stage": "go"}
# (resource kind, item it gives, a tool to carry first or None, whether it should be refused)
STEPS = [
    ("palm", "coconut", None, False),
    ("stone", "stone", None, False),
    ("flint", "flint", None, False),
    ("fiber", "fiber", None, False),
    ("driftwood", "driftwood", None, False),
    ("tree", "log", None, True),
    ("tree", "log", "stone_hatchet", False),
]


def log(msg):
    unreal.log("RiptideHarvest " + msg)


def check(name, ok, detail):
    state["results"].append(ok)
    log(("PASS " if ok else "FAIL ") + name + " (" + detail + ")")


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
        props = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideIslandProps)
        if not walkers or not props or t < 2.0:
            return
        walker, holder = walkers[0], props[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        since = t - state["since"]
        if state["step"] >= len(STEPS):
            # Then the pool: an empty canteen is filled there.
            if state["stage"] == "go":
                check("harvested things are counted as taken", holder.get_depleted_count() >= 5, "%d taken" % holder.get_depleted_count())
                ponds = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptidePond)
                check("the cay has a pool", len(ponds) == 1, "%d pools" % len(ponds))
                if not ponds:
                    finish()
                    return
                where = ponds[0].get_actor_location()
                walker.give_item("canteen", 1)
                stand = unreal.Vector(where.x - 320.0, where.y, 0.0)
                stand.z = unreal.RiptideSeaSubsystem.ground_height_at(world, stand) + 100.0
                walker.set_actor_location(stand, False, True)
                state["aim"] = unreal.Vector(where.x - 60.0, where.y, where.z + 2.0)
                state["stage"] = "pool_look"
                state["since"] = t
            elif state["stage"] == "pool_look":
                eye = pc.player_camera_manager.get_camera_location()
                to = state["aim"] - eye
                pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))
                if since > 0.8:
                    prompt = str(walker.get_interaction().get_focused_prompt())
                    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=harvest_7_pool")
                    used = walker.get_interaction().use_focused()
                    check("the pool offers to fill the canteen", "Fill" in prompt and used, "prompt '%s', used %s" % (prompt, used))
                    state["stage"] = "pool_took"
                    state["since"] = t
            elif state["stage"] == "pool_took" and since > 0.6:
                check("the canteen is full of stream water", unreal.RiptideDataLibrary.count_carried(walker, "canteen_dirty") == 1 and unreal.RiptideDataLibrary.count_carried(walker, "canteen") == 0,
                      "dirty %d, empty %d" % (unreal.RiptideDataLibrary.count_carried(walker, "canteen_dirty"), unreal.RiptideDataLibrary.count_carried(walker, "canteen")))
                finish()
            return
        kind, item, tool, refused = STEPS[state["step"]]
        if state["stage"] == "go":
            # Stand two metres from the nearest one, looking at it.
            if tool:
                walker.give_item(tool, 1)
            found = holder.find_nearest(kind, walker.get_actor_location(), 1.0e7)
            # The binding hands back (bool, Vector) or just the Vector depending on the engine's mood.
            if isinstance(found, tuple):
                ok, where = found[0], found[1]
            else:
                where = found
                ok = where is not None and (abs(where.x) + abs(where.y) + abs(where.z)) > 1.0
            if not ok or where is None:
                check("there is a %s to harvest" % kind, False, "none found; walker at %s, %d holders, %d props, kinds %s, raw %r"
                      % (walker.get_actor_location(), len(props), holder.get_prop_count(),
                         list(holder.get_editor_property("batch_kinds")) if hasattr(holder, "get_editor_property") else "?", found))
                state["step"] += 1
                return
            ground = unreal.RiptideSeaSubsystem.ground_height_at(world, where)
            away = unreal.Vector(where.x - walker.get_actor_location().x, where.y - walker.get_actor_location().y, 0.0).normal()
            stand = unreal.Vector(where.x - away.x * 170.0, where.y - away.y * 170.0, 0.0)
            stand.z = unreal.RiptideSeaSubsystem.ground_height_at(world, stand) + 100.0
            walker.set_actor_location(stand, False, True)
            # where is the thing's centre (a tall one's foot); aim at the trunk, or a little above the sand for things
            # lying on it. Palms lean, so their trunk is looked for (below) once the eyes are in place.
            aim = unreal.Vector(where.x, where.y, ground + {"palm": 100.0, "tree": 80.0, "fiber": 30.0, "stone": 12.0, "flint": 6.0}.get(kind, 12.0))
            state["aim"] = aim
            state["where"] = where
            state["ground"] = ground
            state["searched"] = kind not in ("palm", "tree")
            state["before"] = unreal.RiptideDataLibrary.count_carried(walker, item)
            state["stage"] = "look"
            state["since"] = t
        elif state["stage"] == "look":
            eye = pc.player_camera_manager.get_camera_location()
            if not state["searched"] and since > 0.3:
                # Where the trunk really is, as a player would see it: the first point round the foot that a look from
                # the eyes lands on the island's props, close to where it was aimed.
                state["searched"] = True
                where, ground = state["where"], state["ground"]
                # (A sweeping palm's trunk is well out from its foot: 1.7 m at head height.)
                for h in (130.0, 100.0, 70.0, 45.0):
                    for r in (0.0, 25.0, 50.0, 75.0, 100.0, 125.0, 150.0, 175.0, 200.0):
                        for k in range(12 if r else 1):
                            a = k * math.pi / 6.0
                            p = unreal.Vector(where.x + r * math.cos(a), where.y + r * math.sin(a), ground + h)
                            hit = unreal.SystemLibrary.line_trace_single(world, eye, p, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [walker],
                                                                         unreal.DrawDebugTrace.NONE, True)
                            if hit:
                                tup = hit.to_tuple()
                                if tup[9] == holder and tup[4].distance(p) < 40.0:
                                    state["aim"] = tup[4]
                                    break
                        else:
                            continue
                        break
                    else:
                        continue
                    break
                log("%s: looking at its trunk at %s" % (kind, state["aim"]))
                state["since"] = t
                return
            to = state["aim"] - eye
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))
            if since > 0.8:
                prompt = walker.get_interaction().get_focused_prompt()
                if kind == "driftwood":
                    # What the eyes actually meet on the way to the branch.
                    hit = unreal.SystemLibrary.line_trace_single(world, eye, state["aim"], unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [walker], unreal.DrawDebugTrace.NONE, True)
                    log("driftwood trace from %s to %s: hit %s" % (eye, state["aim"], hit))
                unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=harvest_%d_%s%s" % (state["step"], kind, "_tool" if tool else ""))
                used = walker.get_interaction().use_focused()
                log("step %d %s: prompt '%s', used %s" % (state["step"], kind, prompt, used))
                if refused:
                    check("a tree can't be chopped bare-handed", not used and "needs" in str(prompt).lower(), "prompt '%s', used %s" % (prompt, used))
                else:
                    check("looking at a %s offers to harvest it" % kind, str(prompt) != "", "prompt '%s'" % prompt)
                    check("E harvests the %s" % kind, used, "used %s" % used)
                state["stage"] = "took"
                state["since"] = t
        elif state["stage"] == "took" and since > 0.6:
            after = unreal.RiptideDataLibrary.count_carried(walker, item)
            if not refused:
                check("the %s gave %s" % (kind, item), after > state["before"], "%d -> %d" % (state["before"], after))
            state["step"] += 1
            state["stage"] = "go"
            state["since"] = t
    except Exception as err:  # noqa: BLE001
        import traceback
        log("error " + traceback.format_exc())
        check("the run completed", False, str(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
