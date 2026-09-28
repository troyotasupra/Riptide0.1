# Working title: **Deadwater**: game design plan

A modern-day open-ocean survival and extraction game. It plays like Sea of Thieves, but the boats have engines, the pirates carry rifles, and the islands hide sealed military facilities.

Engine: Unreal Engine 5, chosen for its particle and fluid simulation.

---

## The pitch
A group of islands sits in a stretch of ocean that a military force has quietly locked down. They are searching for something (possibly several things) lost in underground facilities built into the islands decades ago. You arrive with a beat-up motorboat and almost nothing else. You scavenge old military gear and survival supplies. Armed mercenaries raid the same waters, and you have to fight them off with whatever you've found. The military is a third force that you either avoid or use against the mercs.

## Core loop
Sail out → scout an island or wreck → explore a facility on foot → loot gear and clues → escape the mercs or military → return to harbor → upgrade → repeat.

- **Short loop:** land, clear a small bunker, grab loot, get back to the boat.
- **Mid loop:** find keycards and documents that open deeper facility levels, and upgrade the boat.
- **Long loop:** piece together what the military lost and reach the final facility.

## Factions
| Faction | Role |
|---|---|
| **Mercenaries** | Rival crews of modern pirates. They chase, shoot and board your boat, fight over territory, and hunt you harder the more you hurt them. |
| **Military** | Grows stronger over time. Can be fought at any time, and has the best loot in the game. They also attack mercs. |
| **Survivors / traders** *(optional)* | A floating market and radio contacts that sell fuel and ammo and give leads. |

## Mercenaries

**Rival crews, not one army.** Several merc gangs work the islands, and they hate each other almost as much as they hate you. Each crew has its own:
- **Territory:** islands, camps, docks and fuel depots it controls.
- **Style:** how it fights, what boats it runs and what guns it carries. For example, one crew could be fast and reckless with speedboats and SMGs, one disciplined ex-soldiers with rifles and a gunboat, and one scavengers who set traps and ambushes.
- **Leader:** a boss who can be hunted down.

**Territory:**
- Clear a camp and that area is safer for a while, until the crew (or a rival) moves back in.
- When one crew weakens, rival crews expand into its territory, so the map shifts over time.
- You can play crews against each other: lead one gang's boats into another's waters, or steal from one and let the other take the blame.

**Escalation:** each crew tracks how much you've hurt it. The more damage you do, the harder it hunts you:
1. Ignored: you're just another boat.
2. Noticed: patrols attack on sight.
3. Hunted: ambushes at your usual spots, and raids on your anchored boat.
4. War: the crew sends its best boats and fighters after you, and the boss gets involved.

Escalation cools off over time, or drops sharply if you kill the crew's leader.

## Military

**Their presence grows over time.** The military is closing in on whatever it lost, and the islands change as it does:
1. **Early:** a few scout boats, a distant helicopter now and then, a supply drop offshore. Mostly rumors.
2. **Mid:** forward outposts on the islands, regular sea patrols, restricted zones around key bunkers, and searchlights at night.
3. **Late:** a heavy presence. Helicopters, armed patrol boats, drones, checkpoints, and soldiers clearing bunkers ahead of you.

**You can fight them any time, if you can survive it.**
- Soldiers are better trained, better armed and better coordinated than mercs.
- Military outposts and supply drops have the best loot in the game: modern rifles, armor, night vision, explosives and military boats.
- Attacking them raises an alert level. Push it too high and they send reinforcements, a helicopter or a gunboat.
- They fight the mercs as well. Luring a merc crew into a patrol is a real tactic.
- Their progress and yours are linked: the deeper they get into the bunkers, the more of the islands they lock down. That makes getting there first a race.

## Boats

**You start with nothing.** The crew washes up and has to build a raft from driftwood, barrels, rope and scrap. You paddle it at first, then bolt on a salvaged outboard motor once you find one.

**Getting better boats (there is no shop):**
- **Find:** wrecked and abandoned boats on beaches, reefs and in bunker docks. You have to repair them before they run: patch the hull, fix the engine, find fuel.
- **Take:** kill a merc crew and their boat is yours, if it's still afloat.
- **Repair and improve:** a boat is only as good as the parts you put into it: engines, hull plating, gun mounts, searchlights.

**Stealing works both ways:**
- Mercs can board and take your boat. If you leave it anchored unguarded while you raid a bunker, it might be gone when you come back.
- Leaving someone on watch becomes a real crew decision.
- A stolen boat can be tracked down and taken back.

**How boats handle:**
- Realistic handling: weight, momentum, waves, fuel and throttle all matter.
- Damage matters: a shot-up engine smokes and sputters, and hull holes let water in until you patch and bail.
- Rough progression: raft → patched-up small boats → merc speedboats and RHIBs → ex-military patrol boat.
- Mercs pull alongside and board, which is the signature fight.

## Weapons

**Crafted first, looted later.**
- **Early game (crafted):** spears, a machete from scrap, a bow, fishing-spear guns, pipe shotguns, Molotovs, and scrap-metal armor.
- **Later (looted):** old military rifles, pistols, shotguns, grenades and flares from bunkers, plus modern guns taken off dead mercs.
- Crafted weapons stay useful. A quiet bow still matters when the crew wants to avoid attention.

**Realistic gunplay:**
- A few hits kill, for players and enemies alike.
- Real recoil and bullet drop, with no crosshair when firing from the hip.
- Old military guns can jam and wear down, and need cleaning and parts.
- Ammo is scarce, and each gun uses its own caliber.
- Shooting from a moving boat is hard: the boat rocks and bounces on the waves.
- Gunshots carry, so mercs and the military can hear you.

## Survival (medium)
- **Hunger and thirst** matter but don't nag. Fish, gather coconuts and fruit, hunt, collect rainwater, and boil or filter water.
- **Injuries:** bleeding needs bandages and deeper wounds need a med kit. Being hurt slows you down and makes your aim shaky.
- **Sun and heat:** days are hot, and night is dark and dangerous.
- **Crafting** uses what the islands and wrecks provide: driftwood, rope, scrap metal, cloth, plastic and parts.

## Loot
- **Military:** old guns, ammo, grenades, flares, night vision, radios, gas masks, keycards, maps, documents.
- **Survival:** canned food, water, med kits, fuel, tools, batteries, scuba gear.
- **Boat parts:** outboard motors, engines, props, hull patches, fuel.
- **Crafting materials:** scrap, rope, cloth, plastic and electronics.

## Bunkers
Military-style bunkers with the same feel as the caves and bunkers in Sons of the Forest: tight, dark, tense, and satisfying to clear.

**Modular:**
- Built from interchangeable pieces (corridors, blast doors, bunk rooms, armories, generator rooms, stairwells, flooded sections, collapsed tunnels), so there can be many bunkers that each feel different.
- Small bunkers are a single hatch and a few rooms. Big ones are multi-level complexes that tie into the story.
- Special one-off bunkers anchor the story: the flooded sub pen, the radar station, the weapons lab, the comms bunker and the hidden dry dock.

**Occupied or abandoned:**
- **Occupied:** a merc crew or a military unit is holding it. You fight or sneak your way in.
- **Abandoned:** empty of people, but dark, flooded, booby-trapped or collapsing.
- The state changes over time: a bunker you cleared can be taken by a merc crew, and the military sweeps them as it advances.

**Getting through:** keycards, generators that need fuel, jammed blast doors, flooded sections to swim or pump out, collapsed tunnels you dig or blow through, and old traps and tripwires.

**Claim it:**
- Once cleared, a bunker can be claimed as your crew's vault. It's safe, strong storage underground while you build above ground.
- You can restore power, lock the doors, and set traps of your own.
- Claimed bunkers can be raided. Mercs and the military want them back, especially once your notoriety is high.

## Building and defenses
**Build above your bunker:** walls, watchtowers, gates, gun positions and trenches that protect the entrance.

**Two ways to build:**
- **Crafted:** quick and cheap, but weak: wooden walls, spike barricades, lookout platforms.
- **Salvaged:** existing military structures can be taken apart and rebuilt at your base: barbed wire fences, chain-link, steel plates, tank traps, guard posts and gun emplacements.

**Digging:**
- The island can be dug out to a limit (bedrock stops you; near the beach the hole floods with seawater).
- Dig trenches, foxholes, firing pits, hidden stash holes, or clear the entrance of a buried bunker.
- **Sandbags:** dig out an area, fill bags with the sand, and stack them into walls and gun nests. It's cheap and strong, but slow, which makes it good crew work.

**Concrete:**
- Found as bags of dry cement in bunkers, supply drops and wrecks.
- **Keep it dry.** If a bag gets wet (rain, sea spray, a leaky boat, a dropped bag in the surf) it hardens into a useless brick, so transporting it matters. Hardened bags can still be stacked as crude blocks.
- **Mix it** with water and sand. Once mixed, it's on a clock: pour it before it sets.
- **Build forms** from wood by placing plans, either snapped to a grid or placed freely for odd shapes.
- **Fill the form** with what it needs first, like rebar or scrap metal for strength.
- **Carry it in buckets** and pour. The concrete is simulated as a real flowing material: it pours, spreads and fills the form. A badly built form leaks or bursts. Too much water makes it weak, too little and it won't flow.
- **Curing:** the concrete needs time to set before you pull the forms off. Once set, it's the strongest thing you can build, and it's bulletproof.
- It turns base building into a real crew project: one mixes, two haul buckets, one keeps watch.

## Locked-in decisions
- **Co-op crew of up to 4.** Friends share one boat: driver, gunner, and two more to repair, bail, navigate or go ashore. It should still be playable with fewer players.
- **Realistic boat handling.** Weight, momentum, wave behavior and engine limits all matter. Skill at driving the boat counts.
- **Setting:** a made-up island chain with a tropical paradise look: clear turquoise water, white sand, palms, jungle and reefs. The paradise surface hides abandoned military bunkers underneath.
- **First-person** everywhere, including at the helm.
- **Losing a fight:** you're usually captured, not killed. You wake up tied up in the enemy's camp and have to escape and get your gear back. If you die for real, you lose everything you carried, but it stays where you fell for you to recover.

## The lost thing (undecided)
The military is searching the island facilities for something it lost. What that is hasn't been decided yet.

## Home base
- **An island camp:** shelter, storage, crafting benches, a dock and a bed. It can be raided by mercs.
- **A boat that becomes a mobile base:** bigger boats get bunks, storage and a workbench, so the crew can live at sea.

## Downed, captured and escape
Inspired by the capture escapes in Sons of the Forest.

**When enemies take you down, you don't just die: you get captured.**
- You wake up tied up in the camp of whoever took you: a merc crew's camp or a military holding site.
- Your gear is gone, but it's stored somewhere in that camp (a locker, a tent, the boss's quarters). You can take it back on your way out.
- **Guards scale with notoriety.** A nobody gets one bored guard. A crew that's been at war with the mercs, or high on the military's alert, gets more guards, better guards, locked cells and patrols.

**Escaping:**
- Work free of your restraints (a hidden blade, a sharp edge, or a timed struggle while the guard isn't looking).
- Sneak or fight your way out, then steal a skiff from the camp's dock to get away.
- **Crew rescue:** crewmates still free can mount a rescue and break you out.

**Disguises:**
- Take a guard's gear to blend in with that faction.
- It isn't perfect: get too close, act strangely, or carry the wrong weapon, and they start to notice.
- Get close enough and a soldier or merc may **challenge you for a code** to prove you're one of them. Wrong answer, or no answer, and your cover is blown.
- **Codes come from intel:** radio intercepts, documents in bunkers and outposts, codebooks on officers, or interrogating a captured enemy.
- Codes change regularly and differ by faction and merc crew, so old intel goes stale.

**Dying for real** (drowning, sharks, falls, or when nobody takes you alive):
- Your gear stays where you fell until you recover it. Mercs nearby can find and take it, so waiting is risky.
- A sunk boat stays on the seabed with its cargo. Dive for it, or salvage it.
- You come back at your home island with nothing: see *The mermaids* below.

### The mermaids
Instead of a plain respawn, something in the water brings you home.

How this differs from Sea of Thieves:
- **Never seen clearly.** In Sea of Thieves the mermaid appears beside you and you swim to her. Here you never see them properly: a shape in dark water, a hand on your arm, a face half-lit as you black out. Then you wake on your home beach, coughing up seawater.
- **Not on demand.** They're not a fast-travel button for when you fall off the boat. They only come when you would otherwise die.
- **Unsettling, not friendly.** Nobody knows what they are or why they help. There are small wrong details: scratches on your arm, seaweed you've never seen growing on the island, wet footprints leading out of the sea toward your camp.
- **Payment.** They take something each time, like a random item from your camp stash or a trinket you were carrying, and leave odd things in return: shells, old coins, pieces of wrecks.
- **A relationship.** Leaving offerings at a tide pool on your home island makes them more reliable. Neglect them and they're slower to come, so you wake further away, or badly hurt.
- **The military knows.** Bunker documents mention "the things in the water" as a way to hint at a bigger mystery, without it having to be the lost thing.
- **Grounded.** They never fight for you and never appear during gameplay. They're a rumor the players live through, not a creature in the game.

## Tattoos

Crewmates can tattoo each other, and you can tattoo yourself.

**Doing it:**
- **Crafted kit:** a needle from scrap or a sharpened bone, and ink from charcoal, soot, squid ink or gunpowder (like a prison tattoo).
- **Freehand or stencil:** draw directly on the skin, or trace stencils found in the world (military insignia, merc crew symbols, old sailor designs).
- **Steadiness matters:** on land the lines come out clean. On a rocking boat they wobble, and a crewmate bumping you ruins a line. Bad tattoos are part of the fun.
- **On yourself:** you can only reach your arms, hands, legs and chest, which you tattoo from first person. Anything else needs a crewmate, or a mirror found in the world.

**Why it matters in play:**
- **Tattoos are permanent.** They survive death, capture and mermaid rescues. They're the one thing you never lose, so they become your crew's history.
- **Crew identity:** a matching crew mark.
- **Disguises:** merc crews have their own tattoos. You fake one with **markers**, not ink:
  - A marker "tattoo" helps you pass as one of that crew.
  - It washes off with water: swimming, rain, sweat and sea spray all wear it down. It fades gradually, so the fake gets less convincing the longer you're out.
  - A faded fake is worse than none: a guard who spots a smudged crew mark knows you're an impostor.
  - Markers are a scarce loot item, so a fresh fake has to be planned: stay dry on the way in, or redraw it on the spot.
  - Your own real crew tattoo can blow your cover if a guard sees it. Long sleeves and gloves can hide it.
- **Infection risk:** a dirty needle can cause an infection (it ties into survival). Boil or sterilize it first.
- **The mermaids leave marks:** after a rescue you sometimes wake with a faint mark you didn't make. It could slowly form a pattern over the game.

## Milestones
1. The boat feels great on the ocean
2. Walk on a moving boat; swim; drive from the helm
3. One island and one bunker to explore
4. Inventory and three weapons
5. Merc soldiers guarding a bunker
6. Merc boats that chase and board
7. Boat damage and repair
8. The military faction and a heat system
9. Capture and escape, disguises and codes
10. Claiming bunkers, building, digging, sandbags and concrete
11. Raft building, boat salvage and repair, and boat theft
12. Playable slice: 3 islands, 3 facilities, first chapter of the story

## Open design decisions
- When were the facilities built, and by whom?
- What is the island chain called?
- What did the military lose?
