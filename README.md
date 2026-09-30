# Riptide

Co-op naval survival shooter in Unreal Engine 5. The design plan is in `Docs/PLAN.md`.

## Current state: milestone 1 (the boat), and walking its deck from milestone 2
- A full-size 26 ft (7.9 m, 2.6 m beam) aluminium centre-console patrol boat (deep-V hull, fender collar, T-top, bow rail) with twin outboards that swing as they steer and tilt with the trim, floating on the Water plugin ocean. Each motor pushes only while its own prop is in the water. Trimming out lifts the bow at speed (about 6 degrees fully out), trimming in holds it down. The model is our own, generated in code by `Content/Python/riptide_boat_mesh.py` and imported when the editor opens; change the shape there and bump `BOAT_MODEL_VERSION` in `init_unreal.py` to rebuild it.
- The ocean test map (2 km of open sea with a moderate 1-1.5 m swell) builds itself the first time the editor opens (`Content/Python/init_unreal.py`). To rebuild it after changing that script, delete `Content/Riptide/Maps/Ocean_Test.umap` with the editor closed.
- You start on foot on the deck, in first person, and can walk all of it: round the console on either side, up to the bow and across the aft deck, at rest or at full speed through turns. Step up to the wheel and press E to take the helm; E again lets go (the throttle stays where you left it). Falling overboard puts you back on deck for now, since swimming comes next.
- The boat leaves a real wake: its hull pushes the Water plugin's fluid simulation (Epic's boat force, scaled for a small hull), so waves and foam spread behind it in a V; white prop churn trails from the transom (`RiptideWakeFoamComponent`). Materials are built by `init_unreal.py`.
- Sound: the twin engines rev with the throttle and races when the prop leaves the water; water wash rises with speed; the bow slaps into waves; ocean ambience all around. Sounds are imported from `SourceAssets/Audio` when the editor opens (credits in `Docs/CREDITS.md`).
- Builds and runs on Unreal Engine 5.7 (Windows, Visual Studio 2022).

## Handling test
`Tools/boat_handling_test.py` plays Ocean_Test hands-free and checks the boat floats, stays upright, drives, and steers the right way. It also checks the props and hull never flicker in and out of the water, the outboards swing with the wheel, trim moves the bow up and down, and the sea stays below the deck. Meanwhile the player walks laps of the whole deck, at rest and at speed, and must never get stuck, hop or fall off; at the end it takes the helm and leaves it again. Run it with the editor closed:

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
| Jump | Space | A |
| Take the helm (standing at the wheel) | E | X |

## Helm controls
| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Throttle lever up / down (stays where you leave it) | W / S | Right / left trigger |
| Steer the motor | A / D | Left stick |
| Trim out (bow up) / in (bow down) | R / F | D-pad up / down |
| Cut throttle to idle | X | B |
| Look around | Mouse | Right stick |
| Leave the helm | E | X |

The top-left readout shows speed, throttle, motor angle, trim, fuel, engine health, and whether the props are in the water.

## Development PC
Windows 11, AMD Ryzen 5 2600X (6 cores), 32 GB RAM. NVIDIA GeForce RTX 3060. Target: 1080p, 60 fps, Lumen on, with DLSS as an option.
Effects are tuned to run at playable frame rates on this machine first.
