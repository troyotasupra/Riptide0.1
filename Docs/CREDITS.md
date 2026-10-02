# Credits

Every third-party asset in Riptide is CC0 (public domain). No attribution is required, but here is where each one came from.

## Audio (`SourceAssets/Audio/`)

| File in the repo | Original | Author | Source |
|---|---|---|---|
| `ocean_waves.mp3` | `VistulaShort.mp3` from "Sea and river wave sounds" (the 5-minute version) | RandomMind | [OpenGameArt](https://opengameart.org/content/sea-and-river-wave-sounds) |
| `engine_low.wav`, `engine_high.wav` | Excerpts of "Thailand outboard motor boat high speed various rising speeds and tones VERY LONG" (21 min): 20:06 to 20:18 (low revs) and 13:20 to 13:30 (high revs), mixed to mono and trimmed into seamless loops | kyles | [Freesound](https://freesound.org/people/kyles/sounds/177702/) |
| `hull_wash.ogg` | `loop_water_02.ogg` from "40 CC0 water / splash / slime SFX" | rubberduck | [OpenGameArt](https://opengameart.org/content/40-cc0-water-splash-slime-sfx) |
| `hull_slap_04.ogg`, `hull_slap_06.ogg`, `hull_slap_08.ogg`, `hull_slap_13.ogg`, `hull_slap_15.ogg` | `splash_04`, `splash_06`, `splash_08`, `splash_13` and `splash_15.ogg` from "40 CC0 water / splash / slime SFX" | rubberduck | [OpenGameArt](https://opengameart.org/content/40-cc0-water-splash-slime-sfx) |

Apart from the engine excerpts, the files are unedited. Levels are set when the editor imports them (`Content/Python/init_unreal.py`).

## Characters (`SourceAssets/Characters/Quaternius/`)

All by [Quaternius](https://quaternius.com), CC0, downloaded from itch.io (the free Standard versions):

| Folder in the repo | Pack | Source |
|---|---|---|
| `BaseCharacters/` | The rigged Superhero male and female bodies, eye and skin textures, from "Universal Base Characters" | [itch.io](https://quaternius.itch.io/universal-base-characters) |
| `Hair/` | Hairstyles, beard and eyebrows rigged to the head bone, from "Universal Base Characters" | [itch.io](https://quaternius.itch.io/universal-base-characters) |
| `Animations/UAL1_Standard.glb` | "Universal Animation Library" (locomotion, swimming, crouching, combat and more) | [itch.io](https://quaternius.itch.io/universal-animation-library) |
| `Animations/UAL2_Standard.glb` | "Universal Animation Library 2" (parkour, climbing and more) | [itch.io](https://quaternius.itch.io/universal-animation-library-2) |

The files are unedited. What the game uses from them (imported by `Content/Python/init_unreal.py`):
- the Superhero male and female bodies (with their eyes and eyebrows) and their skin, eye and hair textures (tinted in the material to the chosen skin tone and hair colour);
- the hairstyles Hair_Buzzed (Hair_BuzzedFemale for the female body), Hair_SimpleParted, Hair_Long and Hair_Buns, and Hair_Beard, refitted to each head by `riptide_crew_mesh.py`; the separate Eyebrows files aren't needed (the bodies have their own);
- 15 clips: from UAL1 Idle_Loop, Walk_Loop, Jog_Fwd_Loop, Crouch_Idle_Loop, Crouch_Fwd_Loop, Jump_Start, Jump_Loop, Jump_Land, Swim_Idle_Loop, Swim_Fwd_Loop and Pistol_Idle_Loop (the menu's shouldered rifle); from UAL2 ClimbUp_1m (over the top of the ladder), Idle_Rail_Loop (holding on), Hit_Knockback and LayToIdle (knocked down and getting up). There's no ladder clip in the Standard libraries: the climb itself is posed in code on the ladder's treads.

The crew's uniforms, boots, gloves, balaclava, shemagh, headgear, glasses, vests, packs and rifle are our own, generated in code (`Content/Python/riptide_crew_mesh.py`).

## Ground textures (`SourceAssets/Textures/`)

All from [Poly Haven](https://polyhaven.com), CC0, photo-scanned. Each folder holds four JPGs: colour (`_diff`), normal (`_nor_dx`), ambient occlusion / roughness / metal packed (`_arm`) and height (`_disp`), at 2K (`aerial_beach_01` at 4K).

| Folder | Source | Used for |
|---|---|---|
| `dense_sand` | [polyhaven.com/a/dense_sand](https://polyhaven.com/a/dense_sand) | dry beach sand up close |
| `aerial_beach_01` | [polyhaven.com/a/aerial_beach_01](https://polyhaven.com/a/aerial_beach_01) | beach sand from a distance |
| `shell_floor_01` | [polyhaven.com/a/shell_floor_01](https://polyhaven.com/a/shell_floor_01) | broken shell along the high-tide line |
| `coral_mud_01` | [polyhaven.com/a/coral_mud_01](https://polyhaven.com/a/coral_mud_01) | pale coral rock: rock benches and bluff tops |
| `seaside_rock` | [polyhaven.com/a/seaside_rock](https://polyhaven.com/a/seaside_rock) | dark weathered rock: sea cliffs and steep ground |
| `forrest_sand_01` | [polyhaven.com/a/forrest_sand_01](https://polyhaven.com/a/forrest_sand_01) | grove floor: sandy soil with plant litter |
| `low_tide_rocks` | [polyhaven.com/a/low_tide_rocks](https://polyhaven.com/a/low_tide_rocks) | reef and rock under water |

The files are unedited (renamed without the resolution suffix). They're imported and blended by `Content/Python/riptide_islands.py`.

## Models

The patrol skiff and outboard are our own, generated in code (`Content/Python/riptide_boat_mesh.py`).

## Engine content

The ocean, the wake simulation and its boat force (`BP_FluidSim_01`, `M_Fluid_Sim_Force_Boat_Component`, `T_BoatForceFoam`), the churn's foam texture (`T_WaterFlow_01_Foam_Tiled`) and the placeholder shapes come with Unreal Engine (the Water plugin and engine basic shapes).
