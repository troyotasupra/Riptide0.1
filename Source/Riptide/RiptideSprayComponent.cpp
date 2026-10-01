#include "RiptideSprayComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

URiptideSprayComponent::URiptideSprayComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	// Flies after the boat has moved for the frame, so new spray leaves the hull where it is now.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	// While the world is paused (the dev mode's freeze) it still turns each cloud to face the camera flying round it.
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsyncCooking = true;
}

void URiptideSprayComponent::ThrowSpray(const FVector& Origin, const FVector& Velocity, float Scatter, int32 Count,
	float StartSizeCm, float EndSizeCm, float LifeSeconds, float SeaZ, float Opacity, float StreakSeconds)
{
	for (int32 i = 0; i < Count; ++i)
	{
		if (Clouds.Num() >= MaxClouds)
		{
			Clouds.RemoveAt(0, 1, EAllowShrinking::No);
		}
		FCloud& C = Clouds.AddDefaulted_GetRef();
		C.Position = Origin + FMath::VRand() * StartSizeCm * 0.3f;
		C.Velocity = Velocity + FMath::VRand() * Scatter * FMath::FRand();
		C.Life = LifeSeconds * FMath::FRandRange(0.7f, 1.3f);
		C.StartSize = StartSizeCm * FMath::FRandRange(0.7f, 1.3f);
		C.EndSize = EndSizeCm * FMath::FRandRange(0.7f, 1.3f);
		C.SeaZ = SeaZ;
		C.Opacity = Opacity;
		C.Spin = FMath::FRand() * UE_TWO_PI;
		C.Streak = StreakSeconds;
		C.NoiseOffset = FVector2f(FMath::FRand(), FMath::FRand());
	}
}

void URiptideSprayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Clouds.Num() == 0 && GetNumSections() == 0)
	{
		return;
	}
	if (TickType == LEVELTICK_PauseTick)
	{
		// Frozen: the spray hangs where it is.
		RebuildMesh();
		return;
	}

	const float Gravity = GetWorld() ? GetWorld()->GetGravityZ() : -980.f;
	const float DragFactor = FMath::Exp(-Drag * DeltaTime);
	for (int32 i = Clouds.Num() - 1; i >= 0; --i)
	{
		FCloud& C = Clouds[i];
		C.Age += DeltaTime;
		C.Velocity.Z += Gravity * DeltaTime;
		C.Velocity *= DragFactor;
		C.Position += C.Velocity * DeltaTime;
		// Gone once it has lived out its time, fallen back into the sea, or ended up inside the boat.
		const bool bInSea = C.Velocity.Z < 0.f && C.Position.Z < C.SeaZ - 15.f;
		if (C.Age >= C.Life || bInSea || (IsInsideSolid && IsInsideSolid(C.Position)))
		{
			Clouds.RemoveAtSwap(i, 1, EAllowShrinking::No);
		}
	}
	RebuildMesh();
}

void URiptideSprayComponent::RebuildMesh()
{
	if (Clouds.Num() == 0)
	{
		ClearAllMeshSections();
		return;
	}

	// Each cloud faces the camera that's looking at it.
	FVector CamRight = FVector::RightVector, CamUp = FVector::UpVector, CamLoc = FVector::ZeroVector;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager)
		{
			const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
			CamLoc = PC->PlayerCameraManager->GetCameraLocation();
			CamRight = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Y);
			CamUp = FRotationMatrix(CamRot).GetUnitAxis(EAxis::Z);
		}
	}

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colours;
	TArray<FProcMeshTangent> Tangents;
	Verts.Reserve(Clouds.Num() * 4);
	Tris.Reserve(Clouds.Num() * 6);
	Normals.Reserve(Clouds.Num() * 4);
	UVs.Reserve(Clouds.Num() * 4);
	Colours.Reserve(Clouds.Num() * 4);

	static const FVector2D Corner[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	for (const FCloud& C : Clouds)
	{
		const float T = FMath::Clamp(C.Age / C.Life, 0.f, 1.f);
		// Grows fast at first then spreads slowly.
		const float Size = FMath::Lerp(C.StartSize, C.EndSize, FMath::Sqrt(T));
		// Solid through most of its flight, thinning away only near the end (water doesn't dissolve like smoke).
		const float Alpha = C.Opacity * FMath::Min(1.f, T * 12.f) * (1.f - T * T);
		// Stretched along its flight as seen from the camera (a streak), otherwise a round puff at a random spin.
		const FVector Normal = (CamLoc - C.Position).GetSafeNormal();
		const FVector Flight = FVector::VectorPlaneProject(C.Velocity, Normal);
		const float StreakLength = Flight.Size() * C.Streak;
		FVector Along, Across;
		if (StreakLength > Size * 0.25f)
		{
			Along = Flight.GetSafeNormal();
			Across = FVector::CrossProduct(Normal, Along);
		}
		else
		{
			const float S = FMath::Sin(C.Spin), Co = FMath::Cos(C.Spin);
			Along = CamUp * Co - CamRight * S;
			Across = CamRight * Co + CamUp * S;
		}
		const FVector Up = Along * (Size + StreakLength) * 0.5f;
		const FVector Right = Across * Size * 0.5f;
		const FColor Colour(uint8(C.NoiseOffset.X * 255.f), uint8(C.NoiseOffset.Y * 255.f), 255, uint8(FMath::Clamp(Alpha, 0.f, 1.f) * 255.f));

		const int32 Base = Verts.Num();
		Verts.Add(C.Position - Right - Up);
		Verts.Add(C.Position + Right - Up);
		Verts.Add(C.Position + Right + Up);
		Verts.Add(C.Position - Right + Up);
		for (int32 k = 0; k < 4; ++k)
		{
			Normals.Add(Normal);
			UVs.Add(Corner[k]);
			Colours.Add(Colour);
		}
		Tris.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
	}
	// The component sits at the world origin (absolute transform), so world positions are its local ones.
	CreateMeshSection(0, Verts, Tris, Normals, UVs, Colours, Tangents, false);
}
