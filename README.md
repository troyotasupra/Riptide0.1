# Riptide

Co-op naval survival shooter in Unreal Engine 5. The design plan is in `Docs/PLAN.md`.

## Current state: milestone 1 (the boat)
- A placeholder skiff with a physics-driven outboard motor, floating on the Water plugin ocean.
- The ocean test map (2 km of open sea with a moderate 1-1.5 m swell) builds itself the first time the editor opens (`Content/Python/init_unreal.py`). To rebuild it after changing that script, delete `Content/Riptide/Maps/Ocean_Test.umap` with the editor closed.
- Builds and runs on Unreal Engine 5.7 (Windows, Visual Studio 2022).

## Islands (in progress)
`Archipelago_Test` is the island chain: the boat starts just off the start island, and islands load around it as it sails. Open it from the Content Drawer (`Riptide/Maps`) and press Play. It builds itself the first time the editor opens, like Ocean_Test.

Digging works before there's a shovel: while playing, press the backtick key (`` ` ``) and type `Riptide.Dig` to dig where you're looking (or `Riptide.Dig 30` for 30 scoops), and `Riptide.Pile` to dump sand back. How the world is laid out, and what's next, is in `Docs/WORLD.md`.

`Tools/island_test.py` checks the islands hands-free, run the same way as the handling test below, with the result under `RiptideIslandTest`. `Tools/worldgen_test.cpp` tests the island generator without the engine and draws map previews (instructions are at the top of the file).

## Handling test
`Tools/boat_handling_test.py` plays Ocean_Test hands-free and checks the boat floats, stays upright, drives, and steers the right way. Run it with the editor closed:

```
UnrealEditor Riptide.uproject -nullrhi -unattended -nosplash -nosound -ExecCmds="py <project>/Tools/boat_handling_test.py"
```

The result is at the end of `Saved/Logs/Riptide.log` (search for `RiptideTest`). Swap `-nullrhi` for `-RenderOffscreen` to also save helm-camera screenshots to `Saved/Screenshots` and report the frame rate.

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
