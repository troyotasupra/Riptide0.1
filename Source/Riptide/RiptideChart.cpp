#include "RiptideChart.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "RiptideBeachStart.h"
#include "RiptideSea.h"

#define LOCTEXT_NAMESPACE "RiptideChart"

namespace
{
	const FColor Parchment(219, 204, 168);
	const FColor Ink(71, 51, 33);
	constexpr float ChartMargin = 40000.f;        // open sea charted round the islands (cm)
	constexpr int32 SamplesPerFrame = 1500;        // ground heights looked up a frame while drawing (each a trace)
}

void FRiptideChartCells::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	if (Owner)
	{
		for (const int32 Index : AddedIndices)
		{
			Owner->AddSeen(Items[Index].Cell);
		}
		Owner->NoteSeenChanged();
	}
}

ARiptideChart::ARiptideChart()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);
	Cells.Owner = this;
}

void ARiptideChart::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideChart, Cells);
	DOREPLIFETIME(ARiptideChart, bRead);
}

ARiptideChart* ARiptideChart::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	for (TActorIterator<ARiptideChart> It(World); World && It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ARiptideChart::AddSeen(const FIntPoint& Cell)
{
	Seen.Add(Cell);
	SeenBox += FBox2D(FVector2D(Cell.X * CellSize, Cell.Y * CellSize), FVector2D((Cell.X + 1) * CellSize, (Cell.Y + 1) * CellSize));
}

void ARiptideChart::BeginPlay()
{
	Super::BeginPlay();
	Cells.Owner = this;
}

bool ARiptideChart::IsSeen(const FVector2D& World) const
{
	return Seen.Contains(FIntPoint(FMath::FloorToInt32(World.X / CellSize), FMath::FloorToInt32(World.Y / CellSize)));
}

bool ARiptideChart::Read()
{
	if (!HasAuthority() || bRead)
	{
		return false;
	}
	bRead = true;
	ForceNetUpdate();
	return true;
}

void ARiptideChart::Reveal(const FVector2D& World, float Radius)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Steps = FMath::CeilToInt32(Radius / CellSize);
	const FIntPoint Base(FMath::FloorToInt32(World.X / CellSize), FMath::FloorToInt32(World.Y / CellSize));
	bool bAdded = false;
	for (int32 DY = -Steps; DY <= Steps; ++DY)
	{
		for (int32 DX = -Steps; DX <= Steps; ++DX)
		{
			const FIntPoint Cell = Base + FIntPoint(DX, DY);
			const FVector2D Middle((Cell.X + 0.5f) * CellSize, (Cell.Y + 0.5f) * CellSize);
			if (Seen.Contains(Cell) || FVector2D::Distance(Middle, World) > Radius)
			{
				continue;
			}
			AddSeen(Cell);
			FRiptideChartCell& Item = Cells.Items.AddDefaulted_GetRef();
			Item.Cell = Cell;
			Cells.MarkItemDirty(Item);
			bAdded = true;
		}
	}
	if (bAdded)
	{
		NoteSeenChanged();
	}
}

TArray<FIntPoint> ARiptideChart::GetSeenCells() const
{
	TArray<FIntPoint> Out;
	Out.Reserve(Cells.Items.Num());
	for (const FRiptideChartCell& Item : Cells.Items)
	{
		Out.Add(Item.Cell);
	}
	return Out;
}

void ARiptideChart::Restore(const TArray<FIntPoint>& SeenCells, bool bWasRead)
{
	if (!HasAuthority())
	{
		return;
	}
	for (const FIntPoint& Cell : SeenCells)
	{
		if (!Seen.Contains(Cell))
		{
			AddSeen(Cell);
			FRiptideChartCell& Item = Cells.Items.AddDefaulted_GetRef();
			Item.Cell = Cell;
			Cells.MarkItemDirty(Item);
		}
	}
	bRead = bWasRead;
	NoteSeenChanged();
	ForceNetUpdate();
}

void ARiptideChart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The server reveals round every crew member twice a second.
	if (HasAuthority())
	{
		RevealTimer -= DeltaSeconds;
		if (RevealTimer <= 0.f)
		{
			RevealTimer = 0.5f;
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APawn* Pawn = It->IsValid() ? (*It)->GetPawn() : nullptr)
				{
					Reveal(FVector2D(Pawn->GetActorLocation()), RevealRadius);
				}
			}
		}
	}
	// Every machine with a player on it draws its own picture of the ground, a few rows a frame.
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (!bHaveBounds)
		{
			FindIslands();
		}
		if (bHaveBounds && RowsDrawn < Rows)
		{
			DrawRows(FMath::Max(1, SamplesPerFrame / FMath::Max(Columns, 1)));
		}
	}
}

void ARiptideChart::FindIslands()
{
	// Each island's props actor spreads over its island: its middle and size, named for the one the crew washed up on.
	Islands.Reset();
	FBox2D All(ForceInit);
	int32 Unnamed = 0;
	for (TActorIterator<ARiptideIslandProps> It(GetWorld()); It; ++It)
	{
		FBox Box(ForceInit);
		for (const UActorComponent* Component : It->GetComponents())
		{
			if (const UHierarchicalInstancedStaticMeshComponent* Batch = Cast<UHierarchicalInstancedStaticMeshComponent>(Component))
			{
				if (Batch->GetInstanceCount() > 0)
				{
					Box += Batch->Bounds.GetBox();
				}
			}
		}
		if (!Box.IsValid)
		{
			continue;
		}
		FRiptideChartIsland& Island = Islands.AddDefaulted_GetRef();
		Island.Centre = FVector2D(Box.GetCenter());
		Island.Radius = FVector2D(Box.GetExtent()).Size() * 0.7f;
		bool bStart = false;
		for (TActorIterator<ARiptideBeachStart> Start(GetWorld()); Start; ++Start)
		{
			bStart |= FVector2D::Distance(FVector2D(Start->GetActorLocation()), Island.Centre) < Island.Radius * 1.5f;
		}
		Island.Name = bStart ? LOCTEXT("StartCay", "The cay") : FText::Format(LOCTEXT("Island", "Island {0}"), ++Unnamed);
		All += FBox2D(Island.Centre - FVector2D(Island.Radius), Island.Centre + FVector2D(Island.Radius));
	}
	if (!All.bIsValid)
	{
		// No islands (the boat maps): the sea round the start.
		All = FBox2D(FVector2D(-20000.f), FVector2D(20000.f));
	}
	All = All.ExpandBy(ChartMargin);
	Bounds = All;
	// Drawn north up: the picture's columns run east (the world's Y), its rows south (down the world's X).
	Columns = FMath::Clamp(FMath::CeilToInt32(Bounds.GetSize().Y / CmPerPixel), 16, 1024);
	Rows = FMath::Clamp(FMath::CeilToInt32(Bounds.GetSize().X / CmPerPixel), 16, 1024);
	Ground.Init(Parchment, Columns * Rows);
	RowsDrawn = 0;
	bHaveBounds = true;
}

FColor ARiptideChart::GroundColour(const FVector2D& World) const
{
	const URiptideSeaSubsystem* Sea = GetWorld()->GetSubsystem<URiptideSeaSubsystem>();
	float Z = 0.f;
	const bool bGround = Sea && Sea->GetGroundZ(FVector(World, 0.f), Z);
	const float Metres = bGround ? Z / 100.f : -40.f;
	FLinearColor Colour;
	if (Metres < -0.3f)
	{
		// Water: pale over the shallows, deepening blue.
		Colour = FMath::Lerp(FLinearColor(0.55f, 0.72f, 0.74f), FLinearColor(0.24f, 0.4f, 0.52f), FMath::Clamp(-Metres / 25.f, 0.f, 1.f));
	}
	else if (Metres < 1.4f)
	{
		Colour = FLinearColor(0.86f, 0.8f, 0.62f);            // sand
	}
	else
	{
		// Higher ground greener, the hilltops lighter.
		Colour = FMath::Lerp(FLinearColor(0.6f, 0.62f, 0.4f), FLinearColor(0.72f, 0.7f, 0.52f), FMath::Clamp((Metres - 1.4f) / 12.f, 0.f, 1.f));
	}
	// A faint contour every five metres of height.
	if (Metres > 2.f && FMath::Fmod(Metres, 5.f) < 0.35f)
	{
		Colour *= 0.78f;
	}
	// The water's edge inked in.
	if (Metres > -0.3f && Metres < 0.15f)
	{
		Colour = FLinearColor(Ink) * 0.5f + Colour * 0.5f;
	}
	FColor Out = Colour.ToFColor(false);
	Out.A = 255;
	return Out;
}

void ARiptideChart::DrawRows(int32 Count)
{
	for (int32 i = 0; i < Count && RowsDrawn < Rows; ++i, ++RowsDrawn)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			Ground[RowsDrawn * Columns + Column] = GroundColour(PixelToWorld(FVector2D(Column + 0.5f, RowsDrawn + 0.5f)));
		}
	}
}

UTexture2D* ARiptideChart::GetPicture()
{
	if (!bHaveBounds || Columns <= 0)
	{
		return nullptr;
	}
	if (!Picture)
	{
		Picture = UTexture2D::CreateTransient(Columns, Rows, PF_B8G8R8A8);
		Picture->Filter = TF_Bilinear;
		Picture->SRGB = true;
		PictureVersion = -1;
	}
	// Redone when more is seen or more is drawn.
	if (PictureVersion != SeenVersion || PictureRows != RowsDrawn)
	{
		PictureVersion = SeenVersion;
		PictureRows = RowsDrawn;
		FTexture2DMipMap& Mip = Picture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const bool bShown = Row < RowsDrawn && IsSeen(PixelToWorld(FVector2D(Column + 0.5f, Row + 0.5f)));
				Pixels[Row * Columns + Column] = bShown ? Ground[Row * Columns + Column] : Parchment;
			}
		}
		Mip.BulkData.Unlock();
		Picture->UpdateResource();
	}
	return Picture;
}

FVector2D ARiptideChart::WorldToPixel(const FVector2D& World) const
{
	return FVector2D((World.Y - Bounds.Min.Y) / CmPerPixel, (Bounds.Max.X - World.X) / CmPerPixel);
}

FVector2D ARiptideChart::PixelToWorld(const FVector2D& Pixel) const
{
	return FVector2D(Bounds.Max.X - Pixel.Y * CmPerPixel, Bounds.Min.Y + Pixel.X * CmPerPixel);
}

void ARiptideChart::BearingTo(const FVector& From, const FRiptideChartIsland& Island, int32& OutBearing, float& OutMetres)
{
	// Unreal's X is north and Y east (riptide_islands.py lays the islands out so).
	const FVector2D To = Island.Centre - FVector2D(From);
	OutBearing = (FMath::RoundToInt32(FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X))) + 360) % 360;
	OutMetres = To.Size() / 100.f;
}

#undef LOCTEXT_NAMESPACE
