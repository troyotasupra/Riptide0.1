#include "../RiptideItems.h"

/**
 * The item table: every item in the game, one row each, carried over from the Godot build (data/item_table.gd)
 * with the same ids. Food restores hunger and thirst out of 100; spoil and sickness times are seconds; weights kg.
 * RiptideItems::All() and Find() read this through RiptideItems.cpp.
 */
namespace
{
	using K = ERiptideItemKind;
	using S = ERiptideStation;
	using W = ERiptideWearSlot;

	/** Fills in one row's optional parts, so a row reads as one line. */
	struct FRow
	{
		FRiptideItemDef& D;

		FRow& Hint(const TCHAR* Text) { D.Hint = FText::FromString(Text); return *this; }
		FRow& Eats(float Food, float Water = 0.f) { Part().Food = Food; D.Food->Water = Water; return *this; }
		FRow& Spoils(float Seconds) { Part().SpoilSeconds = Seconds; return *this; }
		FRow& Sick(float Seconds, float Chance) { Part().SicknessSeconds = Seconds; D.Food->SickChance = Chance; return *this; }
		FRow& Sips(int32 Count, const TCHAR* EmptiesTo) { Part().Sips = Count; D.Food->EmptiesTo = FName(EmptiesTo); return *this; }
		FRow& CooksTo(const TCHAR* Id) { D.TransformsTo[(int32)S::Cook] = FName(Id); return *this; }
		FRow& BoilsTo(const TCHAR* Id) { return CooksTo(Id); }
		FRow& DriesTo(const TCHAR* Id) { D.TransformsTo[(int32)S::Dry] = FName(Id); return *this; }
		FRow& CompostsTo(const TCHAR* Id) { D.TransformsTo[(int32)S::Compost] = FName(Id); return *this; }
		FRow& Fuel(float Seconds) { D.FuelSeconds = Seconds; return *this; }
		FRow& Floats() { D.bFloats = true; return *this; }
		FRow& Heal(float Amount) { D.Heal = Amount; return *this; }
		FRow& Tool(const TCHAR* Type, int32 Uses = 0, float BurnSeconds = 0.f, float Melee = 0.f)
		{
			FRiptideToolDef T;
			T.Type = FName(Type);
			T.Uses = Uses;
			T.BurnSeconds = BurnSeconds;
			T.Melee = Melee;
			D.Tool = T;
			return *this;
		}
		FRow& Wear(W Slot, float Insulation, int32 Armour = 0, FIntPoint Storage = FIntPoint(0, 0), bool bCrewTint = false)
		{
			FRiptideWearDef V;
			V.Slot = Slot;
			V.Insulation = Insulation;
			V.Armour = Armour;
			V.Storage = Storage;
			V.bCrewTint = bCrewTint;
			D.Wear = V;
			return *this;
		}
		FRow& Places(const TCHAR* Structure) { D.Places = FName(Structure); return *this; }
		FRow& Teaches(std::initializer_list<const TCHAR*> Recipes)
		{
			for (const TCHAR* R : Recipes)
			{
				D.Teaches.Add(FName(R));
			}
			return *this;
		}
		FRow& Note(const TCHAR* Key) { D.Note = FName(Key); return *this; }

	private:
		FRiptideFoodDef& Part()
		{
			if (!D.Food.IsSet())
			{
				D.Food = FRiptideFoodDef();
			}
			return D.Food.GetValue();
		}
	};
}

TArray<FRiptideItemDef> RiptideItems_BuildTable()
{
	TArray<FRiptideItemDef> T;
	T.Reserve(128);
	auto Item = [&T](const TCHAR* Id, const TCHAR* Name, K Kind, int32 Wd, int32 Ht, int32 Stack, float Kg, int32 Rarity) -> FRow
	{
		FRiptideItemDef& D = T.AddDefaulted_GetRef();
		D.Id = FName(Id);
		D.Name = FText::FromString(Name);
		D.Kind = Kind;
		D.Category = RiptideItems::KindName(Kind);
		D.Size = FIntPoint(Wd, Ht);
		D.Stack = Stack;
		D.WeightKg = Kg;
		D.Rarity = Rarity;
		return FRow{ D };
	};
	const TCHAR* Soon = TEXT("Not usable yet: it arrives in a later update.");
	const TCHAR* AmmoHint = TEXT("Rounds for one calibre of gun: they don't fit anything else.");

	// --- Food. Raw fish spoils in a quarter of an hour, cooked food in half an hour, dried food never.
	Item(TEXT("coconut"), TEXT("Coconut"), K::Food, 1, 1, 5, 0.8f, 0).Eats(10, 18).Spoils(2400).Floats().Hint(TEXT("Knock it open for the water and the flesh."));
	Item(TEXT("berries"), TEXT("Berries"), K::Food, 1, 1, 30, 0.05f, 0).Eats(4, 1.5f).Spoils(900).DriesTo(TEXT("dried_berries"));
	Item(TEXT("dried_berries"), TEXT("Dried berries"), K::Food, 1, 1, 30, 0.02f, 0).Eats(5);
	Item(TEXT("red_berries"), TEXT("Red berries"), K::Food, 1, 1, 30, 0.05f, 0).Eats(3, 1).Sick(60, 1.f).Hint(TEXT("Bright red and bitter. They'll make you sick."));
	Item(TEXT("raw_fish"), TEXT("Raw fish"), K::Food, 2, 1, 5, 0.6f, 0).Eats(8).Spoils(900).Sick(60, 0.4f).CooksTo(TEXT("cooked_fish")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("cooked_fish"), TEXT("Cooked fish"), K::Food, 2, 1, 5, 0.5f, 0).Eats(25, 2).Spoils(1800);
	Item(TEXT("dried_fish"), TEXT("Dried fish"), K::Food, 1, 1, 10, 0.25f, 0).Eats(18);
	Item(TEXT("raw_meat"), TEXT("Raw meat"), K::Food, 2, 1, 5, 0.8f, 0).Eats(10).Spoils(900).Sick(90, 0.5f).CooksTo(TEXT("cooked_meat")).DriesTo(TEXT("dried_meat"));
	Item(TEXT("cooked_meat"), TEXT("Cooked meat"), K::Food, 2, 1, 5, 0.7f, 0).Eats(35, 2).Spoils(1800);
	Item(TEXT("dried_meat"), TEXT("Jerky"), K::Food, 1, 1, 10, 0.3f, 0).Eats(25);
	Item(TEXT("raw_sardine"), TEXT("Sardine"), K::Food, 1, 1, 10, 0.15f, 0).Eats(4).Spoils(900).Sick(45, 0.25f).CooksTo(TEXT("cooked_fish")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("raw_mullet"), TEXT("Mullet"), K::Food, 2, 1, 5, 0.9f, 0).Eats(7).Spoils(900).Sick(60, 0.35f).CooksTo(TEXT("cooked_fish")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("raw_pufferfish"), TEXT("Pufferfish"), K::Food, 1, 1, 5, 0.6f, 1).Eats(5).Spoils(900).Sick(240, 1.f).CooksTo(TEXT("cooked_pufferfish")).Hint(TEXT("Poisonous raw, and chancy cooked."));
	Item(TEXT("cooked_pufferfish"), TEXT("Cooked pufferfish"), K::Food, 1, 1, 5, 0.5f, 1).Eats(22, 1).Spoils(1800).Sick(150, 0.35f);
	Item(TEXT("raw_snapper"), TEXT("Red snapper"), K::Food, 2, 1, 3, 2.5f, 1).Eats(10).Spoils(900).Sick(60, 0.35f).CooksTo(TEXT("fish_steak")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("raw_grouper"), TEXT("Grouper"), K::Food, 3, 2, 2, 6.f, 1).Eats(14).Spoils(900).Sick(60, 0.35f).CooksTo(TEXT("fish_steak")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("raw_barracuda"), TEXT("Barracuda"), K::Food, 3, 1, 2, 4.f, 1).Eats(12).Spoils(900).Sick(150, 0.6f).CooksTo(TEXT("fish_steak")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("raw_mahi_mahi"), TEXT("Mahi-mahi"), K::Food, 3, 2, 2, 5.f, 2).Eats(13).Spoils(900).Sick(60, 0.35f).CooksTo(TEXT("fish_steak")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("raw_tuna"), TEXT("Yellowfin tuna"), K::Food, 2, 4, 1, 12.f, 2).Eats(16).Spoils(900).Sick(45, 0.25f).CooksTo(TEXT("fish_steak")).DriesTo(TEXT("dried_fish"));
	Item(TEXT("fish_steak"), TEXT("Fish steak"), K::Food, 2, 1, 5, 0.7f, 1).Eats(40, 3).Spoils(1800);
	Item(TEXT("raw_shark_meat"), TEXT("Raw shark meat"), K::Food, 2, 1, 5, 0.9f, 1).Eats(12).Spoils(900).Sick(90, 0.4f).CooksTo(TEXT("cooked_shark")).DriesTo(TEXT("dried_meat"));
	Item(TEXT("cooked_shark"), TEXT("Shark steak"), K::Food, 2, 1, 5, 0.8f, 1).Eats(32, 2).Spoils(1800);
	Item(TEXT("spoiled_food"), TEXT("Spoiled food"), K::Food, 1, 1, 20, 0.5f, 0).Eats(2).Sick(90, 1.f).CompostsTo(TEXT("soil")).Hint(TEXT("Only good for the compost bin."));
	Item(TEXT("ration_pack"), TEXT("Ration pack"), K::Food, 1, 2, 4, 0.6f, 1).Eats(45, 5).Hint(TEXT("A sealed field meal. Keeps forever."));

	// --- Drink. A canteen holds four drinks; empty, it's a tool you fill at a spring or stream.
	Item(TEXT("canteen_clean"), TEXT("Canteen (clean water)"), K::Drink, 1, 2, 1, 1.f, 0).Sips(4, TEXT("canteen")).Eats(0, 25).Hint(TEXT("A litre of clean water."));
	Item(TEXT("canteen_dirty"), TEXT("Canteen (stream water)"), K::Drink, 1, 2, 1, 1.f, 0).Sips(4, TEXT("canteen")).Eats(0, 25).Sick(90, 0.25f).BoilsTo(TEXT("canteen_clean"))
		.Hint(TEXT("Boil it on a fire before you trust it."));

	// --- Materials.
	Item(TEXT("fiber"), TEXT("Fiber"), K::Material, 1, 1, 50, 0.05f, 0).Hint(TEXT("Stripped leaves and bark. Twist it into rope."));
	Item(TEXT("stone"), TEXT("Stone"), K::Material, 1, 1, 20, 0.6f, 0);
	Item(TEXT("flint"), TEXT("Flint"), K::Material, 1, 1, 20, 0.3f, 0).Hint(TEXT("Knaps to a sharp edge."));
	Item(TEXT("driftwood"), TEXT("Driftwood"), K::Material, 2, 1, 10, 1.2f, 0).Fuel(90).Floats();
	Item(TEXT("log"), TEXT("Log"), K::Material, 3, 1, 5, 2.5f, 0).Fuel(200).Floats();
	Item(TEXT("rope"), TEXT("Rope"), K::Material, 1, 1, 20, 0.1f, 1).Hint(TEXT("Mooring line, lashing, towing."));
	Item(TEXT("soil"), TEXT("Soil"), K::Material, 1, 1, 10, 1.f, 0).Hint(TEXT("From the compost bin. Good for growing things, one day."));
	Item(TEXT("sand"), TEXT("Sand"), K::Material, 1, 1, 10, 2.2f, 0).Hint(TEXT("Dug from the beach. Fills sandbags."));
	Item(TEXT("tarp"), TEXT("Tarp"), K::Material, 2, 2, 2, 1.2f, 2);
	Item(TEXT("paracord"), TEXT("Paracord"), K::Material, 1, 1, 5, 0.3f, 1);
	Item(TEXT("lure"), TEXT("Lure"), K::Material, 1, 1, 10, 0.02f, 1).Hint(TEXT("Bait that lasts: barracuda take it."));
	Item(TEXT("grub"), TEXT("Grub"), K::Material, 1, 1, 30, 0.01f, 0).Spoils(1800).Hint(TEXT("Bait for the small fish by the shore."));
	Item(TEXT("cut_bait"), TEXT("Cut bait"), K::Material, 1, 1, 30, 0.05f, 0).Spoils(900).Hint(TEXT("Bait for the reef fish."));
	Item(TEXT("jig"), TEXT("Jig"), K::Material, 1, 1, 5, 0.03f, 1).Hint(TEXT("Bait that lasts: for the big fish out deep."));

	// --- Ammunition and arrows.
	Item(TEXT("arrow"), TEXT("Arrow"), K::Ammo, 1, 3, 20, 0.03f, 1);
	Item(TEXT("ammo_45"), TEXT(".45 ACP"), K::Ammo, 1, 1, 50, 0.015f, 2).Hint(AmmoHint);
	Item(TEXT("ammo_9mm"), TEXT("9mm"), K::Ammo, 1, 1, 60, 0.012f, 2).Hint(AmmoHint);
	Item(TEXT("ammo_556"), TEXT("5.56"), K::Ammo, 1, 1, 60, 0.013f, 2).Hint(AmmoHint);
	Item(TEXT("ammo_12ga"), TEXT("12 gauge"), K::Ammo, 1, 1, 30, 0.045f, 2).Hint(AmmoHint);
	Item(TEXT("ammo_408"), TEXT(".408"), K::Ammo, 1, 1, 20, 0.032f, 3).Hint(AmmoHint);
	Item(TEXT("flare"), TEXT("Flare"), K::Ammo, 1, 1, 6, 0.15f, 1).Hint(Soon);

	// --- Tools.
	Item(TEXT("knife"), TEXT("Knife"), K::Tool, 1, 2, 1, 0.3f, 1).Tool(TEXT("knife"), 0, 0.f, 15.f);
	Item(TEXT("dagger"), TEXT("Dagger"), K::Tool, 1, 2, 1, 0.35f, 2).Tool(TEXT("knife"), 0, 0.f, 30.f).Hint(TEXT("A thief's blade. Cuts like a knife, and hurts more."));
	Item(TEXT("machete"), TEXT("Machete"), K::Tool, 1, 3, 1, 0.8f, 1).Tool(TEXT("machete"), 0, 0.f, 24.f);
	Item(TEXT("stone_hatchet"), TEXT("Stone hatchet"), K::Tool, 1, 3, 1, 1.2f, 0).Tool(TEXT("hatchet"), 0, 0.f, 18.f).Hint(TEXT("Flint lashed to a handle. Fells trees, slowly."));
	Item(TEXT("oar"), TEXT("Oar"), K::Tool, 1, 4, 1, 1.4f, 0).Tool(TEXT("oar")).Floats().Hint(TEXT("Fits a raft's oarlock. No oars, no rowing."));
	Item(TEXT("lighter"), TEXT("Lighter"), K::Tool, 1, 1, 1, 0.05f, 2).Tool(TEXT("lighter"), 20).Hint(TEXT("Twenty lights in it."));
	Item(TEXT("torch"), TEXT("Torch"), K::Tool, 1, 3, 1, 0.5f, 0).Tool(TEXT("torch"), 0, 1800.f).Hint(TEXT("Burns half an hour once lit."));
	Item(TEXT("canteen"), TEXT("Canteen (empty)"), K::Tool, 1, 2, 1, 0.3f, 0).Tool(TEXT("canteen")).Hint(TEXT("Fill it at a spring, or from a stream and boil it."));
	Item(TEXT("fishing_rod"), TEXT("Fishing rod"), K::Tool, 1, 4, 1, 1.f, 1).Tool(TEXT("fishing_rod")).Hint(TEXT("Q takes it out to fish: hold the left button to cast, right to change the bait."));
	Item(TEXT("cleaning_kit"), TEXT("Cleaning kit"), K::Tool, 2, 1, 1, 0.3f, 1).Tool(TEXT("cleaning_kit"), 10).Hint(Soon);
	Item(TEXT("binoculars"), TEXT("Binoculars"), K::Tool, 2, 1, 1, 0.9f, 2).Tool(TEXT("binoculars")).Hint(Soon);
	Item(TEXT("handheld_radio"), TEXT("Handheld radio"), K::Tool, 1, 2, 1, 0.4f, 2).Tool(TEXT("radio")).Hint(Soon);
	Item(TEXT("tool_kit"), TEXT("Tool kit"), K::Tool, 3, 2, 1, 4.5f, 1).Tool(TEXT("tool_kit")).Hint(TEXT("Spanners, sockets and spares for fixing motors and hulls."));

	// --- Weapons.
	Item(TEXT("spear"), TEXT("Spear"), K::Weapon, 1, 4, 1, 1.5f, 0).Tool(TEXT("spear"), 0, 0.f, 32.f).Hint(TEXT("Flint on a pole. Keeps sharks at arm's length."));
	Item(TEXT("bow"), TEXT("Bow"), K::Weapon, 1, 4, 1, 0.9f, 2).Hint(Soon);
	Item(TEXT("flare_gun"), TEXT("Flare gun"), K::Weapon, 2, 1, 1, 0.6f, 2).Hint(Soon);
	Item(TEXT("m1911"), TEXT("M1911"), K::Weapon, 2, 2, 1, 1.1f, 2).Hint(Soon);
	Item(TEXT("uzi"), TEXT("Uzi"), K::Weapon, 2, 3, 1, 3.6f, 3).Hint(Soon);
	Item(TEXT("m4"), TEXT("M4 carbine"), K::Weapon, 2, 4, 1, 3.4f, 3).Hint(Soon);
	Item(TEXT("mossberg"), TEXT("Mossberg"), K::Weapon, 2, 4, 1, 3.2f, 3).Hint(Soon);
	Item(TEXT("intervention"), TEXT("Intervention"), K::Weapon, 2, 5, 1, 6.5f, 4).Hint(Soon);

	// --- Parts.
	Item(TEXT("outboard_motor"), TEXT("Outboard motor"), K::Part, 2, 4, 1, 18.f, 3).Hint(TEXT("Clamps onto a transom. Twelve litres in its tank."));
	Item(TEXT("fuel_drum"), TEXT("Fuel drum"), K::Part, 2, 3, 1, 18.f, 0).Floats().Hint(TEXT("Twenty litres of outboard fuel in a steel drum."));

	// --- Gun attachments (fitted from a gun's Modify menu).
	const TCHAR* Fit = TEXT("Fits onto a gun from its Modify menu.");
	Item(TEXT("red_dot"), TEXT("Red dot sight"), K::Attachment, 1, 1, 1, 0.1f, 2).Hint(Fit);
	Item(TEXT("holo_sight"), TEXT("Holographic sight"), K::Attachment, 1, 1, 1, 0.15f, 2).Hint(Fit);
	Item(TEXT("prism_3x"), TEXT("3x prism scope"), K::Attachment, 2, 1, 1, 0.3f, 3).Hint(Fit);
	Item(TEXT("lpvo_6x"), TEXT("1-6x scope"), K::Attachment, 2, 1, 1, 0.5f, 3).Hint(Fit);
	Item(TEXT("sniper_scope"), TEXT("Sniper scope"), K::Attachment, 2, 1, 1, 0.7f, 4).Hint(TEXT("Fits the M4 only."));
	Item(TEXT("compensator"), TEXT("Compensator"), K::Attachment, 1, 1, 1, 0.1f, 2).Hint(Fit);
	Item(TEXT("muzzle_brake"), TEXT("Muzzle brake"), K::Attachment, 1, 1, 1, 0.12f, 2).Hint(Fit);
	Item(TEXT("suppressor"), TEXT("Suppressor"), K::Attachment, 2, 1, 1, 0.4f, 3).Hint(Fit);
	Item(TEXT("vertical_grip"), TEXT("Vertical grip"), K::Attachment, 1, 1, 1, 0.1f, 1).Hint(Fit);
	Item(TEXT("angled_grip"), TEXT("Angled grip"), K::Attachment, 1, 1, 1, 0.1f, 1).Hint(Fit);
	Item(TEXT("bipod"), TEXT("Bipod"), K::Attachment, 2, 1, 1, 0.5f, 2).Hint(Fit);
	Item(TEXT("extended_mag"), TEXT("Extended magazine"), K::Attachment, 1, 2, 1, 0.3f, 2).Hint(Fit);
	Item(TEXT("quickdraw_mag"), TEXT("Quickdraw magazine"), K::Attachment, 1, 2, 1, 0.2f, 2).Hint(Fit);
	Item(TEXT("light_stock"), TEXT("Light stock"), K::Attachment, 2, 1, 1, 0.3f, 1).Hint(Fit);
	Item(TEXT("heavy_stock"), TEXT("Heavy stock"), K::Attachment, 2, 1, 1, 0.7f, 2).Hint(Fit);
	Item(TEXT("folding_stock"), TEXT("Folding stock"), K::Attachment, 2, 1, 1, 0.4f, 2).Hint(Fit);
	Item(TEXT("laser"), TEXT("Laser"), K::Attachment, 1, 1, 1, 0.08f, 2).Hint(Fit);
	Item(TEXT("flashlight"), TEXT("Flashlight"), K::Attachment, 1, 1, 1, 0.12f, 1).Hint(Fit);

	// --- Medical and prosthetics.
	Item(TEXT("bandage"), TEXT("Bandage"), K::Medical, 1, 1, 10, 0.05f, 1).Heal(25).Hint(TEXT("Stops bleeding and heals a little."));
	Item(TEXT("first_aid_kit"), TEXT("First aid kit"), K::Medical, 2, 2, 1, 1.2f, 2).Heal(60).Hint(TEXT("A boat's trauma kit: dressings, tourniquets, splints."));
	Item(TEXT("peg_leg"), TEXT("Peg leg"), K::Prosthetic, 1, 3, 1, 1.5f, 1).Wear(W::Leg, 0.f).Hint(TEXT("Stands in for a leg the sharks took."));
	Item(TEXT("hook_hand"), TEXT("Hook hand"), K::Prosthetic, 1, 2, 1, 0.5f, 1).Wear(W::Arm, 0.f).Hint(TEXT("Stands in for a hand the sharks took."));

	// --- Reading, charts and keys.
	Item(TEXT("survival_book"), TEXT("Survival book"), K::Book, 2, 2, 1, 0.4f, 2)
		.Teaches({ TEXT("campfire_kit"), TEXT("lean_to_kit"), TEXT("spear"), TEXT("bandage"), TEXT("torch"), TEXT("cut_bait"), TEXT("jig"), TEXT("compost_bin_kit"), TEXT("sandbag") })
		.Hint(TEXT("Read it: the camp, fire and bait pages go into your crafting book."));
	Item(TEXT("book_page_shelter"), TEXT("Page: shelter"), K::Page, 1, 1, 1, 0.01f, 2).Teaches({ TEXT("tent_kit"), TEXT("drying_rack_kit") }).Hint(TEXT("Torn from a survival book: tents and drying racks."));
	Item(TEXT("book_page_camp"), TEXT("Page: camp"), K::Page, 1, 1, 1, 0.01f, 2).Teaches({ TEXT("storage_crate_kit"), TEXT("stone_hatchet") }).Hint(TEXT("Torn from a survival book: storage crates."));
	Item(TEXT("book_page_prosthetics"), TEXT("Page: prosthetics"), K::Page, 1, 1, 1, 0.01f, 2).Teaches({ TEXT("peg_leg"), TEXT("hook_hand") }).Hint(TEXT("Torn from a survival book: peg legs and hooks."));
	Item(TEXT("logbook"), TEXT("Logbook"), K::Note, 1, 2, 1, 0.3f, 1).Note(TEXT("logbook"));
	Item(TEXT("journal"), TEXT("Journal"), K::Note, 1, 2, 1, 0.2f, 1).Note(TEXT("journal"));
	Item(TEXT("sea_chart"), TEXT("Sea chart"), K::Chart, 1, 2, 1, 0.1f, 2).Hint(TEXT("Marks the islands on your compass once read."));
	Item(TEXT("compartment_key"), TEXT("Footlocker key"), K::Key, 1, 1, 1, 0.02f, 2).Hint(TEXT("Opens the locked footlocker in the shack."));

	// --- Kits: carried, then placed to build something.
	Item(TEXT("campfire_kit"), TEXT("Campfire kit"), K::Kit, 2, 2, 1, 1.5f, 0).Places(TEXT("campfire")).Hint(TEXT("Place it, then add stone and wood."));
	Item(TEXT("lean_to_kit"), TEXT("Lean-to kit"), K::Kit, 2, 3, 1, 2.5f, 0).Places(TEXT("lean_to")).Hint(TEXT("A tarp over poles: some shelter from the rain."));
	Item(TEXT("tent_kit"), TEXT("Tent kit"), K::Kit, 3, 2, 1, 3.f, 1).Places(TEXT("tent")).Hint(TEXT("Place it, add a tarp and rope. Sleep in it."));
	Item(TEXT("drying_rack_kit"), TEXT("Drying rack kit"), K::Kit, 2, 3, 1, 3.f, 1).Places(TEXT("drying_rack")).Hint(TEXT("Dries fish and berries so they keep."));
	Item(TEXT("storage_crate_kit"), TEXT("Storage crate kit"), K::Kit, 3, 3, 1, 6.f, 1).Places(TEXT("storage_crate"));
	Item(TEXT("raft_kit"), TEXT("Raft kit"), K::Kit, 3, 3, 1, 5.f, 1).Places(TEXT("raft_site")).Hint(TEXT("Place it on the shore, then add logs and rope."));
	Item(TEXT("compost_bin_kit"), TEXT("Compost bin kit"), K::Kit, 2, 3, 1, 3.f, 0).Places(TEXT("compost_bin"));
	Item(TEXT("sandbag"), TEXT("Sandbag"), K::Kit, 2, 2, 4, 14.f, 0).Places(TEXT("sandbag_wall")).Hint(TEXT("Stack them into a wall."));

	// --- Wearables.
	Item(TEXT("tshirt"), TEXT("T-shirt"), K::Wearable, 2, 2, 1, 0.2f, 0).Wear(W::Torso, 0.05f, 0, FIntPoint(0, 0), true);
	Item(TEXT("rain_jacket"), TEXT("Rain jacket"), K::Wearable, 2, 2, 1, 1.f, 1).Wear(W::Torso, 0.35f);
	Item(TEXT("wool_sweater"), TEXT("Wool sweater"), K::Wearable, 2, 2, 1, 0.9f, 1).Wear(W::Torso, 0.45f);
	Item(TEXT("shorts"), TEXT("Shorts"), K::Wearable, 2, 1, 1, 0.3f, 0).Wear(W::Legs, 0.f);
	Item(TEXT("cargo_pants"), TEXT("Cargo pants"), K::Wearable, 2, 2, 1, 0.7f, 1).Wear(W::Legs, 0.18f);
	Item(TEXT("sandals"), TEXT("Sandals"), K::Wearable, 2, 1, 1, 0.3f, 0).Wear(W::Feet, 0.f);
	Item(TEXT("hiking_boots"), TEXT("Hiking boots"), K::Wearable, 2, 2, 1, 1.4f, 1).Wear(W::Feet, 0.1f);
	Item(TEXT("wool_beanie"), TEXT("Wool beanie"), K::Wearable, 1, 1, 1, 0.1f, 1).Wear(W::Head, 0.15f);
	Item(TEXT("sun_hat"), TEXT("Sun hat"), K::Wearable, 2, 1, 1, 0.15f, 0).Wear(W::Head, 0.02f);
	Item(TEXT("combat_helmet"), TEXT("Combat helmet"), K::Wearable, 2, 2, 1, 1.5f, 2).Wear(W::Head, 0.05f, 3);
	Item(TEXT("plate_carrier"), TEXT("Plate carrier"), K::Wearable, 3, 3, 1, 7.5f, 3).Wear(W::Vest, 0.08f, 3, FIntPoint(4, 2), true).Hint(TEXT("Stops rounds, and its rig holds a few things."));
	Item(TEXT("daypack"), TEXT("Daypack"), K::Wearable, 3, 3, 1, 0.8f, 1).Wear(W::Back, 0.f, 0, FIntPoint(6, 5));
	Item(TEXT("satchel"), TEXT("Satchel"), K::Wearable, 2, 2, 1, 0.4f, 0).Wear(W::Back, 0.f, 0, FIntPoint(4, 3));

	// --- A bag of gear: a whole grid carried as one thing.
	Item(TEXT("loot_bag"), TEXT("Bag of gear"), K::Bag, 3, 3, 1, 0.8f, 0).Hint(TEXT("Whatever was in it, carried as one bundle."));
	return T;
}
