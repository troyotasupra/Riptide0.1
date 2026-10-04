#include "RiptideGameMode.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"
#include "RiptideBeachStart.h"
#include "RiptideBoat.h"
#include "RiptideSkyClock.h"
#include "RiptideCharacter.h"
#include "RiptideSea.h"
#include "RiptideHUD.h"
#include "RiptidePlayerState.h"
#include "RiptidePlayerController.h"
#include "RiptideWorldSave.h"

ARiptideGameMode::ARiptideGameMode()
{
	DefaultPawnClass = ARiptideCharacter::StaticClass();
	PlayerControllerClass = ARiptidePlayerController::StaticClass();
	PlayerStateClass = ARiptidePlayerState::StaticClass();
	HUDClass = ARiptideHUD::StaticClass();      // the in-game menu (Esc)
	BoatClass = ARiptideBoat::StaticClass();
}

void ARiptideGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Continuing: the save is read now, before anyone joins, so returning crew can be put back where they were.
	bWantContinue = UGameplayStatics::HasOption(Options, TEXT("Continue"));
	Save = bWantContinue ? URiptideWorldSave::Load(this) : nullptr;
	if (Save && Save->Map != UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: the saved world is on %s, not this map; starting afresh"), *Save->Map);
		Save = nullptr;
	}
	if (!Save)
	{
		Save = NewObject<URiptideWorldSave>(this);
	}
}

void ARiptideGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (!ARiptideSkyClock::Get(this))
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		GetWorld()->SpawnActor<ARiptideSkyClock>(ARiptideSkyClock::StaticClass(), FTransform::Identity, Params);
	}
	GetWorldTimerManager().SetTimerForNextTick(this, &ARiptideGameMode::StartSaving);
}

void ARiptideGameMode::StartSaving()
{
	// Only castaway maps keep a save for now: on the boat maps the crew live aboard a boat the save doesn't hold yet.
	bSaving = Save && TActorIterator<ARiptideBeachStart>(GetWorld());
	if (!bSaving)
	{
		return;
	}
	if (bWantContinue && Save->Structures.Num() + Save->WorldItems.Num() + Save->Harvested.Num() + Save->Rafts.Num() + Save->Players.Num() > 0)
	{
		Save->RestoreWorld(GetWorld());
		bContinued = true;
	}
	for (const TWeakObjectPtr<ARiptideCharacter>& Waiting : WaitingForSave)
	{
		const FRiptideSavedPlayer* Record = Waiting.IsValid() ? Save->FindPlayer(Waiting->GetController()) : nullptr;
		if (Record)
		{
			URiptideWorldSave::RestorePlayer(Waiting.Get(), *Record);
			// Put back where they stood again, now the world is: on a raft's deck rather than in the sea where it
			// wasn't yet.
			Waiting->SetActorLocation(Record->Location + FVector(0.f, 0.f, 10.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	WaitingForSave.Reset();
	GetWorldTimerManager().SetTimer(AutosaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { SaveWorld(); }),
		FMath::Max(AutosaveSeconds, 5.f), true);
}

bool ARiptideGameMode::SaveWorld()
{
	if (!bSaving || !Save)
	{
		return false;
	}
	Save->Capture(GetWorld());
	const bool bWritten = Save->Write(this);
	UE_LOG(LogTemp, Log, TEXT("Riptide: saved the world (day %d, %.2f h, %d crew on record): %s"), Save->Day, Save->Hours, Save->Players.Num(),
		bWritten ? TEXT("ok") : TEXT("FAILED"));
	return bWritten;
}

void ARiptideGameMode::NotePlayerLeaving(ARiptideCharacter* Character)
{
	if (bSaving && Save && Character && Character->GetController())
	{
		Save->CapturePlayer(Character);
	}
}

void ARiptideGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	// Their body has gone (and been noted, NotePlayerLeaving); the world is saved with them in it as they left.
	SaveWorld();
}

void ARiptideGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SaveWorld();
	bSaving = false;
	GetWorldTimerManager().ClearTimer(AutosaveTimer);
	Super::EndPlay(EndPlayReason);
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
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !PawnClass->IsChildOf(ARiptideCharacter::StaticClass()))
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}
	int32 Crew = 0;
	for (TActorIterator<ARiptideCharacter> It(GetWorld()); It; ++It)
	{
		++Crew;
	}
	const float HalfHeight = PawnClass->GetDefaultObject<ARiptideCharacter>()->GetDefaultHalfHeight();
	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Castaways: a map with a beach start lands the crew on the sand, on foot, with nothing; or, for someone who's
	// played in this world before, where they were, with what they had.
	for (TActorIterator<ARiptideBeachStart> It(GetWorld()); It; ++It)
	{
		if (const FRiptideSavedPlayer* Record = Save ? Save->FindPlayer(NewPlayer) : nullptr)
		{
			ARiptideCharacter* Back = GetWorld()->SpawnActor<ARiptideCharacter>(PawnClass, Record->Location + FVector(0.f, 0.f, 5.f),
				FRotator(0.f, Record->Yaw, 0.f), Params);
			if (Back && bSaving)
			{
				URiptideWorldSave::RestorePlayer(Back, *Record);
			}
			else if (Back)
			{
				WaitingForSave.Add(Back);
			}
			return Back;
		}
		const FTransform Landing = It->GetLandingTransform(Crew);
		FVector Location = Landing.GetLocation();
		if (const URiptideSeaSubsystem* Sea = GetWorld()->GetSubsystem<URiptideSeaSubsystem>())
		{
			float Ground = 0.f;
			if (Sea->GetGroundZ(Location, Ground))
			{
				Location.Z = FMath::Max(Ground, 0.f) + HalfHeight + 5.f;
			}
		}
		return GetWorld()->SpawnActor<ARiptideCharacter>(PawnClass, Location, Landing.Rotator(), Params);
	}

	ARiptideBoat* Boat = FindOrLaunchBoat(StartSpot);
	if (!Boat)
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}

	// Each player gets their own spot on the deck, starting at the helm.
	const FTransform Spot = Boat->GetDeckSpotTransform(Crew);
	const FVector Location = Spot.GetLocation() + Spot.GetUnitAxis(EAxis::Z) * (HalfHeight + 2.f);
	ARiptideCharacter* Character = GetWorld()->SpawnActor<ARiptideCharacter>(PawnClass, Location, FRotator(0.f, Spot.Rotator().Yaw, 0.f), Params);
	if (Character)
	{
		Character->SetHomeBoat(Boat);
	}
	return Character;
}
