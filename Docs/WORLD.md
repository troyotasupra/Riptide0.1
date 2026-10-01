# The world: islands and map

How Riptide's world is laid out, what's built so far, and what's still to decide. The design plan is `Docs/PLAN.md`.

## The shape of the map

The map is three rings around the start island:

| Ring | Distance from the start | What's there |
|---|---|---|
| **Home chain** | 0-4.5 km | The opening: the start island, the big island, the far island, a wreck reef, and 7-10 more islands. The same every time for a given world seed. |
| **Military cordon** | 6.5-9.5 km | A ring of open water watched from 10 military outpost rocks. Patrols, searchlights, sharks. You can get through it, but not without being seen and hunted. |
| **Open sea** | 9.5 km onwards, forever | Islands without end: archipelagos of dozens of islands, then long empty crossings, then more. Mercenaries sent to bring you back hunt you out here. |

Between the home chain and the cordon is 2 km of empty water. The cordon is visible on the horizon from the chain's outer islands but too far to wander into by accident.

**Sizes**, so the distances make sense:
- The start island is 160-210 m across. The big island is 600-800 m across and about 1 km from the start: a long row on a raft, but always in sight.
- The far island sits about as far past the big island again.
- A boat at 15 knots crosses the whole home chain in about 20 minutes and the cordon ring in about 6.
- Open-sea islands run from 60 m rocks to 1.6 km islands with mountains.

Previews of the map (the home chain, the cordon, and a 120 km stretch of open sea) are drawn by `Tools/worldgen_test.cpp`; see *Checking the generator* below.

## An endless world: how it works, and why it's affordable

The open sea really is endless, in the same way as No Man's Sky: nothing is stored.
- The sea is divided into 4 km squares. Each square's islands come from the world seed and the square's position, so the same square always has the same islands, on every machine, with no saving and nothing to send over the network.
- Only what players **change** gets saved: holes dug, things built, loot taken. Islands nobody has touched cost nothing to keep.
- Islands load around the crew as they sail (2.5 km out) and unload behind them. Islands are generated on spare CPU cores so sailing into new water doesn't stall the game.

"Endless" in practice: at 15 knots, 1,000 km is 36 hours of sailing. The generator tests a spot 5,800 km out and finds islands there like anywhere else.

**What keeps an endless sea interesting**, and the No Man's Sky trap to avoid (everything starts to look the same):
1. **Hand-made set pieces in generated places.** The modular bunkers, wrecks, merc camps, fuel depots and abandoned research stations get dropped onto generated islands, so every island is new ground but the places on it are built properly.
2. **Distance means something.** The further from the cordon, the bigger and stranger the islands, the rarer the loot, and the harder the mercenary crews.
3. **Fuel is the leash.** Going further needs fuel caches and outposts, which is what makes claiming and defending islands worth it.
4. **Archipelagos and crossings.** Clusters to explore and base in, separated by open water where you're exposed.

## The opening (home chain)

The home chain follows what the Godot build proved out:
- **Start island:** small and wooded, ringed with beach, with a low hill and some rock. That's enough for the first recipes: fibre from plants, flint from rock, logs from trees and palms, and sand.
- **Big island:** the big green island about 1 km away, with the smoke, the fishing shack and the castaway's camp.
- **Wreck reef:** a sand bar off to one side of the crossing, with the wreck and fuel drums.
- **Far island:** rocky and steep, "as far past the big island as the big island is from the start".
- **Filler islands:** small to medium islands across the rest of the chain, for scavenging and the first boat upgrades.

Placement is randomised by the world seed, but always keeps clear water between coasts. Tested over 500 seeds.

## Islands

Three kinds so far:
- **Cays:** low sand islands, all beach and dunes, with palms.
- **Green islands:** hills under jungle, with beaches in most places and rocky stretches in between.
- **Rocky islands:** steep and high, mostly cliff, with a few coves to land in.

Each island is stretched along its own axis and has a second lobe joined on (a headland, a hooked bay, or a second hill on a saddle), so islands come out long and irregular instead of round. Around every island is a shelf of turquoise sand flats, then the reef edge drops off to deep blue.

The ground is coloured by what it is: dry sand, darker wet sand at the waterline, jungle floor and grass inland, bare rock on cliffs, and fresh dirt where someone has dug. That colouring is a stand-in until ground textures are chosen (see *Still to do*).

## Digging

The sand moves like sand:
- Every spot on an island has a layer of loose ground over bedrock: about 2.5-3 m of sand on beaches and cays, 0.6-1.4 m of soil inland, and none on cliffs and steep rock. **Nothing digs deeper than 3 m**, and nothing digs through bedrock.
- After every scoop the sand slides. Hole walls steeper than sand can stand cave in until they settle at sand's natural slope, which takes about half a second to a second, so you see it run in. A dumped pile spreads into a cone the same way.
- Wet sand stands steeper than dry sand, so you can dig a neater hole near the waterline, but sand under water slumps flattest of all. A hole dug below sea level on the beach fills with seawater.
- Volume is kept exactly: every litre dug out can be put back, piled up, or (later) bagged into sandbags.

**What that means in play:** a narrow shaft in dry sand won't stay deep. Dig 2.5 m straight down and the walls run in until it's about 1 m deep with sloping sides. A deep foxhole needs either a wide crater or walls shored up with planks or sandbags, which suits the plan's sandbag and trench building.

**Is 3 m right?** It's deep enough for a standing foxhole, trenches, and uncovering a buried bunker hatch. It's shallow enough that nobody tunnels under a whole island, and it keeps bunkers as the only way underground. Easy to change.

## Still to do (and what needs Troy)

1. **Ground textures** (needs your OK): CC0 sand, wet sand, soil, grass and rock textures from Poly Haven or ambientCG, replacing the flat colours. The Godot build already used several; I'd list them with sizes for you to approve first.
2. **Plants** (needs your OK): the islands already know where palms, trees, bushes and boulders grow (a handful on a sand bar, about 300 on the start island, 5,000-6,500 on the big island). They need realistic CC0 models. Until then they're left out rather than shown as placeholder shapes.
3. **Wildlife:** crabs on beaches, seabirds, fish on the reefs, sharks around the cordon and over deep water, and maybe wild pigs on big islands. They need models too.
4. **Digging over the network:** right now each player's game digs its own copy. Shared digging comes with the on-foot character (milestone 2), which also brings the shovel.
5. **Set pieces:** bunkers, wrecks and camps placed onto generated islands.
6. **The cordon itself:** patrol boats, searchlights and outpost buildings on the 10 rocks.
7. **Distant islands:** islands past 2.5 km don't appear yet, so they pop in at the horizon haze. They need a cheap far-away version.
8. **Water at long range:** the test sea is 24 km across. Sailing out past about 12 km needs the ocean to follow the boat. That's the next technical step for the open sea.

## Open decisions

- **What the military and the crew are both hunting.** It could be what gets you through the cordon, or the reason the cordon exists.
- **Whether the home chain is the same for everyone.** A fixed seed for the story chain makes guides and shared stories possible; a random one makes each crew's opening their own. The open sea can be random either way.
- **Island names** for the home chain.

---

## Technical notes (for Josh)

- `Source/Riptide/RiptideWorldGen.*` is the generator: island placement, island shape, the terrain grid, digging and sand slumping. It's plain C++ with no engine headers, so it's tested outside Unreal.
- `ARiptideWorldDirector` (one per level) streams islands around every pawn and player camera, generates terrain on the thread pool, and keeps the terrain of any island that was dug when it unloads.
- `ARiptideIsland` builds `UProceduralMeshComponent` chunks (96×96 quads) from the terrain. Detail drops with distance from the crew (full detail within 150 m, 2 m within 500 m, 6 m within 1.2 km, 16 m beyond), and chunks deeper than 12 m are never finer than 4 m. Only chunks of 2 m or finer get collision. Chunk edges have skirts, so different detail levels never show gaps. Dug chunks rebuild in the same frame; detail changes spread over frames.
- Grid spacing is 0.5 m on islands up to 150 m radius, 1 m up to 400 m, and 2 m beyond, with the grid capped at 1200 vertices a side.
- `ARiptideGameMode` spawns boats off the start island when the level has a director; otherwise it behaves as before (Ocean_Test is unchanged).
- Console commands (play in editor, then press the backtick key): `Riptide.Dig [scoops]` and `Riptide.Pile [litres]` act on the ground you're looking at, within 60 m.
- `M_IslandTerrain` is built by `Content/Python/init_unreal.py`: base colour from vertex RGB, and roughness from vertex alpha (wetness).
- Known limit: procedural meshes have no mesh distance fields, so software Lumen sees the islands only through screen traces. If island lighting looks flat, the fix is hardware ray tracing (the RTX 3060 supports it) or baking settled chunks to static meshes.

### Checking the generator

```
c++ -std=c++17 -O2 -ISource/Riptide Tools/worldgen_test.cpp Source/Riptide/RiptideWorldGen.cpp -o worldgen_test
./worldgen_test 1337 Saved/WorldPreview
```

It checks layout, determinism, overlap, spawn depth, digging, bedrock, slumping and volume, then writes PNG previews into `Saved/WorldPreview`: `home_chain.png`, `cordon.png`, `region_120km.png`, `start_island.png`, `start_island_foliage.png`, `big_island.png`, `far_island.png` and `dig_profile.png`.

In the engine, `Tools/island_test.py` checks the same things are wired up in play (see the README).
