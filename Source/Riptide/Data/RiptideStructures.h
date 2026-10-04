#pragma once

#include "CoreMinimal.h"
#include "RiptideRecipes.h"

/** What a station does to what's put on it. */
enum class ERiptideStationKind : uint8
{
	None,
	Cook,       // needs a lit fire: cooks and boils
	Dry,
	Compost,
};

/**
 * What the crew can build from kits (the Godot build's data/structure_table.gd): a kit is placed as the first
 * stage, then materials are added stage by stage until it's finished; finished, it may be a station, a shelter, a
 * bed, a container, or (the raft site) something to launch.
 */
struct FRiptideStructureDef
{
	FName Type;
	FText Name;
	TArray<TArray<FRiptideNeed>> Stages;   // materials added in order; empty means the kit alone finishes it
	ERiptideStationKind Station = ERiptideStationKind::None;
	float Warmth = 0.f;                    // degrees added within WarmRadius when lit (a fire) or sheltered
	float WarmRadius = 0.f;
	bool bBed = false;                     // sleep and respawn here
	bool bShore = false;                   // must be placed at the water's edge
	FIntPoint Container = FIntPoint(0, 0); // a grid this big, if it stores things
	float FootprintRadius = 100.f;         // cm kept clear round it
	float Hp = 100.f;
	FName Launches;                        // what a finished raft site becomes
};

namespace RiptideStructures
{
	RIPTIDE_API const TArray<FRiptideStructureDef>& All();
	RIPTIDE_API const FRiptideStructureDef* Find(FName Type);

	/** The share of materials given back when dismantled by hand, and when it collapses. */
	constexpr float DismantleShare = 0.75f;
	constexpr float CollapseShare = 0.25f;

	/** The station's times (seconds): cooking (needs a lit fire), drying, composting; a fire's most fuel. */
	constexpr float CookSeconds = 25.f;
	constexpr float DrySeconds = 240.f;
	constexpr float CompostSeconds = 600.f;
	constexpr float MaxFuelSeconds = 600.f;
}
