#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
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
