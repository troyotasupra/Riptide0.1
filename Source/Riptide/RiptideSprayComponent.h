#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "RiptideSprayComponent.generated.h"

/**
 * Water thrown into the air: sheets and bursts of spray off a hull, as clouds of droplets that fly out, arc up, fall
 * back under gravity and drag, and vanish into the sea. Each cloud is drawn as a soft puff facing the camera (the
 * M_Spray material), growing and thinning as it spreads. Cosmetic: every machine runs its own.
 *
 * The owner decides where and how hard to throw water (ThrowSpray); this component flies and draws it. Positions are
 * in world space, so the spray is left behind by a moving boat rather than dragged along with it.
 */
UCLASS(ClassGroup = (Riptide), meta = (BlueprintSpawnableComponent))
class RIPTIDE_API URiptideSprayComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	URiptideSprayComponent(const FObjectInitializer& ObjectInitializer);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Throws Count clouds of spray from Origin at Velocity (world, cm/s), each with its own random extra velocity up to
	 * Scatter in any direction. They start StartSizeCm across and grow to EndSizeCm, live about LifeSeconds, and drop
	 * out of sight once they fall below SeaZ.
	 */
	void ThrowSpray(const FVector& Origin, const FVector& Velocity, float Scatter, int32 Count, float StartSizeCm,
		float EndSizeCm, float LifeSeconds, float SeaZ, float Opacity = 1.f, float StreakSeconds = 0.05f);

	/** Most clouds in the air at once; the oldest make way. */
	UPROPERTY(EditAnywhere, Category = "Spray")
	int32 MaxClouds = 8000;

	/** Air drag on the spray, per second (spray slows quickly once thrown). */
	UPROPERTY(EditAnywhere, Category = "Spray")
	float Drag = 0.7f;

	int32 GetCloudCount() const { return Clouds.Num(); }

	/**
	 * How far a point is from the solid thing the spray flies off (the hull), in cm: negative inside it. Spray that
	 * ends up inside disappears, and each cloud is drawn no bigger than its distance from the hull (fading as it
	 * gets close), so no part of it ever shows through the boat, however big the cloud has grown.
	 */
	TFunction<float(const FVector&)> ClearanceFromSolid;

private:
	struct FCloud
	{
		FVector Position;
		FVector Velocity;
		float Age = 0.f;
		float Life = 1.f;
		float StartSize = 30.f;
		float EndSize = 90.f;
		float SeaZ = 0.f;
		float Opacity = 1.f;
		float Spin = 0.f;
		float Streak = 0.05f;   // drawn stretched along its flight by this much of its motion, so fast droplets streak
		float Clearance = 1e6f; // how far it is from the hull this frame (cm)
		FVector2f NoiseOffset;
	};

	void RebuildMesh();

	TArray<FCloud> Clouds;
};
