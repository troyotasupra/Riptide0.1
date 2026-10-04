"""Checks the raft on the cay: a castaway places a raft site at the water's edge, builds it up with logs and rope and
launches it; swims to it and climbs aboard; fits a pair of oars, takes them up and rows out (ahead, then turning);
lets go of the oars and takes them out again. Pictures of the raft afloat and of the castaway at the oars.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/raft_test.py"

(-nullrhi runs the checks without the pictures.) Pictures: Saved/Screenshots/WindowsEditor/raft_*.png. Results:
RiptideRaft lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "start", "since": 0.0, "results": []}


def log(msg):
    unreal.log("RiptideRaft " + msg)


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


def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=raft_%s" % name)


def look_at(pc, where):
    eye = pc.player_camera_manager.get_camera_location()
    to = unreal.Vector(where.x - eye.x, where.y - eye.y, where.z - eye.z)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))


def fly_to(pc, where, target):
    to = target - where
    pc.set_flying(True)
    cam = pc.get_dev_camera()
    cam.set_actor_location_and_rotation(where, unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))),
                                                               yaw=math.degrees(math.atan2(to.y, to.x))), False, True)
    cam.set_mode(unreal.RiptideDevCameraMode.FREE)


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
        if not walkers or t < 3.0:
            return
        walker = walkers[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        since = t - state["since"]
        phase = state["phase"]
        rafts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideRaft)
        raft = rafts[0] if rafts else None

        if phase == "start":
            # At the water's edge on the wash-up beach, facing the sea, with the makings of a raft and a pair of oars.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            for back in [d * 0.5 for d in range(0, 40)]:
                x, y = bx - ox / length * back, by - oy / length * back
                if island.height(x, y) > 0.25:
                    break
            walker.set_actor_location(unreal.Vector(y * 100.0, x * 100.0, (island.height(x, y) + 1.1) * 100.0), False, True)
            state["yaw"] = math.degrees(math.atan2(ox, oy))
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=state["yaw"]))
            for item, count in (("raft_kit", 1), ("log", 6), ("rope", 3), ("oar", 2)):
                walker.give_item(item, count)
            # (Three cells square, it goes in the pack, not the pockets.)
            uid = unreal.RiptideDataLibrary.first_carried_uid(walker, "raft_kit")
            walker.use_item(0, uid)
            walker.use_item(1, uid)
            enter("placed", t)
        elif phase == "placed" and since > 1.0:
            sites = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideStructure) if str(s.get_type()) == "raft_site"]
            check("the raft kit lays a raft site", len(sites) == 1, "%d sites" % len(sites))
            if not sites:
                finish()
                return
            site = sites[0]
            site.add_materials_from(walker)
            site.add_materials_from(walker)
            check("logs and rope finish it", site.is_finished(), "stage %d" % site.get_stage())
            state["site"] = site.get_actor_location()
            look_at(pc, site.get_actor_location() + unreal.Vector(0.0, 0.0, 20.0))
            enter("launch", t)
        elif phase == "launch" and since > 0.8:
            prompt = str(walker.get_interaction().get_focused_prompt())
            used = walker.get_interaction().use_focused()
            check("the finished site offers to launch the raft", used and "Launch" in prompt, "prompt '%s'" % prompt)
            enter("launched", t)
        elif phase == "launched" and since > 4.0:
            check("the raft is launched onto the water and floats", raft is not None and raft.is_afloat(),
                  "%s%s" % (raft, (", %.0f m from the site" % (raft.get_actor_location().distance(state["site"]) / 100.0)) if raft else ""))
            sites = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideStructure) if str(s.get_type()) == "raft_site"]
            check("the site is used up", not sites, "%d sites" % len(sites))
            if not raft:
                finish()
                return
            # Floating with its deck clear of the water (the box's top is the deck, 25 cm above its middle).
            here = raft.get_actor_location()
            sea = unreal.RiptideSeaSubsystem.sea_surface_at(world, here)
            check("it floats with its deck above the water", 5.0 < here.z + 25.0 - sea < 60.0, "deck %.0f cm above the sea" % (here.z + 25.0 - sea))
            # Into the sea beside it, two metres off its side.
            here = raft.get_actor_location()
            right = raft.get_actor_right_vector()
            walker.set_actor_location(here + right * 260.0 + unreal.Vector(0.0, 0.0, -20.0), False, True)
            enter("swim", t)
        elif phase == "swim" and since > 1.5:
            check("the castaway is swimming beside it", walker.is_in_sea(), "in sea %s" % walker.is_in_sea())
            look_at(pc, raft.get_actor_location() + unreal.Vector(0.0, 0.0, 25.0))
            enter("climb", t)
        elif phase == "climb" and since > 0.6:
            prompt = str(walker.get_interaction().get_focused_prompt())
            walker.get_interaction().use_focused()
            state["climb_prompt"] = prompt
            enter("aboard", t)
        elif phase == "aboard" and since > 1.5:
            local = raft.get_actor_transform().inverse_transform_location(walker.get_actor_location())
            check("E climbs aboard from the water", not walker.is_in_sea() and abs(local.x) < 170.0 and abs(local.y) < 130.0 and local.z > 0.0,
                  "prompt '%s', on the raft at %s" % (state["climb_prompt"], local))
            lock = raft.get_actor_transform().transform_location(unreal.Vector(0.0, 124.0, 40.0))
            look_at(pc, lock)
            enter("fit", t)
        elif phase == "fit" and since > 0.6:
            prompt = str(walker.get_interaction().get_focused_prompt())
            walker.get_interaction().use_focused()
            state["fit_prompt"] = prompt
            enter("fitted", t)
        elif phase == "fitted" and since > 0.6:
            check("a pair of oars goes into the oarlocks", raft.has_oars() and unreal.RiptideDataLibrary.count_carried(walker, "oar") == 0,
                  "prompt '%s', oars %s, carried %d" % (state["fit_prompt"], raft.has_oars(), unreal.RiptideDataLibrary.count_carried(walker, "oar")))
            here = raft.get_actor_location()
            fly_to(pc, here - raft.get_actor_forward_vector() * 520.0 + raft.get_actor_right_vector() * 420.0 + unreal.Vector(0.0, 0.0, 260.0), here)
            enter("afloat shot", t)
        elif phase == "afloat shot" and since > 0.8:
            shot(world, "1_afloat")
            enter("afloat back", t)
        elif phase == "afloat back" and since > 0.6:
            pc.set_flying(False)
            enter("eyes back", t)
        elif phase == "eyes back" and since > 0.6:
            # (Once the view is back in the castaway's eyes.)
            look_at(pc, raft.get_actor_transform().transform_location(unreal.Vector(60.0, 0.0, 25.0)))
            enter("take up", t)
        elif phase == "take up" and since > 0.6:
            prompt = str(walker.get_interaction().get_focused_prompt())
            walker.get_interaction().use_focused()
            state["row_prompt"] = prompt
            enter("rowing", t)
        elif phase == "rowing" and since > 0.8:
            facing = (walker.get_actor_rotation().yaw - raft.get_actor_rotation().yaw + 540.0) % 360.0 - 180.0
            check("E takes up the oars, kneeling to face the bow", walker.get_rowing_raft() == raft and raft.get_rower() == walker and abs(facing) < 10.0,
                  "prompt '%s', facing %.0f degrees off the bow" % (state["row_prompt"], facing))
            if walker.get_rowing_raft() != raft:
                finish()
                return
            state["start"] = raft.get_actor_location()
            state["forward"] = raft.get_actor_forward_vector()
            walker.set_row_input(1.0, 0.0)
            enter("pulling", t)
        elif phase == "pulling" and since > 2.5 and "rowing_shot" not in state:
            state["rowing_shot"] = True
            here = raft.get_actor_location()
            fly_to(pc, here + raft.get_actor_forward_vector() * 380.0 + raft.get_actor_right_vector() * 300.0 + unreal.Vector(0.0, 0.0, 180.0),
                   here + unreal.Vector(0.0, 0.0, 60.0))
        elif phase == "pulling" and since > 3.3 and "rowing_shot_taken" not in state:
            state["rowing_shot_taken"] = True
            shot(world, "2_rowing")
        elif phase == "pulling" and since > 9.0:
            pc.set_flying(False)
            moved = raft.get_actor_location() - state["start"]
            ahead = moved.x * state["forward"].x + moved.y * state["forward"].y
            check("rowing pulls it ahead", ahead > 500.0 and raft.get_speed_ms() > 0.6,
                  "%.1f m ahead in 9 s, %.2f m/s now" % (ahead / 100.0, raft.get_speed_ms()))
            state["yaw0"] = raft.get_actor_rotation().yaw
            walker.set_row_input(0.0, 1.0)
            enter("turning", t)
        elif phase == "turning" and since > 4.0:
            turned = (raft.get_actor_rotation().yaw - state["yaw0"] + 540.0) % 360.0 - 180.0
            check("one oar alone turns it (to the right with D)", 30.0 < turned < 120.0, "turned %.0f degrees in 4 s" % turned)
            walker.set_row_input(0.0, 0.0)
            walker.stop_rowing()
            enter("let go", t)
        elif phase == "let go" and since > 1.5:
            local = raft.get_actor_transform().inverse_transform_location(walker.get_actor_location())
            check("letting go of the oars stands them up on the deck", walker.get_rowing_raft() is None and not walker.is_in_sea() and local.z > 0.0,
                  "rowing %s, at %s on the raft" % (walker.get_rowing_raft(), local))
            raft.take_out_oars(walker)
            enter("oars out", t)
        elif phase == "oars out" and since > 0.5:
            check("the oars come out with them (nobody rows it away)", not raft.has_oars() and unreal.RiptideDataLibrary.count_carried(walker, "oar") == 2,
                  "oars %s, carried %d" % (raft.has_oars(), unreal.RiptideDataLibrary.count_carried(walker, "oar")))
            finish()
        if t > 300.0:
            check("finished in time", False, "stuck at " + phase)
            finish()
    except Exception as err:  # noqa: BLE001 - report and stop
        import traceback
        log("ERROR %s" % traceback.format_exc().replace("\n", " | "))
        check("no script error", False, repr(err))
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
