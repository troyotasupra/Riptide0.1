#pragma once

#include "CoreMinimal.h"
#include "RiptideItems.generated.h"

/**
 * Items and Delta Force-style storage grids, carried over from the Godot build (player/item_grid.gd and
 * data/item_table.gd): an item takes up width x height cells and can be turned sideways; stacks top up to a limit.
 */

/** What an item is: one row of the item table. */
struct FRiptideItemDef
{
	FName Id;
	FText Name;
	FText Category;
	FIntPoint Size = FIntPoint(1, 1);     // cells, upright
	int32 Stack = 1;                        // most in one stack
	float WeightKg = 0.f;
	int32 Rarity = 0;                       // 0 common .. 5 exotic, sets the card's colour
	FText Hint;
};

namespace RiptideItems
{
	/** The item table row for Id, or null if there's no such item. */
	RIPTIDE_API const FRiptideItemDef* Find(FName Id);

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

	/** Removes up to Count of item Uid (all of it when Count <= 0) and returns what was taken (Count 0 if nothing). */
	FRiptideItem Take(int32 Uid, int32 Count = 0);

	float TotalWeight() const;
};
