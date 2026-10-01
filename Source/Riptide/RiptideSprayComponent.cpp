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
	// No collision to cook (cooking it asynchronously would make a new body setup every time the mesh changes).
	bUseAsyncCooking = false;
}

void URiptideSprayComponent::ThrowSpray(const FVector& Origin, const FVector& Velocity, float Scatter, int32 Count, float StartSizeCm,
	float EndSizeCm, float LifeSeconds, float SeaZ, float Opacity, float StreakSeconds, const FVector& NeverToward)
{
	for (int32 i = 0; i < Count; ++i)
	{
		if (Clouds.Num() >= MaxClouds)
		{
			return;
		}
		FCloud& C = Clouds.AddDefaulted_GetRef();
		C.Position = Origin + FMath::VRand() * StartSizeCm * 0.3f;
		// Scattered every which way, except (when asked) back toward NeverToward's opposite: water shoved out from a
		// hull doesn't scatter back into it.
		FVector Scattered = FMath::VRand() * Scatter * FMath::FRand();
		if (!NeverToward.IsNearlyZero() && FVector::DotProduct(Scattered, NeverToward) < 0.f)
		{
			Scattered -= 2.f * FVector::DotProduct(Scattered, NeverToward) * NeverToward;
		}
		C.Velocity = Velocity + Scattered;
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
		C.Clearance = ClearanceFromSolid ? ClearanceFromSolid(C.Position) : 1e6f;
		if (C.Age >= C.Life || bInSea || C.Clearance <= 0.f)
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
		if (GetNumSections() > 0)
		{
			ClearAllMeshSections();
			SectionQuads = 0;
		}
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

	// Room for a block of quads at a time, so the arrays aren't reallocated as the spray grows and shrinks; quads not
	// in use are folded away to nothing.
	constexpr int32 Block = 1024;
	const int32 Needed = FMath::DivideAndRoundUp(Clouds.Num(), Block) * Block;
	const bool bRemake = Needed > SectionQuads || Needed * 2 < SectionQuads;
	const int32 Quads = bRemake ? Needed : SectionQuads;
	Verts.SetNumUninitialized(Quads * 4, EAllowShrinking::No);
	Normals.SetNumUninitialized(Quads * 4, EAllowShrinking::No);
	UVs.SetNumUninitialized(Quads * 4, EAllowShrinking::No);
	Colours.SetNumUninitialized(Quads * 4, EAllowShrinking::No);

	static const FVector2D Corner[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	for (int32 q = 0; q < Clouds.Num(); ++q)
	{
		const FCloud& C = Clouds[q];
		const float T = FMath::Clamp(C.Age / C.Life, 0.f, 1.f);
		// Grows fast at first then spreads slowly, but never reaches the hull: a cloud is a flat card facing the
		// camera, and one wider than its distance from the hull would poke through into the boat.
		const float Size = FMath::Min(FMath::Lerp(C.StartSize, C.EndSize, FMath::Sqrt(T)), 2.f * C.Clearance);
		// Solid through most of its flight, thinning away only near the end (water doesn't dissolve like smoke), and
		// fading out in the last few centimetres before it touches the hull.
		const float Alpha = C.Opacity * FMath::Min(1.f, T * 12.f) * (1.f - T * T) * FMath::Clamp(C.Clearance / 6.f, 0.f, 1.f);
		// Stretched along its flight as seen from the camera (a streak), otherwise a round puff at a random spin.
		const FVector Normal = (CamLoc - C.Position).GetSafeNormal();
		const FVector Flight = FVector::VectorPlaneProject(C.Velocity, Normal);
		const float StreakLength = FMath::Min(Flight.Size() * C.Streak, FMath::Max(0.f, 2.f * C.Clearance - Size));
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
		const int32 Base = q * 4;
		Verts[Base + 0] = C.Position - Right - Up;
		Verts[Base + 1] = C.Position + Right - Up;
		Verts[Base + 2] = C.Position + Right + Up;
		Verts[Base + 3] = C.Position - Right + Up;
		for (int32 k = 0; k < 4; ++k)
		{
			Normals[Base + k] = Normal;
			UVs[Base + k] = Corner[k];
			Colours[Base + k] = Colour;
		}
	}
	// Spare quads: folded to a point on the first cloud (so they don't stretch the bounds), invisible.
	const FVector Spare = Clouds[0].Position;
	for (int32 v = Clouds.Num() * 4; v < Quads * 4; ++v)
	{
		Verts[v] = Spare;
		Normals[v] = FVector::UpVector;
		UVs[v] = FVector2D::ZeroVector;
		Colours[v] = FColor(0, 0, 0, 0);
	}

	// The component sits at the world origin (absolute transform), so world positions are its local ones. The
	// section is made afresh each frame (updating its vertices in place didn't reach the screen); the triangle list
	// is only rebuilt when the room for quads changes.
	if (bRemake || Tris.Num() != Quads * 6)
	{
		Tris.SetNumUninitialized(Quads * 6, EAllowShrinking::No);
		for (int32 q = 0; q < Quads; ++q)
		{
			const int32 B = q * 4;
			int32* T = &Tris[q * 6];
			T[0] = B; T[1] = B + 2; T[2] = B + 1; T[3] = B; T[4] = B + 3; T[5] = B + 2;
		}
		SectionQuads = Quads;
	}
	CreateMeshSection(0, Verts, Tris, Normals, UVs, Colours, TArray<FProcMeshTangent>(), false);
}
