#include "RiptideIsland.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY(LogRiptideWorld);

namespace RiptideIslandDetail
{
	// How fine the ground is built at each distance from the crew: grid spacing wanted, in metres.
	struct FDetailBand
	{
		float WithinM;
		float SpacingM;
	};
	constexpr FDetailBand DetailBands[] = { { 150.f, 0.f }, { 500.f, 2.f }, { 1200.f, 6.f } };
	constexpr float FarSpacingM = 16.f;

	/** Chunks entirely deeper than this are never built finer than DeepSpacingM: nobody sees that much detail there. */
	constexpr float DeepBelowM = -12.f;
	constexpr float DeepSpacingM = 4.f;

	/** Ground coarser than this has no collision: nothing that far from the crew needs to touch it. */
	constexpr float MaxCollisionSpacingM = 2.f;

	// Ground colours (linear albedo). Textures replace these once ground textures are chosen.
	const FLinearColor DrySand(0.62f, 0.54f, 0.38f);
	const FLinearColor WetSand(0.33f, 0.28f, 0.19f);
	const FLinearColor Seabed(0.58f, 0.52f, 0.40f);
	const FLinearColor JungleFloor(0.10f, 0.09f, 0.05f);
	const FLinearColor Grass(0.07f, 0.13f, 0.04f);
	const FLinearColor DugSoil(0.17f, 0.12f, 0.08f);
	const FLinearColor Rock(0.21f, 0.20f, 0.18f);
}

ARiptideIsland::ARiptideIsland()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void ARiptideIsland::Build(const RiptideGen::FIslandSite& InSite, const TSharedPtr<RiptideGen::FTerrain>& InTerrain,
	UMaterialInterface* Material, const TArray<UStaticMesh*>& ScatterMeshes, const TArray<FVector>& FocusPoints)
{
	Site = InSite;
	Terrain = InTerrain;
	check(Terrain.IsValid() && Terrain->Size > 1);

	ChunksPerSide = FMath::DivideAndRoundUp(Terrain->Size - 1, ChunkQuads);
	const int32 ChunkCount = ChunksPerSide * ChunksPerSide;
	Chunks.Reserve(ChunkCount);
	for (int32 C = 0; C < ChunkCount; ++C)
	{
		UProceduralMeshComponent* Chunk = NewObject<UProceduralMeshComponent>(this);
		Chunk->bUseAsyncCooking = true;
		Chunk->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Chunk->SetupAttachment(Root);
		Chunk->RegisterComponent();
		if (Material)
		{
			Chunk->SetMaterial(0, Material);
		}
		Chunks.Add(Chunk);
	}
	BuiltStep.Init(0, ChunkCount);
	WantedStep.Init(1, ChunkCount);
	UpdateDetail(FocusPoints);

	// Plants, one instanced component per kind that has a mesh.
	const std::vector<RiptideGen::FScatterPoint> Points = RiptideGen::ScatterFoliage(*Terrain, Site);
	const int32 KindCount = static_cast<int32>(RiptideGen::EScatter::Count);
	for (int32 Kind = 0; Kind < KindCount && Kind < ScatterMeshes.Num(); ++Kind)
	{
		UStaticMesh* Mesh = ScatterMeshes[Kind];
		if (!Mesh)
		{
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* Instances = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		Instances->SetStaticMesh(Mesh);
		Instances->SetupAttachment(Root);
		const bool bSmall = Kind == static_cast<int32>(RiptideGen::EScatter::Bush);
		// Walk through bushes; trees, palms and boulders block.
		Instances->SetCollisionEnabled(bSmall ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		Instances->SetCullDistances(0, bSmall ? 15000 : 120000);
		Instances->RegisterComponent();

		TArray<FTransform> Transforms;
		for (const RiptideGen::FScatterPoint& Point : Points)
		{
			if (static_cast<int32>(Point.Kind) != Kind)
			{
				continue;
			}
			const FVector Local((Point.X - Terrain->OriginX) * 100.0, (Point.Y - Terrain->OriginY) * 100.0, Point.Z * 100.0);
			Transforms.Add(FTransform(FRotator(0.f, Point.YawDeg, 0.f), Local, FVector(Point.Scale)));
		}
		Instances->AddInstances(Transforms, false);
		Plants.Add(Instances);
	}

	UE_LOG(LogRiptideWorld, Log, TEXT("Island loaded: %s, %d plants"), *GetDescription(), static_cast<int32>(Points.size()));
	SetActorTickEnabled(true);
}

// --- Detail ---

int32 ARiptideIsland::DetailStepFor(int32 ChunkIndex, const TArray<FVector>& FocusPoints) const
{
	using namespace RiptideIslandDetail;

	const int32 Cx = ChunkIndex % ChunksPerSide;
	const int32 Cy = ChunkIndex / ChunksPerSide;
	const float SpanM = ChunkQuads * Terrain->Spacing;
	const FVector Origin = GetActorLocation();
	const FBox2D Bounds(
		FVector2D(Origin.X + Cx * SpanM * 100.0, Origin.Y + Cy * SpanM * 100.0),
		FVector2D(Origin.X + (Cx + 1) * SpanM * 100.0, Origin.Y + (Cy + 1) * SpanM * 100.0));

	double NearestM = UE_BIG_NUMBER;
	for (const FVector& Focus : FocusPoints)
	{
		NearestM = FMath::Min(NearestM, FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(FVector2D(Focus))) / 100.0);
	}

	float WantM = FarSpacingM;
	for (const FDetailBand& Band : DetailBands)
	{
		if (NearestM < Band.WithinM)
		{
			WantM = Band.SpacingM;
			break;
		}
	}

	// Deep chunks never need fine detail.
	const int32 X0 = Cx * ChunkQuads;
	const int32 Y0 = Cy * ChunkQuads;
	const int32 X1 = FMath::Min(X0 + ChunkQuads, Terrain->Size - 1);
	const int32 Y1 = FMath::Min(Y0 + ChunkQuads, Terrain->Size - 1);
	float Highest = -UE_BIG_NUMBER;
	for (int32 Y = Y0; Y <= Y1; Y += 4)
	{
		for (int32 X = X0; X <= X1; X += 4)
		{
			Highest = FMath::Max(Highest, Terrain->Height[Terrain->Index(X, Y)]);
		}
	}
	if (Highest < DeepBelowM)
	{
		WantM = FMath::Max(WantM, DeepSpacingM);
	}

	// Steps are powers of two, which divide the chunk evenly.
	int32 Step = 1;
	while (Step < 32 && (Step * 2) * Terrain->Spacing <= WantM)
	{
		Step *= 2;
	}
	return Step;
}

void ARiptideIsland::UpdateDetail(const TArray<FVector>& FocusPoints)
{
	if (!Terrain.IsValid())
	{
		return;
	}
	bool bAnyChange = false;
	for (int32 C = 0; C < WantedStep.Num(); ++C)
	{
		WantedStep[C] = DetailStepFor(C, FocusPoints);
		bAnyChange |= WantedStep[C] != BuiltStep[C];
	}
	if (bAnyChange)
	{
		SetActorTickEnabled(true);
	}
}

void ARiptideIsland::BuildAllChunksNow()
{
	for (int32 C = 0; C < Chunks.Num(); ++C)
	{
		if (BuiltStep[C] != WantedStep[C])
		{
			RebuildChunk(C);
		}
	}
}

// --- Mesh building ---

FVector ARiptideIsland::GridToLocal(int32 X, int32 Y) const
{
	return FVector(X * Terrain->Spacing * 100.f, Y * Terrain->Spacing * 100.f, Terrain->Height[Terrain->Index(X, Y)] * 100.f);
}

FVector ARiptideIsland::GridNormal(int32 X, int32 Y) const
{
	const RiptideGen::FTerrain& T = *Terrain;
	const int32 X0 = FMath::Max(0, X - 1);
	const int32 X1 = FMath::Min(T.Size - 1, X + 1);
	const int32 Y0 = FMath::Max(0, Y - 1);
	const int32 Y1 = FMath::Min(T.Size - 1, Y + 1);
	const float Dzdx = (T.Height[T.Index(X1, Y)] - T.Height[T.Index(X0, Y)]) / ((X1 - X0) * T.Spacing);
	const float Dzdy = (T.Height[T.Index(X, Y1)] - T.Height[T.Index(X, Y0)]) / ((Y1 - Y0) * T.Spacing);
	return FVector(-Dzdx, -Dzdy, 1.f).GetSafeNormal();
}

FLinearColor ARiptideIsland::GroundColour(int32 X, int32 Y) const
{
	using namespace RiptideIslandDetail;

	const RiptideGen::FTerrain& T = *Terrain;
	const int32 I = T.Index(X, Y);
	const float H = T.Height[I];
	const float Loose = H - T.Bedrock[I];
	const float Dug = T.OriginalHeight[I] - H;
	const RiptideGen::ESurface Surface = T.Surface[I];

	// Soft patches of lighter and darker ground so it doesn't read as one flat colour.
	const double Wx = X * static_cast<double>(T.Spacing);
	const double Wy = Y * static_cast<double>(T.Spacing);
	const float Patch = static_cast<float>(RiptideGen::Fbm(Wx / 7.0, Wy / 7.0, Site.Seed + 40U, 3));

	FLinearColor Colour;
	float Wetness = 0.f;
	if (Loose <= 0.02f && (Surface == RiptideGen::ESurface::Rock || Dug > 0.05f))
	{
		Colour = Rock;
	}
	else if (Surface == RiptideGen::ESurface::Soil)
	{
		// Fresh digging turns up bare dirt; untouched ground is jungle floor and grass.
		const float Grassy = FMath::Clamp(0.5f + Patch, 0.f, 1.f);
		Colour = Dug > 0.08f ? DugSoil : FMath::Lerp(JungleFloor, Grass, Grassy);
	}
	else
	{
		// Sand, or sand piled on rock. Damp near the sea and in fresh holes, soaked below the waterline.
		Wetness = FMath::Clamp(1.f - H / 0.6f, 0.f, 1.f);
		if (Dug > 0.15f)
		{
			Wetness = FMath::Max(Wetness, 0.35f);
		}
		Colour = H < -0.5f ? Seabed : FMath::Lerp(DrySand, WetSand, Wetness);
	}

	Colour *= 1.f + 0.12f * Patch;
	Colour.A = Wetness;
	return Colour;
}

void ARiptideIsland::RebuildChunk(int32 ChunkIndex)
{
	using namespace RiptideIslandDetail;

	UProceduralMeshComponent* Chunk = Chunks[ChunkIndex];
	const RiptideGen::FTerrain& T = *Terrain;
	const int32 Step = WantedStep[ChunkIndex];
	const int32 Cx = ChunkIndex % ChunksPerSide;
	const int32 Cy = ChunkIndex / ChunksPerSide;
	const int32 X0 = Cx * ChunkQuads;
	const int32 Y0 = Cy * ChunkQuads;
	const int32 X1 = FMath::Min(X0 + ChunkQuads, T.Size - 1);
	const int32 Y1 = FMath::Min(Y0 + ChunkQuads, T.Size - 1);

	TArray<int32> Xs;
	TArray<int32> Ys;
	for (int32 X = X0; X < X1; X += Step)
	{
		Xs.Add(X);
	}
	Xs.Add(X1);
	for (int32 Y = Y0; Y < Y1; Y += Step)
	{
		Ys.Add(Y);
	}
	Ys.Add(Y1);
	const int32 Nx = Xs.Num();
	const int32 Ny = Ys.Num();

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colours;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	const int32 Expected = Nx * Ny + 2 * (Nx + Ny);
	Vertices.Reserve(Expected);
	Normals.Reserve(Expected);
	UVs.Reserve(Expected);
	Colours.Reserve(Expected);
	Tangents.Reserve(Expected);
	Triangles.Reserve((Nx - 1) * (Ny - 1) * 6 + 4 * (Nx + Ny) * 6);

	const auto AddVertex = [&](int32 X, int32 Y, float Drop)
	{
		const FVector Normal = GridNormal(X, Y);
		Vertices.Add(GridToLocal(X, Y) - FVector(0.f, 0.f, Drop));
		Normals.Add(Normal);
		// Texture coordinates repeat every 4 m.
		UVs.Add(FVector2D(X * T.Spacing / 4.f, Y * T.Spacing / 4.f));
		Colours.Add(GroundColour(X, Y));
		Tangents.Add(FProcMeshTangent((FVector::XAxisVector - Normal * Normal.X).GetSafeNormal(), false));
		return Vertices.Num() - 1;
	};

	for (int32 J = 0; J < Ny; ++J)
	{
		for (int32 I = 0; I < Nx; ++I)
		{
			AddVertex(Xs[I], Ys[J], 0.f);
		}
	}
	for (int32 J = 0; J + 1 < Ny; ++J)
	{
		for (int32 I = 0; I + 1 < Nx; ++I)
		{
			const int32 V00 = J * Nx + I;
			const int32 V10 = V00 + 1;
			const int32 V01 = V00 + Nx;
			const int32 V11 = V01 + 1;
			// Wound so the faces point up (Unreal's front faces).
			Triangles.Append({ V00, V10, V01, V10, V11, V01 });
		}
	}

	// Skirts: each edge hangs down a little, hiding any gap against a neighbour built at a different detail.
	const float SkirtDropCm = (Step * T.Spacing * 50.f) + 50.f;
	const auto AddSkirt = [&](const TArray<int32>& EdgeTop, const TArray<FIntPoint>& EdgeGrid)
	{
		TArray<int32> Low;
		for (const FIntPoint& P : EdgeGrid)
		{
			Low.Add(AddVertex(P.X, P.Y, SkirtDropCm));
		}
		for (int32 K = 0; K + 1 < EdgeTop.Num(); ++K)
		{
			const int32 A = EdgeTop[K];
			const int32 B = EdgeTop[K + 1];
			const int32 La = Low[K];
			const int32 Lb = Low[K + 1];
			// Both windings, so the skirt shows from either side.
			Triangles.Append({ A, B, La, B, Lb, La, A, La, B, B, La, Lb });
		}
	};
	{
		TArray<int32> Top;
		TArray<FIntPoint> Grid;
		for (int32 I = 0; I < Nx; ++I)
		{
			Top.Add(I);
			Grid.Add(FIntPoint(Xs[I], Ys[0]));
		}
		AddSkirt(Top, Grid);
		Top.Reset();
		Grid.Reset();
		for (int32 I = 0; I < Nx; ++I)
		{
			Top.Add((Ny - 1) * Nx + I);
			Grid.Add(FIntPoint(Xs[I], Ys[Ny - 1]));
		}
		AddSkirt(Top, Grid);
		Top.Reset();
		Grid.Reset();
		for (int32 J = 0; J < Ny; ++J)
		{
			Top.Add(J * Nx);
			Grid.Add(FIntPoint(Xs[0], Ys[J]));
		}
		AddSkirt(Top, Grid);
		Top.Reset();
		Grid.Reset();
		for (int32 J = 0; J < Ny; ++J)
		{
			Top.Add(J * Nx + Nx - 1);
			Grid.Add(FIntPoint(Xs[Nx - 1], Ys[J]));
		}
		AddSkirt(Top, Grid);
	}

	const bool bCollision = Step * T.Spacing <= MaxCollisionSpacingM;
	Chunk->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colours, Tangents, bCollision);
	BuiltStep[ChunkIndex] = Step;
}

void ARiptideIsland::MarkEdited(const RiptideGen::FGridRect& Rect)
{
	// Vertices on a chunk's edge belong to both chunks, and normals reach one vertex further.
	const int32 MinCx = FMath::Clamp((Rect.MinX - 1) / ChunkQuads, 0, ChunksPerSide - 1);
	const int32 MaxCx = FMath::Clamp((Rect.MaxX + 1) / ChunkQuads, 0, ChunksPerSide - 1);
	const int32 MinCy = FMath::Clamp((Rect.MinY - 1) / ChunkQuads, 0, ChunksPerSide - 1);
	const int32 MaxCy = FMath::Clamp((Rect.MaxY + 1) / ChunkQuads, 0, ChunksPerSide - 1);
	for (int32 Cy = MinCy; Cy <= MaxCy; ++Cy)
	{
		for (int32 Cx = MinCx; Cx <= MaxCx; ++Cx)
		{
			EditedChunks.Add(Cy * ChunksPerSide + Cx);
		}
	}
	SetActorTickEnabled(true);
}

void ARiptideIsland::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Terrain.IsValid())
	{
		SetActorTickEnabled(false);
		return;
	}

	if (Terrain->IsSettling())
	{
		SettleClock += DeltaSeconds;
		const int32 Passes = FMath::Min(FMath::FloorToInt32(SettleClock * SettlePassesPerSecond), 4);
		if (Passes > 0)
		{
			SettleClock -= Passes / SettlePassesPerSecond;
			Terrain->Settle(Passes);
		}
	}
	else
	{
		SettleClock = 0.f;
	}

	RiptideGen::FGridRect Changed;
	if (Terrain->TakeChanges(Changed))
	{
		MarkEdited(Changed);
	}

	// Dug ground rebuilds straight away so the shovel feels immediate.
	for (const int32 C : EditedChunks)
	{
		RebuildChunk(C);
	}
	EditedChunks.Reset();

	// Detail changes are spread over frames.
	int32 Budget = DetailRebuildsPerFrame;
	bool bMoreToBuild = false;
	for (int32 C = 0; C < Chunks.Num(); ++C)
	{
		if (BuiltStep[C] == WantedStep[C])
		{
			continue;
		}
		if (Budget > 0)
		{
			RebuildChunk(C);
			--Budget;
		}
		else
		{
			bMoreToBuild = true;
			break;
		}
	}

	if (!bMoreToBuild && !Terrain->IsSettling())
	{
		SetActorTickEnabled(false);
	}
}

// --- Digging ---

float ARiptideIsland::Dig(FVector WorldLocation, float RadiusCm, float DepthCm)
{
	if (!Terrain.IsValid())
	{
		return 0.f;
	}
	const float CubicMetres = Terrain->Dig(WorldLocation.X / 100.0, WorldLocation.Y / 100.0, RadiusCm / 100.f, DepthCm / 100.f);
	if (CubicMetres > 0.f)
	{
		SetActorTickEnabled(true);
	}
	return CubicMetres * 1000.f;
}

float ARiptideIsland::Pile(FVector WorldLocation, float Litres, float RadiusCm)
{
	if (!Terrain.IsValid() || !Covers(WorldLocation))
	{
		return 0.f;
	}
	const float CubicMetres = Terrain->Deposit(WorldLocation.X / 100.0, WorldLocation.Y / 100.0, RadiusCm / 100.f, Litres / 1000.f);
	if (CubicMetres > 0.f)
	{
		SetActorTickEnabled(true);
	}
	return CubicMetres * 1000.f;
}

float ARiptideIsland::GetGroundHeight(FVector WorldLocation) const
{
	return Terrain.IsValid() ? Terrain->SampleHeight(WorldLocation.X / 100.0, WorldLocation.Y / 100.0) * 100.f : -UE_BIG_NUMBER;
}

float ARiptideIsland::GetDiggableDepth(FVector WorldLocation) const
{
	if (!Terrain.IsValid() || !Covers(WorldLocation))
	{
		return 0.f;
	}
	const RiptideGen::FTerrain& T = *Terrain;
	const int32 X = FMath::Clamp(FMath::RoundToInt32((WorldLocation.X / 100.0 - T.OriginX) / T.Spacing), 0, T.Size - 1);
	const int32 Y = FMath::Clamp(FMath::RoundToInt32((WorldLocation.Y / 100.0 - T.OriginY) / T.Spacing), 0, T.Size - 1);
	const int32 I = T.Index(X, Y);
	return FMath::Max(0.f, T.Height[I] - T.Bedrock[I]) * 100.f;
}

bool ARiptideIsland::IsSettling() const
{
	return Terrain.IsValid() && Terrain->IsSettling();
}

bool ARiptideIsland::IsFullyBuilt() const
{
	for (int32 C = 0; C < BuiltStep.Num(); ++C)
	{
		if (BuiltStep[C] != WantedStep[C])
		{
			return false;
		}
	}
	return BuiltStep.Num() > 0;
}

double ARiptideIsland::GetTerrainVolumeLitres() const
{
	return Terrain.IsValid() ? Terrain->TotalVolume() * 1000.0 : 0.0;
}

FString ARiptideIsland::GetDescription() const
{
	return FString::Printf(TEXT("%s island (%s), %.0f m across, at %.1f km %.1f km"), ANSI_TO_TCHAR(RiptideGen::RoleName(Site.Role)),
		ANSI_TO_TCHAR(RiptideGen::KindName(Site.Kind)), Site.Radius * 2.f, Site.X / 1000.0, Site.Y / 1000.0);
}

bool ARiptideIsland::Covers(const FVector& WorldLocation) const
{
	if (!Terrain.IsValid())
	{
		return false;
	}
	const double SpanM = (Terrain->Size - 1) * static_cast<double>(Terrain->Spacing);
	const double Mx = WorldLocation.X / 100.0 - Terrain->OriginX;
	const double My = WorldLocation.Y / 100.0 - Terrain->OriginY;
	return Mx >= 0.0 && My >= 0.0 && Mx <= SpanM && My <= SpanM;
}

// --- Console commands, for trying digging before the on-foot character and shovel exist ---

namespace RiptideIslandCommands
{
	/** The island ground the local player is looking at, within 60 m. */
	ARiptideIsland* LookedAtIsland(UWorld* World, FVector& OutPoint)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return nullptr;
		}
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(RiptideDigTrace), true, PC->GetPawn());
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 6000.f, ECC_Visibility, Params))
		{
			return nullptr;
		}
		OutPoint = Hit.ImpactPoint;
		return Cast<ARiptideIsland>(Hit.GetActor());
	}

	void Dig(const TArray<FString>& Args, UWorld* World)
	{
		FVector Point;
		ARiptideIsland* Island = LookedAtIsland(World, Point);
		if (!Island)
		{
			UE_LOG(LogRiptideWorld, Warning, TEXT("Riptide.Dig: look at an island's ground within 60 m first"));
			return;
		}
		const int32 Scoops = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 10;
		float Litres = 0.f;
		for (int32 K = 0; K < Scoops; ++K)
		{
			Litres += Island->Dig(Point);
		}
		UE_LOG(LogRiptideWorld, Log, TEXT("Riptide.Dig: %d scoops, %.0f litres, %.0f cm more could be dug here"), Scoops, Litres,
			Island->GetDiggableDepth(Point));
	}

	void Pile(const TArray<FString>& Args, UWorld* World)
	{
		FVector Point;
		ARiptideIsland* Island = LookedAtIsland(World, Point);
		if (!Island)
		{
			UE_LOG(LogRiptideWorld, Warning, TEXT("Riptide.Pile: look at an island's ground within 60 m first"));
			return;
		}
		const float Litres = Args.Num() > 0 ? FMath::Max(1.f, FCString::Atof(*Args[0])) : 500.f;
		Island->Pile(Point, Litres);
		UE_LOG(LogRiptideWorld, Log, TEXT("Riptide.Pile: dumped %.0f litres"), Litres);
	}

	FAutoConsoleCommandWithWorldAndArgs DigCommand(
		TEXT("Riptide.Dig"),
		TEXT("Digs where you're looking. Optional: number of shovel scoops (default 10)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dig));

	FAutoConsoleCommandWithWorldAndArgs PileCommand(
		TEXT("Riptide.Pile"),
		TEXT("Dumps sand where you're looking. Optional: litres (default 500)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Pile));
}
