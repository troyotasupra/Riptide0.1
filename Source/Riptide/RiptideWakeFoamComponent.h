#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "RiptideWakeFoamComponent.generated.h"

class UWaterBodyComponent;

/**
 * White foam trails laid on the water surface behind a boat: strips that start where the boat meets the water,
 * spread and drift as they age, fade out, and ride up and down with the waves.
 *
 * The owner feeds each trail every frame with UpdateTrail (where foam is being made and how strongly), then
 * calls RebuildMesh once. The mesh is built in world space, so the component ignores its parent's transform.
 */
UCLASS(ClassGroup = (Riptide))
class RIPTIDE_API URiptideWakeFoamComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	URiptideWakeFoamComponent(const FObjectInitializer& ObjectInitializer);

	/** How one trail looks: its starting half-width, how it grows, drifts sideways and fades. Lengths in cm. */
	struct FTrailStyle
	{
		float StartHalfWidth = 60.f;
		float GrowthPerSecond = 30.f;
		/** Sideways drift per second, along the side direction given when the foam was made (for V-shaped wash). */
		float DriftPerSecond = 0.f;
		float LifeSeconds = 8.f;
		float Opacity = 0.8f;
		/** How far the trail meanders side to side, in cm: at birth and once fully aged. */
		float WobbleAtBirth = 10.f;
		float WobbleWhenOld = 80.f;
		/** Length of one meander along the trail, in cm. */
		float WobbleWavelength = 900.f;
		/** How much the width swells and thins along the trail (0 = even, 0.5 = +/-50%). */
		float WidthVariation = 0.35f;
		/** How patchy the foam gets along the trail (0 = even, 1 = gaps). */
		float Patchiness = 0.4f;
	};

	/** Adds a trail with the given style and returns its index. */
	int32 AddTrail(const FTrailStyle& Style);

	/**
	 * Foam is being made at Location (on the waterline) with Strength 0..1. Side is the direction the trail
	 * widens and drifts toward (usually the hull's right, flipped for the port side). Strength 0 ends the strip.
	 */
	void UpdateTrail(int32 Trail, float DeltaSeconds, const FVector& Location, const FVector& Side, float Strength);

	/** Rebuilds the foam mesh from all trails, following the water surface. */
	void RebuildMesh();

	/** The water the foam sits on. Without it the foam stays at the height it was made. */
	void SetWaterBody(UWaterBodyComponent* InWaterBody) { WaterBody = InWaterBody; }

	/** Distance travelled between foam points, in cm. */
	float PointSpacing = 60.f;

	/** How far above the surface the foam floats, in cm, so the water doesn't cover it. */
	float SurfaceOffset = 12.f;

private:
	struct FPoint
	{
		FVector2D Position;
		FVector2D Side;
		float Height = 0.f;
		/** Distance along the trail since it began, in cm; drives the meander and patchiness. */
		float Distance = 0.f;
		float Age = 0.f;
		float Strength = 0.f;
		bool bStartsStrip = false;
	};

	struct FTrail
	{
		FTrailStyle Style;
		TArray<FPoint> Points;
		bool bEmitting = false;
		FVector2D LastEmitted = FVector2D::ZeroVector;
		float Distance = 0.f;
		/** Random offset into the noise, so no two trails meander alike. */
		float NoiseSeed = 0.f;
	};

	TArray<FTrail> Trails;

	UPROPERTY(Transient)
	TObjectPtr<UWaterBodyComponent> WaterBody;
};
