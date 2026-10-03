#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "RiptideBeachStart.generated.h"

/**
 * Where castaways wash up: a player start on an island's beach. A map with one of these starts the crew on foot
 * along the tide line with empty pockets and no boat (ARiptideGameMode); without one, the crew start aboard the
 * patrol boat at an ordinary player start. The island build places it (riptide_islands.py).
 */
UCLASS()
class RIPTIDE_API ARiptideBeachStart : public APlayerStart
{
	GENERATED_BODY()

public:
	ARiptideBeachStart(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

	/** Where the Nth crew member lands: spread along the beach either side of the start, facing inland. */
	FTransform GetLandingTransform(int32 Crew) const
	{
		const float Along = (Crew + 1) / 2 * 160.f * ((Crew % 2) ? -1.f : 1.f);
		return FTransform(FRotator(0.f, GetActorRotation().Yaw, 0.f), GetActorLocation() + GetActorRightVector() * Along);
	}
};
