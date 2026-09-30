#include "RiptideCharacterMovement.h"

#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"

URiptideCharacterMovement::URiptideCharacterMovement()
{
	NavAgentProps.bCanSwim = true;
	// A steady crawl, floating with the head clear of the water at rest (the body floats about three quarters under).
	MaxSwimSpeed = 180.f;
	Buoyancy = 1.35f;
}

float URiptideCharacterMovement::GetSeaSurfaceZ() const
{
	if (!Ocean.IsValid())
	{
		if (const AWaterBodyOcean* Actor = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass())))
		{
			Ocean = Actor->GetWaterBodyComponent();
		}
	}
	if (UWaterBodyComponent* Body = Ocean.Get())
	{
		const auto Query = Body->TryQueryWaterInfoClosestToWorldLocation(UpdatedComponent->GetComponentLocation(),
			EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
		if (Query.HasValue())
		{
			return Query.GetValue().GetWaterSurfaceLocation().Z;
		}
	}
	return -UE_BIG_NUMBER;
}

bool URiptideCharacterMovement::IsInWater() const
{
	// In the water once its middle is under the surface.
	return UpdatedComponent && CharacterOwner && UpdatedComponent->GetComponentLocation().Z < GetSeaSurfaceZ();
}

float URiptideCharacterMovement::ImmersionDepth() const
{
	// How much of the body is under the surface, 0 (clear of it) to 1 (fully under).
	if (!UpdatedComponent || !CharacterOwner)
	{
		return 0.f;
	}
	const float HalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();
	const float Bottom = UpdatedComponent->GetComponentLocation().Z - HalfHeight;
	return FMath::Clamp((GetSeaSurfaceZ() - Bottom) / (2.f * HalfHeight), 0.f, 1.f);
}

float URiptideCharacterMovement::GetMaxSpeed() const
{
	return IsSeaSwimming() ? MaxSwimSpeed : Super::GetMaxSpeed();
}

FVector URiptideCharacterMovement::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
	// The engine flattens input unless swimming or flying in its own modes; in the sea, up and down count too.
	return IsSeaSwimming() ? InputAcceleration : Super::ConstrainInputAcceleration(InputAcceleration);
}

void URiptideCharacterMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Falling or stepping into the sea starts swimming.
	if (CharacterOwner && (MovementMode == MOVE_Falling || MovementMode == MOVE_Walking) && CanEverSwim() && IsInWater())
	{
		SetMovementMode(MOVE_Custom, RIPTIDE_MOVE_SeaSwim);
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void URiptideCharacterMovement::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode == RIPTIDE_MOVE_SeaSwim)
	{
		PhysSeaSwim(DeltaTime, Iterations);
		return;
	}
	Super::PhysCustom(DeltaTime, Iterations);
}

void URiptideCharacterMovement::PhysSeaSwim(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME || !CharacterOwner)
	{
		return;
	}
	const float Depth = ImmersionDepth();
	if (Depth <= 0.f)
	{
		// Clear of the water (kicked up out of it, or the sea dropped away): fall.
		SetMovementMode(MOVE_Falling);
		StartNewPhysics(DeltaTime, Iterations);
		return;
	}

	// Strokes where the player steers (Acceleration is the input at full MaxAcceleration), buoyancy against gravity
	// by how deep the body sits, and water drag.
	const float StrokeScale = MaxAcceleration > 0.f ? SwimAcceleration / MaxAcceleration : 0.f;
	FVector Accel = Acceleration * StrokeScale;
	Accel.Z += GetGravityZ() * (1.f - Buoyancy * Depth);
	Velocity += Accel * DeltaTime;
	Velocity *= FMath::Exp(-SwimDrag * DeltaTime);
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	if (Flat.SizeSquared() > FMath::Square(MaxSwimSpeed))
	{
		const FVector Capped = Flat.GetSafeNormal() * MaxSwimSpeed;
		Velocity.X = Capped.X;
		Velocity.Y = Capped.Y;
	}
	Velocity.Z = FMath::Clamp(Velocity.Z, -1.5f * MaxSwimSpeed, 2.5f * MaxSwimSpeed);

	const FVector Delta = Velocity * DeltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		// Bumping a hull or a reef: slide along it.
		HandleImpact(Hit, DeltaTime, Delta);
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
}
