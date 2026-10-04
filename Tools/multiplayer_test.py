"""Two-player check: one copy of the game hosts from the main menu, a second finds it and joins, and leaves again.

Run it with any Python 3 (the editor must be closed; it starts two copies of the game itself, headless):
  python Tools/multiplayer_test.py            (add --shots to render the client and save screenshots of the
                                               in-game menu, its settings and the join browser)
It prints PASS or FAIL, with each check. Set UE_EDITOR to UnrealEditor's path if the engine isn't in the default place.

Both copies run with -nosteam, on the Null online subsystem, as playing without Steam does:
- the host sets its callsign to Alpha-1, opens the main menu's Host game (public), and is put on the boat;
- the client, as Bravo-7 with its own look, searches the local network from the Join screen's code, joins Alpha-1's
  game when it shows up (or connects to 127.0.0.1 if the LAN search doesn't find it), and checks it's on the deck in
  its own crew member, with the name and look it sent, as everyone sees them;
- the host checks the same from its side (a second crew member on the deck, named Bravo-7, in Bravo-7's look);
- the host takes a fishing rod out and casts off the beach, and the client sees the rod in its hands and its line out;
- the client leaves to the main menu (it must arrive there with the menu up), and the host sees it go;
- the client joins again by IP: it comes back where it left, carrying the flint the host gave it (the world's save
  keeps every crew member's record), then leaves for good.

Inside the game the same file runs each side (picked by -RiptideTestRole=host or client); its lines in each side's
log start with "RiptideMPTest".
"""

import os
import subprocess
import sys
import math
import time

ROLE_FLAG = "-RiptideTestRole="
HOST_NAME, CLIENT_NAME = "Alpha-1", "Bravo-7"
CLIENT_LOOK = "1.4.3.2.1.2.3.5.2.3.1.0.2.3.1"   # female, brown skin, long dark hair... (gear choices kept but not worn), shirt, shorts, sandals
GAME_MAP = "Island_Test"     # what hosting loads (URiptideGameInstance::GameMap)
TIMEOUT_S = 720


# --- The launcher (plain Python, outside the game) ---

def _editor_path():
    if os.environ.get("UE_EDITOR"):
        return os.environ["UE_EDITOR"]
    if sys.platform == "darwin":
        return "/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
    return r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"


def _launch(role, project, script, log, render):
    args = [_editor_path(), project, "-game", "-RenderOffscreen" if render else "-nullrhi", "-windowed", "-ResX=1280", "-ResY=720",
            "-unattended", "-nosplash", "-nosound", "-nosteam", f"-abslog={log}", f"{ROLE_FLAG}{role}",
            "-RiptideSaveSlot=MultiplayerTest"]
    if sys.platform == "win32":
        # Windows hands the game its command line as typed, so the script's switch needs its quotes just so.
        line = subprocess.list2cmdline(args) + f' -ExecCmds="py {script}"'
        return subprocess.Popen(line)
    return subprocess.Popen(args + [f"-ExecCmds=py {script}"])


def _results(log):
    lines = []
    if os.path.exists(log):
        with open(log, encoding="utf-8", errors="replace") as f:
            lines = [l.rstrip() for l in f if "RiptideMPTest" in l]
    return lines


def launch(shots):
    project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    project = os.path.join(project_dir, "Riptide.uproject")
    script = os.path.abspath(__file__).replace("\\", "/")
    logs = {r: os.path.join(project_dir, "Saved", "Logs", f"MultiplayerTest_{r}.log") for r in ("host", "client")}
    for log in logs.values():
        if os.path.exists(log):
            os.remove(log)
    host = _launch("host", project, script, logs["host"], False)
    time.sleep(10)   # the host is set up first; the client searches until its game shows anyway
    client = _launch("client", project, script, logs["client"], shots)
    start = time.time()
    for proc in (client, host):
        try:
            proc.wait(timeout=max(1, TIMEOUT_S - (time.time() - start)))
        except subprocess.TimeoutExpired:
            proc.kill()
            print(f"{'host' if proc is host else 'client'}: timed out, killed")
    ok = True
    for role, log in logs.items():
        lines = _results(log)
        print(f"--- {role} ({log})")
        for line in lines:
            print("  " + line.split("RiptideMPTest", 1)[1])
        passed = any("RESULT PASS" in l for l in lines)
        ok &= passed
        if not passed and not any("RESULT" in l for l in lines):
            print("  (no result: it crashed, hung or never started)")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


# --- Each side, inside the game ---

def run_in_game(role):
    import unreal

    st = {"t0": time.time(), "step": "start", "since": time.time(), "checks": [], "handle": None, "done": False}
    shots = "-renderoffscreen" in unreal.SystemLibrary.get_command_line().lower()

    def log(msg):
        unreal.log(f"RiptideMPTest[{role}]: {msg}")

    def check(name, ok, detail=""):
        st["checks"].append(ok)
        log(f"{'ok  ' if ok else 'FAIL'} {name}{(': ' + detail) if detail else ''}")

    def finish(extra_fail=None):
        if st["done"]:
            return
        st["done"] = True
        if extra_fail:
            check(extra_fail, False)
        log("RESULT %s" % ("PASS" if st["checks"] and all(st["checks"]) else "FAIL"))
        unreal.unregister_slate_post_tick_callback(st["handle"])
        unreal.SystemLibrary.quit_editor()

    def game():
        for o in unreal.ObjectIterator(unreal.RiptideGameInstance):
            if not o.get_name().startswith("Default__"):
                return o
        return None

    def go(step):
        st["step"], st["since"] = step, time.time()
        log(f"-> {step}")

    def waited():
        return time.time() - st["since"]

    def cmd(c):
        unreal.SystemLibrary.execute_console_command(game().get_world(), c)

    def look_of(player_state):
        return ".".join(str(c) for c in player_state.get_appearance().choices)

    def tick(_dt):
        if st["done"]:
            return
        try:
            gi = game()
            world = gi.get_world() if gi else None
            if not world:
                return
            map_name = gi.get_current_map_name()
            step = st["step"]

            if role == "host":
                if step == "start" and map_name == "MainMenu" and time.time() - st["t0"] > 4:
                    gi.set_profile_from_text(HOST_NAME, "0.2.1.1.0.1.0.0.1.0.1.1", False)
                    check("hosting starts from the menu", gi.host_game(False))
                    go("hosting")
                elif step == "hosting":
                    if map_name == GAME_MAP and gi.is_hosting():
                        check("the host is in the game as a listen server", True)
                        go("waiting for the client")
                    elif waited() > 90:
                        finish("the host never got into the game")
                elif step == "waiting for the client":
                    states = unreal.GameplayStatics.get_game_state(world).player_array
                    if len(states) >= 2:
                        go("client joined")
                    elif waited() > 300:
                        finish("nobody joined within 5 minutes")
                elif step == "client joined" and waited() > 8:
                    states = list(unreal.GameplayStatics.get_game_state(world).player_array)
                    names = [s.get_player_name() for s in states]
                    log(f"crew: {names}")
                    check("the host's own crew member is named by its callsign", HOST_NAME in names, str(names))
                    client = next((s for s in states if s.get_player_name() == CLIENT_NAME), None)
                    check("the client joined under its callsign", client is not None, str(names))
                    if client:
                        check("the client's crew member wears the look it sent", look_of(client) == CLIENT_LOOK, look_of(client))
                        pawn = client.get_pawn()
                        check("the client has its own crew member", isinstance(pawn, unreal.RiptideCharacter), str(pawn))
                        if isinstance(pawn, unreal.RiptideCharacter):
                            check("the client's crew member stands on the island", pawn.get_component_by_class(unreal.CharacterMovementComponent).is_moving_on_ground())
                        if isinstance(pawn, unreal.RiptideCharacter):
                            pawn.give_item("flint", 2)      # to come back with (see "the client is back")
                    crew = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
                    check("two crew members are aboard", len(crew) == 2, str(len(crew)))
                    hud = unreal.GameplayStatics.get_player_controller(world, 0).get_hud()
                    check("the host's keyboard and mouse drive the game, not the menu it came from",
                          isinstance(hud, unreal.RiptideHUD) and hud.is_game_input_active())
                    go("radio")
                elif step == "radio":
                    # The host takes the boat's radio mic, switches it to the loudhailer, then back to the CB, and an
                    # NPC calls on the boat's channel: the client checks where the host's voice would come out.
                    pc = unreal.GameplayStatics.get_player_controller(world, 0)
                    crew = pc.get_controlled_pawn()
                    boat = crew.get_home_boat() if isinstance(crew, unreal.RiptideCharacter) else None
                    r = st.setdefault("radio", {})
                    if not boat:
                        # Castaways start on the beach with no boat: nothing to work but their own voices.
                        log("no boat in this game: the radio checks wait for one")
                        go("waiting for the client to leave")
                    elif "took" not in r:
                        crew.set_actor_location(boat.get_actor_transform().transform_location(unreal.Vector(-95.0, 25.0, 112.0)), False, True)
                        eye = crew.get_component_by_class(unreal.CameraComponent).get_world_location()
                        to = boat.get_mic_hook_location() - unreal.Vector(0, 0, 6) - eye
                        pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(to.z, math.hypot(to.x, to.y))),
                                                               yaw=math.degrees(math.atan2(to.y, to.x))))
                        crew.try_toggle_mic()
                        r["took"] = crew.is_holding_mic()
                        check("the host takes the radio mic", r["took"])
                        pc.get_component_by_class(unreal.RiptideVoiceComponent).toggle_mic_mode()
                        r["at"] = time.time()
                    elif boat and "back" not in r and time.time() - r["at"] > 8:
                        pc.get_component_by_class(unreal.RiptideVoiceComponent).toggle_mic_mode()
                        r["back"] = time.time()
                    elif boat and "back" in r and time.time() - r["back"] > 2 and time.time() - r.get("called", 0) > 2:
                        unreal.RiptideRadioSubsystem.radio_call(world, boat.get_radio_channel(), "Coast Guard", "Radio check, over")
                        r["called"] = time.time()
                        r["calls"] = r.get("calls", 0) + 1
                        if r["calls"] >= 6:
                            crew.try_toggle_mic()
                            go("waiting for the client to leave")
                elif step == "waiting for the client to leave":
                    # Meanwhile the host fishes off the beach, for the client to see (see its "watching the host fish").
                    pc = unreal.GameplayStatics.get_player_controller(world, 0)
                    me = pc.get_controlled_pawn()
                    if isinstance(me, unreal.RiptideCharacter) and not me.get_home_boat():
                        f = st.setdefault("fish", {"next": time.time() + 1.0})
                        angler = me.get_angler()
                        if str(me.get_held_item()) != "fishing_rod":
                            if "rod" not in f:
                                me.give_item("fishing_rod", 1)
                                starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideBeachStart)
                                f["yaw"] = (starts[0].get_actor_rotation().yaw + 180.0) if starts else 0.0
                                f["rod"] = True
                            me.hold_item("fishing_rod")
                        elif "release" in f and time.time() > f["release"]:
                            angler.primary_released()
                            del f["release"]
                            f["next"] = time.time() + 6.0
                        elif angler.get_state() == unreal.RiptideAnglerState.IDLE and time.time() > f["next"] and "release" not in f:
                            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=-5.0, yaw=f["yaw"]))
                            angler.primary_pressed()
                            f["release"] = time.time() + 0.9
                    states = list(unreal.GameplayStatics.get_game_state(world).player_array)
                    if len(states) == 1:
                        check("the host sees the client leave, and its game goes on", map_name == GAME_MAP)
                        go("waiting for the client to come back")
                    elif waited() > 180:
                        finish("the client never left")
                    else:
                        client = next((s for s in states if s.get_player_name() == CLIENT_NAME), None)
                        if client and isinstance(client.get_pawn(), unreal.RiptideCharacter):
                            st["client_at"] = client.get_pawn().get_actor_location()
                elif step == "waiting for the client to come back":
                    states = list(unreal.GameplayStatics.get_game_state(world).player_array)
                    client = next((s for s in states if s.get_player_name() == CLIENT_NAME), None)
                    if client and isinstance(client.get_pawn(), unreal.RiptideCharacter) and waited() > 0:
                        go("client is back")
                    elif waited() > 180:
                        finish("the client never came back")
                elif step == "client is back" and waited() > 6:
                    client = next((s for s in unreal.GameplayStatics.get_game_state(world).player_array if s.get_player_name() == CLIENT_NAME), None)
                    pawn = client.get_pawn() if client else None
                    if isinstance(pawn, unreal.RiptideCharacter) and "client_at" in st:
                        at, was = pawn.get_actor_location(), st["client_at"]
                        moved = math.hypot(at.x - was.x, at.y - was.y)
                        check("the client comes back where it left", moved < 60.0, f"{moved:.0f} cm from where it left")
                        check("with what it carried", unreal.RiptideDataLibrary.count_carried(pawn, "flint") == 2,
                              str(unreal.RiptideDataLibrary.count_carried(pawn, "flint")))
                    else:
                        check("the client comes back in its own crew member", False, str(pawn))
                    go("waiting for the client to leave again")
                elif step == "waiting for the client to leave again":
                    if len(unreal.GameplayStatics.get_game_state(world).player_array) == 1:
                        finish()
                    elif waited() > 120:
                        finish("the client never left the second time")

            else:
                if step == "start" and map_name == "MainMenu" and time.time() - st["t0"] > 4:
                    gi.set_profile_from_text(CLIENT_NAME, CLIENT_LOOK, False)
                    st["searches"] = 0
                    go("searching")
                elif step == "searching":
                    if gi.get_activity() == unreal.RiptideOnlineActivity.NONE:
                        found = list(gi.get_found_games())
                        index = next((i for i, g in enumerate(found) if g.host_name == HOST_NAME), -1)
                        if index >= 0:
                            g = found[index]
                            log(f"found {g.host_name}'s game: {g.players}/{g.max_players}, ping {g.ping_ms}")
                            check("the game browser finds the host's game on the local network", True)
                            check("the listing shows the host's crew count", g.players == 1 and g.max_players == 4,
                                  f"{g.players}/{g.max_players}")
                            if shots:
                                hud = unreal.GameplayStatics.get_player_controller(world, 0).get_hud()
                                hud.show_screen(unreal.RiptideMenuScreen.JOIN)
                                st["join_index"] = index
                                go("browser shot")
                            else:
                                check("joining the found game starts", gi.join_game(index))
                                go("joining")
                        elif st["searches"] >= 12:
                            log("the LAN search never found the host; joining by IP instead")
                            check("joining by IP starts", gi.join_by_address("127.0.0.1"))
                            go("joining")
                        elif waited() > 2:
                            st["searches"] += 1
                            gi.find_games()
                            st["since"] = time.time()
                elif step == "browser shot":
                    # The Join screen searches again when it opens; once it has listed the game, a picture of it.
                    if waited() > 8 and gi.get_activity() == unreal.RiptideOnlineActivity.NONE:
                        cmd("shot showui")
                        go("browser joining")
                elif step == "browser joining" and waited() > 1.5:
                    found = list(gi.get_found_games())
                    index = next((i for i, g in enumerate(found) if g.host_name == HOST_NAME), -1)
                    check("joining the found game starts", index >= 0 and gi.join_game(index))
                    go("joining")
                elif step == "joining":
                    if map_name == GAME_MAP and not gi.is_hosting():
                        go("joined")
                    elif waited() > 90:
                        finish("the client never got into the host's game")
                elif step == "joined" and waited() > 8:
                    pc = unreal.GameplayStatics.get_player_controller(world, 0)
                    me = pc.player_state
                    check("in the game as a client, under its callsign", me is not None and me.get_player_name() == CLIENT_NAME,
                          me.get_player_name() if me else "no player state")
                    if me:
                        check("its look came back from the host as it was sent", look_of(me) == CLIENT_LOOK, look_of(me))
                    pawn = pc.get_controlled_pawn()
                    check("it controls its own crew member", isinstance(pawn, unreal.RiptideCharacter), str(pawn))
                    if isinstance(pawn, unreal.RiptideCharacter):
                        check("its crew member stands on the island", pawn.get_component_by_class(unreal.CharacterMovementComponent).is_moving_on_ground())
                    crew = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RiptideCharacter)
                    check("it sees both crew members", len(crew) == 2, str(len(crew)))
                    hud = pc.get_hud()
                    check("the in-game menu's HUD is there", isinstance(hud, unreal.RiptideHUD), str(hud))
                    if isinstance(hud, unreal.RiptideHUD):
                        check("its keyboard and mouse drive the game, not the menu it came from", hud.is_game_input_active())
                    go("radio")
                elif step == "radio":
                    # Where the host's voice comes out here, as the host works the boat's radio mic (see the host's side).
                    pc = unreal.GameplayStatics.get_player_controller(world, 0)
                    host = next((s for s in unreal.GameplayStatics.get_game_state(world).player_array if s.get_player_name() == HOST_NAME), None)
                    route = unreal.RiptideVoiceComponent.route_for(host, pc) if host else None
                    r = st.setdefault("radio", {})
                    if host and not pc.get_controlled_pawn().get_home_boat() and "no_boat" not in r:
                        r["no_boat"] = True
                        check("castaways without a boat hear each other's own voices", route == unreal.RiptideVoiceRoute.PROXIMITY, str(route))
                        voice = pc.get_component_by_class(unreal.RiptideVoiceComponent)
                        voice.set_push_to_talk(True)
                        check("push-to-talk starts and stops", voice.is_pushing_to_talk())
                        voice.set_push_to_talk(False)
                        go("watching the host fish")
                        return
                    if route == unreal.RiptideVoiceRoute.LOUDHAILER and "hailer" not in r:
                        r["hailer"] = True
                        check("the host's voice comes out of the loudhailer when it switches the mic to it", True)
                    if "hailer" in r and route == unreal.RiptideVoiceRoute.PROXIMITY and "cb" not in r:
                        r["cb"] = True
                        check("on the CB, aboard the same boat, the host is heard speaking (not over the radio)", True)
                    heard = pc.get_component_by_class(unreal.RiptideVoiceComponent).get_last_heard_line()
                    if "cb" in r and "Radio check" in heard:
                        check("an NPC's call on the boat's channel is heard on its radio", True, heard)
                        pc.get_component_by_class(unreal.RiptideVoiceComponent).set_push_to_talk(True)
                        check("push-to-talk starts and stops", pc.get_component_by_class(unreal.RiptideVoiceComponent).is_pushing_to_talk())
                        pc.get_component_by_class(unreal.RiptideVoiceComponent).set_push_to_talk(False)
                        hud = pc.get_hud()
                        hud.set_menu_open(True)
                        check("the in-game menu opens", hud.is_menu_open())
                        go("menu open")
                    elif waited() > 45:
                        finish(f"the radio checks didn't all happen: {r}, last heard '{heard}'")
                elif step == "watching the host fish":
                    # The host takes a rod out and casts off the beach: everyone sees the rod in its hands and its line out.
                    pc = unreal.GameplayStatics.get_player_controller(world, 0)
                    host = next((s for s in unreal.GameplayStatics.get_game_state(world).player_array if s.get_player_name() == HOST_NAME), None)
                    pawn = host.get_pawn() if host else None
                    shown = pawn.get_angler().get_shown_state() if isinstance(pawn, unreal.RiptideCharacter) else None
                    if shown in (unreal.RiptideAnglerState.FLYING, unreal.RiptideAnglerState.WAITING, unreal.RiptideAnglerState.BITE):
                        check("the host is seen fishing: the rod in its hands, its line out", str(pawn.get_held_item()) == "fishing_rod", str(shown))
                        hud = pc.get_hud()
                        hud.set_menu_open(True)
                        check("the in-game menu opens", hud.is_menu_open())
                        go("menu open")
                    elif waited() > 45:
                        finish(f"never saw the host fishing (held {pawn.get_held_item() if pawn else None}, shown {shown})")
                elif step == "menu open" and waited() > 2:
                    # (A screenshot is taken at the end of the frame, so each change waits for the one before.)
                    if shots:
                        cmd("shot showui")
                    go("menu shot")
                elif step == "menu shot" and waited() > 1:
                    unreal.GameplayStatics.get_player_controller(world, 0).get_hud().show_menu_settings(True)
                    go("menu settings")
                elif step == "menu settings" and waited() > 2:
                    if shots:
                        cmd("shot showui")
                    go("settings shot")
                elif step == "settings shot" and waited() > 1:
                    hud = unreal.GameplayStatics.get_player_controller(world, 0).get_hud()
                    hud.show_menu_settings(False)
                    hud.set_menu_open(False)
                    check("the in-game menu closes", not hud.is_menu_open())
                    check("closing it gives the keyboard and mouse back to the game", hud.is_game_input_active())
                    gi.leave_game()
                    go("leaving")
                elif step == "leaving":
                    if map_name == "MainMenu" and waited() > 3:
                        hud = unreal.GameplayStatics.get_player_controller(world, 0).get_hud()
                        check("leaving returns to the main menu, with the menu up",
                              isinstance(hud, unreal.RiptideMenuHUD) and hud.is_menu_shown(), str(hud))
                        check("no longer connected", not gi.is_hosting() and gi.get_activity() == unreal.RiptideOnlineActivity.NONE)
                        check("joining again starts", gi.join_by_address("127.0.0.1"))
                        go("rejoining")
                    elif waited() > 60:
                        finish("leaving never got back to the main menu")
                elif step == "rejoining":
                    if map_name == GAME_MAP and not gi.is_hosting():
                        go("rejoined")
                    elif waited() > 90:
                        finish("the client never got back into the host's game")
                elif step == "rejoined" and waited() > 8:
                    pawn = unreal.GameplayStatics.get_player_controller(world, 0).get_controlled_pawn()
                    check("back in the game, it carries what it had", isinstance(pawn, unreal.RiptideCharacter)
                          and unreal.RiptideDataLibrary.count_carried(pawn, "flint") == 2, str(pawn))
                    gi.leave_game()
                    go("leaving for good")
                elif step == "leaving for good":
                    if map_name == "MainMenu" and waited() > 3:
                        finish()
                    elif waited() > 60:
                        finish("leaving the second time never got back to the main menu")
            if time.time() - st["t0"] > TIMEOUT_S - 20:
                finish(f"ran out of time at step '{st['step']}'")
        except Exception as err:  # noqa: BLE001 - any script error fails the test
            finish(f"script error at step '{st['step']}': {err!r}")

    st["handle"] = unreal.register_slate_post_tick_callback(tick)
    log("started")


def _role_from_command_line():
    try:
        import unreal
    except ImportError:
        return None
    for arg in unreal.SystemLibrary.get_command_line().split():
        if arg.lower().startswith(ROLE_FLAG.lower()):
            return arg[len(ROLE_FLAG):].lower()
    return ""


_role = _role_from_command_line()
if _role is None:
    if __name__ == "__main__":
        sys.exit(launch("--shots" in sys.argv))
elif _role in ("host", "client"):
    run_in_game(_role)
else:
    import unreal
    unreal.log_error("multiplayer_test.py: run it with plain Python (it starts the game itself), not inside the editor")
