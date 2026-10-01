#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RiptideCharacterMovement.generated.h"

class UWaterBodyComponent;

/** Riptide's own movement modes (MOVE_Custom sub-modes). */
enum ERiptideMove : uint8
{
	RIPTIDE_MOVE_SeaSwim = 1,
};

/**
 * Character movement that swims in the Water plugin's ocean. The engine's swimming only works inside water volumes,
 * which the ocean doesn't give characters, so sea swimming is a custom mode: buoyancy from how deep the body sits in
 * the sea's real surface (waves included), water drag, and strokes in any direction (where you look, plus up and down).
 */
UCLASS()
class RIPTIDE_API URiptideCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	URiptideCharacterMovement();

	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation,
		const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual bool IsInWater() const override;
	virtual float ImmersionDepth() const override;
	virtual float GetMaxSpeed() const override;
	virtual FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const override;

	/** Swimming in the sea. */
	bool IsSeaSwimming() const { return MovementMode == MOVE_Custom && CustomMovementMode == RIPTIDE_MOVE_SeaSwim; }

	/** Height of the sea's surface (waves included) where the character is, or a very low number with no ocean. */
	float GetSeaSurfaceZ() const;

	/** How fast strokes speed you up in the water (cm/s^2), and how quickly the water slows you (1/s). */
	UPROPERTY(EditAnywhere, Category = "Swimming")
	float SwimAcceleration = 450.f;

	UPROPERTY(EditAnywhere, Category = "Swimming")
	float SwimDrag = 2.2f;

protected:
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

private:
	void PhysSeaSwim(float DeltaTime, int32 Iterations);

	mutable TWeakObjectPtr<UWaterBodyComponent> Ocean;
	/** When to look for the ocean again if there wasn't one (a map without sea doesn't search every frame). */
	mutable double NextOceanSearch = 0.0;
};
