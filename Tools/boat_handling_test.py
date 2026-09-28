"""Headless check that the boat floats and drives on Ocean_Test.

Run it with the editor closed (it quits the editor when done):
  UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/boat_handling_test.py"
(-ExecutePythonScript won't work: it closes the editor as soon as the script returns, before the test has run.)
Then read the "RiptideTest:" lines in Saved/Logs/Riptide.log. The last one says PASS or FAIL.
Use -RenderOffscreen instead of -nullrhi to also save helm-camera screenshots and report frame rate.
"""

import math

import unreal

# (start time in game seconds, throttle input, steer input, phase name)
SCRIPT = [
    (0.0, 0.0, 0.0, "settle"),          # spawns 1.5 m up and drops in; checks use t >= 5
    (15.0, 1.0, 0.0, "throttle up"),     # lever reaches full ahead in under 2 s, then stays there
    (18.0, 0.0, 0.0, "full ahead"),
    (33.0, 0.0, 1.0, "hard right"),
    (43.0, 0.0, -1.0, "hard left"),
    (53.0, -1.0, 0.0, "throttle down"),  # lever back through idle toward reverse
    (55.0, 0.0, 0.0, "coast"),
]
END_TIME = 65.0
LOG_EVERY = 1.0

# When the editor renders (run with -RenderOffscreen instead of -nullrhi), grab helm-camera shots
# into Saved/Screenshots and report frame rate. Shots: at rest, full ahead, mid-turn.
RENDERING = "-nullrhi" not in unreal.SystemLibrary.get_command_line().lower()
SHOTS = [(13.0, "at_rest"), (30.0, "full_ahead"), (38.0, "hard_right")]

state = {"ticks": 0, "started": False, "boat": None, "next_log": 0.0, "samples": [], "handle": None,
         "frame_times": [], "shots_taken": 0}


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
        "fuel": boat.get_fuel_liters(),
    }
    state["samples"].append(s)
    log("t=%5.1f %-13s z=%7.1f pitch=%6.1f roll=%6.1f yaw=%7.1f speed=%5.1fkn lever=%+.2f engine=%+.2f prop=%s fuel=%.2f"
        % (t, phase, s["z"], s["pitch"], s["roll"], s["yaw"], s["kn"], s["lever"], s["engine"],
           "wet" if s["prop"] else "DRY", s["fuel"]))


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
