#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "GameFramework/Actor.h"
#include "RiptideWorldGen.h"
#include "RiptideWorldDirector.generated.h"

class ARiptideIsland;
class UMaterialInterface;
class UStaticMesh;

/** Rings of the map, measured from the start island. */
UENUM(BlueprintType)
enum class ERiptideZone : uint8
{
	/** The starting island chain, inside the military cordon. */
	HomeChain,
	/** The military ring around the chain. */
	Cordon,
	/** Past the cordon: islands without end. */
	OpenSea
};

/** An island being generated on a worker thread. */
struct FRiptidePendingIsland
{
	RiptideGen::FIslandSite Site;
	TFuture<TSharedPtr<RiptideGen::FTerrain>> Terrain;
};

/**
 * Runs the world: places one in a level and it loads the islands around the crew as they sail, and unloads the
 * ones left behind. The world is endless and comes from WorldSeed, so it's never stored; only islands the crew
 * has dug into are remembered, so their holes are still there when the crew comes back.
 *
 * Islands are generated on worker threads, so sailing into new water doesn't stall the game.
 */
UCLASS()
class RIPTIDE_API ARiptideWorldDirector : public AActor
{
	GENERATED_BODY()

public:
	ARiptideWorldDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "World")
	ERiptideZone GetZoneAt(FVector WorldLocation) const;

	/** Where a crew member's boat starts: just off the start island's beach, facing it. */
	UFUNCTION(BlueprintPure, Category = "World")
	FTransform GetStartSpawnTransform() const;

	UFUNCTION(BlueprintCallable, Category = "World")
	TArray<ARiptideIsland*> GetLoadedIslands() const;

	UFUNCTION(BlueprintCallable, Category = "World")
	ARiptideIsland* GetStartIsland() const;

	/** The loaded island whose ground covers a world point, if any. */
	UFUNCTION(BlueprintCallable, Category = "World")
	ARiptideIsland* FindIslandAt(FVector WorldLocation) const;

	/** Islands still being generated on worker threads. */
	UFUNCTION(BlueprintPure, Category = "World")
	int32 GetPendingIslandCount() const { return Pending.Num(); }

	/** The same seed always makes the same world. */
	UPROPERTY(EditAnywhere, Category = "World")
	int32 WorldSeed = 1337;

	/** Islands whose shore comes within this distance of anyone in the crew are loaded. */
	UPROPERTY(EditAnywhere, Category = "World", meta = (ClampMin = "500"))
	float LoadRadiusM = 2500.f;

	/** ...and unloaded once everyone is this far away. Kept larger than the load radius so islands don't flicker. */
	UPROPERTY(EditAnywhere, Category = "World", meta = (ClampMin = "600"))
	float UnloadRadiusM = 3200.f;

	UPROPERTY(EditAnywhere, Category = "World")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	// Plant meshes. Empty until CC0 models are chosen; islands simply grow nothing of that kind until then.

	UPROPERTY(EditAnywhere, Category = "World|Plants")
	TObjectPtr<UStaticMesh> PalmMesh;

	UPROPERTY(EditAnywhere, Category = "World|Plants")
	TObjectPtr<UStaticMesh> TreeMesh;

	UPROPERTY(EditAnywhere, Category = "World|Plants")
	TObjectPtr<UStaticMesh> BushMesh;

	UPROPERTY(EditAnywhere, Category = "World|Plants")
	TObjectPtr<UStaticMesh> BoulderMesh;

private:
	const RiptideGen::FWorld& GetGenerator() const;
	void GatherFocusPoints(TArray<FVector>& Out) const;
	void UpdateStreaming();
	void SpawnIsland(const RiptideGen::FIslandSite& Site, const TSharedPtr<RiptideGen::FTerrain>& Terrain, const TArray<FVector>& Focus);

	/** Built on first use, so the game mode can ask for the spawn point before play begins. */
	mutable TUniquePtr<RiptideGen::FWorld> Generator;

	TMap<uint64, TWeakObjectPtr<ARiptideIsland>> Loaded;
	TArray<FRiptidePendingIsland> Pending;

	/** Terrain of islands the crew dug into, kept after they unload. */
	TMap<uint64, TSharedPtr<RiptideGen::FTerrain>> Remembered;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedMaterial;

	float StreamClock = 0.f;
};
