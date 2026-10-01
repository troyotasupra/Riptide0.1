#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RiptideGameMode.generated.h"

UCLASS()
class RIPTIDE_API ARiptideGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARiptideGameMode();

	/** On a map with a world director, boats start off the start island instead of at a player start. */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

private:
	int32 BoatsSpawned = 0;
};
