#include "RiptideGameMode.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"
#include "RiptideBoat.h"
#include "RiptideCharacter.h"
#include "RiptidePlayerState.h"
#include "RiptidePlayerController.h"

ARiptideGameMode::ARiptideGameMode()
{
	DefaultPawnClass = ARiptideCharacter::StaticClass();
	PlayerControllerClass = ARiptidePlayerController::StaticClass();
	PlayerStateClass = ARiptidePlayerState::StaticClass();
	BoatClass = ARiptideBoat::StaticClass();
}

ARiptideBoat* ARiptideGameMode::FindOrLaunchBoat(AActor* StartSpot)
{
	for (TActorIterator<ARiptideBoat> It(GetWorld()); It; ++It)
	{
		return *It;
	}

	// Launched on the water at the player start (the hull's origin sits 15 cm above its waterline), facing its way.
	// It sits on the swell as it is right there: dropped into a crest's flank at flat sea level, the hull would be
	// buried a metre deep and thrown clear of the water.
	FVector Location = StartSpot ? StartSpot->GetActorLocation() : FVector::ZeroVector;
	float SurfaceZ = 0.f;
	if (const AWaterBodyOcean* Ocean = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass())))
	{
		const auto Query = Ocean->GetWaterBodyComponent()->TryQueryWaterInfoClosestToWorldLocation(
			FVector(Location.X, Location.Y, 0.f), EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
		if (Query.HasValue())
		{
			SurfaceZ = Query.GetValue().GetWaterSurfaceLocation().Z;
		}
	}
	Location.Z = SurfaceZ + 15.f;
	const FRotator Facing(0.f, StartSpot ? StartSpot->GetActorRotation().Yaw : 0.f, 0.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return GetWorld()->SpawnActor<ARiptideBoat>(BoatClass, Location, Facing, Params);
}

FString ARiptideGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
	const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (ARiptidePlayerState* State = NewPlayerController ? NewPlayerController->GetPlayerState<ARiptidePlayerState>() : nullptr)
	{
		const FString Look = UGameplayStatics::ParseOption(Options, TEXT("Look"));
		if (!Look.IsEmpty())
		{
			State->SetAppearance(FRiptideAppearance::FromString(Look));
		}
		else if (NewPlayerController->IsLocalController())
		{
			State->SetAppearance(URiptideProfileSave::LoadOrCreate()->Appearance);
		}
	}
	return Error;
}

APawn* ARiptideGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	ARiptideBoat* Boat = FindOrLaunchBoat(StartSpot);
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!Boat || !PawnClass || !PawnClass->IsChildOf(ARiptideCharacter::StaticClass()))
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}

	// Each player gets their own spot on the deck, starting at the helm.
	int32 Crew = 0;
	for (TActorIterator<ARiptideCharacter> It(GetWorld()); It; ++It)
	{
		++Crew;
	}
	const FTransform Spot = Boat->GetDeckSpotTransform(Crew);
	const float HalfHeight = PawnClass->GetDefaultObject<ARiptideCharacter>()->GetDefaultHalfHeight();
	const FVector Location = Spot.GetLocation() + Spot.GetUnitAxis(EAxis::Z) * (HalfHeight + 2.f);

	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideCharacter* Character = GetWorld()->SpawnActor<ARiptideCharacter>(PawnClass, Location, FRotator(0.f, Spot.Rotator().Yaw, 0.f), Params);
	if (Character)
	{
		Character->SetHomeBoat(Boat);
	}
	return Character;
}
