#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RiptideGameMode.generated.h"

class ARiptideBoat;

/**
 * Puts the crew on a boat: the first player to join launches one at the player start, and every player stands
 * on its deck (the first at the helm) on foot.
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

protected:
	UPROPERTY(EditAnywhere, Category = "Riptide")
	TSubclassOf<ARiptideBoat> BoatClass;

private:
	ARiptideBoat* FindOrLaunchBoat(AActor* StartSpot);
};
