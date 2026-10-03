#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RiptideProfile.generated.h"

/**
 * Frame-time reporting for the off-screen game runs that check the islands: with -RiptideProfile on the command
 * line, the game logs its average frame time every few seconds once the map is up, and once writes the GPU's
 * breakdown of a frame (the engine's ProfileGPU) to the log, so a slow island can be read off the log.
 */
UCLASS()
class RIPTIDE_API URiptideProfileSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(URiptideProfileSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return true; }

private:
	float Elapsed = 0.f;
	float SinceReport = 0.f;
	int32 Frames = 0;
	float Worst = 0.f;
	bool bDumped = false;
};
