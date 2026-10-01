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

protected:
	UPROPERTY(EditAnywhere, Category = "Riptide")
	TSubclassOf<ARiptideBoat> BoatClass;

private:
	ARiptideBoat* FindOrLaunchBoat(AActor* StartSpot);
};
