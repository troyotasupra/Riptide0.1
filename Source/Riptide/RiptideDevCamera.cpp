#include "RiptideDevCamera.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "RiptideBoat.h"

namespace
{
	// The dev mode's on-screen notes (kept apart from the game's own prompts).
	constexpr uint64 SpeedMessageKey = 0x44455631ull;
	const FColor DevNoteColour(120, 215, 255);

	constexpr float MinFlySpeed = 50.f;
	constexpr float MaxFlySpeed = 30000.f;
	constexpr float MinZoom = 300.f;
	constexpr float MaxZoom = 20000.f;

	// Circling or chasing, the keys swing the camera round at this many degrees a second, and zoom by this factor a
	// second.
	constexpr float KeyOrbitDegPerSec = 60.f;
	constexpr float KeyZoomPerSec = 1.2f;

	constexpr float CmPerSecToKnots = 0.0194384f;
}

ARiptideDevCamera::ARiptideDevCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	// After everything else has moved for the frame (the boat's physics, the crew), so a camera riding along with the
	// boat is never a frame behind it.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	// It flies while the world is frozen.
	PrimaryActorTick.bTickEvenWhenPaused = true;

	// Only the player flying it needs it: nothing to show anyone else, and nothing for it to bump into.
	bReplicates = false;
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
	AutoPossessAI = EAutoPossessAI::Disabled;

	// The camera turns itself (see Tick); the controller's rotation doesn't steer it.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->bUsePawnControlRotation = false;
	RootComponent = Camera;
}

// --- Input ---

void ARiptideDevCamera::BuildInput()
{
	if (FlyMapping)
	{
		return;
	}

	// Built in code like the crew's and the helm's. Every action works while the world is frozen.
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		Action->bTriggerWhenPaused = true;
		return Action;
	};
	MoveAction = MakeAction(TEXT("IA_DevFly"), EInputActionValueType::Axis3D);
	LookAction = MakeAction(TEXT("IA_DevLook"), EInputActionValueType::Axis2D);
	FastAction = MakeAction(TEXT("IA_DevFast"), EInputActionValueType::Boolean);
	SlowAction = MakeAction(TEXT("IA_DevSlow"), EInputActionValueType::Boolean);
	WheelAction = MakeAction(TEXT("IA_DevWheel"), EInputActionValueType::Axis1D);

	FlyMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_DevFly"));

	// Fly is (forward, right, up): a key's 1 lands on X, so right and up keys are swizzled onto Y and Z.
	auto Swizzle = [this](EInputAxisSwizzle Order)
	{
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(FlyMapping);
		S->Order = Order;
		return S;
	};
	auto Negate = [this]() { return NewObject<UInputModifierNegate>(FlyMapping); };
	FlyMapping->MapKey(MoveAction, EKeys::W);
	FlyMapping->MapKey(MoveAction, EKeys::S).Modifiers.Add(Negate());
	FlyMapping->MapKey(MoveAction, EKeys::D).Modifiers.Add(Swizzle(EInputAxisSwizzle::YXZ));
	FEnhancedActionKeyMapping& Left = FlyMapping->MapKey(MoveAction, EKeys::A);
	Left.Modifiers.Add(Swizzle(EInputAxisSwizzle::YXZ));
	Left.Modifiers.Add(Negate());
	for (const FKey& Up : { EKeys::E, EKeys::SpaceBar, EKeys::Gamepad_RightTriggerAxis })
	{
		FlyMapping->MapKey(MoveAction, Up).Modifiers.Add(Swizzle(EInputAxisSwizzle::ZYX));
	}
	for (const FKey& Down : { EKeys::Q, EKeys::C, EKeys::Gamepad_LeftTriggerAxis })
	{
		FEnhancedActionKeyMapping& Sink = FlyMapping->MapKey(MoveAction, Down);
		Sink.Modifiers.Add(Swizzle(EInputAxisSwizzle::ZYX));
		Sink.Modifiers.Add(Negate());
	}
	// The left stick is (right, forward): swapped onto (forward, right).
	FlyMapping->MapKey(MoveAction, EKeys::Gamepad_Left2D).Modifiers.Add(Swizzle(EInputAxisSwizzle::YXZ));

	FlyMapping->MapKey(LookAction, EKeys::Mouse2D);
	FlyMapping->MapKey(LookAction, EKeys::Gamepad_Right2D);
	FlyMapping->MapKey(FastAction, EKeys::LeftShift);
	FlyMapping->MapKey(FastAction, EKeys::Gamepad_RightShoulder);
	FlyMapping->MapKey(SlowAction, EKeys::LeftControl);
	FlyMapping->MapKey(SlowAction, EKeys::Gamepad_LeftShoulder);
	FlyMapping->MapKey(WheelAction, EKeys::MouseWheelAxis);
}

void ARiptideDevCamera::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARiptideDevCamera::OnMove);
		Input->BindActionValueLambda(MoveAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { MoveInput = FVector::ZeroVector; });
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARiptideDevCamera::OnLook);
		Input->BindActionValueLambda(FastAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bFast = true; });
		Input->BindActionValueLambda(FastAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bFast = false; });
		Input->BindActionValueLambda(SlowAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bSlow = true; });
		Input->BindActionValueLambda(SlowAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bSlow = false; });
		Input->BindAction(WheelAction, ETriggerEvent::Triggered, this, &ARiptideDevCamera::OnWheel);
	}
}

void ARiptideDevCamera::NotifyControllerChanged()
{
	// The flying controls come and go with whoever flies it (before Super, which forgets the previous controller).
	BuildInput();
	auto InputFor = [](AController* C) -> UEnhancedInputLocalPlayerSubsystem*
	{
		const APlayerController* PC = Cast<APlayerController>(C);
		return PC && PC->IsLocalController() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	};
	if (UEnhancedInputLocalPlayerSubsystem* Old = InputFor(PreviousController))
	{
		Old->RemoveMappingContext(FlyMapping);
	}
	if (UEnhancedInputLocalPlayerSubsystem* New = InputFor(Controller))
	{
		New->AddMappingContext(FlyMapping, 50);
	}
	MoveInput = FVector::ZeroVector;
	bFast = bSlow = false;
	Super::NotifyControllerChanged();
}

void ARiptideDevCamera::OnMove(const FInputActionValue& Value)
{
	MoveInput = Value.Get<FVector>().BoundToCube(1.f);
}

void ARiptideDevCamera::OnLook(const FInputActionValue& Value)
{
	LookInput += Value.Get<FVector2D>();
}

void ARiptideDevCamera::OnWheel(const FInputActionValue& Value)
{
	const float Notches = Value.Get<float>();
	if (FMath::IsNearlyZero(Notches))
	{
		return;
	}
	// Flying, the wheel sets the speed; circling or chasing the boat, it zooms (up is closer).
	if (IsFollowingBoat() && (Mode == ERiptideDevCameraMode::Orbit || Mode == ERiptideDevCameraMode::Chase))
	{
		float& Distance = Mode == ERiptideDevCameraMode::Chase ? ChaseDistance : OrbitDistance;
		Distance = FMath::Clamp(Distance * FMath::Pow(0.88f, Notches), MinZoom, MaxZoom);
	}
	else
	{
		SetFlySpeed(FlySpeed * FMath::Pow(1.25f, Notches));
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(SpeedMessageKey, 1.5f, DevNoteColour, GetSpeedText());
	}
}

void ARiptideDevCamera::SetFlyInput(FVector Move)
{
	MoveInput = Move.BoundToCube(1.f);
}

void ARiptideDevCamera::AddLookInput(float YawDeg, float PitchDeg)
{
	LookInput += FVector2D(YawDeg, PitchDeg) / FMath::Max(LookSensitivity, UE_KINDA_SMALL_NUMBER);
}

void ARiptideDevCamera::SetFlySpeed(float CmPerSecond)
{
	FlySpeed = FMath::Clamp(CmPerSecond, MinFlySpeed, MaxFlySpeed);
}

float ARiptideDevCamera::CurrentSpeed() const
{
	return FlySpeed * (bFast ? FastMultiplier : 1.f) * (bSlow ? SlowMultiplier : 1.f);
}

FString ARiptideDevCamera::GetSpeedText() const
{
	if (IsFollowingBoat() && (Mode == ERiptideDevCameraMode::Orbit || Mode == ERiptideDevCameraMode::Chase))
	{
		return FString::Printf(TEXT("%.0f m from the boat"), GetZoomDistance() / 100.f);
	}
	return FString::Printf(TEXT("%.1f m/s (%.0f kn)"), FlySpeed / 100.f, FlySpeed * CmPerSecToKnots);
}

FString ARiptideDevCamera::GetModeName(ERiptideDevCameraMode InMode)
{
	switch (InMode)
	{
	case ERiptideDevCameraMode::RideAlong: return TEXT("Ride along with the boat");
	case ERiptideDevCameraMode::Orbit: return TEXT("Orbit the boat");
	case ERiptideDevCameraMode::Chase: return TEXT("Chase the boat");
	default: return TEXT("Free fly");
	}
}

// --- Modes ---

void ARiptideDevCamera::SetBoat(ARiptideBoat* InBoat)
{
	Boat = InBoat;
	AdoptCurrentPlace();
}

void ARiptideDevCamera::SetMode(ERiptideDevCameraMode InMode)
{
	if (InMode != ERiptideDevCameraMode::Free && !Boat.IsValid())
	{
		InMode = ERiptideDevCameraMode::Free;
	}
	// Glide from the view as it is now into the new mode's.
	BlendFromLocation = GetActorLocation();
	BlendFromRotation = GetActorQuat();
	BlendElapsed = 0.f;
	Mode = InMode;
	AdoptCurrentPlace();
}

void ARiptideDevCamera::KeepWorldPlaceAfterBoatMoved()
{
	AdoptCurrentPlace();
}

float ARiptideDevCamera::BoatYaw() const
{
	return Boat.IsValid() ? Boat->GetActorRotation().Yaw : 0.f;
}

FVector ARiptideDevCamera::BoatLookPoint() const
{
	// About the height of the helm, so the whole boat, T-top and all, sits in the middle of the picture.
	return Boat.IsValid() ? Boat->GetActorLocation() + FVector(0.f, 0.f, 150.f) : GetActorLocation();
}

void ARiptideDevCamera::AdoptCurrentPlace()
{
	const FVector Loc = GetActorLocation();
	const FRotator Rot = GetActorRotation();

	FreeLocation = Loc;
	FreeView = FRotator(FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch), -89.f, 89.f), Rot.Yaw, 0.f);
	FreeVelocity = FVector::ZeroVector;

	const ARiptideBoat* B = Boat.Get();
	if (!B)
	{
		return;
	}
	const FTransform Frame = B->GetActorTransform();
	RideOffset = Frame.InverseTransformPosition(Loc);
	RideYaw = FRotator::NormalizeAxis(Rot.Yaw - BoatYaw());
	RidePitch = FreeView.Pitch;
	RideVelocity = FVector::ZeroVector;

	// Circling from the side of the boat the camera is on now, not too close or too far to see it whole.
	const FVector ToBoat = BoatLookPoint() - Loc;
	const FRotator Aim = ToBoat.Rotation();
	OrbitYaw = FRotator::NormalizeAxis(Aim.Yaw - BoatYaw());
	OrbitPitch = FMath::Clamp(FRotator::NormalizeAxis(Aim.Pitch), -85.f, 20.f);
	OrbitDistance = FMath::Clamp(ToBoat.Size(), 800.f, 6000.f);

	// The chase camera flies from here to its place behind the boat (keeping the angle and distance it last had).
	const FVector Velocity = B->GetVelocity();
	ChaseHeading = Velocity.Size2D() > 150.f ? Velocity.Rotation().Yaw : BoatYaw();
	ChaseLocation = Loc;
}

FVector ARiptideDevCamera::WishDirection(const FRotator& View) const
{
	const FRotationMatrix Axes(View);
	return (Axes.GetUnitAxis(EAxis::X) * MoveInput.X + Axes.GetUnitAxis(EAxis::Y) * MoveInput.Y + FVector::UpVector * MoveInput.Z).GetClampedToMaxSize(1.f);
}

void ARiptideDevCamera::UpdateFree(float Dt, FVector& OutLoc, FRotator& OutRot)
{
	FreeView.Yaw = FRotator::NormalizeAxis(FreeView.Yaw + LookInput.X * LookSensitivity);
	FreeView.Pitch = FMath::Clamp(FreeView.Pitch + LookInput.Y * LookSensitivity, -89.f, 89.f);
	FreeVelocity = FMath::VInterpTo(FreeVelocity, WishDirection(FreeView) * CurrentSpeed(), Dt, FlyResponse);
	FreeLocation += FreeVelocity * Dt;
	OutLoc = FreeLocation;
	OutRot = FreeView;
}

void ARiptideDevCamera::UpdateRideAlong(float Dt, FVector& OutLoc, FRotator& OutRot)
{
	// The camera's place is fixed to the hull (it rolls and pitches with it, so the boat holds still in the picture),
	// but the view stays level, turning only with the boat's heading: riding along at speed doesn't rock the horizon.
	const FTransform Frame = Boat->GetActorTransform();
	RideYaw = FRotator::NormalizeAxis(RideYaw + LookInput.X * LookSensitivity);
	RidePitch = FMath::Clamp(RidePitch + LookInput.Y * LookSensitivity, -89.f, 89.f);
	const FRotator View(RidePitch, BoatYaw() + RideYaw, 0.f);
	const FVector Wish = Frame.InverseTransformVectorNoScale(WishDirection(View) * CurrentSpeed());
	RideVelocity = FMath::VInterpTo(RideVelocity, Wish, Dt, FlyResponse);
	RideOffset += RideVelocity * Dt;
	OutLoc = Frame.TransformPosition(RideOffset);
	OutRot = View;
}

void ARiptideDevCamera::UpdateOrbit(float Dt, FVector& OutLoc, FRotator& OutRot)
{
	// The mouse swings the camera round the boat (and up and down), the wheel zooms. The keys do the same: A and D
	// circle, W and S zoom, E and Q raise and lower it.
	const float KeyRate = KeyOrbitDegPerSec * (bFast ? 2.f : 1.f) * (bSlow ? 0.35f : 1.f) * Dt;
	OrbitYaw = FRotator::NormalizeAxis(OrbitYaw + LookInput.X * LookSensitivity - MoveInput.Y * KeyRate);
	OrbitPitch = FMath::Clamp(OrbitPitch + LookInput.Y * LookSensitivity - MoveInput.Z * KeyRate, -85.f, 20.f);
	OrbitDistance = FMath::Clamp(OrbitDistance * FMath::Exp(-MoveInput.X * KeyZoomPerSec * Dt), MinZoom, MaxZoom);

	const FVector Target = BoatLookPoint();
	OutRot = FRotator(OrbitPitch, BoatYaw() + OrbitYaw, 0.f);
	OutLoc = Target - OutRot.Vector() * OrbitDistance;
	KeepAboveSea(Target, OutLoc, OutRot);
}

void ARiptideDevCamera::UpdateChase(float Dt, FVector& OutLoc, FRotator& OutRot)
{
	// Follows the way the boat is going (its heading, when it's barely moving), swinging round after it as it turns,
	// so a hard turn shows the boat sliding and its spray fanning out.
	const FVector Velocity = Boat->GetVelocity();
	const float Travel = Velocity.Size2D() > 150.f ? Velocity.Rotation().Yaw : BoatYaw();
	ChaseHeading = FRotator::NormalizeAxis(ChaseHeading + FRotator::NormalizeAxis(Travel - ChaseHeading) * (1.f - FMath::Exp(-2.5f * Dt)));

	const float KeyRate = KeyOrbitDegPerSec * (bFast ? 2.f : 1.f) * (bSlow ? 0.35f : 1.f) * Dt;
	ChaseYawOffset = FRotator::NormalizeAxis(ChaseYawOffset + LookInput.X * LookSensitivity - MoveInput.Y * KeyRate);
	ChasePitch = FMath::Clamp(ChasePitch + LookInput.Y * LookSensitivity - MoveInput.Z * KeyRate, -70.f, 5.f);
	ChaseDistance = FMath::Clamp(ChaseDistance * FMath::Exp(-MoveInput.X * KeyZoomPerSec * Dt), MinZoom, MaxZoom);

	// On a spring: it trails a little behind the place it's heading for, so it feels the boat's surges and turns.
	const FVector Target = BoatLookPoint();
	const FVector Wanted = Target - FRotator(ChasePitch, ChaseHeading + ChaseYawOffset, 0.f).Vector() * ChaseDistance;
	ChaseLocation = FMath::VInterpTo(ChaseLocation, Wanted, Dt, 6.f);
	OutLoc = ChaseLocation;
	OutRot = (Target - OutLoc).Rotation();
	KeepAboveSea(Target, OutLoc, OutRot);
	ChaseLocation = OutLoc;
}

void ARiptideDevCamera::KeepAboveSea(const FVector& LookAt, FVector& Loc, FRotator& Rot) const
{
	const float Floor = Boat.IsValid() ? Boat->GetSeaSurfaceZ(Loc) + 60.f : -UE_BIG_NUMBER;
	if (Loc.Z < Floor)
	{
		Loc.Z = Floor;
		Rot = (LookAt - Loc).Rotation();
	}
}

void ARiptideDevCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Real time, not game time: full speed in slow motion and while the world is frozen. Capped, so a hitch doesn't
	// fling the camera.
	const float Dt = FMath::Min(GetWorld()->DeltaRealTimeSeconds, 0.1f);

	if (Mode != ERiptideDevCameraMode::Free && !Boat.IsValid())
	{
		// The boat's gone: carry on flying from here.
		Mode = ERiptideDevCameraMode::Free;
		AdoptCurrentPlace();
	}

	FVector Loc;
	FRotator Rot;
	switch (Mode)
	{
	case ERiptideDevCameraMode::RideAlong: UpdateRideAlong(Dt, Loc, Rot); break;
	case ERiptideDevCameraMode::Orbit: UpdateOrbit(Dt, Loc, Rot); break;
	case ERiptideDevCameraMode::Chase: UpdateChase(Dt, Loc, Rot); break;
	default: UpdateFree(Dt, Loc, Rot); break;
	}
	LookInput = FVector2D::ZeroVector;

	if (BlendElapsed < BlendTime)
	{
		BlendElapsed += Dt;
		const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(BlendElapsed / BlendTime, 0.f, 1.f));
		Loc = FMath::Lerp(BlendFromLocation, Loc, Alpha);
		Rot = FQuat::Slerp(BlendFromRotation, Rot.Quaternion(), Alpha).Rotator();
	}
	SetActorLocationAndRotation(Loc, Rot);
}
