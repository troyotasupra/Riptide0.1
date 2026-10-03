#include "RiptideSea.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Data/RiptideResources.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.h"
#include "RiptideWorldItem.h"
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

float URiptideSeaSubsystem::SeaSurfaceAt(const UObject* WorldContextObject, FVector Location)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AWaterBodyOcean* Ocean = World ? Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(World, AWaterBodyOcean::StaticClass())) : nullptr;
	const UWaterBodyComponent* Body = Ocean ? Ocean->GetWaterBodyComponent() : nullptr;
	if (!Body)
	{
		return 0.f;
	}
	const auto Query = Body->TryQueryWaterInfoClosestToWorldLocation(FVector(Location.X, Location.Y, 0.f),
		EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
	return Query.HasValue() ? Query.GetValue().GetWaterSurfaceLocation().Z : 0.f;
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
	// Placed in the level, so every machine has the same batches; only what's been harvested travels.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);
	PrimaryActorTick.bCanEverTick = true;
}

void ARiptideIslandProps::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideIslandProps, Depleted);
}

void ARiptideIslandProps::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Depleted();
}

void ARiptideIslandProps::AddProp(UStaticMesh* Mesh, const FTransform& WorldTransform, bool bSolid, FName Kind)
{
	if (!Mesh)
	{
		return;
	}
	// Harvestable things that aren't solid (ferns, branches) still need to be looked at: they answer the eye's
	// trace and nothing else (the RiptideHarvest profile in DefaultEngine.ini).
	const bool bHarvestable = !RiptideResources::KindOfProp(Kind).IsNone();
	const FName Profile = bSolid ? FName(TEXT("BlockAll")) : bHarvestable ? FName(TEXT("RiptideHarvest")) : FName(TEXT("NoCollision"));
	UHierarchicalInstancedStaticMeshComponent* Batch = nullptr;
	for (int32 i = 0; i < Batches.Num(); ++i)
	{
		UHierarchicalInstancedStaticMeshComponent* Existing = Batches[i];
		if (Existing && Existing->GetStaticMesh() == Mesh && Existing->GetCollisionProfileName() == Profile
			&& BatchKinds.IsValidIndex(i) && BatchKinds[i] == Kind)
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
		// Their shadows are drawn once and kept: a palm's fronds sway in the wind through its material, and without
		// this every swaying frond would have the shadow maps redrawn under it every frame.
		Batch->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Static;
		Batch->SetupAttachment(Root);
		AddInstanceComponent(Batch);
		Batch->RegisterComponent();
		Batches.Add(Batch);
		BatchKinds.SetNum(Batches.Num());
		BatchKinds[Batches.Num() - 1] = Kind;
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

// --- Harvesting ---

bool ARiptideIslandProps::FindHit(const FHitResult& Hit, int32& OutBatch, int32& OutInstance) const
{
	const UPrimitiveComponent* Component = Hit.GetComponent();
	for (int32 i = 0; i < Batches.Num(); ++i)
	{
		if (Batches[i] && Batches[i] == Component && Hit.Item >= 0 && Hit.Item < Batches[i]->GetInstanceCount())
		{
			OutBatch = i;
			OutInstance = Hit.Item;
			return true;
		}
	}
	return false;
}

bool ARiptideIslandProps::IsDepleted(int32 Batch, int32 Instance) const
{
	return Depleted.ContainsByPredicate([&](const FRiptideDepletedProp& D) { return D.Batch == Batch && D.Instance == Instance; });
}

void ARiptideIslandProps::ShowInstance(int32 Batch, int32 Instance, bool bShow)
{
	UHierarchicalInstancedStaticMeshComponent* Component = Batches.IsValidIndex(Batch) ? Batches[Batch].Get() : nullptr;
	if (!Component || Instance < 0 || Instance >= Component->GetInstanceCount())
	{
		return;
	}
	const uint64 Key = (uint64(uint32(Batch)) << 32) | uint32(Instance);
	FTransform Current;
	Component->GetInstanceTransform(Instance, Current, true);
	if (!bShow && !Sunk.Contains(Key))
	{
		// Twenty metres under the ground, out of sight and out of reach, keeping its place in the batch.
		Sunk.Add(Key, Current);
		Current.AddToTranslation(FVector(0.f, 0.f, -2000.f));
		Component->UpdateInstanceTransform(Instance, Current, true, true, true);
	}
	else if (bShow)
	{
		if (const FTransform* Was = Sunk.Find(Key))
		{
			Component->UpdateInstanceTransform(Instance, *Was, true, true, true);
			Sunk.Remove(Key);
		}
	}
}

void ARiptideIslandProps::OnRep_Depleted()
{
	// Whatever was shown as taken and no longer is comes back; whatever is newly taken goes.
	for (const FRiptideDepletedProp& Was : ShownDepleted)
	{
		if (!IsDepleted(Was.Batch, Was.Instance))
		{
			ShowInstance(Was.Batch, Was.Instance, true);
		}
	}
	for (const FRiptideDepletedProp& Now : Depleted)
	{
		const FRiptideResourceDef* Def = BatchKinds.IsValidIndex(Now.Batch) ? RiptideResources::Find(RiptideResources::KindOfProp(BatchKinds[Now.Batch])) : nullptr;
		if (Def && Def->bHideWhenDepleted)
		{
			ShowInstance(Now.Batch, Now.Instance, false);
		}
	}
	ShownDepleted = Depleted;
}

void ARiptideIslandProps::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || Depleted.Num() == 0)
	{
		return;
	}
	RespawnCheck += DeltaSeconds;
	if (RespawnCheck < 1.f)
	{
		return;
	}
	RespawnCheck = 0.f;
	const AGameStateBase* State = GetWorld()->GetGameState();
	const float Now = State ? float(State->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
	const int32 Before = Depleted.Num();
	Depleted.RemoveAll([Now](const FRiptideDepletedProp& D) { return Now >= D.RespawnAt; });
	if (Depleted.Num() != Before)
	{
		OnRep_Depleted();
	}
}

bool ARiptideIslandProps::FindNearest(FName ResourceKind, const FVector& Near, float Reach, FVector& OutLocation) const
{
	float Best = Reach;
	bool bFound = false;
	for (int32 i = 0; i < Batches.Num(); ++i)
	{
		if (!Batches[i] || !BatchKinds.IsValidIndex(i) || RiptideResources::KindOfProp(BatchKinds[i]) != ResourceKind)
		{
			continue;
		}
		for (int32 Instance = 0; Instance < Batches[i]->GetInstanceCount(); ++Instance)
		{
			if (IsDepleted(i, Instance))
			{
				continue;
			}
			FTransform T;
			Batches[i]->GetInstanceTransform(Instance, T, true);
			// Where the thing itself is: a scanned branch can lie half a metre from its pivot. A tall thing (a palm,
			// a tree) is found by its foot, where its trunk stands.
			const FBoxSphereBounds& Bounds = Batches[i]->GetStaticMesh()->GetBounds();
			const bool bTall = Bounds.BoxExtent.Z > FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
			const FVector Centre = bTall ? T.GetLocation() : T.TransformPosition(Bounds.Origin);
			const float Dist = FVector::Dist(Centre, Near);
			if (Dist < Best)
			{
				Best = Dist;
				OutLocation = Centre;
				bFound = true;
			}
		}
	}
	return bFound;
}

bool ARiptideIslandProps::GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const
{
	int32 Batch, Instance;
	if (!FindHit(Hit, Batch, Instance) || !BatchKinds.IsValidIndex(Batch))
	{
		return false;
	}
	const FRiptideResourceDef* Def = RiptideResources::Find(RiptideResources::KindOfProp(BatchKinds[Batch]));
	if (!Def)
	{
		return false;
	}
	Out.Item = Instance;
	if (IsDepleted(Batch, Instance))
	{
		Out.Prompt = Def->DepletedLabel;
		Out.WhyNot = Def->DepletedLabel;
		Out.bEnabled = false;
		return true;
	}
	TArray<FName> Tools;
	if (const URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr)
	{
		for (int32 Grid = 0; Grid < Carrying->Num(); ++Grid)
		{
			if (const FRiptideStorage* Storage = Carrying->GetStorage(Grid))
			{
				Tools.Append(Storage->Grid.ToolTypes());
			}
		}
	}
	Out.Prompt = Def->Label;
	Out.HoldSeconds = RiptideResources::HarvestSeconds(*Def, Tools);
	if (!Def->RequiresTool.IsNone() && !Tools.Contains(Def->RequiresTool))
	{
		Out.bEnabled = false;
		Out.WhyNot = FText::Format(NSLOCTEXT("RiptideHarvest", "NeedsTool", "{0}: needs a {1}"), Def->Label, FText::FromName(Def->RequiresTool));
		Out.Prompt = Out.WhyNot;
	}
	return true;
}

void ARiptideIslandProps::Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb)
{
	int32 Batch, Instance;
	if (!HasAuthority() || !Who || !FindHit(Hit, Batch, Instance) || !BatchKinds.IsValidIndex(Batch) || IsDepleted(Batch, Instance))
	{
		return;
	}
	const FRiptideResourceDef* Def = RiptideResources::Find(RiptideResources::KindOfProp(BatchKinds[Batch]));
	if (!Def)
	{
		return;
	}
	// The same thing gives the same amounts to everyone, each time round (so tests can count on it).
	FRandomStream Rng(HashCombine(HashCombine(GetTypeHash(Batch), GetTypeHash(Instance)), Depleted.Num()));
	FTransform Where;
	Batches[Batch]->GetInstanceTransform(Instance, Where, true);
	for (const FRiptideYield& Yield : Def->Yields)
	{
		const int32 Count = Rng.RandRange(Yield.Min, Yield.Max);
		if (Count > 0)
		{
			const int32 Left = Who->GiveItem(Yield.Item, Count);
			if (Left > 0)
			{
				ARiptideWorldItem::Drop(GetWorld(), FRiptideItemGrid::NewStack(Yield.Item, Left), Where.GetLocation() + FVector(0.f, 0.f, 60.f));
			}
		}
	}
	const AGameStateBase* State = GetWorld()->GetGameState();
	const float Now = State ? float(State->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
	FRiptideDepletedProp Taken;
	Taken.Batch = Batch;
	Taken.Instance = Instance;
	Taken.RespawnAt = Now + Def->RespawnSeconds;
	Depleted.Add(Taken);
	OnRep_Depleted();
	Who->StartAction(Def->RequiresTool == TEXT("hatchet") ? ERiptideCrewAction::Chop : ERiptideCrewAction::Harvest);
}
