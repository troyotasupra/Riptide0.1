#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RiptideGameMode.generated.h"

class ARiptideBoat;
class ARiptideCharacter;
class URiptideWorldSave;

/**
 * Puts the crew in the world: on a castaway map (one with a beach start) washed up on the sand; otherwise on a boat
 * the first player to join launches at the player start, every player on its deck (the first at the helm).
 *
 * On a castaway map it also keeps the world's save (URiptideWorldSave): hosted with ?Continue it carries on from the
 * saved world, and a crew member who has played here before comes back where they were, with what they carried. It
 * saves every AutosaveSeconds, when the night is slept through, when a crew member leaves and at the end.
 */
UCLASS()
class RIPTIDE_API ARiptideGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARiptideGameMode();

	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;
	/** Starts the day: the sky clock (ARiptideSkyClock) every machine's sun follows. */
	virtual void BeginPlay() override;

	/** Takes the joining player's look from their join URL (?Look=..., see FRiptideAppearance::ToString), or, for
	 * a player on this machine who didn't send one (playing in the editor), from their saved profile. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void Logout(AController* Exiting) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Saves the world now (castaway maps; once the saved world, if continuing, is back). True if it was written. */
	UFUNCTION(BlueprintCallable, Category = "Riptide")
	bool SaveWorld();

	/** Whether this game continued a saved world (and has put it back). */
	UFUNCTION(BlueprintPure, Category = "Riptide")
	bool IsContinued() const { return bContinued; }

	/** A crew member's body is leaving the game (their player left, or the game is ending): their record is
	 * updated while it still has everything on it. */
	void NotePlayerLeaving(ARiptideCharacter* Character);

	/** Real seconds between saves. */
	UPROPERTY(EditAnywhere, Category = "Riptide")
	float AutosaveSeconds = 60.f;

protected:
	UPROPERTY(EditAnywhere, Category = "Riptide")
	TSubclassOf<ARiptideBoat> BoatClass;

private:
	ARiptideBoat* FindOrLaunchBoat(AActor* StartSpot);

	/** Puts the saved world back (continuing) and gives returning crew their things: the tick after play begins,
	 * once everything in the level is up. */
	void StartSaving();

	UPROPERTY(Transient)
	TObjectPtr<URiptideWorldSave> Save;

	/** Crew spawned before StartSaving, waiting for their saved things. */
	TArray<TWeakObjectPtr<ARiptideCharacter>> WaitingForSave;

	bool bWantContinue = false;
	bool bContinued = false;
	bool bSaving = false;
	FTimerHandle AutosaveTimer;
};
