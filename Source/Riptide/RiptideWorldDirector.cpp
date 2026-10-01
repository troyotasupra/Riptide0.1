#include "RiptideWorldDirector.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "RiptideIsland.h"

namespace RiptideDirectorDetail
{
	/** How often the director checks which islands should be loaded, in seconds. */
	constexpr float StreamInterval = 0.5f;

	/** At the start of play, islands this close to the spawn are finished before the first frame. */
	constexpr double StartupWaitRadiusM = 1500.0;
}

ARiptideWorldDirector::ARiptideWorldDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	// Every machine runs its own director and builds the same islands from the seed.
	bReplicates = false;
	TerrainMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Riptide/Materials/M_IslandTerrain.M_IslandTerrain")));
}

const RiptideGen::FWorld& ARiptideWorldDirector::GetGenerator() const
{
	if (!Generator.IsValid() || Generator->GetSeed() != static_cast<uint32>(WorldSeed))
	{
		Generator = MakeUnique<RiptideGen::FWorld>(static_cast<uint32>(WorldSeed));
	}
	return *Generator;
}

void ARiptideWorldDirector::BeginPlay()
{
	Super::BeginPlay();

	LoadedMaterial = TerrainMaterial.LoadSynchronous();
	if (!LoadedMaterial)
	{
		UE_LOG(LogRiptideWorld, Warning, TEXT("Island material %s not found; islands use the default material"),
			*TerrainMaterial.ToString());
	}

	const RiptideGen::FWorld& Gen = GetGenerator();
	UE_LOG(LogRiptideWorld, Log, TEXT("World seed %d: %d islands in the home chain and cordon"), WorldSeed,
		static_cast<int32>(Gen.GetFixedSites().size()));

	// Don't let the crew arrive to an empty sea: the islands around the spawn are finished before play starts.
	UpdateStreaming();
	const FVector Spawn = GetStartSpawnTransform().GetLocation();
	for (FRiptidePendingIsland& Island : Pending)
	{
		const double DistM = FVector2D::Distance(FVector2D(Spawn) / 100.0, FVector2D(Island.Site.X, Island.Site.Y)) - Island.Site.Radius;
		if (DistM < RiptideDirectorDetail::StartupWaitRadiusM)
		{
			Island.Terrain.Wait();
		}
	}
	UpdateStreaming();
	if (ARiptideIsland* Start = GetStartIsland())
	{
		Start->BuildAllChunksNow();
	}
}

void ARiptideWorldDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	StreamClock += DeltaSeconds;
	if (StreamClock >= RiptideDirectorDetail::StreamInterval)
	{
		StreamClock = 0.f;
		UpdateStreaming();
	}
}

void ARiptideWorldDirector::GatherFocusPoints(TArray<FVector>& Out) const
{
	UWorld* World = GetWorld();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Out.Add(It->GetActorLocation());
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->PlayerCameraManager)
		{
			Out.Add(PC->PlayerCameraManager->GetCameraLocation());
		}
	}
	if (Out.IsEmpty())
	{
		// Nobody has spawned yet: load around where they will.
		Out.Add(GetStartSpawnTransform().GetLocation());
	}
}

void ARiptideWorldDirector::UpdateStreaming()
{
	TArray<FVector> Focus;
	GatherFocusPoints(Focus);
	const RiptideGen::FWorld& Gen = GetGenerator();

	// Islands finished on worker threads go into the world.
	for (int32 K = Pending.Num() - 1; K >= 0; --K)
	{
		if (Pending[K].Terrain.IsReady())
		{
			const TSharedPtr<RiptideGen::FTerrain> Terrain = Pending[K].Terrain.Get();
			const RiptideGen::FIslandSite Site = Pending[K].Site;
			Pending.RemoveAtSwap(K);
			SpawnIsland(Site, Terrain, Focus);
		}
	}

	// Start generating islands that have come into range.
	for (const FVector& Point : Focus)
	{
		for (const RiptideGen::FIslandSite& Site : Gen.SitesNear(Point.X / 100.0, Point.Y / 100.0, LoadRadiusM))
		{
			if (Loaded.Contains(Site.Id) || Pending.ContainsByPredicate([&Site](const FRiptidePendingIsland& P) { return P.Site.Id == Site.Id; }))
			{
				continue;
			}
			if (const TSharedPtr<RiptideGen::FTerrain>* Kept = Remembered.Find(Site.Id))
			{
				SpawnIsland(Site, *Kept, Focus);
				continue;
			}
			FRiptidePendingIsland Request;
			Request.Site = Site;
			Request.Terrain = Async(EAsyncExecution::ThreadPool, [Site]()
			{
				return TSharedPtr<RiptideGen::FTerrain>(MakeShared<RiptideGen::FTerrain>(RiptideGen::BuildIslandTerrain(Site)));
			});
			Pending.Add(MoveTemp(Request));
		}
	}

	// Unload islands everyone has left behind; keep the ground of any that were dug.
	for (auto It = Loaded.CreateIterator(); It; ++It)
	{
		ARiptideIsland* Island = It.Value().Get();
		if (!Island)
		{
			It.RemoveCurrent();
			continue;
		}
		const RiptideGen::FIslandSite& Site = Island->GetSite();
		double NearestM = UE_BIG_NUMBER;
		for (const FVector& Point : Focus)
		{
			NearestM = FMath::Min(NearestM, FVector2D::Distance(FVector2D(Point) / 100.0, FVector2D(Site.X, Site.Y)) - Site.Radius);
		}
		if (NearestM > UnloadRadiusM)
		{
			if (Island->GetTerrain()->IsEdited())
			{
				Remembered.Add(Site.Id, Island->GetTerrain());
			}
			UE_LOG(LogRiptideWorld, Log, TEXT("Island unloaded: %s"), *Island->GetDescription());
			Island->Destroy();
			It.RemoveCurrent();
			continue;
		}
		Island->UpdateDetail(Focus);
	}
}

void ARiptideWorldDirector::SpawnIsland(const RiptideGen::FIslandSite& Site, const TSharedPtr<RiptideGen::FTerrain>& Terrain,
	const TArray<FVector>& Focus)
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Location(Terrain->OriginX * 100.0, Terrain->OriginY * 100.0, 0.0);
	ARiptideIsland* Island = GetWorld()->SpawnActor<ARiptideIsland>(ARiptideIsland::StaticClass(), FTransform(Location), Params);
	if (!Island)
	{
		return;
	}

	// Same order as RiptideGen::EScatter.
	const TArray<UStaticMesh*> Meshes = { PalmMesh.Get(), TreeMesh.Get(), BushMesh.Get(), BoulderMesh.Get() };
	Island->Build(Site, Terrain, LoadedMaterial, Meshes, Focus);
	Loaded.Add(Site.Id, Island);
}

ERiptideZone ARiptideWorldDirector::GetZoneAt(FVector WorldLocation) const
{
	switch (GetGenerator().ZoneAt(WorldLocation.X / 100.0, WorldLocation.Y / 100.0))
	{
	case RiptideGen::EZone::HomeChain:
		return ERiptideZone::HomeChain;
	case RiptideGen::EZone::Cordon:
		return ERiptideZone::Cordon;
	case RiptideGen::EZone::OpenSea:
	default:
		return ERiptideZone::OpenSea;
	}
}

FTransform ARiptideWorldDirector::GetStartSpawnTransform() const
{
	double X = 0.0;
	double Y = 0.0;
	float YawDeg = 0.f;
	GetGenerator().GetStartSpawn(X, Y, YawDeg);
	// The boat drops onto the water from 1.5 m up, as from a player start.
	return FTransform(FRotator(0.f, YawDeg, 0.f), FVector(X * 100.0, Y * 100.0, 150.0));
}

TArray<ARiptideIsland*> ARiptideWorldDirector::GetLoadedIslands() const
{
	TArray<ARiptideIsland*> Islands;
	for (const auto& Entry : Loaded)
	{
		if (ARiptideIsland* Island = Entry.Value.Get())
		{
			Islands.Add(Island);
		}
	}
	return Islands;
}

ARiptideIsland* ARiptideWorldDirector::GetStartIsland() const
{
	for (ARiptideIsland* Island : GetLoadedIslands())
	{
		if (Island->IsStartIsland())
		{
			return Island;
		}
	}
	return nullptr;
}

ARiptideIsland* ARiptideWorldDirector::FindIslandAt(FVector WorldLocation) const
{
	// Neighbouring islands' seabeds can overlap; the point belongs to whichever has the higher ground there.
	ARiptideIsland* Best = nullptr;
	float BestHeight = -UE_BIG_NUMBER;
	for (ARiptideIsland* Island : GetLoadedIslands())
	{
		if (Island->Covers(WorldLocation))
		{
			const float Height = Island->GetGroundHeight(WorldLocation);
			if (Height > BestHeight)
			{
				BestHeight = Height;
				Best = Island;
			}
		}
	}
	return Best;
}
