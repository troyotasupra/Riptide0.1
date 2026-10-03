#pragma once

#include "CoreMinimal.h"
#include "RiptideItems.generated.h"

/**
 * Items and Delta Force-style storage grids, carried over from the Godot build (player/item_grid.gd and
 * data/item_table.gd): an item takes up width x height cells and can be turned sideways; stacks top up to a limit.
 * What an item does (food, tool, clothing, a kit that places something, a page that teaches recipes) hangs off its
 * table row in small optional parts, so the row itself stays short. The rows are in Data/RiptideItemTable.cpp.
 */

enum class ERiptideItemKind : uint8
{
	Food, Drink, Material, Tool, Weapon, Ammo, Attachment, Medical, Prosthetic, Book, Page, Note, Chart, Key, Kit, Wearable, Part, Bag
};

/** Where clothing and gear is worn. Arm and Leg are for prosthetics. */
enum class ERiptideWearSlot : uint8
{
	None, Head, Torso, Vest, Legs, Feet, Back, Arm, Leg
};

/** What a station does to the things put on it; also indexes FRiptideItemDef::TransformsTo. */
enum class ERiptideStation : uint8
{
	Cook, Dry, Compost, Count
};

/** Something you eat or drink. */
struct FRiptideFoodDef
{
	float Food = 0.f;                 // hunger restored, of 100
	float Water = 0.f;                // thirst restored, of 100
	float SpoilSeconds = 0.f;         // 0 keeps forever; otherwise becomes spoiled_food after this long
	float SicknessSeconds = 0.f;      // how long it makes you sick, if it does
	float SickChance = 0.f;           // 0..1
	int32 Sips = 0;                   // for canteens: drinks in a full one (each gives Water)
	FName EmptiesTo;                  // what a canteen becomes when its last sip is gone
};

/** Something you use: the type is what recipes and harvesting ask for (knife, hatchet, lighter...). */
struct FRiptideToolDef
{
	FName Type;
	int32 Uses = 0;                   // 0 lasts forever; otherwise FRiptideItem::Charges counts down
	float BurnSeconds = 0.f;          // a torch: how long it burns while held
	float Melee = 0.f;                // damage when swung
};

/** Something you wear. */
struct FRiptideWearDef
{
	ERiptideWearSlot Slot = ERiptideWearSlot::None;
	float Insulation = 0.f;           // warmth, combined across everything worn with diminishing returns
	int32 Armour = 0;
	FIntPoint Storage = FIntPoint(0, 0);   // a bag or rig adds a grid this big while worn
	bool bCrewTint = false;           // takes the crew's colour
};

/** What an item is: one row of the item table. */
struct FRiptideItemDef
{
	FName Id;
	FText Name;
	FText Category;                        // the kind's name, for the inventory's tooltip
	ERiptideItemKind Kind = ERiptideItemKind::Material;
	FIntPoint Size = FIntPoint(1, 1);      // cells, upright
	int32 Stack = 1;                       // most in one stack
	float WeightKg = 0.f;
	int32 Rarity = 0;                      // 0 common .. 5 exotic, sets the card's colour
	FText Hint;

	float FuelSeconds = 0.f;               // burns this long on a fire
	FName TransformsTo[(int32)ERiptideStation::Count];   // what cooking (or boiling), drying and composting make of it
	FName Places;                          // a kit: the structure it places
	TArray<FName> Teaches;                 // a book or page: the recipes it teaches when read
	FName Note;                            // a note: its text, by key
	float Heal = 0.f;                      // health restored when applied
	bool bFloats = false;                  // lies on the sea's surface when dropped in the water, not the seabed
	TOptional<FRiptideFoodDef> Food;
	TOptional<FRiptideToolDef> Tool;
	TOptional<FRiptideWearDef> Wear;

	bool IsFood() const { return Food.IsSet() && (Food->Food > 0.f || Food->Water > 0.f); }
	bool IsTool(FName Type) const { return Tool.IsSet() && Tool->Type == Type; }
	FName TransformedBy(ERiptideStation Station) const { return TransformsTo[(int32)Station]; }
};

namespace RiptideItems
{
	/** The item table row for Id, or null if there's no such item. */
	RIPTIDE_API const FRiptideItemDef* Find(FName Id);

	/** Every row, in table order. */
	RIPTIDE_API const TArray<FRiptideItemDef>& All();

	/** The items a group name stands for in recipes (wood: driftwood or log), or null if Group isn't a group. */
	RIPTIDE_API const TArray<FName>* Group(FName Group);

	/** Whether item Id satisfies Need, which is an item id or a group name. */
	RIPTIDE_API bool Matches(FName Need, FName Id);

	RIPTIDE_API FText KindName(ERiptideItemKind Kind);

	/** Card colours by rarity, and their names (the old build's RARITY_COLORS / RARITY_NAMES). */
	RIPTIDE_API FLinearColor RarityColour(int32 Rarity);
	RIPTIDE_API FText RarityName(int32 Rarity);
}

/** One stack of an item sitting in a grid. */
USTRUCT(BlueprintType)
struct RIPTIDE_API FRiptideItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Uid = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Count = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 X = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Y = 0;

	/** Turned sideways (its width and height swapped). */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	bool bRotated = false;

	/** What's left in it: a lighter's uses, a canteen's sips, a torch's seconds. 0 for things that don't run out. */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Charges = 0;

	/** Server time (seconds) at which the food in it goes off; 0 never. */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	float SpoilAt = 0.f;

	/** Whether two stacks of the same item can be one stack: not when either is partly used up. */
	bool CanMergeWith(const FRiptideItem& Other) const { return Id == Other.Id && Charges == Other.Charges; }
};

/** A storage grid: pockets, a backpack, a boat's locker. */
USTRUCT(BlueprintType)
struct RIPTIDE_API FRiptideItemGrid
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Width = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 Height = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	TArray<FRiptideItem> Items;

	static int32 NewUid();

	/** Cells item Id covers, turned sideways when bRotated. */
	static FIntPoint Footprint(FName Id, bool bRotated);
	static FIntRect RectOf(const FRiptideItem& Item);

	/** A fresh stack of Id, with its charges full. */
	static FRiptideItem NewStack(FName Id, int32 Count = 1);

	const FRiptideItem* Get(int32 Uid) const;
	FRiptideItem* Get(int32 Uid);
	const FRiptideItem* ItemAt(FIntPoint Cell) const;

	bool InBounds(FName Id, int32 X, int32 Y, bool bRotated) const;
	/** Whether Id fits at (X, Y) without overlapping anything but IgnoreUid. */
	bool Fits(FName Id, int32 X, int32 Y, bool bRotated, int32 IgnoreUid = 0) const;
	/** The item Id would land on at (X, Y) if it's exactly one (for merging and swapping); null otherwise. */
	const FRiptideItem* SingleOverlap(FName Id, int32 X, int32 Y, bool bRotated, int32 IgnoreUid) const;

	/** First free spot for Id, upright then sideways. False if there's none. */
	bool FindSpace(FName Id, int32& OutX, int32& OutY, bool& bOutRotated) const;

	/** Puts a stack at (X, Y), or anywhere it fits when X < 0. True if placed. */
	bool Place(FRiptideItem Stack, int32 X = -1, int32 Y = -1, bool bRotated = false);

	/** Tops up matching stacks, then places the rest wherever it fits. Returns how many didn't fit. */
	int32 Add(FName Id, int32 Count);

	/** The same for a stack that already exists (keeping its charges and spoil time). Returns how many didn't fit. */
	int32 AddStack(const FRiptideItem& Stack);

	/** Removes up to Count of item Uid (all of it when Count <= 0) and returns what was taken (Count 0 if nothing). */
	FRiptideItem Take(int32 Uid, int32 Count = 0);

	/** How many of Need are here, across every stack. Need may also be a group name (wood). */
	int32 CountOf(FName Need) const;

	/** Takes Count of Need (an item id or group; a group's first-listed members go first). Returns how many were short. */
	int32 Remove(FName Need, int32 Count);

	/** Whether a tool of this type (knife, hatchet, lighter...) with anything left in it is in here. */
	bool HasTool(FName Type) const;
	const FRiptideItem* FindTool(FName Type) const;
	TArray<FName> ToolTypes() const;

	/** Turns everything past its spoil time into spoiled food. Returns how many stacks changed. Server only. */
	int32 Spoil(float Now);

	float TotalWeight() const;
};
