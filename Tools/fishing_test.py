"""Checks fishing on the cay: a castaway takes the rod out (Q), baits it with a grub, casts from the beach out over
the water, strikes when it bites, plays the fish in (reeling while the line's slack, easing off when it's tight),
then kills the fish flopping at their feet and takes it. Pictures of the rod held, wound back for a cast and out
over the water, from the castaway's eyes and from beside them.

    UnrealEditor Riptide.uproject -RenderOffscreen -unattended -nosplash -nosound -ResX=1920 -ResY=1080
        -ExecCmds="py <project>/Tools/fishing_test.py"

(-nullrhi runs the checks without the pictures.) Pictures: Saved/Screenshots/WindowsEditor/fishing_*.png. Results:
RiptideFishing lines in Saved/Logs/Riptide.log.
"""
import math

import unreal

import riptide_island_shape as shape

island = shape.StartCay()
state = {"ticks": 0, "handle": None, "phase": "start", "since": 0.0, "results": [], "tries": 0}
S = unreal.RiptideAnglerState


def log(msg):
    unreal.log("RiptideFishing " + msg)


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
    unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=fishing_%s" % name)


def look_at(pc, where):
    eye = pc.player_camera_manager.get_camera_location()
    to = unreal.Vector(where.x - eye.x, where.y - eye.y, where.z - eye.z)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))), yaw=math.degrees(math.atan2(to.y, to.x))))


def side_view(pc, walker, ahead, across, up):
    """The dev mode's fly camera beside the castaway, looking at them (they stay as they are while it flies)."""
    here = walker.get_actor_location()
    forward = unreal.Rotator(roll=0.0, pitch=0.0, yaw=state["yaw"]).get_forward_vector()
    right = unreal.Vector(-forward.y, forward.x, 0.0)
    where = here + forward * ahead + right * across + unreal.Vector(0.0, 0.0, up)
    to = here + unreal.Vector(0.0, 0.0, 10.0) - where
    pc.set_flying(True)
    camera = pc.get_dev_camera()
    camera.set_actor_location_and_rotation(where, unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))),
                                                                  yaw=math.degrees(math.atan2(to.y, to.x))), False, True)
    camera.set_mode(unreal.RiptideDevCameraMode.FREE)


def back_in(pc, walker):
    pc.set_flying(False)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=-8.0, yaw=state["yaw"]))


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
        angler = walker.get_angler()
        since = t - state["since"]
        phase = state["phase"]

        if phase == "start":
            # On the wash-up beach a few metres above the water, facing out to sea, with a rod and grubs.
            bx, by = island.beach
            ox, oy = island.beach_out[0] - bx, island.beach_out[1] - by
            length = math.hypot(ox, oy)
            for back in [d * 0.5 for d in range(0, 40)]:
                x, y = bx - ox / length * back, by - oy / length * back
                if island.height(x, y) > 0.35:
                    break
            walker.set_actor_location(unreal.Vector(y * 100.0, x * 100.0, (island.height(x, y) + 1.1) * 100.0), False, True)
            state["yaw"] = math.degrees(math.atan2(ox, oy))
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=-8.0, yaw=state["yaw"]))
            walker.give_item("fishing_rod", 1)
            walker.give_item("grub", 3)
            angler.set_fast_bites(True)
            enter("hold", t)
        elif phase == "hold" and since > 1.0:
            walker.hold_item("fishing_rod")
            enter("held", t)
        elif phase == "held" and since > 1.0:
            check("the rod is taken in hand", str(walker.get_held_item()) == "fishing_rod", str(walker.get_held_item()))
            angler.set_bait("grub")
            check("the grub goes on the hook", str(angler.get_bait()) == "grub", str(angler.get_bait()))
            shot(world, "1_held")
            enter("side held", t)
        elif phase == "side held" and since > 0.6:
            side_view(pc, walker, 95.0, 95.0, 25.0)
            enter("side held shot", t)
        elif phase == "side held shot" and since > 0.8:
            shot(world, "2_held_side")
            enter("side held back", t)
        elif phase == "side held back" and since > 0.6:
            back_in(pc, walker)
            enter("charge", t)
        elif phase == "charge" and since > 0.8:
            angler.primary_pressed()
            enter("charging", t)
        elif phase == "charging" and since > 0.9:
            check("holding the button winds up a cast", angler.get_state() == S.CHARGING and angler.get_charge() > 0.6,
                  "%s, charge %.2f" % (angler.get_state(), angler.get_charge()))
            shot(world, "3_wound_back")
            enter("throw", t)
        elif phase == "throw" and since > 0.3:
            angler.primary_released()
            enter("thrown", t)
        elif phase == "thrown" and since > 1.2:
            bobber = angler.get_bobber_location()
            ground = unreal.RiptideSeaSubsystem.ground_height_at(world, bobber)
            out = math.hypot(bobber.x - walker.get_actor_location().x, bobber.y - walker.get_actor_location().y) / 100.0
            check("the cast lands out on the water", angler.get_state() in (S.WAITING, S.BITE) and ground < -50.0,
                  "%s, %.1f m out, seabed %.0f cm" % (angler.get_state(), out, ground))
            shot(world, "4_waiting")
            enter("wait bite", t)
        elif phase == "wait bite":
            if angler.get_state() == S.BITE:
                check("something bites", True, "bite")
                angler.primary_pressed()
                enter("fight", t)
            elif since > 8.0:
                check("something bites", False, str(angler.get_state()))
                finish()
                return
        elif phase == "fight":
            st = angler.get_state()
            if st == S.FIGHT:
                # Reel while the line's easy, ease off as it tightens.
                tension = angler.get_tension()
                if tension > 0.7:
                    angler.primary_released()
                elif tension < 0.45:
                    angler.primary_pressed()
                if "fight_shot" not in state and since > 0.6:
                    state["fight_shot"] = True
                    shot(world, "5_fight")
            else:
                angler.primary_released()
                end = angler.get_last_end()
                state["tries"] += 1
                log("the fight ended: %s (try %d)" % (end, state["tries"]))
                if end == unreal.RiptideCastEnd.LANDED or state["tries"] >= 4:
                    check("the fish is played in and landed", end == unreal.RiptideCastEnd.LANDED, str(end))
                    enter("landed", t)
                else:
                    enter("charge", t)
            if since > 60.0:
                check("the fight ends", False, "still going")
                finish()
                return
        elif phase == "landed" and since > 1.0:
            fish = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideLandedFish)
            check("the catch is flopping on the ground at the castaway's feet", len(fish) == 1 and fish[0].is_alive()
                  and fish[0].get_actor_location().distance(walker.get_actor_location()) < 250.0,
                  "%d fish%s" % (len(fish), (", %.0f cm away" % fish[0].get_actor_location().distance(walker.get_actor_location())) if fish else ""))
            check("the grub was used up", unreal.RiptideDataLibrary.count_carried(walker, "grub") < 3,
                  "%d grubs left" % unreal.RiptideDataLibrary.count_carried(walker, "grub"))
            if not fish:
                finish()
                return
            state["fish"] = fish[0]
            state["fish_item"] = "raw_" + str(fish[0].get_fish())
            state["before"] = unreal.RiptideDataLibrary.count_carried(walker, state["fish_item"])
            shot(world, "6_landed")
            enter("look fish", t)
        elif phase == "look fish":
            look_at(pc, state["fish"].get_actor_location())
            if since > 0.8:
                prompt = str(walker.get_interaction().get_focused_prompt())
                used = walker.get_interaction().use_focused()
                check("E kills the fish", used and "Kill" in prompt, "prompt '%s'" % prompt)
                enter("killed", t)
        elif phase == "killed" and since > 0.6:
            check("it lies still", not state["fish"].is_alive(), "alive %s" % state["fish"].is_alive())
            prompt = str(walker.get_interaction().get_focused_prompt())
            walker.get_interaction().use_focused()
            state["take_prompt"] = prompt
            enter("taken", t)
        elif phase == "taken" and since > 0.6:
            after = unreal.RiptideDataLibrary.count_carried(walker, state["fish_item"])
            left = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideLandedFish)
            check("E again takes it into the pack", after == state["before"] + 1 and not left,
                  "prompt '%s', %s %d -> %d" % (state["take_prompt"], state["fish_item"], state["before"], after))
            # One more cast to see it from beside: wound back, then out on the water.
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=-8.0, yaw=state["yaw"]))
            angler.primary_pressed()
            enter("side charge", t)
        elif phase == "side charge" and since > 1.0:
            side_view(pc, walker, 20.0, 260.0, 40.0)
            enter("side charge shot", t)
        elif phase == "side charge shot" and since > 0.8:
            shot(world, "7_wound_back_side")
            enter("side charge back", t)
        elif phase == "side charge back" and since > 0.6:
            back_in(pc, walker)
            angler.primary_released()
            angler.set_fast_bites(False)
            enter("side wait", t)
        elif phase == "side wait" and since > 1.5:
            side_view(pc, walker, 130.0, 160.0, 30.0)
            enter("side wait shot", t)
        elif phase == "side wait shot" and since > 0.8:
            shot(world, "8_waiting_side")
            enter("away", t)
        elif phase == "away" and since > 0.6:
            back_in(pc, walker)
            walker.hold_item("None")
            enter("put away", t)
        elif phase == "put away" and since > 1.0:
            check("Q puts the rod away and the line comes in", str(walker.get_held_item()) == "None" and angler.get_state() == S.IDLE,
                  "%s, %s" % (walker.get_held_item(), angler.get_state()))
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
