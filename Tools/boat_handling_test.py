"""Headless check that the boat floats and drives on Ocean_Test, and that the crew can walk its deck.

Run it with the editor closed (it quits the editor when done):
  UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/boat_handling_test.py"
(-ExecutePythonScript won't work: it closes the editor as soon as the script returns, before the test has run.)
Then read the "RiptideTest:" lines in Saved/Logs/Riptide.log. The last one says PASS or FAIL.
Use -RenderOffscreen instead of -nullrhi to also save first-person screenshots and report frame rate.
While the boat runs its course, the player's character walks laps of the whole deck (round the console, up to
the bow, across the aft deck), first at rest and then at speed and through the turns. At the end it takes the
helm and leaves it again.
To try tuning values without rebuilding, set RIPTIDE_TEST_SET before launching, e.g. "planing_lift=0,rock_damping=4".
"""

import math
import os

import unreal

# (start time in game seconds, throttle input, steer input, phase name)
SCRIPT = [
    (0.0, 0.0, 0.0, "settle"),          # launched at the waterline; checks use t >= 5
    (15.0, 1.0, 0.0, "throttle up"),     # lever reaches full ahead in under 2 s, then stays there
    (18.0, 0.0, 0.0, "full ahead"),
    (33.0, 0.0, 1.0, "hard right"),
    (43.0, 0.0, -1.0, "hard left"),
    (53.0, -1.0, 0.0, "throttle down"),  # lever back through idle toward reverse
    (55.0, 0.0, 0.0, "coast"),
    (63.0, 1.0, 0.0, "throttle up again"),
    (66.0, 0.0, 0.0, "trim neutral"),    # full ahead at neutral trim
    (78.0, 0.0, 0.0, "trim out"),        # trims out to the stop (bow should ride higher)
    (92.0, 0.0, 0.0, "trim in"),         # trims in to the stop (bow should ride lower)
]
END_TIME = 106.0
# Points on the deck (cm, boat frame) checked against the sea surface every frame.
DECK_POINTS = {"aft deck": (-300, 0, 20), "aft corner": (-360, 100, 20), "side deck": (0, 100, 20),
               "helm": (-110, 0, 20), "foredeck": (150, 0, 20), "bow": (300, 0, 29)}
TRIM_SWITCH = {"trim out": (1.0, 4.0), "trim in": (-1.0, 5.5)}   # phase -> (switch, seconds held from the phase's start)
TRIM_WINDOW = 10.0                                                 # bow pitch is averaged over each phase's last 10 s (the swell rocks it)
LOG_EVERY = 1.0

# When the editor renders (run with -RenderOffscreen instead of -nullrhi), grab first-person shots
# into Saved/Screenshots and report frame rate. Shots: at rest, full ahead, mid-turn.
RENDERING = "-nullrhi" not in unreal.SystemLibrary.get_command_line().lower()
SHOTS = [(13.0, "at_rest"), (30.0, "full_ahead"), (38.0, "hard_right")]

# Set RIPTIDE_TEST_AUDIO=1 to log the boat's sounds (playing, volume, pitch) every 10 s. Launch with
# -DeterministicAudio so the game mixes audio without playing it out loud. Those volumes and pitches feed
# Tools/model_boat_mix.py, which checks the loudness of the mix.
AUDIO_LOG = bool(os.environ.get("RIPTIDE_TEST_AUDIO"))

# Laps of the deck, as points on the boat (cm, X forward, Y starboard): from the helm down the port side past the
# console, up to the bow, back down the starboard side, round the aft deck, and back to the helm.
WALK_ROUTE = [(-110, 0), (-110, -85), (60, -85), (150, -40), (300, 0), (150, 40), (60, 85), (-85, 85), (-200, 85),
              (-330, 70), (-330, -70), (-200, -85), (-110, -85), (-110, 0)]
WALK_START, WALK_END = 6.0, 52.0          # walks laps from settling in until the turns end, finishing the last lap at the helm
WALK_REACHED_CM = 35.0
WALK_STUCK_S = 8.0                          # longest a single leg of the route may take
HELM_TAKE_T, HELM_LEAVE_T = 58.0, 61.0

state = {"ticks": 0, "started": False, "boat": None, "walker": None,
         "walk": {"leg": 1, "leg_start": None, "laps": 0, "laps_underway": 0, "stuck": [], "off_deck": 0,
                  "on_deck": 0, "lowest_on_deck": 1e9, "highest_on_deck": -1e9, "lap_started_underway": False},
         "helm": {}, "next_log": 0.0, "samples": [], "handle": None,
         "frame_times": [], "shots_taken": 0, "bow": {}}


def log(msg):
    unreal.log(f"RiptideTest: {msg}")


def phase_at(t):
    current = SCRIPT[0]
    for entry in SCRIPT:
        if t >= entry[0]:
            current = entry
    return current


def sample(boat, t, phase):
    loc = boat.get_actor_location()
    rot = boat.get_actor_rotation()
    s = {
        "t": t, "phase": phase, "x": loc.x, "y": loc.y, "z": loc.z,
        "pitch": rot.pitch, "roll": rot.roll, "yaw": rot.yaw,
        "kn": boat.get_speed_knots(), "lever": boat.get_throttle_lever(),
        "engine": boat.get_engine_output(), "prop": boat.is_propeller_submerged(),
        "fuel": boat.get_fuel_liters(), "trim": boat.get_trim_deg(),
    }
    for comp in boat.get_components_by_class(unreal.StaticMeshComponent):
        if comp.get_name() == "MotorMesh":
            r = comp.get_editor_property("relative_rotation")
            s["motor_yaw"], s["motor_pitch"] = r.yaw, r.pitch
    state["samples"].append(s)
    log("t=%5.1f %-13s z=%7.1f pitch=%6.1f roll=%6.1f yaw=%7.1f speed=%5.1fkn lever=%+.2f engine=%+.2f prop=%s fuel=%.2f trim=%+.0f motor yaw %+.0f pitch %+.0f"
        % (t, phase, s["z"], s["pitch"], s["roll"], s["yaw"], s["kn"], s["lever"], s["engine"],
           "wet" if s["prop"] else "DRY", s["fuel"], s["trim"], s.get("motor_yaw", 0), s.get("motor_pitch", 0)))
    if AUDIO_LOG and int(t) % 10 == 0:
        for audio in boat.get_components_by_class(unreal.AudioComponent):
            sound = audio.get_editor_property("sound")
            log("  audio %s: sound=%s playing=%s volume=%.2f pitch=%.2f" % (
                audio.get_name(), sound.get_name() if sound else None, audio.is_playing(),
                audio.get_editor_property("volume_multiplier"), audio.get_editor_property("pitch_multiplier")))


def local_point(boat, world_loc):
    return boat.get_actor_transform().inverse_transform_location(world_loc)


def walk(world, boat, walker, t):
    """Steers the character round WALK_ROUTE, as a player holding the stick would, and records how it went."""
    w = state["walk"]
    half = walker.get_editor_property("capsule_component").get_scaled_capsule_half_height()
    feet = walker.get_actor_location() - unreal.Vector(0, 0, half)
    local = local_point(boat, feet)
    if boat.get_helmsman() == walker:
        pass  # driving: stands at the helm out of sight
    elif walker.is_standing_on_boat():
        w["on_deck"] += 1
        # Standing on the deck, the feet should glide with it. A jump of more than 10 cm in one frame is the
        # character being shoved (it overlapped something), and shows as the view popping.
        if w.get("prev_z") is not None and abs(local.z - w["prev_z"]) > 10.0:
            w.setdefault("hops", []).append((round(local.x), round(local.y), round(local.z - w["prev_z"]), round(t, 1)))
        w["prev_z"] = local.z
        w["lowest_on_deck"] = min(w["lowest_on_deck"], local.z)
        if local.z > w["highest_on_deck"]:
            w["highest_on_deck"] = local.z
            w["highest_at"] = (round(local.x), round(local.y), round(t, 1))
    else:
        w["off_deck"] += 1
        w["prev_z"] = None
    if t < WALK_START or (t >= WALK_END and w["leg"] == 1):
        return

    if w["leg_start"] is None:
        w["leg_start"] = t
        w["lap_started_underway"] = boat.get_speed_knots() > 5.0
    tx, ty = WALK_ROUTE[w["leg"]]
    dx, dy = tx - local.x, ty - local.y
    if math.hypot(dx, dy) < WALK_REACHED_CM:
        w["leg"] += 1
        w["leg_start"] = t
        if w["leg"] == len(WALK_ROUTE):
            w["laps"] += 1
            if w["lap_started_underway"]:
                w["laps_underway"] += 1
            log("t=%5.1f walked a lap of the deck (%d so far, %d underway)" % (t, w["laps"], w["laps_underway"]))
            w["leg"] = 1
            w["lap_started_underway"] = boat.get_speed_knots() > 5.0
        return
    if t - w["leg_start"] > WALK_STUCK_S:
        w["stuck"].append((t, WALK_ROUTE[w["leg"] - 1], (tx, ty), (round(local.x), round(local.y))))
        log("t=%5.1f STUCK walking to %s, at %.0f, %.0f on the boat" % (t, (tx, ty), local.x, local.y))
        w["leg"] = w["leg"] % (len(WALK_ROUTE) - 1) + 1
        w["leg_start"] = t
        return
    # The direction to the next point, turned from the boat's frame into the world.
    direction = boat.get_actor_transform().transform_direction(unreal.Vector(dx, dy, 0))
    direction.z = 0
    walker.add_movement_input(direction.normal(), 1.0)


def helm_swap(world, boat, walker, t):
    """Takes the helm from the helm's spot, then leaves it: the player should drive the boat, then be back on foot."""
    h = state["helm"]
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if t >= HELM_TAKE_T and "took" not in h:
        h["at_helm"] = walker.is_at_helm()
        walker.try_take_helm()
        h["took"] = pc.get_controlled_pawn() == boat and boat.get_helmsman() == walker
        log("t=%5.1f take the helm: standing at it %s, now driving %s" % (t, h["at_helm"], h["took"]))
    if t >= HELM_LEAVE_T and h.get("took") and "left" not in h:
        boat.leave_helm()
        h["left"] = pc.get_controlled_pawn() == walker and boat.get_helmsman() is None
        log("t=%5.1f leave the helm: back on foot %s" % (t, h["left"]))
    if "left" in h and "stood" not in h and t >= HELM_LEAVE_T + 1.5:
        h["stood"] = walker.is_standing_on_boat()
        log("t=%5.1f standing on the deck after leaving the helm: %s" % (t, h["stood"]))


def in_phase(name):
    return [s for s in state["samples"] if s["phase"] == name]


def yaw_change(samples):
    total = 0.0
    for a, b in zip(samples, samples[1:]):
        total += (b["yaw"] - a["yaw"] + 180.0) % 360.0 - 180.0
    return total


def verdict():
    checks = []
    settle = [s for s in in_phase("settle") if s["t"] >= 5.0]
    ahead = in_phase("full ahead")
    right = in_phase("hard right")
    left = in_phase("hard left")
    coast = in_phase("coast")

    if settle:
        # The hull rides the swell, so allow the wave height on top of a metre of slack.
        slack = 100.0 + state.get("wave_cm", 0.0)
        zs = [s["z"] for s in settle]
        checks.append(("floats on the surface (rides within the swell)", all(-slack < z < slack for z in zs),
                       "z %.0f..%.0f cm, allowed +/-%.0f" % (min(zs), max(zs), slack)))
        # A small boat rocks and lifts its prop on big swells, so these limits loosen with wave height.
        rough = state.get("wave_cm", 0.0) > 150.0
        tilt_limit = 30.0 if rough else 15.0
        wet_needed = 0.7 if rough else 1.0
        worst = max(max(abs(s["pitch"]), abs(s["roll"])) for s in settle)
        wet = sum(1 for s in settle if s["prop"]) / len(settle)
        checks.append(("stays upright at rest (pitch/roll under %.0f deg)" % tilt_limit, worst < tilt_limit,
                       "worst %.0f deg" % worst))
        checks.append(("prop is in the water at rest (%.0f%% of the time)" % (wet_needed * 100), wet >= wet_needed,
                       "wet %.0f%%" % (wet * 100)))
    else:
        checks.append(("settle samples exist", False, ""))

    if ahead:
        dist = math.hypot(ahead[-1]["x"] - ahead[0]["x"], ahead[-1]["y"] - ahead[0]["y"]) / 100.0
        top = max(s["kn"] for s in ahead)
        checks.append(("throttle lever holds at full ahead after the key is let go",
                       all(s["lever"] > 0.95 for s in ahead), ""))
        checks.append(("moves under power (over 5 kn)", top > 5.0, "top %.1f kn, %.0f m in %.0f s" % (top, dist, ahead[-1]["t"] - ahead[0]["t"])))
        slack = 150.0 + state.get("wave_cm", 0.0)
        checks.append(("still floating at speed", all(-slack < s["z"] < slack for s in ahead), ""))
    if right and left:
        r, l = yaw_change(right), yaw_change(left)
        checks.append(("steering right turns right (yaw increases)", r > 20, "%+.0f deg" % r))
        checks.append(("steering left turns left (yaw decreases)", l < -20, "%+.0f deg" % l))
    if coast:
        checks.append(("slows down after throttle is pulled back", coast[-1]["kn"] < max(s["kn"] for s in ahead) * 0.6 if ahead else False,
                       "%.1f kn at end" % coast[-1]["kn"]))
    checks.append(("never flipped", all(abs(s["roll"]) < 60 and abs(s["pitch"]) < 60 for s in state["samples"]), ""))

    for phase in ("settle", "full ahead", "hard right"):
        frames = state["bow"].get(phase, [])
        if frames:
            under = sum(1 for fb, _ in frames if fb < 0) / len(frames)
            log("bow in %-10s: lowest %+.0f cm above water, under water %.0f%% of the time, pitch %.1f..%.1f deg (avg %.1f)"
                % (phase, min(fb for fb, _ in frames), under * 100, min(p for _, p in frames), max(p for _, p in frames),
                   sum(p for _, p in frames) / len(frames)))
    ahead_bow = state["bow"].get("full ahead", [])
    if ahead_bow:
        under = sum(1 for fb, _ in ahead_bow if fb < 0) / len(ahead_bow)
        checks.append(("bow stays above water at full ahead (under less than 5% of the time)", under < 0.05,
                       "under %.0f%%" % (under * 100)))
        high = sum(1 for _, p in ahead_bow if p > 12.0) / len(ahead_bow)
        checks.append(("bow doesn't leap at full ahead (pitched up over 12 deg less than 5% of the time)", high < 0.05,
                       "over 12 deg %.0f%%, highest %.0f deg" % (high * 100, max(p for _, p in ahead_bow))))

    f = state.get("flips")
    if f:
        checks.append(("prop reading is steady (never flips back within 10 frames)", f["short"] == 0,
                       "%d flips in %d frames, %d of them quick"
                       % (f["prop"], f["frames"], f["short"])))
        checks.append(("hull never reads as out of the water while it's in it", f["water"] == 0, "%d glitches" % f["water"]))

    right_turn = [s for s in right if s["t"] >= right[0]["t"] + 1.0] if right else []
    if right_turn:
        yaws = [s.get("motor_yaw", 0.0) for s in right_turn]
        checks.append(("the outboards swing with the steering", all(y < -25.0 for y in yaws),
                       "motor yaw %.0f..%.0f deg steering hard right" % (min(yaws), max(yaws))))
    dw = state.get("deck_wet")
    if dw:
        for name in DECK_POINTS:
            log("sea above the %-10s %4.1f%% of frames, at most %+.0f cm" % (
                name, 100.0 * dw["wet"].get(name, 0) / dw["frames"], dw["worst"].get(name, 0)))
    tp = state.get("trim_pitch", {})
    if all(k in tp for k in ("trim neutral", "trim out", "trim in")):
        avg = {k: sum(v) / len(v) for k, v in tp.items()}
        checks.append(("trimming out lifts the bow", avg["trim out"] > avg["trim neutral"] + 1.0,
                       "bow pitch %.1f deg trimmed out vs %.1f neutral" % (avg["trim out"], avg["trim neutral"])))
        checks.append(("trimming in lowers the bow", avg["trim in"] < avg["trim neutral"] - 0.5,
                       "bow pitch %.1f deg trimmed in vs %.1f neutral" % (avg["trim in"], avg["trim neutral"])))

    w, h, walker = state["walk"], state["helm"], state["walker"]
    if walker:
        checks.append(("walks laps of the whole deck at rest and underway (round the console, bow, aft deck)",
                       w["laps"] >= 2 and w["laps_underway"] >= 1,
                       "%d laps, %d started underway" % (w["laps"], w["laps_underway"])))
        checks.append(("never stuck on the deck", not w["stuck"], "; ".join("t=%.0f %s->%s at %s" % x for x in w["stuck"])))
        overboard = walker.get_overboard_count()
        checks.append(("never went overboard", overboard == 0, "%d times" % overboard))
        frac = w["on_deck"] / max(1, w["on_deck"] + w["off_deck"])
        checks.append(("keeps its footing (standing on the deck 95% of the time)", frac > 0.95,
                       "%.0f%%, feet %.0f..%.0f cm on the boat (deck is 20, foredeck up to 34), highest at x, y, t = %s"
                       % (frac * 100, w["lowest_on_deck"], w["highest_on_deck"], w.get("highest_at"))))
        hops = w.get("hops", [])
        checks.append(("feet glide with the deck (never hop over 10 cm in a frame)", not hops,
                       "%d hops, first at x, y, dz, t = %s" % (len(hops), hops[:3]) if hops else ""))
        checks.append(("takes the helm and drives", bool(h.get("at_helm")) and bool(h.get("took")), ""))
        checks.append(("leaves the helm and stands on the deck again", bool(h.get("left")) and bool(h.get("stood")), ""))
    else:
        checks.append(("the player spawned on foot", False, ""))

    ok = True
    for name, passed, detail in checks:
        ok &= bool(passed)
        log("%s  %s  %s" % ("ok  " if passed else "FAIL", name, detail))
    log("RESULT %s" % ("PASS" if ok else "FAIL"))


def finish():
    unreal.unregister_slate_post_tick_callback(state["handle"])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(_dt):
    if state.get("in_tick"):
        return
    state["in_tick"] = True
    try:
        _tick(_dt)
    finally:
        state["in_tick"] = False


def _tick(_dt):
    state["ticks"] += 1
    try:
        if not state["started"]:
            # Let the editor finish loading the startup map before playing.
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

        if state["boat"] is None:
            boats = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBoat)
            if not boats:
                if state["ticks"] > 3000:
                    log("RESULT FAIL (no boat was spawned)")
                    finish()
                return
            state["boat"] = boats[0]
            log("boat found: %s" % state["boat"].get_name())
            walkers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
            state["walker"] = walkers[0] if walkers else None
            log("player on foot: %s" % (state["walker"].get_name() if state["walker"] else None))
            # RIPTIDE_TEST_HIDE="HullMesh,MotorMesh" hides those boat parts, for measuring what they cost to draw.
            for name in filter(None, os.environ.get("RIPTIDE_TEST_HIDE", "").split(",")):
                for comp in state["boat"].get_components_by_class(unreal.PrimitiveComponent):
                    if comp.get_name() == name.strip():
                        comp.set_visibility(False)
                        log("hidden %s" % name.strip())
            if os.environ.get("RIPTIDE_TEST_CALM"):
                # Flat water: shows how the hull trims on its own, without swell tilting it.
                for ocean in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WaterBodyOcean):
                    ocean.set_water_waves(None)
                state["wave_cm"] = 0.0
                log("calm water (waves off)")
            for pair in filter(None, os.environ.get("RIPTIDE_TEST_SET", "").split(",")):
                name, value = pair.split("=")
                state["boat"].set_editor_property(name.strip(), float(value))
                log("override %s = %s" % (name.strip(), value))
            for ocean in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WaterBodyOcean):
                try:
                    wave_cm = ocean.get_water_body_component().get_max_wave_height()
                    state["wave_cm"] = wave_cm
                    log("ocean max wave height %.0f cm" % wave_cm)
                except Exception as err:  # noqa: BLE001 - informational only
                    log("could not read wave height: %s" % err)

        boat = state["boat"]
        t = unreal.GameplayStatics.get_time_seconds(world)
        start, throttle, steer, phase = phase_at(t)
        boat.set_helm_input(throttle, steer)
        switch, held = TRIM_SWITCH.get(phase, (0.0, 0.0))
        boat.set_trim_input(switch if t - start < held else 0.0)
        if phase.startswith("trim"):
            end = next((e[0] for e in SCRIPT if e[0] > start), END_TIME)
            if t >= end - TRIM_WINDOW:
                state.setdefault("trim_pitch", {}).setdefault(phase, []).append(boat.get_actor_rotation().pitch)
        if state["walker"]:
            walk(world, boat, state["walker"], t)
            helm_swap(world, boat, state["walker"], t)

        # How often the prop and in-water readings flip, every frame: real ventilation comes and goes over a swell,
        # but a reading that flips back and forth every few frames is a glitch (the HUD and engine sound stutter).
        # Counted once the boat has settled: at spawn it can drop off the crest it was launched on.
        flips = state.setdefault("flips", {"prop": 0, "water": 0, "frames": 0, "last": None, "short": 0, "since": 0})
        if t < 5.0:
            flips["last"] = None
        reading = (boat.is_propeller_submerged(), boat.get_component_by_class(unreal.BuoyancyComponent).is_in_water_body())
        if flips["last"] is not None:
            # Only while the hull is in the water: in the air (off a crest) the props rightly read dry.
            if reading[0] != flips["last"][0] and reading[1] and flips["last"][1]:
                flips["prop"] += 1
                if flips["since"] < 10:
                    flips["short"] += 1
                flips["since"] = 0
            if reading[1] != flips["last"][1]:
                # Leaving the water for real (catching air off a crest) is fine; reading "out" with the sea still
                # above the keel is the glitch.
                keel = boat.get_actor_transform().transform_location(unreal.Vector(0, 0, -35))
                surface = boat.get_sea_surface_z(keel)
                genuine = reading[1] or surface < keel.z
                if not genuine:
                    flips["water"] += 1
                log("t=%5.2f hull reads %s the water (sea %s cm, keel %.0f cm)%s" % (
                    t, "in" if reading[1] else "OUT OF", "%.0f" % surface, keel.z,
                    "" if genuine else "  GLITCH"))
        flips["since"] += 1
        flips["last"] = reading
        flips["frames"] += 1

        # Sea above the deck: where the water surface is higher than the deck, it shows through the floor.
        if t >= 5.0:
            xf = boat.get_actor_transform()
            deck = state.setdefault("deck_wet", {"frames": 0, "wet": {}, "worst": {}})
            deck["frames"] += 1
            for name, (dx, dy, dz) in DECK_POINTS.items():
                p = xf.transform_location(unreal.Vector(dx, dy, dz))
                above = boat.get_sea_surface_z(p) - p.z
                if above > 0.0:
                    deck["wet"][name] = deck["wet"].get(name, 0) + 1
                deck["worst"][name] = max(deck["worst"].get(name, -1e9), above)

        # Bow height above the water, every frame, so brief dives into a swell aren't missed between samples.
        if t >= 5.0:
            bow = state["bow"].setdefault(phase, [])
            bow.append((boat.get_bow_freeboard_cm(), boat.get_actor_rotation().pitch))

        if t >= state["next_log"]:
            sample(boat, t, phase)
            state["next_log"] += LOG_EVERY

        if RENDERING:
            if t >= 10.0:
                state["frame_times"].append(_dt)
            if state["shots_taken"] < len(SHOTS) and t >= SHOTS[state["shots_taken"]][0]:
                name = "RiptideTest_%s" % SHOTS[state["shots_taken"]][1]
                # The console command is queued for the next frame; calling the screenshot API directly
                # re-enters this tick callback and recurses.
                unreal.SystemLibrary.execute_console_command(world, "HighResShot 1920x1080 filename=%s" % name)
                log("screenshot %s" % name)
                state["shots_taken"] += 1

        if t >= END_TIME:
            if state["frame_times"]:
                ft = sorted(state["frame_times"])
                avg = sum(ft) / len(ft)
                slow = ft[int(len(ft) * 0.99) - 1]
                log("frame rate: average %.0f fps, 1%% low %.0f fps (editor, includes editor overhead)"
                    % (1.0 / avg, 1.0 / slow))
            verdict()
            finish()
    except Exception as err:  # noqa: BLE001 - report and quit instead of hanging the editor
        log("RESULT FAIL (script error: %r)" % err)
        finish()


state["handle"] = unreal.register_slate_post_tick_callback(tick)
log("handling test registered")
