#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideWorldGen.h"
#include "RiptideIsland.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;

DECLARE_LOG_CATEGORY_EXTERN(LogRiptideWorld, Log, All);

/**
 * One island's ground, built while the game runs from the world generator, which players can dig into and pile
 * sand onto. Dug sand slumps into the hole over the next second or so, the way real sand does, and nothing digs
 * below the bedrock 0.5-3 m down.
 *
 * The ground is split into square chunks. Chunks near the crew are full detail with collision; farther ones are
 * built coarser, and their edges hang down a little so no gaps show between chunks of different detail.
 *
 * Spawned and removed by ARiptideWorldDirector as the crew moves. Not replicated: every machine builds the same
 * island from the world seed. (Digging isn't sent over the network yet; that comes with the on-foot character.)
 */
UCLASS(NotPlaceable)
class RIPTIDE_API ARiptideIsland : public AActor
{
	GENERATED_BODY()

public:
	ARiptideIsland();

	/**
	 * Builds the island's ground and plants. The terrain is shared with the director, which keeps it if the
	 * island was dug, so the holes are still there if the crew comes back. ScatterMeshes is indexed by
	 * RiptideGen::EScatter; a missing mesh just leaves that kind of plant out.
	 */
	void Build(const RiptideGen::FIslandSite& InSite, const TSharedPtr<RiptideGen::FTerrain>& InTerrain,
		UMaterialInterface* Material, const TArray<UStaticMesh*>& ScatterMeshes, const TArray<FVector>& FocusPoints);

	/** Picks each chunk's detail from how close the nearest crew member or camera is (world positions). */
	void UpdateDetail(const TArray<FVector>& FocusPoints);

	/** Builds every chunk's mesh this frame instead of a few per frame, for the island the crew starts on. */
	void BuildAllChunksNow();

	virtual void Tick(float DeltaSeconds) override;

	/** Digs a scoop out of the ground at a world point. Returns the litres of sand or soil removed. */
	UFUNCTION(BlueprintCallable, Category = "Island")
	float Dig(FVector WorldLocation, float RadiusCm = 50.f, float DepthCm = 20.f);

	/** Dumps litres of sand at a world point; it spreads into a natural mound. Returns the litres placed. */
	UFUNCTION(BlueprintCallable, Category = "Island")
	float Pile(FVector WorldLocation, float Litres, float RadiusCm = 40.f);

	/** Height of the ground under a world point, in cm. */
	UFUNCTION(BlueprintPure, Category = "Island")
	float GetGroundHeight(FVector WorldLocation) const;

	/** How much loose sand or soil is left to dig under a world point, in cm. 0 on rock. */
	UFUNCTION(BlueprintPure, Category = "Island")
	float GetDiggableDepth(FVector WorldLocation) const;

	/** True while dug or piled sand is still sliding. */
	UFUNCTION(BlueprintPure, Category = "Island")
	bool IsSettling() const;

	/** True once every chunk has its mesh. */
	UFUNCTION(BlueprintPure, Category = "Island")
	bool IsFullyBuilt() const;

	/** All the ground's volume in litres, measured from a fixed depth. Only digging and piling change it. */
	UFUNCTION(BlueprintPure, Category = "Island")
	double GetTerrainVolumeLitres() const;

	/** For logs and tests, e.g. "start island (green), 95 m across". */
	UFUNCTION(BlueprintPure, Category = "Island")
	FString GetDescription() const;

	UFUNCTION(BlueprintPure, Category = "Island")
	bool IsStartIsland() const { return Site.Role == RiptideGen::EIslandRole::Start; }

	const RiptideGen::FIslandSite& GetSite() const { return Site; }
	const TSharedPtr<RiptideGen::FTerrain>& GetTerrain() const { return Terrain; }

	/** Whether a world point falls inside this island's ground grid. */
	bool Covers(const FVector& WorldLocation) const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Island")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Chunks;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Plants;

	/** How many rounds of sand slumping run per second. Lower makes collapses slower to watch. */
	UPROPERTY(EditAnywhere, Category = "Island")
	float SettlePassesPerSecond = 30.f;

	/** Chunks rebuilt per frame for detail changes, to spread the work out. Dug chunks always rebuild at once. */
	UPROPERTY(EditAnywhere, Category = "Island")
	int32 DetailRebuildsPerFrame = 4;

private:
	void RebuildChunk(int32 ChunkIndex);
	void MarkEdited(const RiptideGen::FGridRect& Rect);
	int32 DetailStepFor(int32 ChunkIndex, const TArray<FVector>& FocusPoints) const;
	FLinearColor GroundColour(int32 X, int32 Y) const;
	FVector GridToLocal(int32 X, int32 Y) const;
	FVector GridNormal(int32 X, int32 Y) const;

	RiptideGen::FIslandSite Site;
	TSharedPtr<RiptideGen::FTerrain> Terrain;

	/** Grid vertices along one side of a chunk, minus one (chunks share their edge vertices). */
	static constexpr int32 ChunkQuads = 96;
	int32 ChunksPerSide = 0;

	/** Vertex step each chunk is built at (1 = full detail), and the step it should be at. */
	TArray<int32> BuiltStep;
	TArray<int32> WantedStep;

	TSet<int32> EditedChunks;
	float SettleClock = 0.f;
};
