#include "RiptideSea.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "WaterBodyTypes.h"
#include "WaterTerrainComponent.h"

// --- Island ground ---------------------------------------------------------------------------------------------

ARiptideIslandGround::ARiptideIslandGround()
{
	WaterTerrain = CreateDefaultSubobject<UWaterTerrainComponent>(TEXT("WaterTerrain"));
}

void ARiptideIslandGround::BeginPlay()
{
	Super::BeginPlay();
	if (URiptideSeaSubsystem* Sea = GetWorld()->GetSubsystem<URiptideSeaSubsystem>())
	{
		Sea->AddGround(GetStaticMeshComponent());
	}
}

void ARiptideIslandGround::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URiptideSeaSubsystem* Sea = GetWorld() ? GetWorld()->GetSubsystem<URiptideSeaSubsystem>() : nullptr)
	{
		Sea->RemoveGround(GetStaticMeshComponent());
	}
	Super::EndPlay(EndPlayReason);
}

// --- Where the seabed is -----------------------------------------------------------------------------------------

void URiptideSeaSubsystem::AddGround(UStaticMeshComponent* Ground)
{
	if (!Ground)
	{
		return;
	}
	const FBox Box = Ground->Bounds.GetBox();
	// A hair inside its bounds, so a square whose edge lies exactly on a cell line isn't listed in the next cell too.
	const FIntPoint Min(FMath::FloorToInt32((Box.Min.X + 1.0) / CellSize), FMath::FloorToInt32((Box.Min.Y + 1.0) / CellSize));
	const FIntPoint Max(FMath::FloorToInt32((Box.Max.X - 1.0) / CellSize), FMath::FloorToInt32((Box.Max.Y - 1.0) / CellSize));
	for (int32 X = Min.X; X <= Max.X; ++X)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			Grounds.Add(FIntPoint(X, Y), Ground);
		}
	}
}

void URiptideSeaSubsystem::RemoveGround(UStaticMeshComponent* Ground)
{
	for (auto It = Grounds.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid() || It.Value().Get() == Ground)
		{
			It.RemoveCurrent();
		}
	}
}

bool URiptideSeaSubsystem::GetGroundZ(const FVector& Location, float& OutZ) const
{
	if (Grounds.IsEmpty())
	{
		return false;
	}
	const FIntPoint Cell(FMath::FloorToInt32(Location.X / CellSize), FMath::FloorToInt32(Location.Y / CellSize));
	bool bFound = false;
	for (auto It = Grounds.CreateConstKeyIterator(Cell); It; ++It)
	{
		UStaticMeshComponent* Ground = It.Value().Get();
		if (!Ground)
		{
			continue;
		}
		const FBox Box = Ground->Bounds.GetBox();
		if (Location.X < Box.Min.X || Location.X > Box.Max.X || Location.Y < Box.Min.Y || Location.Y > Box.Max.Y)
		{
			continue;
		}
		// Straight down through this square alone: nothing else (a hull, a crew member) can be mistaken for the seabed.
		FHitResult Hit;
		const FVector Top(Location.X, Location.Y, Box.Max.Z + 10.0);
		const FVector Bottom(Location.X, Location.Y, Box.Min.Z - 10.0);
		if (Ground->LineTraceComponent(Hit, Top, Bottom, FCollisionQueryParams(NAME_None, /*bTraceComplex=*/ true)))
		{
			if (!bFound || Hit.ImpactPoint.Z > OutZ)
			{
				OutZ = Hit.ImpactPoint.Z;
				bFound = true;
			}
		}
	}
	return bFound;
}

float URiptideSeaSubsystem::GetGroundHeight(FVector Location) const
{
	float Z = 0.f;
	return GetGroundZ(Location, Z) ? Z : -1.e6f;
}

float URiptideSeaSubsystem::GroundHeightAt(const UObject* WorldContextObject, FVector Location)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const URiptideSeaSubsystem* Sea = World ? World->GetSubsystem<URiptideSeaSubsystem>() : nullptr;
	return Sea ? Sea->GetGroundHeight(Location) : -1.e6f;
}

float URiptideSeaSubsystem::WaveScaleAt(const UObject* WorldContextObject, FVector Location)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AWaterBodyOcean* Ocean = World ? Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(World, AWaterBodyOcean::StaticClass())) : nullptr;
	const UWaterBodyComponent* Body = Ocean ? Ocean->GetWaterBodyComponent() : nullptr;
	if (!Body || !Body->HasWaves())
	{
		return 1.f;
	}
	const auto Query = Body->TryQueryWaterInfoClosestToWorldLocation(Location, EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
	return Query.HasValue() ? Query.GetValue().GetWaveInfo().AttenuationFactor : 1.f;
}

// --- The ocean -----------------------------------------------------------------------------------------------------

TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> URiptideOceanComponent::TryQueryWaterInfoClosestToWorldLocation(
	const FVector& InWorldLocation, EWaterBodyQueryFlags InQueryFlags, const TOptional<float>& InSplineInputKey) const
{
	const bool bWaves = EnumHasAnyFlags(InQueryFlags, EWaterBodyQueryFlags::IncludeWaves) && HasWaves();
	const URiptideSeaSubsystem* Sea = GetWorld() ? GetWorld()->GetSubsystem<URiptideSeaSubsystem>() : nullptr;
	if (!bWaves || !Sea)
	{
		return Super::TryQueryWaterInfoClosestToWorldLocation(InWorldLocation, InQueryFlags, InSplineInputKey);
	}

	// The flat sea first, from the plugin (location, normal, velocity, exclusion volumes), then our own depth and waves.
	const EWaterBodyQueryFlags WaveFlags = EWaterBodyQueryFlags::IncludeWaves | EWaterBodyQueryFlags::SimpleWaves | EWaterBodyQueryFlags::ComputeDepth;
	const EWaterBodyQueryFlags FlatFlags = (InQueryFlags & ~WaveFlags) | EWaterBodyQueryFlags::ComputeLocation;
	TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Flat = Super::TryQueryWaterInfoClosestToWorldLocation(InWorldLocation, FlatFlags, InSplineInputKey);
	if (Flat.HasError())
	{
		return Flat;
	}
	FWaterBodyQueryResult Result = Flat.GetValue();
	Result.SetQueryFlags(Result.GetQueryFlags() | EWaterBodyQueryFlags::IncludeWaves | EWaterBodyQueryFlags::ComputeDepth
		| (InQueryFlags & EWaterBodyQueryFlags::SimpleWaves));

	const FVector Plane = Result.GetWaterPlaneLocation();
	float GroundZ = 0.f;
	float Depth = OpenSeaDepth;
	FWaveInfo WaveInfo;
	if (Sea->GetGroundZ(Plane, GroundZ))
	{
		Depth = Plane.Z - GroundZ;
		if (Depth < 0.f)
		{
			// Under the land: a flat sea, as the plugin does under a landscape.
			WaveInfo.AttenuationFactor = 0.f;
			Depth = 0.f;
		}
	}
	Result.SetWaterPlaneDepth(Depth);
	Result.SetWaterSurfaceDepth(Depth);

	if (!Result.IsInExclusionVolume())
	{
		if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeNormal))
		{
			WaveInfo.Normal = Result.GetWaterPlaneNormal();
		}
		GetWaveInfoAtPosition(Plane, Depth, EnumHasAnyFlags(InQueryFlags, EWaterBodyQueryFlags::SimpleWaves), WaveInfo);
	}
	Result.SetWaveInfo(WaveInfo);

	FVector Surface = Result.GetWaterSurfaceLocation();
	Surface.Z += WaveInfo.Height;
	Result.SetWaterSurfaceLocation(Surface);
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeNormal))
	{
		Result.SetWaterSurfaceNormal(WaveInfo.Normal);
	}
	Result.SetWaterSurfaceDepth(Depth + WaveInfo.Height);
	if (EnumHasAnyFlags(Result.GetQueryFlags(), EWaterBodyQueryFlags::ComputeImmersionDepth))
	{
		Result.SetImmersionDepth(Result.IsInExclusionVolume() ? 0.f : Surface.Z - InWorldLocation.Z);
	}
	return MakeValue(Result);
}

ARiptideOcean::ARiptideOcean(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyOceanComponentClass = URiptideOceanComponent::StaticClass();
}

// --- Island props ------------------------------------------------------------------------------------------------

ARiptideIslandProps::ARiptideIslandProps()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

void ARiptideIslandProps::AddProp(UStaticMesh* Mesh, const FTransform& WorldTransform, bool bSolid)
{
	if (!Mesh)
	{
		return;
	}
	const FName Profile = bSolid ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision"));
	UHierarchicalInstancedStaticMeshComponent* Batch = nullptr;
	for (UHierarchicalInstancedStaticMeshComponent* Existing : Batches)
	{
		if (Existing && Existing->GetStaticMesh() == Mesh && Existing->GetCollisionProfileName() == Profile)
		{
			Batch = Existing;
			break;
		}
	}
	if (!Batch)
	{
		Batch = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transactional);
		Batch->SetMobility(EComponentMobility::Static);
		Batch->SetStaticMesh(Mesh);
		Batch->SetCollisionProfileName(Profile);
		// Small things (ferns, twigs) cast no shadow: hundreds of little shadow casters cost more than they show.
		Batch->SetCastShadow(Mesh->GetBounds().SphereRadius > 75.f);
		Batch->SetupAttachment(Root);
		AddInstanceComponent(Batch);
		Batch->RegisterComponent();
		Batches.Add(Batch);
	}
	Batch->AddInstance(WorldTransform, /*bWorldSpace=*/ true);
}

int32 ARiptideIslandProps::GetPropCount() const
{
	int32 Count = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* Batch : Batches)
	{
		Count += Batch ? Batch->GetInstanceCount() : 0;
	}
	return Count;
}
