"""Plays Island_Test hands-free, photographs the start cay, and checks its ground in the running game:

- the ground is solid where the design says it is (traced from above at points all over the island);
- a crew member put down at the wash-up beach's waterline walks up the beach and on to the summit.

Run it with the editor closed (the first run also builds the island and the map):

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/island_shots.py"

Pictures go to Saved/Screenshots/WindowsEditor/island_*.png; results are in Saved/Logs/Riptide.log (search for
RiptideIsland).
"""
import math

import unreal

import riptide_island_shape as shape

ISLAND_MAP = "/Game/Riptide/Maps/Island_Test"
EYE = 1.7   # metres above the ground for standing views


def log(msg):
    unreal.log("RiptideIsland " + msg)


island = shape.StartCay()
state = {"ticks": 0, "handle": None, "shot": 0, "phase": "open", "phase_start": 0.0, "results": []}


def ue(x, y, z):
    """Design metres (x east, y north, z up) to Unreal centimetres (X north, Y east)."""
    return unreal.Vector(y * 100.0, x * 100.0, z * 100.0)


def standing(x, y):
    return (x, y, max(island.height(x, y), 0.0) + EYE)


def toward(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def _shots():
    bx, by = island.beach
    ox, oy = island.beach_out
    sx, sy = island.summit
    tip = island.spit_tip
    out = ((ox - bx) / math.hypot(ox - bx, oy - by), (oy - by) / math.hypot(ox - bx, oy - by))
    mid = toward(island.beach, island.summit, 0.5)
    grove = island.place(20.0, -8.0)
    seaward = island.place(40.0, -75.0)         # off the rock shore
    seaward_shore = island.place(45.0, -44.0)
    high = (bx + out[0] * 230.0, by + out[1] * 230.0, 120.0)
    # (name, camera position, looks at), design metres
    return [
        ("02_from_the_air", high, (0.0, 0.0, 0.0)),
        ("03_from_above", (0.0, -1.0, 330.0), (0.0, 0.0, 0.0)),
        ("04_low_over_the_bay", (bx + out[0] * 60.0 + 25.0, by + out[1] * 60.0, 9.0), (mid[0], mid[1], 2.0)),
        ("05_wading_in", (bx + out[0] * 9.0, by + out[1] * 9.0, EYE - 0.5), (sx, sy, 5.0)),
        ("06_on_the_beach", standing(bx - out[0] * 6.0, by - out[1] * 6.0), (sx, sy, 6.0)),
        ("07_beach_toward_the_spit", standing(bx - out[0] * 8.0, by - out[1] * 8.0), tip + (1.0,)),
        ("08_in_the_grove", standing(*grove), (bx + out[0] * 45.0, by + out[1] * 45.0, 1.0)),
        ("09_from_the_summit", standing(sx, sy), tip + (0.0,)),
        ("10_summit_out_to_the_boat", standing(sx, sy), (bx + out[0] * 45.0, by + out[1] * 45.0, 0.0)),
        ("11_rock_shore_from_the_sea", seaward + (1.6,), seaward_shore + (1.0,)),
        ("12_on_the_rock_shore", standing(*island.place(40.0, -40.0)), island.place(95.0, -20.0) + (2.0,)),
        ("13_spit_looking_back", standing(*island.place(-108.0, 0.0)), (sx, sy, 4.0)),
    ]


SHOTS = _shots()


def look(a, b):
    v = ue(*b) - ue(*a)
    return unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(v.z, math.hypot(v.x, v.y))), yaw=math.degrees(math.atan2(v.y, v.x)))


def check(name, passed, detail=""):
    state["results"].append(passed)
    log(("PASS " if passed else "FAIL ") + name + ((" (" + detail + ")") if detail else ""))


def trace_ground(world, x, y, ignore=()):
    """Height of the solid ground at a design point, metres, or None if nothing is hit."""
    hit = unreal.SystemLibrary.line_trace_single(world, ue(x, y, 60.0), ue(x, y, -60.0), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                 True, list(ignore), unreal.DrawDebugTrace.NONE, True)
    if not hit:
        return None
    data = hit.to_tuple()
    for value in data:
        if isinstance(value, unreal.Vector):
            # The first vector in a hit result is its location.
            return value.z / 100.0
    return None


def ground_check(world, ignore):
    worst, missed, n = 0.0, 0, 0
    for i in range(-110, 111, 11):
        for j in range(-110, 111, 11):
            x, y = float(i), float(j)
            want = island.height(x, y)
            if want < -4.0:
                continue
            n += 1
            got = trace_ground(world, x, y, ignore)
            if got is None:
                missed += 1
            else:
                worst = max(worst, abs(got - want))
    check("the ground is solid everywhere on and around the island", missed == 0, "%d of %d points have no ground" % (missed, n))
    check("the solid ground is where the design puts it", worst < 0.25, "worst difference %.2f m" % worst)


def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=island_%s" % name)
    log("shot " + name)


def finish():
    unreal.unregister_slate_post_tick_callback(state["handle"])
    log("RESULT %s (%d checks)" % ("PASS" if all(state["results"]) and state["results"] else "FAIL", len(state["results"])))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def enter(phase, t):
    state["phase"], state["phase_start"] = phase, t


def tick(dt):
    state["ticks"] += 1
    try:
        if state["ticks"] == 20:
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(ISLAND_MAP)
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
        walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
        if not pc or not walkers:
            return
        walker = walkers[0]
        t = unreal.GameplayStatics.get_time_seconds(world)
        since = t - state["phase_start"]
        phase = state["phase"]

        if phase == "open":
            pc.set_photo_mode(True)
            enter("settle", t)
        elif phase == "settle" and since > 6.0:
            shot(world, "01_from_the_boat")
            ground_check(world, list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBoat)) + list(walkers))
            enter("fly", t)
        elif phase == "fly" and since > 0.5:
            pc.set_flying(True)
            state["placed"] = False
            enter("shots", t)
        elif phase == "shots":
            if state["shot"] >= len(SHOTS):
                pc.set_flying(False)
                enter("to_beach", t)
                return
            name, where, target = SHOTS[state["shot"]]
            if not state["placed"]:
                rot = look(where, target)
                cam = pc.get_dev_camera()
                cam.set_actor_location_and_rotation(ue(*where), rot, False, True)
                # Taking up a mode makes the fly camera adopt the place it has just been put.
                cam.set_mode(unreal.RiptideDevCameraMode.FREE)
                state["placed"] = True
                state["phase_start"] = t
            elif since > 1.6:
                shot(world, name)
                state["shot"] += 1
                state["placed"] = False
        elif phase == "to_beach" and since > 0.5:
            # Put down at the waterline of the wash-up beach, then walked to the summit like a player would.
            bx, by = island.beach
            out = (island.beach_out[0] - bx, island.beach_out[1] - by)
            length = math.hypot(*out)
            edge = (bx + out[0] / length * 2.0, by + out[1] / length * 2.0)
            walker.set_actor_location(ue(edge[0], edge[1], max(island.height(*edge), 0.0) + 1.2), False, True)
            state["walk_from"] = walker.get_actor_location()
            enter("walk", t)
        elif phase == "walk":
            here = walker.get_actor_location()
            goal = ue(island.summit[0], island.summit[1], 0.0)
            to = unreal.Vector(goal.x - here.x, goal.y - here.y, 0.0)
            far = to.length() / 100.0
            if far > 2.0 and since < 90.0:
                walker.add_movement_input(to.normal(), 1.0)
                pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=-4.0, yaw=math.degrees(math.atan2(to.y, to.x))))
                if "half_way" not in state and since > 6.0:
                    state["half_way"] = True
                    shot(world, "14_walking_up_first_person")
            else:
                feet = here.z / 100.0 - walker.get_editor_property("capsule_component").get_scaled_capsule_half_height() / 100.0
                check("a crew member walks from the wash-up beach's waterline to the summit", far <= 2.0,
                      "%.0f s, ended %.1f m from the summit, feet at %.1f m" % (since, far, feet))
                check("they end standing on the summit's ground", abs(feet - island.height(here.y / 100.0, here.x / 100.0)) < 0.4,
                      "feet %.2f m, ground %.2f m" % (feet, island.height(here.y / 100.0, here.x / 100.0)))
                shot(world, "15_at_the_summit_first_person")
                enter("done", t)
        elif phase == "done" and since > 1.5:
            finish()
    except Exception as err:  # noqa: BLE001 - always quit, so a broken run doesn't leave the editor open
        log("FAIL error %r" % err)
        state["results"].append(False)
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
