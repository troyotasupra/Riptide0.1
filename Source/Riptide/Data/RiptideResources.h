#pragma once

#include "CoreMinimal.h"

/**
 * What the island's plants, stones and driftwood give when harvested (the Godot build's data/resource_table.gd):
 * keyed by the prop kind the island design plants (riptide_island_shape.py: palm_*, tree, shrub, fern, log...).
 */
struct FRiptideYield
{
	FName Item;
	int32 Min = 1;
	int32 Max = 1;
};

struct FRiptideResourceDef
{
	FName Kind;                      // the resource kind: palm, tree, berry_bush, fiber, stone, flint, driftwood
	FText Label;                     // "Pick coconuts"
	FText DepletedLabel;             // "Picked clean"
	FName RequiresTool;              // a tool type that must be carried (hatchet), or none
	TArray<FRiptideYield> Yields;
	float RespawnSeconds = 600.f;
	float HoldSeconds = 1.f;         // how long E is held; tools in ToolSpeed cut it
	TMap<FName, float> ToolSpeed;    // tool type -> how many times faster
	bool bHideWhenDepleted = true;   // the thing itself is taken (a stone, driftwood); a tree stays
};

namespace RiptideResources
{
	/** The resource kind a planted prop kind belongs to (palm_tall -> palm), or none if it can't be harvested. */
	RIPTIDE_API FName KindOfProp(FName PropKind);

	RIPTIDE_API const FRiptideResourceDef* Find(FName Kind);

	/** Seconds of holding E to harvest Kind with these tool types carried. */
	RIPTIDE_API float HarvestSeconds(const FRiptideResourceDef& Def, const TArray<FName>& Tools);
}
