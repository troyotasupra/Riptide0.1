#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.h"
#include "RiptideStructure.h"
#include "RiptideSurvivalComponent.h"
#include "RiptideWorldSave.generated.h"

class AController;
class ARiptideCharacter;

/** One crew member as they were: where, their condition, what they carried and the recipes they'd learned. Kept for
 * everyone who has played in this world, so a friend who comes back finds their things. */
USTRUCT()
struct FRiptideSavedPlayer
{
	GENERATED_BODY()

	/** Who: "host" for the player hosting, otherwise their online id (or "name:<callsign>" without one). */
	UPROPERTY()
	FString Key;

	UPROPERTY()
	FString Callsign;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;

	UPROPERTY()
	FRiptideVitals Vitals;

	/** Their pockets and backpack, in order; spoil times as seconds left. */
	UPROPERTY()
	TArray<FRiptideItemGrid> Carried;

	UPROPERTY()
	TArray<FName> Recipes;
};

/** Something the crew built, as it stood. */
USTRUCT()
struct FRiptideSavedStructure
{
	GENERATED_BODY()

	UPROPERTY()
	FName Type;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	int32 Stage = 0;

	UPROPERTY()
	TArray<int32> Have;

	UPROPERTY()
	float Fuel = 0.f;

	UPROPERTY()
	bool bLit = false;

	UPROPERTY()
	float Health = 100.f;

	/** What's on a station; DoneAt as seconds left. */
	UPROPERTY()
	TArray<FRiptideStationSlot> Slots;

	/** A crate's contents (spoil times as seconds left). */
	UPROPERTY()
	TArray<FRiptideItemGrid> Stored;
};

/** Something lying on the ground, or a bag of things. */
USTRUCT()
struct FRiptideSavedWorldItem
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FRiptideStorage Contents;
};

/** A raft afloat (or beached), and whether its oars are in it. */
USTRUCT()
struct FRiptideSavedRaft
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	bool bOars = false;
};

/** One harvested thing on an island, growing back. */
USTRUCT()
struct FRiptideSavedProp
{
	GENERATED_BODY()

	/** The island's props actor (by name in the level), and how many props it had, to know it's the same island. */
	UPROPERTY()
	FString Props;

	UPROPERTY()
	int32 PropCount = 0;

	UPROPERTY()
	int32 Batch = 0;

	UPROPERTY()
	int32 Instance = 0;

	UPROPERTY()
	float RespawnIn = 0.f;
};

/**
 * A hosted world, saved on the host's machine: the day and hour, what's been built and dropped and harvested, the
 * rafts, and
 * every crew member who has played in it. Continue (the host screen) loads it; a new game replaces it at its first
 * save. The game saves itself every minute, when the night is slept through, when a crew member leaves and when the
 * host leaves or quits.
 *
 * Times that run on the server's clock (food spoiling, a fire's cooking, a palm growing back) are kept as the
 * seconds left, so they carry on from where they were.
 */
UCLASS()
class RIPTIDE_API URiptideWorldSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bumped when the format changes in a way an older save can't be read into. */
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY()
	int32 Version = CurrentVersion;

	/** The map's package ("/Game/Riptide/Maps/Island_Test"). */
	UPROPERTY()
	FString Map;

	/** When (UTC). */
	UPROPERTY()
	FDateTime SavedAt;

	UPROPERTY()
	int32 Day = 1;

	UPROPERTY()
	float Hours = 8.f;

	UPROPERTY()
	TArray<FRiptideSavedPlayer> Players;

	UPROPERTY()
	TArray<FRiptideSavedStructure> Structures;

	UPROPERTY()
	TArray<FRiptideSavedWorldItem> WorldItems;

	UPROPERTY()
	TArray<FRiptideSavedProp> Harvested;

	UPROPERTY()
	TArray<FRiptideSavedRaft> Rafts;

	/** The save slot: separate when playing in the editor (or named with -RiptideSaveSlot=), so tests never touch the
	 * game's own save. */
	static FString SlotFor(const UObject* WorldContext);

	/** The saved world, or null if there isn't one (or it's from a version that can't be read). */
	static URiptideWorldSave* Load(const UObject* WorldContext);

	/** Writes it to its slot. */
	bool Write(const UObject* WorldContext);

	/** Takes in the world as it is now: the clock, structures, dropped things, harvested props, and everyone playing
	 * (crew who've left keep their last record). Server. */
	void Capture(UWorld* World);

	/** Puts the world back as saved: the clock, structures, dropped things, harvested props. Server, once, at the start. */
	void RestoreWorld(UWorld* World) const;

	/** The record for the player with this controller, if there is one. */
	const FRiptideSavedPlayer* FindPlayer(const AController* Controller) const;

	/** Updates the record of the player playing this character. */
	void CapturePlayer(const ARiptideCharacter* Character);

	/** Gives a character back what their record says (the condition, the carried things, the recipes). Not their
	 * place: ARiptideGameMode spawns them there. */
	static void RestorePlayer(ARiptideCharacter* Character, const FRiptideSavedPlayer& Saved);

	/** How a player is known across games: "host" for this machine's player, else their online id, else their name. */
	static FString PlayerKey(const AController* Controller);

	/** A grid as saved (spoil times as seconds left), and back (fresh ids, spoil times on the server's clock). */
	static FRiptideItemGrid ToSaved(const FRiptideItemGrid& Grid, double Now);
	/** Puts saved stacks back into Into, in their places when they fit there, anywhere otherwise; returns the ones
	 * that didn't fit at all. */
	static TArray<FRiptideItem> FromSaved(FRiptideItemGrid& Into, const FRiptideItemGrid& Saved, double Now);

	/** The server's clock, as everything that counts down uses it. */
	static double ServerNow(const UWorld* World);
};
