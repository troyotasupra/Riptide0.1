#pragma once

#include "CoreMinimal.h"

/**
 * Fish, and the rules of catching them (the Godot build's data/fish_table.gd and items/fishing_math.gd): where each
 * kind lives, what it bites and when, how big it grows and how hard it fights; and the fight on the line. Hold to
 * reel and the tension climbs; ease off and it falls, but the fish runs. Too much tension snaps the line; too much
 * slack, or letting it run too far, loses it.
 *
 * Spots: "shore" (shallow water), "reef" (moderate depth) and "deep". Time bands: dawn, day, dusk, night. Bait kinds:
 * "bare" (an empty hook), grub, berries, cut_bait, lure, jig.
 */
struct FRiptideFishDef
{
	FName Id;                          // sardine, snapper...
	FText Name;                        // "Red snapper"
	FName Item;                        // the raw fish it gives (raw_snapper)
	float MinKg = 0.1f, MaxKg = 1.f;
	float Fight = 0.3f;                // 0..1: how hard it pulls
	float Speed = 1.f;                 // how often it surges
	TMap<FName, float> Habitats;       // spot -> weight
	TMap<FName, float> Baits;          // bait kind -> appetite
	TMap<FName, float> Times;          // time band -> multiplier (1 if not listed)
};

/** A fight on the line, as the angler plays it. */
struct FRiptideFishFight
{
	float Distance = 10.f;             // metres of line out
	float Start = 10.f;                // where it was hooked
	float Tension = 0.25f;             // 0..1; 1 snaps
	float Slack = 0.f;                 // seconds the line has hung slack
	bool bShark = false;               // a shark has taken the fish on the line
};

enum class ERiptideFightResult : uint8 { None, Landed, Snapped, Escaped };

namespace RiptideFish
{
	constexpr float ShoreDepth = 3.5f;          // metres
	constexpr float ReefDepth = 14.f;
	constexpr float MinCast = 4.f;
	constexpr float MaxCast = 22.f;
	constexpr float ChargeSeconds = 1.1f;
	constexpr float HookWindow = 1.3f;          // seconds to strike once it bites
	constexpr float ReelSpeed = 4.f;            // m/s
	constexpr float TensionReel = 0.06f;        // tension added per second reeling, before the fish's pull
	constexpr float RunSpeed = 1.4f;            // how fast a fish takes line while you ease off, per unit of pull
	constexpr float TensionEase = 0.9f;
	constexpr float SlackEscape = 3.f;
	constexpr float LandDistance = 1.2f;
	constexpr float EscapeExtra = 10.f;         // it's gone if it runs this much further out than where it was hooked
	constexpr float SharkChance = 0.14f;        // in deep water, that a shark goes for the fish on the line
	constexpr float SharkPull = 2.4f;
	constexpr float MinFightSeconds = 1.f;      // a landed catch must have been on the line this long (the server checks)

	RIPTIDE_API const TArray<FRiptideFishDef>& All();
	RIPTIDE_API const FRiptideFishDef* Find(FName Id);

	/** The bait kind an item is ("bare" for anything that isn't bait), and whether it survives a catch (a lure, a jig). */
	RIPTIDE_API FName BaitKind(FName Item);
	RIPTIDE_API bool IsBait(FName Item);
	RIPTIDE_API bool IsBaitReusable(FName Item);
	/** Every bait item, in the order right-click cycles them. */
	RIPTIDE_API const TArray<FName>& BaitItems();

	RIPTIDE_API FName SpotOf(float WaterDepthMetres);
	RIPTIDE_API FName TimeBand(float Hours);

	/** How likely each kind is to take this bait at this spot and time. Kinds that won't are left out. */
	RIPTIDE_API TMap<FName, float> Weights(FName Spot, FName BaitKind, FName Band);
	RIPTIDE_API FName Pick(const TMap<FName, float>& Odds, float Roll);
	/** Seconds until something bites: hungrier water bites sooner. A huge number if nothing will. */
	RIPTIDE_API float BiteSeconds(const TMap<FName, float>& Odds, float Roll);
	/** A catch's weight: most are small for their kind, a few are monsters. */
	RIPTIDE_API float RollKg(const FRiptideFishDef& Fish, float Roll);
	/** How hard it pulls: its kind's fight, stronger the bigger it is. */
	RIPTIDE_API float Strength(const FRiptideFishDef& Fish, float Kg);

	RIPTIDE_API FRiptideFishFight NewFight(float Distance);
	/** Advances a fight by Dt. Surge (0..1.3) is how hard the fish pulls right now. */
	RIPTIDE_API ERiptideFightResult Step(FRiptideFishFight& Fight, bool bReeling, float Dt, float Surge, float PullStrength);
	/** The rhythm of a fish's pull, Time seconds into the fight. */
	RIPTIDE_API float SurgeAt(float Time, float Speed);
}
