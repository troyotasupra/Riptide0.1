# Riptide

Co-op naval survival shooter in Unreal Engine 5. The design plan is in `Docs/PLAN.md`.

## Current state: milestone 1 (the boat)
- A 6 m aluminium centre-console patrol skiff (deep-V hull, fender collar, T-top, bow rail) with an outboard that swings as it steers, floating on the Water plugin ocean. The model is our own, generated in code by `Content/Python/riptide_boat_mesh.py` and imported when the editor opens; change the shape there and bump `BOAT_MODEL_VERSION` in `init_unreal.py` to rebuild it.
- The ocean test map (2 km of open sea with a moderate 1-1.5 m swell) builds itself the first time the editor opens (`Content/Python/init_unreal.py`). To rebuild it after changing that script, delete `Content/Riptide/Maps/Ocean_Test.umap` with the editor closed.
- The boat leaves a real wake: its hull pushes the Water plugin's fluid simulation (Epic's boat force, scaled for a small hull), so waves and foam spread behind it in a V; white prop churn trails from the transom (`RiptideWakeFoamComponent`). Materials are built by `init_unreal.py`.
- Sound: the engine revs with the throttle and races when the prop leaves the water; water wash rises with speed; the bow slaps into waves; ocean ambience all around. Sounds are imported from `SourceAssets/Audio` when the editor opens (credits in `Docs/CREDITS.md`).
- Builds and runs on Unreal Engine 5.7 (Windows, Visual Studio 2022).

## Handling test
`Tools/boat_handling_test.py` plays Ocean_Test hands-free and checks the boat floats, stays upright, drives, and steers the right way. Run it with the editor closed:

```
UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/boat_handling_test.py"
```

The result is at the end of `Saved/Logs/Riptide.log` (search for `RiptideTest`). Swap `-nullrhi` for `-RenderOffscreen` to also save helm-camera screenshots to `Saved/Screenshots` and report the frame rate.

## Sound levels
Each sound is levelled on import to a target loudness, with peaks held below -8 dBFS (the table in `Content/Python/init_unreal.py`). The full mix sits around -21.5 LUFS at full throttle (the loudest moment) and -26 LUFS at rest, with peaks no higher than -8.7 dBFS. Averaged over play, that fits the common -24 ±2 LUFS guideline for games. The engine is two real outboard recordings (low and high revs), crossfaded and pitched with the throttle. To check after changing sounds:
- `Tools/measure_loudness.py` measures WAV files (peak, RMS, LUFS). To get WAVs, export the imported sounds from the editor.
- `Tools/model_boat_mix.py` rebuilds the boat's mix from those WAVs and measures it at rest and at full throttle.

## Helm controls
| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Throttle lever up / down (stays where you leave it) | W / S | Right / left trigger |
| Steer the motor | A / D | Left stick |
| Cut throttle to idle | X | B |
| Look around | Mouse | Right stick |

The top-left readout shows speed, throttle, fuel, engine health, and whether the prop is in the water.

## Development PC
Windows 11, AMD Ryzen 5 2600X (6 cores), 32 GB RAM. NVIDIA GeForce RTX 3060. Target: 1080p, 60 fps, Lumen on, with DLSS as an option.
Effects are tuned to run at playable frame rates on this machine first.
