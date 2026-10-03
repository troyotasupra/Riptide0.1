"""Headless check of the dev mode (ARiptidePlayerController and its fly camera) on Ocean_Test.

Run it with the editor closed (it quits the editor when done):
  UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/dev_mode_test.py"
Then read the "RiptideDevTest:" lines in Saved/Logs/Riptide.log. The last one says PASS or FAIL.

It drives the dev tools through the controller's functions (the same ones the dev keys call) while the boat runs at
full throttle: flying from on foot and from the helm and back, the camera flying through the hull, riding along with
the boat underway, orbiting and chasing it, slow motion, freezing the world while the camera still flies, dropping in
on the deck and into the sea, back to the boat, refuelling and repairing, righting and stopping the boat, bringing it
to the camera, god mode against a rough sea, sea states, time of day, the panel, the overlay and photo mode.
Steps run on real time (the world's own clock stops while it's frozen).
"""

import math

import unreal

state = {"ticks": 0, "started": False, "step": 0, "step_start": None, "checks": [], "handle": None, "data": {}}


def log(msg):
    unreal.log(f"RiptideDevTest: {msg}")


def check(name, passed, detail=""):
    state["checks"].append((name, bool(passed), detail))
    log("%s  %s  %s" % ("ok  " if passed else "FAIL", name, detail))


def ctx():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    return world, pc, state["boat"], state["walker"]


def local(boat, loc):
    return boat.get_actor_transform().inverse_transform_location(loc)


def world_at(boat, x, y, z):
    return boat.get_actor_transform().transform_location(unreal.Vector(x, y, z))


def boat_yaw(boat):
    return boat.get_actor_rotation().yaw


def place_camera(pc, boat, x, y, z, yaw_from_bow, pitch=0.0, mode=None):
    """Puts the fly camera at a point on the boat (its frame), looking yaw_from_bow degrees off the bow, then takes up
    a mode from there."""
    cam = pc.get_dev_camera()
    cam.set_actor_location_and_rotation(world_at(boat, x, y, z),
                                        unreal.Rotator(roll=0.0, pitch=pitch, yaw=boat_yaw(boat) + yaw_from_bow), False, True)
    pc.set_dev_camera_mode(mode if mode is not None else unreal.RiptideDevCameraMode.RIDE_ALONG)
    return cam


# --- Steps: (name, real seconds, function(elapsed, first)) ---

def s_settle(e, first):
    world, pc, boat, walker = ctx()
    if first:
        boat.set_helm_input(1.0, 0.0)     # the lever runs up to full ahead and stays there
        pc.set_dev_panel_shown(True)
    if e > 4.0:
        boat.set_helm_input(0.0, 0.0)


def s_refuel(e, first):
    world, pc, boat, walker = ctx()
    if first:
        boat.apply_engine_damage(0.7, -1)
        d = state["data"]
        d["fuel_before"], d["health_before"] = boat.get_fuel_liters(), (boat.get_motor_health(0), boat.get_motor_health(1))
        pc.refuel_and_repair()
        fuel, health = boat.get_fuel_liters(), (boat.get_motor_health(0), boat.get_motor_health(1))
        check("refuel and repair fills the tank and mends both motors", fuel > 449.0 and min(health) > 0.999,
              "fuel %.0f -> %.0f L, health %.2f/%.2f -> %.2f/%.2f" % (d["fuel_before"], fuel, d["health_before"][0],
                                                                    d["health_before"][1], health[0], health[1]))
        check("the dev panel is on screen", pc.is_dev_panel_on_screen(), "")


def s_fly_from_foot(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("foot", {"drift": 0.0, "standing": 0, "frames": 0})
    if first:
        d["pawn_before"] = pc.get_controlled_pawn() == walker
        d["local0"] = local(boat, walker.get_actor_location())
        d["world0"] = walker.get_actor_location()
        pc.set_flying(True)
        d["flying"] = pc.is_flying() and pc.get_controlled_pawn() == pc.get_dev_camera()
        return
    lp = local(boat, walker.get_actor_location())
    d["drift"] = max(d["drift"], math.hypot(lp.x - d["local0"].x, lp.y - d["local0"].y))
    d["frames"] += 1
    d["standing"] += 1 if walker.is_standing_on_boat() else 0


def s_fly_back_to_foot(e, first):
    world, pc, boat, walker = ctx()
    if first:
        d = state["data"]["foot"]
        moved = (walker.get_actor_location() - d["world0"]).length() / 100.0
        check("flying from on foot: the player takes the fly camera", d["pawn_before"] and d["flying"], "")
        check("the crew member left on deck rides it while flying (not left behind)",
              d["drift"] < 150.0 and d["standing"] > 0.9 * d["frames"] and moved > 20.0,
              "moved %.0f m with the boat, drifted at most %.0f cm on the deck, standing %d/%d frames, boat %.1f kn"
              % (moved, d["drift"], d["standing"], d["frames"], boat.get_speed_knots()))
        pc.set_flying(False)
        check("flying off returns to the crew member on foot", not pc.is_flying() and pc.get_controlled_pawn() == walker,
              "pawn %s" % pc.get_controlled_pawn().get_name())


def s_ride_along(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("ride", {"drift": 0.0})
    if first:
        pc.set_flying(True)
        # Off the port side, at the height of the bulwarks, looking across the boat to starboard.
        place_camera(pc, boat, -100.0, -700.0, 45.0, 90.0)
        return
    cam = pc.get_dev_camera()
    lp = local(boat, cam.get_actor_location())
    if 1.0 <= e < 1.05 and "local0" not in d:
        d["local0"], d["world0"] = lp, cam.get_actor_location()
    elif "local0" in d:
        d["drift"] = max(d["drift"], (lp - d["local0"]).length())
        d["world1"] = cam.get_actor_location()


def s_noclip(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("noclip", {"ys": [], "inside": 0})
    cam = pc.get_dev_camera()
    if first:
        r = state["data"]["ride"]
        carried = (r["world1"] - r["world0"]).length() / 100.0
        check("ride-along: the camera stays put on the boat underway", r["drift"] < 5.0 and carried > 20.0,
              "carried %.0f m, moved %.1f cm on the boat, boat %.1f kn" % (carried, r["drift"], boat.get_speed_knots()))
        cam.set_fly_speed(800.0)
        cam.set_fly_input(unreal.Vector(1.0, 0.0, 0.0))
        return
    lp = local(boat, cam.get_actor_location())
    d["ys"].append(lp.y)
    if abs(lp.y) < 110.0:
        d["inside"] += 1
    if e >= 1.75:
        cam.set_fly_input(unreal.Vector(0.0, 0.0, 0.0))


def s_orbit(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("orbit", {"worst_aim": 0.0, "worst_dist": 0.0})
    if first:
        n = state["data"]["noclip"]
        check("the camera flies straight through the hull (noclip)",
              min(n["ys"]) < -600.0 and max(n["ys"]) > 500.0 and n["inside"] > 0,
              "across the boat from y %.0f to %.0f cm, %d frames inside its sides" % (min(n["ys"]), max(n["ys"]), n["inside"]))
        pc.set_dev_camera_mode(unreal.RiptideDevCameraMode.ORBIT)
        return
    if e < 1.0:
        return
    cam = pc.get_dev_camera()
    target = boat.get_actor_location() + unreal.Vector(0, 0, 150)
    to = target - cam.get_actor_location()
    view = cam.get_actor_rotation().get_forward_vector()
    aim = math.degrees(math.acos(max(-1.0, min(1.0, to.normal().dot(view)))))
    d["worst_aim"] = max(d["worst_aim"], aim)
    d["worst_dist"] = max(d["worst_dist"], abs(to.length() - cam.get_zoom_distance()))
    cam.add_look_input(0.4, 0.0)   # swings round the boat


def s_chase(e, first):
    world, pc, boat, walker = ctx()
    if first:
        o = state["data"]["orbit"]
        check("orbit: circles the boat looking at it, at its zoom distance", o["worst_aim"] < 2.0 and o["worst_dist"] < 60.0,
              "aim off by at most %.1f deg, distance off by %.0f cm" % (o["worst_aim"], o["worst_dist"]))
        pc.set_dev_camera_mode(unreal.RiptideDevCameraMode.CHASE)
        return
    if e >= 3.9 and "chase" not in state["data"]:
        cam = pc.get_dev_camera()
        heading = boat.get_velocity().normal()
        to_boat = (boat.get_actor_location() - cam.get_actor_location()).normal()
        state["data"]["chase"] = True
        check("chase: follows behind the boat the way it's going", heading.dot(to_boat) > 0.8,
              "%.0f m from the boat, alignment %.2f" % ((boat.get_actor_location() - cam.get_actor_location()).length() / 100.0,
                                                        heading.dot(to_boat)))


def s_slow_motion(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("slow", {})
    if first:
        pc.set_dev_camera_mode(unreal.RiptideDevCameraMode.FREE)
        pc.set_time_scale(0.25)
        d["game0"], d["real0"] = unreal.GameplayStatics.get_time_seconds(world), unreal.GameplayStatics.get_real_time_seconds(world)
        d["cam0"] = pc.get_dev_camera().get_actor_location()
        pc.get_dev_camera().set_fly_input(unreal.Vector(1.0, 0.0, 0.0))
        return
    if e >= 2.0 and "done" not in d:
        d["done"] = True
        game = unreal.GameplayStatics.get_time_seconds(world) - d["game0"]
        real = unreal.GameplayStatics.get_real_time_seconds(world) - d["real0"]
        flown = (pc.get_dev_camera().get_actor_location() - d["cam0"]).length()
        pc.get_dev_camera().set_fly_input(unreal.Vector(0.0, 0.0, 0.0))
        check("slow motion 0.25x: the world runs at a quarter speed, the camera flies at full speed",
              abs(pc.get_time_scale() - 0.25) < 0.01 and 0.2 < game / real < 0.3 and flown > 0.8 * 800.0 * real,
              "%.2f s of game time in %.2f s, camera flew %.0f cm" % (game, real, flown))
        pc.set_time_scale(1.0)


def s_freeze(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("freeze", {})
    cam = pc.get_dev_camera()
    if first:
        pc.set_world_frozen(True)
        d["game0"] = unreal.GameplayStatics.get_time_seconds(world)
        d["boat0"] = boat.get_actor_location()
        d["cam0"] = cam.get_actor_location()
        cam.set_fly_input(unreal.Vector(1.0, 0.0, 0.5))
        return
    if e >= 1.5 and "frozen_checked" not in d:
        d["frozen_checked"] = True
        cam.set_fly_input(unreal.Vector(0.0, 0.0, 0.0))
        game = unreal.GameplayStatics.get_time_seconds(world) - d["game0"]
        boat_moved = (boat.get_actor_location() - d["boat0"]).length()
        flown = (cam.get_actor_location() - d["cam0"]).length()
        check("freeze stops the world (clock and boat) while the fly camera still flies",
              pc.is_world_frozen() and game < 0.001 and boat_moved < 0.1 and flown > 500.0,
              "game time +%.3f s, boat moved %.2f cm, camera flew %.0f cm" % (game, boat_moved, flown))
        pc.set_world_frozen(False)
        d["game1"] = unreal.GameplayStatics.get_time_seconds(world)
    if e >= 2.4 and "unfrozen_checked" not in d:
        d["unfrozen_checked"] = True
        check("unfreezing lets the world go again", not pc.is_world_frozen()
              and unreal.GameplayStatics.get_time_seconds(world) - d["game1"] > 0.5, "")


def s_drop_in_deck(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("drop", {})
    if first:
        # Over the aft deck, 2.3 m up, looking forward.
        cam = place_camera(pc, boat, -280.0, 0.0, 250.0, 0.0, -10.0)
        return
    if e >= 0.8 and "dropped" not in d:
        cam = pc.get_dev_camera()
        eye = cam.get_actor_location()
        pc.drop_in_here()
        d["dropped"] = True
        d["off"] = (walker.get_actor_location() + unreal.Vector(0, 0, 70) - eye).length()
        d["pawn"] = pc.get_controlled_pawn() == walker and not pc.is_flying()
    if e >= 3.0 and "stood" not in d:
        d["stood"] = walker.is_standing_on_boat()
        lp = local(boat, walker.get_actor_location())
        check("drop in here puts the crew member at the camera and back in control, landing on the deck underway",
              d["pawn"] and d["off"] < 60.0 and d["stood"],
              "eyes %.0f cm from the camera, standing on the deck %s at x %.0f, y %.0f on the boat (%.1f kn)"
              % (d["off"], d["stood"], lp.x, lp.y, boat.get_speed_knots()))


def s_fly_from_helm(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("helm", {})
    if first:
        pc.back_to_the_boat()
        return
    if e >= 1.5 and "took" not in d:
        walker.try_take_helm()
        d["took"] = pc.get_controlled_pawn() == boat and boat.get_helmsman() == walker
        pc.set_flying(True)
        d["flying"] = pc.is_flying() and boat.get_helmsman() == walker and walker.is_manning_helm()
        d["lever0"] = boat.get_throttle_lever()
    if e >= 5.5 and "back" not in d:
        lever, kn = boat.get_throttle_lever(), boat.get_speed_knots()
        pc.set_flying(False)
        d["back"] = pc.get_controlled_pawn() == boat and boat.get_helmsman() == walker and walker.is_manning_helm()
        check("flying from the helm leaves the boat running with its throttle where it was", d["took"] and d["flying"]
              and lever > 0.95 and kn > 20.0, "took the helm %s, lever %.2f -> %.2f, %.1f kn while flying" % (d["took"], d["lever0"], lever, kn))
        check("flying off returns to the boat's helm", d["back"], "pawn %s, helmsman %s" % (
            pc.get_controlled_pawn().get_name(), boat.get_helmsman().get_name() if boat.get_helmsman() else None))


def s_drop_in_sea(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("sea", {})
    if first:
        pc.set_flying(True)
        place_camera(pc, boat, 0.0, 3000.0, 300.0, -90.0, -10.0, unreal.RiptideDevCameraMode.FREE)
        return
    if e >= 0.8 and "dropped" not in d:
        pc.drop_in_here()
        d["dropped"] = True
        d["ok"] = pc.get_controlled_pawn() == walker and not walker.is_manning_helm() and boat.get_helmsman() is None
    if e >= 4.0 and "swim" not in d:
        d["swim"] = walker.is_in_sea()
        check("dropping in from the helm over the sea: off the helm and swimming", d["ok"] and d["swim"],
              "swimming %s, helmsman %s, boat still going %.1f kn" % (d["swim"], boat.get_helmsman(), boat.get_speed_knots()))
        pc.back_to_the_boat()
    if e >= 6.5 and "back" not in d:
        feet = local(boat, walker.get_actor_location())
        d["back"] = True
        check("back to the boat puts the swimmer on the deck at the helm", walker.is_standing_on_boat()
              and math.hypot(feet.x + 110.0, feet.y) < 80.0,
              "standing %s at x %.0f, y %.0f (the helm's spot is -110, 0)" % (walker.is_standing_on_boat(), feet.x, feet.y))


def s_right_boat(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("right", {})
    if first:
        rot = boat.get_actor_rotation()
        boat.set_actor_rotation(unreal.Rotator(roll=75.0, pitch=10.0, yaw=rot.yaw), True)
        d["kn"] = boat.get_speed_knots()
        pc.right_and_stop_boat()
        r = boat.get_actor_rotation()
        d["upright"] = max(abs(r.roll), abs(r.pitch))
        d["still"] = boat.get_velocity().length()
        return
    if e >= 1.5 and "after" not in d:
        r = boat.get_actor_rotation()
        d["after"] = max(abs(r.roll), abs(r.pitch))
        check("right the boat and stop it: upright, dead in the water, and stays upright",
              d["upright"] < 1.0 and d["still"] < 1.0 and d["after"] < 15.0,
              "from %.1f kn heeled 75 deg: tilt %.1f deg, speed %.1f cm/s; %.1f deg 1.5 s later" % (d["kn"], d["upright"], d["still"], d["after"]))


def s_bring_boat(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("bring", {})
    if first:
        pc.back_to_the_boat()       # standing on the deck, to be carried along
        return
    if e >= 1.0 and "flying" not in d:
        d["flying"] = True
        d["aboard"] = walker.is_standing_on_boat()
        pc.set_flying(True)
        cam = pc.get_dev_camera()
        start = boat.get_actor_location() + unreal.Vector(8000, 5000, 1500)
        cam.set_actor_location_and_rotation(start, unreal.Rotator(roll=0.0, pitch=-45.0, yaw=30.0), False, True)
        pc.set_dev_camera_mode(unreal.RiptideDevCameraMode.FREE)
        return
    if e >= 1.7 and "brought" not in d:
        cam = pc.get_dev_camera()
        eye, look = cam.get_actor_location(), cam.get_actor_rotation().get_forward_vector()
        expect = eye + look * ((eye.z - 0.0) / -look.z)
        pc.bring_boat_here()
        d["brought"] = True
        off = math.hypot(boat.get_actor_location().x - expect.x, boat.get_actor_location().y - expect.y)
        check("bring the boat here: onto the sea where the camera looks", off < 300.0 and abs(boat.get_actor_rotation().yaw - 30.0) < 1.0,
              "%.0f cm from where the view meets the sea" % off)
    if e >= 3.0 and "crew" not in d:
        d["crew"] = True
        check("the crew member standing on deck comes along with the boat", d["aboard"] and walker.is_standing_on_boat()
              and (walker.get_actor_location() - boat.get_actor_location()).length() < 500.0,
              "on deck before %s, after %s, %.0f cm from the boat's middle" % (d["aboard"], walker.is_standing_on_boat(),
                                                                          (walker.get_actor_location() - boat.get_actor_location()).length()))


def s_god_mode(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"].setdefault("god", {"heights": []})
    if first:
        pc.set_flying(False)
        # Stood mid-deck, not holding on, at full throttle in a rough sea.
        walker.set_actor_location(world_at(boat, -200.0, 0.0, 20.0 + 92.0), False, True)
        walker.set_bracing(False)
        d["level_max"] = boat.get_component_by_class(unreal.BuoyancyComponent) and 0
        ocean = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WaterBodyOcean)[0]
        d["ocean"] = ocean
        d["moderate_max"] = ocean.get_water_body_component().get_max_wave_height()
        pc.set_sea_state(unreal.RiptideSeaState.ROUGH)
        d["rough_max"] = ocean.get_water_body_component().get_max_wave_height()
        d["stagger0"], d["knock0"] = walker.get_stagger_count(), walker.get_knockdown_count()
        d["in_water"] = d["frames"] = 0
        return
    # Hard over one way, then the other, every three seconds: the turns and the rough sea jolt the deck.
    boat.set_helm_input(0.0, 1.0 if int(e / 3.0) % 2 == 0 else -1.0)
    # The sea's surface at a spot near the boat, and whether the hull counts as in it.
    p = boat.get_actor_location() + unreal.Vector(2000, 0, 0)
    d["heights"].append(boat.get_sea_surface_z(p))
    d["frames"] += 1
    d["in_water"] += 1 if boat.get_component_by_class(unreal.BuoyancyComponent).is_in_water_body() else 0
    if e >= 14.0 and "control" not in d:
        d["control"] = (walker.get_stagger_count() - d["stagger0"], walker.get_knockdown_count() - d["knock0"])
        d["rough_range"] = max(d["heights"]) - min(d["heights"])
        pc.set_god_mode(True)
        walker.set_actor_location(world_at(boat, -200.0, 0.0, 20.0 + 92.0), False, True)
        d["stagger1"], d["knock1"] = walker.get_stagger_count(), walker.get_knockdown_count()
        d["standing"] = d["standframes"] = 0
    if "control" in d:
        d["standframes"] += 1
        d["standing"] += 1 if walker.is_standing_on_boat() else 0


def s_god_verdict(e, first):
    world, pc, boat, walker = ctx()
    d = state["data"]["god"]
    if first:
        thrown = (walker.get_stagger_count() - d["stagger1"], walker.get_knockdown_count() - d["knock1"])
        check("rough sea state: bigger waves, and the boat still floats in them",
              d["rough_max"] > 1.8 * d["moderate_max"] and d["rough_range"] > 150.0 and d["in_water"] > 0.9 * d["frames"],
              "max wave %.0f cm (moderate %.0f), surface rose and fell %.0f cm at one spot, hull in the water %d/%d frames"
              % (d["rough_max"], d["moderate_max"], d["rough_range"], d["in_water"], d["frames"]))
        check("without god mode, standing unbraced at speed in a rough sea throws the crew member (control)",
              d["control"][0] > 0, "thrown %d times, knocked down %d" % d["control"])
        check("god mode: never thrown or knocked down, and the tank stays full",
              thrown == (0, 0) and boat.get_fuel_liters() > 449.0 and d["standing"] > 0.8 * d["standframes"],
              "thrown %d, knocked down %d, fuel %.0f L, standing %d/%d frames" % (thrown[0], thrown[1], boat.get_fuel_liters(),
                                                                                d["standing"], d["standframes"]))
        boat.set_helm_input(0.0, 0.0)
        pc.set_sea_state(unreal.RiptideSeaState.CALM)
        d["calm_max"] = d["ocean"].get_water_body_component().get_max_wave_height()
        d["calm_heights"] = []
        pc.set_god_mode(False)
        return
    d["calm_heights"].append(boat.get_sea_surface_z(boat.get_actor_location() + unreal.Vector(2000, 0, 0)))
    if e >= 5.0 and "calm" not in d:
        d["calm"] = True
        rng = max(d["calm_heights"]) - min(d["calm_heights"])
        check("calm sea state: small waves", d["calm_max"] < 0.4 * d["moderate_max"] and rng < 60.0,
              "max wave %.0f cm, surface rose and fell %.0f cm" % (d["calm_max"], rng))
        pc.set_sea_state(unreal.RiptideSeaState.MODERATE)
        check("moderate sea state brings back the level's own swell", abs(d["ocean"].get_water_body_component().get_max_wave_height()
                                                                          - d["moderate_max"]) < 1.0
              and pc.get_sea_state() == unreal.RiptideSeaState.MODERATE, "")


def s_view(e, first):
    world, pc, boat, walker = ctx()
    if first:
        pc.set_physics_overlay(True)
        pc.set_photo_mode(True)
        hidden = not pc.are_screen_messages_shown() and not pc.is_dev_panel_on_screen()
        pc.set_photo_mode(False)
        back = pc.are_screen_messages_shown() and pc.is_dev_panel_on_screen()
        check("photo mode hides the prompts and the dev panel, and brings them back", hidden and back, "")
        suns = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.DirectionalLight)
        sun = suns[0]
        pitches = {}
        for tod in (unreal.RiptideTimeOfDay.GOLDEN_HOUR, unreal.RiptideTimeOfDay.DUSK, unreal.RiptideTimeOfDay.NIGHT,
                    unreal.RiptideTimeOfDay.DAY):
            pc.set_time_of_day(tod)
            pitches[str(tod)] = round(sun.get_actor_rotation().pitch, 1)
        movable = sun.root_component.get_editor_property("mobility") == unreal.ComponentMobility.MOVABLE
        values = list(pitches.values())
        check("time of day turns the sun (made movable) through golden hour, dusk, night (the moon) and back to day",
              movable and values == [-6.0, 3.0, -40.0, -35.0], "sun pitch %s" % pitches)


def s_end(e, first):
    world, pc, boat, walker = ctx()
    if first:
        pc.set_physics_overlay(False)
        ok = all(p for _, p, _ in state["checks"])
        log("RESULT %s" % ("PASS" if ok else "FAIL"))
        finish()


STEPS = [
    ("settle", 10.0, s_settle),
    ("refuel", 0.5, s_refuel),
    ("fly from foot", 5.0, s_fly_from_foot),
    ("fly back", 0.5, s_fly_back_to_foot),
    ("ride along", 3.5, s_ride_along),
    ("noclip", 2.0, s_noclip),
    ("orbit", 3.0, s_orbit),
    ("chase", 4.0, s_chase),
    ("slow motion", 2.5, s_slow_motion),
    ("freeze", 3.0, s_freeze),
    ("drop in on deck", 3.2, s_drop_in_deck),
    ("fly from helm", 6.0, s_fly_from_helm),
    ("drop in at sea", 7.0, s_drop_in_sea),
    ("right the boat", 2.0, s_right_boat),
    ("bring the boat", 3.5, s_bring_boat),
    ("god mode", 26.0, s_god_mode),
    ("god verdict", 5.5, s_god_verdict),
    ("view", 0.5, s_view),
    ("end", 1.0, s_end),
]


def finish():
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(_dt):
    if state.get("in_tick"):
        return
    state["in_tick"] = True
    try:
        _tick()
    except Exception as err:  # noqa: BLE001 - report and quit instead of hanging the editor
        import traceback
        log("RESULT FAIL (script error in step %s: %r)\n%s" % (STEPS[min(state["step"], len(STEPS) - 1)][0], err, traceback.format_exc()))
        finish()
    finally:
        state["in_tick"] = False


def _tick():
    state["ticks"] += 1
    if not state["started"]:
        if state["ticks"] == 30:
            log("starting play-in-editor")
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
            state["started"] = True
        return
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        if state["ticks"] > 3000:
            log("RESULT FAIL (play-in-editor never started)")
            finish()
        return
    if "boat" not in state:
        boats = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBoat)
        walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        if not boats or not walkers or not pc:
            return
        state["boat"], state["walker"] = boats[0], walkers[0]
        log("boat %s, crew member %s, controller %s" % (boats[0].get_name(), walkers[0].get_name(), pc.get_class().get_name()))
        if not isinstance(pc, unreal.RiptidePlayerController):
            log("RESULT FAIL (the player controller isn't a RiptidePlayerController)")
            finish()
            return
    now = unreal.GameplayStatics.get_real_time_seconds(world)
    if state["step_start"] is None:
        state["step_start"] = now
        state["first"] = True
        log("step: %s" % STEPS[state["step"]][0])
    name, duration, fn = STEPS[state["step"]]
    elapsed = now - state["step_start"]
    fn(elapsed, state["first"])
    state["first"] = False
    if elapsed >= duration and state["step"] < len(STEPS) - 1:
        state["step"] += 1
        state["step_start"] = None


state["handle"] = unreal.register_slate_post_tick_callback(tick)
log("dev mode test registered")
