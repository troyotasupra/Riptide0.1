#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "RiptideInteractable.h"
#include "RiptideSea.generated.h"

class UStaticMeshComponent;
class UWaterTerrainComponent;

/**
 * One 64 m square of an island's ground (land and seabed), generated and placed by Content/Python/riptide_islands.py.
 *
 * It tells the sea where the bottom is, twice over: its water terrain component draws it into the Water plugin's
 * depth map (so the drawn waves shrink in the shallows), and it's listed with URiptideSeaSubsystem (so the waves
 * the game reads for buoyancy, swimming and spray shrink the same way).
 */
UCLASS()
class RIPTIDE_API ARiptideIslandGround : public AStaticMeshActor
{
	GENERATED_BODY()

public:
	ARiptideIslandGround();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Riptide")
	TObjectPtr<UWaterTerrainComponent> WaterTerrain;
};

/** Knows where the seabed is under any point of the sea, from the island ground in the level. */
UCLASS()
class RIPTIDE_API URiptideSeaSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void AddGround(UStaticMeshComponent* Ground);
	void RemoveGround(UStaticMeshComponent* Ground);

	/** The height of the ground (seabed or land) under or over a point, in cm. False where there's no island ground:
	 *  open sea, too deep to matter to the waves. */
	bool GetGroundZ(const FVector& Location, float& OutZ) const;

	/** As GetGroundZ, for scripts and tests: the ground's height, or a very low number over open sea. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Sea")
	float GetGroundHeight(FVector Location) const;

	/** GetGroundHeight for the world an object is in (tests reach it this way). */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Sea", meta = (WorldContext = "WorldContextObject"))
	static float GroundHeightAt(const UObject* WorldContextObject, FVector Location);

	/** How big the waves are at a point, as a fraction of their open-sea size (0 at the shore, 1 in deep water),
	 *  for the world an object is in. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Sea", meta = (WorldContext = "WorldContextObject"))
	static float WaveScaleAt(const UObject* WorldContextObject, FVector Location);

	/** The sea's surface height at a point this frame, waves included (tests; what the boat's buoyancy reads). */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Sea", meta = (WorldContext = "WorldContextObject"))
	static float SeaSurfaceAt(const UObject* WorldContextObject, FVector Location);

private:
	/** Ground squares by the 64 m cell(s) of the world they cover. */
	TMultiMap<FIntPoint, TWeakObjectPtr<UStaticMeshComponent>> Grounds;

	static constexpr double CellSize = 6400.0;
};

/**
 * The ocean's water body, with waves that die away in shallow water over island ground.
 *
 * The Water plugin only knows the depth of water over a Landscape; over anything else it assumes deep water, and the
 * waves it reports (to buoyancy, swimmers, everything that asks) stay full size right up the beach while the drawn
 * ones shrink. This asks URiptideSeaSubsystem for the depth instead, so every reader of the sea gets the same
 * waves as the ones on screen.
 */
UCLASS()
class RIPTIDE_API URiptideOceanComponent : public UWaterBodyOceanComponent
{
	GENERATED_BODY()

public:
	virtual TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> TryQueryWaterInfoClosestToWorldLocation(const FVector& InWorldLocation,
		EWaterBodyQueryFlags InQueryFlags, const TOptional<float>& InSplineInputKey = TOptional<float>()) const override;

	/** Water this deep or deeper has full-size waves: the depth assumed over open sea, cm. */
	static constexpr float OpenSeaDepth = 3000.f;
};

/** The ocean actor for maps with islands: an AWaterBodyOcean whose water body is a URiptideOceanComponent. */
UCLASS()
class RIPTIDE_API ARiptideOcean : public AWaterBodyOcean
{
	GENERATED_BODY()

public:
	ARiptideOcean(const FObjectInitializer& ObjectInitializer);
};

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

/** One harvested thing on an island: which instance of which batch, and when it grows back (server time). */
USTRUCT()
struct FRiptideDepletedProp
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Batch = 0;

	UPROPERTY()
	int32 Instance = 0;

	UPROPERTY()
	float RespawnAt = 0.f;
};

/**
 * Everything standing on an island that isn't its ground: palms, trees, shrubs, grass, rocks, driftwood. Placed by
 * Content/Python/riptide_islands.py from the island's design. Each kind of mesh is drawn as one batch, however many
 * of it there are. The crew harvest from them (Data/RiptideResources): a palm gives coconuts, a tree logs with a
 * hatchet, a stone is picked up. What's been taken is a short replicated list; a taken thing sinks out of sight
 * until it grows back.
 */
UCLASS()
class RIPTIDE_API ARiptideIslandProps : public AActor, public IRiptideInteractable
{
	GENERATED_BODY()

public:
	ARiptideIslandProps();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Adds one of a mesh at a place in the world. `bSolid` makes it block the crew, boats and shots (trunks, rocks);
	 *  otherwise it's walked through (grass, ferns). `Kind` is the design's prop kind (palm_tall, fern, stone), which
	 *  says what it gives when harvested. Editor-time: the batches are saved with the level. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Islands")
	void AddProp(UStaticMesh* Mesh, const FTransform& WorldTransform, bool bSolid, FName Kind = NAME_None);

	/** How many props have been added, over all kinds. */
	UFUNCTION(BlueprintPure, Category = "Riptide|Islands")
	int32 GetPropCount() const;

	/** How many things are harvested and waiting to grow back. */
	UFUNCTION(BlueprintPure, Category = "Riptide|Islands")
	int32 GetDepletedCount() const { return Depleted.Num(); }

	/** The nearest harvestable thing of a resource kind (palm, tree, stone...) to a point, within Reach cm: where it
	 * stands, or false. For tests. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Islands")
	bool FindNearest(FName ResourceKind, const FVector& Near, float Reach, FVector& OutLocation) const;

	// IRiptideInteractable
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const override;
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Riptide")
	TObjectPtr<USceneComponent> Root;

	/** One batch per mesh and solidity, and each batch's prop kind. */
	UPROPERTY(VisibleAnywhere, Category = "Riptide")
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Batches;

	UPROPERTY(VisibleAnywhere, Category = "Riptide")
	TArray<FName> BatchKinds;

	UPROPERTY(ReplicatedUsing = OnRep_Depleted)
	TArray<FRiptideDepletedProp> Depleted;

	UFUNCTION()
	void OnRep_Depleted();

	/** Which batch and instance a hit is on, or false. */
	bool FindHit(const FHitResult& Hit, int32& OutBatch, int32& OutInstance) const;
	bool IsDepleted(int32 Batch, int32 Instance) const;
	/** Sinks a taken thing out of sight, or brings it back. */
	void ShowInstance(int32 Batch, int32 Instance, bool bShow);
	/** The transforms of things sunk out of sight, to put them back. */
	TMap<uint64, FTransform> Sunk;
	TArray<FRiptideDepletedProp> ShownDepleted;
	float RespawnCheck = 0.f;
};
