# Riptide

Co-op naval survival shooter in Unreal Engine 5. The design plan is in `Docs/PLAN.md`.

## Current state: milestone 1 (the boat), and walking its deck from milestone 2
- A full-size 26 ft (7.9 m, 2.6 m beam) aluminium centre-console patrol boat (deep-V hull, fender collar, T-top, bow rail) with twin 300 hp outboards (about 30 knots flat out) that swing as they steer and tilt with the trim on brackets fixed to the transom, floating on the Water plugin ocean. Each motor pushes only while its own prop is in the water. Trimming out lifts the bow at speed, trimming in holds it down. It's the standard boat of the islands' mercenary crews, which players can take in a fight or steal: fitted out for patrol work, with open-array radar, satcom and GPS antennas, VHF and AIS whips, masthead and bow nav lights, a searchlight and FLIR camera, a loudhailer, an overhead radio box, cleats, a tow post, bow and stern eyes, trim tabs, a boarding ladder, weapon mount sockets, and a stern bulkhead and splashwell closing the cockpit off from the sea. The model is our own, generated in code by `Content/Python/riptide_boat_mesh.py` and imported when the editor opens; change the shape there and bump `BOAT_MODEL_VERSION` in `init_unreal.py` to rebuild it.
- The ocean test map (2 km of open sea with a moderate 1-1.5 m swell) builds itself the first time the editor opens (`Content/Python/init_unreal.py`), and so does the main menu map (`MainMenu`, the same sea at night). The game starts on MainMenu; the editor opens on Ocean_Test, so Play in the editor puts you straight on the deck. To rebuild it after changing that script, delete `Content/Riptide/Maps/Ocean_Test.umap` with the editor closed.
- You start on foot on the deck, in first person, and can walk all of it: round the console on either side, up to the bow and across the aft deck, at rest or at full speed through turns. Step up to the wheel and press E to take the helm; E again lets go (the throttle stays where you left it). At speed the deck's jolts throw you: slams, hard turns and throttle changes stagger an unbraced crew member, and the worst slams knock you off your feet. Hold Shift at a handhold to ride it out (you can still shuffle along). At the helm you're holding the wheel. Go over the side and you swim (where you look; Space swims up, C or Ctrl dives). The boarding ladder on the stern's port side reaches into the water: swim into it and you take hold. W climbs, S climbs down, and letting go of both keeps you hanging there, so you can look over the transom before going aboard; W at the top takes you over onto the deck, and Space (or S at the bottom) lets go. One crew member on the ladder at a time. The VHF radio in the overhead box has a hand mic on a coiled cord: look at it and press E to take it (M at the helm), look at its clip and press E to hang it up; walk out of the cord's reach and it's pulled back onto its clip. The wheel turns with the motors, and the props spin with the engines.
- At the helm: a twin-lever throttle (centred is neutral; forward engages ahead, then opens the throttle; back is astern), and live gauges on the dash: tachometer, speedometer, and a screen showing gear, throttle, trim, fuel, heading and warnings.
- Fuel and engines: a 450 L tank (about 70% full when you find a boat), burned at twin-300 rates (about 225 L an hour flat out, a trickle at idle). Carry the fuel drum from the stern locker to the filler on the starboard gunwale and E pours its 20 L in. Each motor has its own health: a damaged one sputters, a dead one stops, and the boat limps on the other, pulling toward the dead side. The dash warns which motor, and when fuel runs low.
- Storage: the floor lockers in front of the console, the stern hatches and the anchor locker hold gear. E beside one opens it in the inventory screen (the Godot build's Delta Force-style grid): drag items between the locker and your pockets and backpack. Tab opens your own inventory.
- Surfaces (M_BoatDetail, built from engine noise textures, no downloads): a non-skid deck, weathered aluminium with a light grain, brushed stainless rails, matte rubber, grime on paint and fibreglass, and dark antifouling below the hull's waterline.
- Lights: navigation lights (masthead all-round white, screened red and green sidelights at the bow) are on from the start; the searchlight on the T-top swings to wherever the helmsman looks and throws a visible beam at night (the map has light volumetric fog for it); floods under the canopy light the cockpit.
- Spray: at speed the bow wave peels off in fans of spray, slamming into a swell throws bursts out both sides, and the props churn whitewater behind the stern.
- The boat leaves a real wake: its hull pushes the Water plugin's fluid simulation (Epic's boat force, scaled for a small hull), so waves and foam spread behind it in a V; white prop churn trails from the transom (`RiptideWakeFoamComponent`). Materials are built by `init_unreal.py`.
- Sound: the twin engines rev with the throttle and races when the prop leaves the water; water wash rises with speed; the bow slaps into waves; ocean ambience all around. Sounds are imported from `SourceAssets/Audio` when the editor opens (credits in `Docs/CREDITS.md`).
- The crew: every crew member is a real character (Quaternius's CC0 male and female bodies, 1.81 m and 1.77 m) in a combat uniform, boots and kit built from your look, which every player sees: build, skin tone, hairstyle and hair colour, beard (male), headgear (high-cut combat helmet with NVG shroud, rails and chinstrap; boonie hat in the uniform's camouflage; patrol cap; knit beanie), face cover (sunglasses, ballistic glasses, balaclava or shemagh), uniform camouflage (multi-terrain, woodland, desert, urban grey, plain olive, black), vest (plate carrier with plates, cummerbund, magazine, admin and radio pouches; or a chest rig), gear colour (coyote, ranger green, black, wolf grey, tan: vest, helmet, pouches, pack, gloves, and brown or black boots to match), pack (assault or hydration) and tactical gloves. Hair tucks flat under headgear. The uniform, boots, gloves, balaclava and shemagh are the body's own surface pushed out and smoothed, so they bend exactly with it; the gear is modelled around each body's measurements. All of it is generated in code (`Content/Python/riptide_crew_mesh.py`) and imported when the editor opens; change it there and bump `CREW_VERSION` in `init_unreal.py`. Animated in code (`URiptideCrewAnimInstance`): standing, walking and running blended by speed (sideways and backwards too), looking up and down, falling, swimming, hand over hand up the ladder (hands and feet on its treads), over the top, holding on to a rail, and knocked flat and getting up. Your own body is hidden from your first-person view (you see its shadow).
- The game opens on the main menu: a live shot of the patrol boat at night, drifting on the swell with its searchlight sweeping slowly across the water and a crew member in kit standing dark at the bow rail. From there you host or join a game with up to four crew, set your callsign and look, and change the settings (see "Playing together" below).
- Builds and runs on Unreal Engine 5.7 (Windows, Visual Studio 2022).

## Installing and playing
Riptide runs from its source, on Windows or macOS, with Unreal Engine 5.7.

1. **Install the tools.**
   - **Windows:** the Epic Games Launcher with Unreal Engine 5.7, and Visual Studio 2022 with the "Game development with C++" workload.
   - **macOS:** Unreal Engine 5.7 from the Epic Games Launcher, and Xcode (the version Epic lists for 5.7).
   - **Both:** Git with Git LFS (`git lfs install` once), and Steam for online play.
2. **Get the game:** `git clone https://github.com/troyotasupra/Riptide0.1.git`, then `git lfs pull` inside it. The art and sounds come down through LFS.
3. **Play:** use the launcher in `Tools/Launch/`. It brings the code up to date, builds the game's content (maps, boat, crew, islands, items) when it needs to, and starts the game in its own window, on the main menu.
   - The first launch, and the first after an update, takes several minutes while the content is built; after that it's quick.
   - **Windows:** `Play Riptide.bat`. Right-click it and choose Send to, Desktop (create shortcut) for a desktop icon.
   - **macOS:** `Play Riptide.command`. Double-click it in Finder; the first time, macOS may ask you to allow it in System Settings, Privacy & Security.
   - If Unreal Engine isn't in the default place, set `UE_ROOT` to its folder.
4. **Updating:** `git pull` (and `git lfs pull`), then play as usual: the launcher rebuilds whatever changed.

For online play, start Steam first on every PC (see "Playing together"). Windows and Mac players can play together.

## Playing together
Riptide is online co-op for up to four players, over Steam when it's running.

- **Host:** Main menu, Host game, then Friends only (your Steam friends, and anyone you invite) or Public (listed in everyone's browser), then Host. You wash up on the island. To bring friends, press Esc (P in the editor) in the game and choose Invite friends: it opens the Steam overlay's invite list. Friends can also join you from their Steam friends list ("Join game").
- **Join:** Main menu, Join game. The browser lists public games and your Steam friends' games (host, crew, ping): pick one and press Join, or double-click it. Accepting a Steam invite joins straight away, even from the desktop.
- **Saving:** the host's game saves itself every minute, when everyone sleeps through a night, when a friend leaves and when the host leaves or quits. Next time, Host game offers **Continue** (the day and hour, what was built, dropped and harvested, and every castaway where they were with what they carried, friends included when they rejoin) or **New game**, which replaces the save. The save lives on the host's PC, in `Saved/SaveGames/World.sav`.
- **Without Steam** (it isn't running, or you start the game with `-nosteam`), the game uses the local network instead: hosted games show in the Join browser on the same network, and anyone can join by typing the host's IP address under Join by IP address (the host's address is shown on the Host screen and in the in-game menu). Players outside the host's network need UDP port 7777 forwarded to the host's PC.
- **Steam setup:** Steam has to be running and logged in before the game starts. Until Riptide has its own Steam app, it uses Valve's public test app (480, "Spacewar"), so Steam shows you as playing Spacewar. Playing in the editor always uses the local network, not Steam.
- **Your crew member:** Main menu, Crew: your callsign (letters, digits, `-`, `_` and `.`) and a row for each part of your look (build, skin, hair, beard, headgear, face, uniform, vest, gear colour, pack, gloves), with Randomise. The preview turns when you drag it (or Q / E, LB / RB). Save keeps it; every game you host or join, the others see you by that callsign and in that look.
- **Leaving:** the in-game menu's Leave to main menu (as the host, that ends the game for everyone), or Quit to desktop. If the connection drops or a join fails, you're back at the main menu with a message saying why.
- **Settings** (main menu, or the in-game menu): look sensitivity, invert look, field of view, master / effects / ambience volume, graphics quality, display mode, resolution, VSync and frame-rate limit. Changes take effect at once and are saved when you go back.

Menus work with the mouse, the keyboard (arrows to move, Left / Right to change a setting, Enter to choose, Esc to go back) and a gamepad (stick or D-pad, A to choose, B to go back). The in-game menu doesn't pause anything: the boat and your crew carry on while it's open, but your crew member stands still.

| In the game | Keyboard / mouse | Gamepad |
|---|---|---|
| In-game menu (Resume, Settings, Invite friends, Leave, Quit) | Esc (P in the editor, where Esc stops play) | Start (Menu) |

## Handling test
`Tools/boat_handling_test.py` plays Ocean_Test hands-free and checks the boat floats, stays upright, drives, and steers the right way. It also checks:
- the props and hull never flicker in and out of the water;
- the outboards and the wheel swing with the steering, the props spin ahead and astern, and trim moves the bow up and down;
- a locker opens from beside it and gives up its gear, and the fuel drum from the stern locker pours into the tank at the filler;
- the boat runs on one motor when the other dies;
- each crew member has a body built from their look (one put on the deck in a chosen look wears exactly its parts), and the animation follows what they do: walking, swimming, on the ladder, at the helm;
- it handles like a real 26 ft aluminium boat with twin 300s: it idles along in gear at 3-5 knots, full astern tops out under 8 knots, it reaches 25 knots 6-12 seconds after the lever goes forward (onto the plane in 3-4), and it tops out at 28-32 knots;
- a crew member who goes in the sea swims with their head above water, swims into the ladder, climbs, hangs on when W is let go, and climbs back aboard;
- the radio mic comes off its clip and is pulled back when carried out of the cord's reach;
- the player walks laps of the whole deck, at rest and at speed, and never gets stuck, hops or falls off;
- the player takes the helm and steps off it again, at rest and at 30 knots, landing on the deck;
- a crew member standing without holding on rides out running flat out in the swell, but is thrown by a hard turn at speed.

It logs, without failing on them, how often the sea comes over each part of the deck, how often the walking crew member is thrown off balance, the boat's attitude at rest and flat out, and its roll and pitch periods (it kicks the hull at rest and times the swings: run it with `RIPTIDE_TEST_CALM=1` set for clean numbers, as the swell muddles them). Run it with the editor closed:

```
UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/boat_handling_test.py"
```

The result is at the end of `Saved/Logs/Riptide.log` (search for `RiptideTest`). Swap `-nullrhi` for `-RenderOffscreen` to also save first-person screenshots to `Saved/Screenshots` and report the frame rate.

## Two-player test
`Tools/multiplayer_test.py` starts two copies of the game headless (on the local network, `-nosteam`): one hosts from the main menu as Alpha-1, the other finds the game in the Join browser and joins as Bravo-7 with its own look. It checks both sides see Bravo-7 by name, in its look, in its own crew member standing on the deck, that the in-game menu opens and closes, and that leaving brings the client back to the main menu while the host's game goes on. Run it with any Python 3, with the editor closed:

```
python Tools/multiplayer_test.py
```

It prints each check and PASS or FAIL (the two logs are `Saved/Logs/MultiplayerTest_host.log` and `..._client.log`). Add `--shots` to render the client and save screenshots of the browser and the in-game menu to `Saved/Screenshots`. Set `UE_EDITOR` to UnrealEditor's path if the engine isn't installed in the default place.

## Sound levels
Each sound is levelled on import to a target loudness, with peaks held below -8 dBFS (the table in `Content/Python/init_unreal.py`). The full mix sits around -21.5 LUFS at full throttle (the loudest moment) and -26 LUFS at rest, with peaks no higher than -8.7 dBFS. Averaged over play, that fits the common -24 ±2 LUFS guideline for games. The engine is two real outboard recordings (low and high revs), crossfaded and pitched with the throttle. Each of the twin motors plays its own, slightly out of tune with the other, at half power, so together they're exactly as loud as the single engine the levels were set for. To check after changing sounds:
- `Tools/measure_loudness.py` measures WAV files (peak, RMS, LUFS). To get WAVs, export the imported sounds from the editor.
- `Tools/model_boat_mix.py` rebuilds the boat's mix from those WAVs and measures it at rest and at full throttle.

## On foot
| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Walk | W A S D | Left stick |
| Look around | Mouse | Right stick |
| Hold on (near a rail, the gunwale, a T-top leg or the leaning post) | Shift | Left bumper |
| Jump (swim up in the water) | Space | A |
| Dive (in the water) | C or Ctrl | B |
| On the ladder: climb up / down (let go of both to hang on), let go | W / S, Space | Left stick, A |
| Take the helm (at the wheel), take or hang up the radio mic (looking at it), refuel (at the filler with a fuel drum), or open a locker (beside it) | E | X |
| Inventory (Tab, E or Esc closes it) | Tab | View (B, X or Start closes it) |

## Helm controls
| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Throttle lever forward / back: neutral in the middle, then ahead or astern (stays where you leave it) | W / S | Right / left trigger |
| Steer the motor | A / D | Left stick |
| Trim out (bow up) / in (bow down) | R / F | D-pad up / down |
| Cut throttle to idle | X | B |
| Look around | Mouse | Right stick |
| Leave the helm | E | X |
| Searchlight on / off (it follows where you look) | L | D-pad left |
| Navigation lights on / off | N | |
| Cockpit floods on / off | K | D-pad right |
| Radio mic: take it / hang it up | M | Right stick press |
| Tuning readout on screen | H | |

The dash gauges show everything you need; H brings up the old tuning readout (speed, throttle, motor angle, trim, fuel, engine health, props in the water).

## Dev mode
For playtesting and checking the boat and its effects from anywhere. It's in every build except Shipping, and works in single player (or for the host). F1 shows a panel listing all of this with each key's state, plus live readings: frame rate, spray in the air, the boat's speed, heading, trim, fuel and engines, the camera's position and mode, the time of day and game speed.

| Action | Key |
|---|---|
| Dev panel on / off | F1 |
| Fly (a camera that goes through anything); again to go back to exactly where you were, on foot or at the helm (the boat keeps running) | F2 |
| Camera mode: free fly, ride along with the boat (the camera is carried with it underway), orbit the boat, chase the boat | F3 |
| Flying: drop in where the camera is (onto the deck, or into the sea). On foot: back to the boat's helm | F4 |
| Time of day: day, golden hour, dusk, night | F5 |
| Slow motion: 1×, ½×, ¼×, 1/10× | F6 |
| Freeze the world (the camera still flies, so you can fly round spray hanging in the air) | Pause, or backslash (`\`) |
| God mode: the deck never throws you, and the tank never empties | F7 |
| Photo mode: hides every prompt and the panel | F10 |
| Physics overlay: buoyancy pontoons (blue in the sea, orange out, with the sea's surface over each), props (green biting, red dry), thrust, velocity, the physics box | Page Up |
| Sea state: calm, moderate (the map's own swell), rough | Page Down |
| Refuel and repair the boat | Insert or Delete |
| Right the boat and stop it where it is (the throttle stays where it was) | Home |
| Bring the boat to where the camera is looking | End |

Flying: W A S D fly where you look, E or Space up, Q or C down, Shift fast, Ctrl slow, mouse to look, mouse wheel sets the speed. Orbiting or chasing, the mouse swings the camera round the boat, the wheel (or W and S) zooms, A and D circle it, E and Q raise and lower it. A gamepad flies too (sticks, triggers for up and down, bumpers for fast and slow).

`Tools/dev_mode_test.py` checks all of it headless, the same way as the handling test (search the log for `RiptideDevTest`).

## Development PC
Windows 11, AMD Ryzen 5 2600X (6 cores), 32 GB RAM. NVIDIA GeForce RTX 3060. Target: 1080p, 60 fps, Lumen on, with DLSS as an option.
Effects are tuned to run at playable frame rates on this machine first.
