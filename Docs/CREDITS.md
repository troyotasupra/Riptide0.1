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
| `rock_face_03` | [polyhaven.com/a/rock_face_03](https://polyhaven.com/a/rock_face_03) | the sea cliffs' rock (three JPGs: colour, normal, roughness; carried over from the Godot build) |
| `palm_tree_bark` | [polyhaven.com/a/palm_tree_bark](https://polyhaven.com/a/palm_tree_bark) | the palms' trunks (three JPGs: colour, normal, roughness; carried over from the Godot build) |

The files are unedited (renamed without the resolution suffix). They're imported and blended by `Content/Python/riptide_islands.py`.


## Rocks and plants (`SourceAssets/Models/`)

All from [Poly Haven](https://polyhaven.com), CC0, photo-scanned. Each folder holds the model (`.fbx`) and its 2K textures, unedited. They're imported by `Content/Python/riptide_island_props.py`; only each scan's full-detail mesh is used.

| Folder | Source | Used for |
|---|---|---|
| `coastal_cliff_02` | [polyhaven.com/a/coastal_cliff_02](https://polyhaven.com/a/coastal_cliff_02) | sea cliffs |
| `coast_rocks_05` | [polyhaven.com/a/coast_rocks_05](https://polyhaven.com/a/coast_rocks_05) | rock outcrops |
| `boulder_01` | [polyhaven.com/a/boulder_01](https://polyhaven.com/a/boulder_01) | boulders |
| `sand_rocks_small_01` | [polyhaven.com/a/sand_rocks_small_01](https://polyhaven.com/a/sand_rocks_small_01) | rock rubble where beach meets rock |
| `island_tree_02` | [polyhaven.com/a/island_tree_02](https://polyhaven.com/a/island_tree_02) | scrub trees |
| `searsia_lucida` | [polyhaven.com/a/searsia_lucida](https://polyhaven.com/a/searsia_lucida) | coastal shrubs |
| `fern_02` | [polyhaven.com/a/fern_02](https://polyhaven.com/a/fern_02) | undergrowth |
| `grass_bermuda_01` | [polyhaven.com/a/grass_bermuda_01](https://polyhaven.com/a/grass_bermuda_01) | grass tufts (imported, not planted yet) |
| `dead_tree_trunk_02` | [polyhaven.com/a/dead_tree_trunk_02](https://polyhaven.com/a/dead_tree_trunk_02) | driftwood logs |
| `dry_branches_medium_01` | [polyhaven.com/a/dry_branches_medium_01](https://polyhaven.com/a/dry_branches_medium_01) | driftwood branches |
| `tree_small_02` | [polyhaven.com/a/tree_small_02](https://polyhaven.com/a/tree_small_02) | scrub trees (second shape) |
| `island_tree_01` | [polyhaven.com/a/island_tree_01](https://polyhaven.com/a/island_tree_01) | larger coastal trees |
| `grass_medium_01` | [polyhaven.com/a/grass_medium_01](https://polyhaven.com/a/grass_medium_01) | grass clumps |
| `shrub_sorrel_01` | [polyhaven.com/a/shrub_sorrel_01](https://polyhaven.com/a/shrub_sorrel_01) | low ground cover under the trees |
| `shrub_04` | [polyhaven.com/a/shrub_04](https://polyhaven.com/a/shrub_04) | low shrubs |
| `lambis_shell` | [polyhaven.com/a/lambis_shell](https://polyhaven.com/a/lambis_shell) | shells along the tide line |

The coconut palms are our own, generated in code (`Content/Python/riptide_palm_mesh.py`).

## Models

The patrol skiff and outboard are our own, generated in code (`Content/Python/riptide_boat_mesh.py`).

## Engine content

The ocean, the wake simulation and its boat force (`BP_FluidSim_01`, `M_Fluid_Sim_Force_Boat_Component`, `T_BoatForceFoam`), the churn's foam texture (`T_WaterFlow_01_Foam_Tiled`) and the placeholder shapes come with Unreal Engine (the Water plugin and engine basic shapes).

## Characters (`SourceAssets/Characters/MakeHuman/`)

Bodies made with [MakeHuman Community 1.2.0](http://www.makehumancommunity.org) and written out by `Tools/mh_export.py`, run with MakeHuman's own Python (a headless script over MakeHuman's own libraries). Everything MakeHuman ships or produces is CC0: the base mesh, the game-engine rig, the skin, eye, eyebrow, eyelash, teeth and hair meshes and their textures.

| Folder in the repo | What | Source |
|---|---|---|
| `Male/`, `Female/` | `<Body>_FullBody.gltf` (body, eyes, eyebrows, eyelashes, teeth on the game-engine rig) and `SK_Hair_*.gltf` (short04, short01, long01, braid01), with their textures (`middleage_lightskinned_*_diffuse.png`, `brown_eye.png`, `eyebrow*.png`, `eyelashes01.png`, `teeth.png`, `*_diffuse.png`) | MakeHuman 1.2.0 bundled assets, CC0 |
