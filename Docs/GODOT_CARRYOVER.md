# What carries over from the Godot build

The Godot version (`troyotasupra/Riptide`, Godot 4.7, about 28,000 lines of GDScript) is the prototype for this project. It doesn't get ported line by line: Unreal does most of what it hand-built (water, lighting, physics, networking, particles). What carries over is the design it proved out, the data, the CC0 assets, and the working rules.

## Design already proven in Godot (to rebuild in Unreal)
**The opening:**
- You wash up on a small camp island with nothing.
- Gather fiber, flint and driftwood. Twist rope, make a stone hatchet, chop trees for logs, carve an oar.
- Build a raft frame on the beach (6 logs, 3 rope) and push it into the water.
- Row toward the smoke on the big island.
- At the fishing shack there's a survival book, a bunk, a locked footlocker, and a john boat tied up outside.
- The castaway's camp: tent, campfire, and a pack holding a key, a journal and a page.

**The first quest chain:**
- The cave behind the waterfall, with the skeletons of a thief (dagger) and a hunter (bow), and an outboard motor.
- Fuel drums on a wreck.
- A third island placed as far past the second as the second is from the first.

**Boats:**
- Oars act as the key (fitted in the oarlocks).
- Outboard motor, fuel and helm.
- Tow lines, and cargo carried on the raft.
- You can push and pull a beached raft.

**Survival:**
- Hunger and thirst, draining gently. Canteens hold 4 drinks.
- Cooking, fishing (29 fish), and landing fish on docks and boats.
- A compost bin turns spoiled food into soil.
- Tents and bunks to sleep in. Once the whole crew has turned in, a 5-second countdown to sleep.
- Weather with rain that you can still see through doors and windows.

**Fire:**
- Campfires, torches (30 minutes), a lighter, and a stove with a chimney.
- Wildfire spreads slowly, and trees char with glowing embers.
- Flames should behave like gas, with tongues that spike up and break away.

**Inventory and crafting:**
- 106 items and 17 recipes, with a crafting book (B).
- Grid inventory with gear slots. Whole bags can be picked up (their weight counts) and set down.
- Dropped items lie on the ground as themselves.
- Dismantling (hold Z) refunds 75%; a collapsed structure refunds 25%.

**Weapons:**
- Bow and arrows, Uzi, Mossberg, Intervention (10x scope), and melee (knife, machete, dagger).
- 16 attachments, fitted by right-clicking a gun and choosing "Modify".
- Aim down the sights with no dot. Recoil.
- Gunshot sound with distance delay, occlusion, and cave and room echo.
- A friendly-fire toggle.

**World:**
- Procedurally generated islands.
- A sea chart (M) that fills in as you explore.
- Clear water near shore that darkens with depth.
- Sharks that should be realistic and frightening.

**Co-op:**
- Character creator, with a crew colour and emblem.
- Trade requests that both players approve.
- Proximity voice chat with push-to-talk.
- An update check when the game launches.

## Assets (all CC0, credited in the Godot repo's `assets/CREDITS.md`)
- **Quaternius models** via poly.pizza: 7 guns, palms, trees, bushes. They're `.glb` files, which Unreal imports directly.
- **Poly Haven and ambientCG textures:** sand, wet sand, mud, rock, bark, planks, wood, linen, hessian, corrugated iron, grass, rope, metals, plastic.
- **Kenney sound effects** and an ocean ambience loop.

These are about 110 MB. The Godot repo stored them without Git LFS, so this repo will take them through LFS.

## Conflicts between the two plans, for Troy to settle
- **Crew size:** the Godot build supports 1–6 players, and the new plan says up to 4.
- **Art style:** Godot went stylized low-poly (Quaternius). Unreal makes realistic visuals much more reachable. Stylized keeps the free assets usable as they are.
- **Second developer:** Josh (`JoshuaGessner`) worked on the Godot build on macOS. Unreal C++ on a Mac needs Xcode and a different setup, so decide whether Josh comes along.
