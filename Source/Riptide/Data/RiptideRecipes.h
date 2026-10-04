#pragma once

#include "CoreMinimal.h"

/** One ingredient: an item id or a group name (wood), and how many. */
struct FRiptideNeed
{
	FName Item;
	int32 Count = 1;
};

/**
 * What the crew can make, from the Godot build's data/recipe_table.gd: ingredients (groups spend their first-listed
 * member first), an optional tool that must be carried (not used up), the product, and the seconds the hands take
 * (the old build made everything in an instant).
 */
struct FRiptideRecipe
{
	FName Id;
	FText Name;
	TArray<FRiptideNeed> Needs;
	FName Makes;
	int32 Count = 1;
	FName Tool;            // a tool type (hatchet, knife), or none
	float Seconds = 2.f;
};

namespace RiptideRecipes
{
	RIPTIDE_API const TArray<FRiptideRecipe>& All();
	RIPTIDE_API const FRiptideRecipe* Find(FName Id);

	/** Recipes every crew knows from the start, and the ones the survival book teaches. */
	RIPTIDE_API const TArray<FName>& KnownAtStart();
	RIPTIDE_API const TArray<FName>& TaughtByBook();
}
