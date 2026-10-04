#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "RiptideChart.generated.h"

class ARiptideChart;
class UTexture2D;

/** One square of the chart the crew has seen. */
USTRUCT()
struct FRiptideChartCell : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Cell = FIntPoint::ZeroValue;
};

/** The squares seen, sent as they're added. */
USTRUCT()
struct FRiptideChartCells : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FRiptideChartCell> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<ARiptideChart> Owner;

	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FRiptideChartCell, FRiptideChartCells>(Items, DeltaParams, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FRiptideChartCells> : public TStructOpsTypeTraitsBase2<FRiptideChartCells>
{
	enum { WithNetDeltaSerializer = true };
};

/** An island on the chart: where its middle is, how far it spreads, and what it's called. */
struct FRiptideChartIsland
{
	FText Name;
	FVector2D Centre = FVector2D::ZeroVector;
	float Radius = 0.f;
};

/**
 * The crew's chart, shared by everyone in the game (the Godot build's map panel): the islands drawn from their own
 * ground, but only the parts the crew has been near; the rest is blank parchment until someone goes and looks. Each
 * crew member reveals everything within RevealRadius of where they go. Reading a sea chart (the item) marks the
 * islands on everyone's compass, with their bearings, and draws their outlines on the chart.
 *
 * The server keeps what's been seen (and the save keeps it); every machine draws its own picture of the ground,
 * a few rows a frame after the game starts. M opens the chart (SRiptideChartPanel).
 */
UCLASS()
class RIPTIDE_API ARiptideChart : public AActor
{
	GENERATED_BODY()

public:
	ARiptideChart();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The game's chart (ARiptideGameMode places one in every game). */
	static ARiptideChart* Get(const UObject* WorldContext);

	/** Whether a world point has been seen. */
	bool IsSeen(const FVector2D& World) const;

	UFUNCTION(BlueprintPure, Category = "Chart")
	int32 GetSeenCount() const { return Cells.Items.Num(); }

	/** Whether a world point (cm) has been seen, for tests. */
	UFUNCTION(BlueprintPure, Category = "Chart")
	bool IsSeenAt(FVector Location) const { return IsSeen(FVector2D(Location)); }

	/** A sea chart has been read: the islands are on everyone's compass. */
	UFUNCTION(BlueprintPure, Category = "Chart")
	bool IsRead() const { return bRead; }

	/** Reads a sea chart (server): the islands go on the compass. False if they already were. */
	UFUNCTION(BlueprintCallable, Category = "Chart")
	bool Read();

	/** Reveals everything within Radius (cm) of a point (server). */
	void Reveal(const FVector2D& World, float Radius);

	/** The squares seen and whether the chart's been read, for the save, and back. */
	TArray<FIntPoint> GetSeenCells() const;
	void Restore(const TArray<FIntPoint>& Seen, bool bWasRead);

	/** The islands, as charted (centres and sizes from their ground). */
	const TArray<FRiptideChartIsland>& GetIslands() const { return Islands; }

	/** The bearing (degrees from north, clockwise) and distance (m) from a point to an island. */
	static void BearingTo(const FVector& From, const FRiptideChartIsland& Island, int32& OutBearing, float& OutMetres);

	/** The world rectangle the chart covers (cm), its picture of it (null until drawn; parchment where unseen) and how
	 * far through drawing it is (0-1). */
	FBox2D GetBounds() const { return Bounds; }
	FIntPoint GetPictureSize() const { return FIntPoint(Columns, Rows); }

	/** Where a world point (cm) falls on the picture (pixels, north up, east right), and back. */
	FVector2D WorldToPixel(const FVector2D& World) const;
	FVector2D PixelToWorld(const FVector2D& Pixel) const;
	UTexture2D* GetPicture();
	float GetDrawnFraction() const { return Rows > 0 ? float(RowsDrawn) / Rows : 0.f; }

	/** The world rectangle (cm) the crew has seen, round the squares seen (invalid before any). */
	FBox2D GetSeenBounds() const { return SeenBox; }

	/** Changes each time the seen squares change (so the picture is redone). */
	int32 GetSeenVersion() const { return SeenVersion; }
	void NoteSeenChanged() { ++SeenVersion; }

	/** How close a crew member has to come for the chart to show a place (cm), the squares it's kept in, and how
	 * finely it's drawn (cm a pixel). */
	static constexpr float RevealRadius = 7000.f;
	static constexpr float CellSize = 1600.f;
	static constexpr float CmPerPixel = 200.f;

private:
	UPROPERTY(Replicated)
	FRiptideChartCells Cells;

	UPROPERTY(Replicated)
	bool bRead = false;

	/** The seen squares for looking up (the replicated list's mirror). */
	TSet<FIntPoint> Seen;
	FBox2D SeenBox = FBox2D(ForceInit);
	void AddSeen(const FIntPoint& Cell);
	int32 SeenVersion = 0;
	int32 SyncedCount = 0;
	float RevealTimer = 0.f;

	// The picture: the ground's colours, drawn a few rows at a time, and the texture shown (unseen as parchment).
	void FindIslands();
	void DrawRows(int32 Count);
	FColor GroundColour(const FVector2D& World) const;
	FBox2D Bounds = FBox2D(ForceInit);
	int32 Columns = 0;
	int32 Rows = 0;
	int32 RowsDrawn = 0;
	TArray<FColor> Ground;
	TArray<FRiptideChartIsland> Islands;
	bool bHaveBounds = false;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Picture;
	int32 PictureVersion = -1;
	int32 PictureRows = -1;

	friend struct FRiptideChartCells;
};
