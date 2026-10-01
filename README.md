# Riptide

Co-op naval survival shooter in Unreal Engine 5. The design plan is in `Docs/PLAN.md`.

## Current state: milestone 1 (the boat), and walking its deck from milestone 2
- A full-size 26 ft (7.9 m, 2.6 m beam) aluminium centre-console patrol boat (deep-V hull, fender collar, T-top, bow rail) with twin 250 hp outboards (about 30 knots flat out) that swing as they steer and tilt with the trim on brackets fixed to the transom, floating on the Water plugin ocean. Each motor pushes only while its own prop is in the water. Trimming out lifts the bow at speed, trimming in holds it down. It's the standard boat of the islands' mercenary crews, which players can take in a fight or steal: fitted out for patrol work, with open-array radar, satcom and GPS antennas, VHF and AIS whips, masthead and bow nav lights, a searchlight and FLIR camera, a loudhailer, an overhead radio box, cleats, a tow post, bow and stern eyes, trim tabs, a boarding ladder, weapon mount sockets, and a stern bulkhead and splashwell closing the cockpit off from the sea. The model is our own, generated in code by `Content/Python/riptide_boat_mesh.py` and imported when the editor opens; change the shape there and bump `BOAT_MODEL_VERSION` in `init_unreal.py` to rebuild it.
- The ocean test map (2 km of open sea with a moderate 1-1.5 m swell) builds itself the first time the editor opens (`Content/Python/init_unreal.py`). To rebuild it after changing that script, delete `Content/Riptide/Maps/Ocean_Test.umap` with the editor closed.
- You start on foot on the deck, in first person, and can walk all of it: round the console on either side, up to the bow and across the aft deck, at rest or at full speed through turns. Step up to the wheel and press E to take the helm; E again lets go (the throttle stays where you left it). At speed the deck's jolts throw you: slams, hard turns and throttle changes stagger an unbraced crew member, and the worst slams knock you off your feet. Hold Shift at a handhold to ride it out (you can still shuffle along). At the helm you're holding the wheel. Go over the side and you swim (where you look; Space swims up, C dives); the boarding ladder on the stern's port side reaches into the water, and E at its foot climbs you back aboard.
- At the helm: a twin-lever throttle (centred is neutral; forward engages ahead, then opens the throttle; back is astern), and live gauges on the dash: tachometer, speedometer, and a screen showing gear, throttle, trim, fuel, heading and warnings.
- Fuel and engines: a 450 L tank (about 70% full when you find a boat), burned at twin-250 rates (about 190 L an hour flat out, a trickle at idle). Carry the fuel drum from the stern locker to the filler on the starboard gunwale and E pours its 20 L in. Each motor has its own health: a damaged one sputters, a dead one stops, and the boat limps on the other, pulling toward the dead side. The dash warns which motor, and when fuel runs low.
- Storage: the floor lockers in front of the console, the stern hatches and the anchor locker hold gear. E beside one opens it in the inventory screen (the Godot build's Delta Force-style grid): drag items between the locker and your pockets and backpack. Tab opens your own inventory.
- Surfaces (M_BoatDetail, built from engine noise textures, no downloads): a non-skid deck, weathered aluminium with a light grain, brushed stainless rails, matte rubber, grime on paint and fibreglass, and dark antifouling below the hull's waterline.
- Lights: navigation lights (masthead all-round white, screened red and green sidelights at the bow) are on from the start; the searchlight on the T-top swings to wherever the helmsman looks and throws a visible beam at night (the map has light volumetric fog for it); floods under the canopy light the cockpit.
- Spray: at speed the bow wave peels off in fans of spray, slamming into a swell throws bursts out both sides, and the props churn whitewater behind the stern.
- The boat leaves a real wake: its hull pushes the Water plugin's fluid simulation (Epic's boat force, scaled for a small hull), so waves and foam spread behind it in a V; white prop churn trails from the transom (`RiptideWakeFoamComponent`). Materials are built by `init_unreal.py`.
- Sound: the twin engines rev with the throttle and races when the prop leaves the water; water wash rises with speed; the bow slaps into waves; ocean ambience all around. Sounds are imported from `SourceAssets/Audio` when the editor opens (credits in `Docs/CREDITS.md`).
- Builds and runs on Unreal Engine 5.7 (Windows, Visual Studio 2022).

## Handling test
`Tools/boat_handling_test.py` plays Ocean_Test hands-free and checks the boat floats, stays upright, drives, and steers the right way. It also checks the props and hull never flicker in and out of the water, the outboards swing with the wheel, trim moves the bow up and down, the sea stays below the deck, a locker opens from beside it and gives up its gear, the fuel drum from the stern locker pours into the tank at the filler, the boat runs on one motor when the other dies, a crew member walking laps at speed holds on and isn't thrown (while one standing unbraced at 30 knots is), and a crew member who goes in the sea swims, keeps their head above water, and climbs the ladder back aboard. Meanwhile the player walks laps of the whole deck, at rest and at speed, and must never get stuck, hop or fall off; at the end it takes the helm and leaves it again. Run it with the editor closed:

```
UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/boat_handling_test.py"
```

The result is at the end of `Saved/Logs/Riptide.log` (search for `RiptideTest`). Swap `-nullrhi` for `-RenderOffscreen` to also save first-person screenshots to `Saved/Screenshots` and report the frame rate.

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
| Dive (in the water) | C | B |
| Take the helm (at the wheel), or open a locker (beside it) | E | X |
| Inventory | Tab | Menu |

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
