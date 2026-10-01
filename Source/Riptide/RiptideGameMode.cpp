#include "RiptideGameMode.h"
#include "EngineUtils.h"
#include "RiptideBoat.h"
#include "RiptideWorldDirector.h"

ARiptideGameMode::ARiptideGameMode()
{
	// Until the on-foot character exists (milestone 2), players spawn straight into a boat.
	DefaultPawnClass = ARiptideBoat::StaticClass();
}

APawn* ARiptideGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	TActorIterator<ARiptideWorldDirector> Director(GetWorld());
	if (!Director)
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}

	// Crew boats line up astern of the first, 12 m apart and out into deeper water, so none spawn inside each other.
	FTransform Spawn = Director->GetStartSpawnTransform();
	const float Astern = (BoatsSpawned % 6) * 1200.f;
	Spawn.AddToTranslation(-Spawn.GetRotation().GetForwardVector() * Astern);
	++BoatsSpawned;
	return SpawnDefaultPawnAtTransform(NewPlayer, Spawn);
}
