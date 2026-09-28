#include "DeadwaterBoat.h"

#include "BuoyancyComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Unreal works in centimetres, so forces in Newtons are scaled by 100 (kg*cm/s^2).
	constexpr float NewtonsToUnreal = 100.f;
	constexpr float CmPerSecToKnots = 0.0194384f;

	// Hull size in cm (length, beam, depth) for the placeholder skiff.
	const FVector HullExtent(300.f, 110.f, 35.f);
}

ADeadwaterBoat::ADeadwaterBoat()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// The physics body is an unscaled box so pontoon and propeller offsets stay in real centimetres.
	HullBody = CreateDefaultSubobject<UBoxComponent>(TEXT("HullBody"));
	HullBody->SetBoxExtent(HullExtent);
	HullBody->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	HullBody->SetSimulatePhysics(true);
	HullBody->SetMassOverrideInKg(NAME_None, HullMassKg, true);
	HullBody->SetLinearDamping(0.f);
	HullBody->SetAngularDamping(0.5f);
	RootComponent = HullBody;

	// Placeholder visuals until real boat models exist.
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(HullBody);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->SetRelativeScale3D(HullExtent / 50.f);
	if (CubeMesh.Succeeded())
	{
		HullMesh->SetStaticMesh(CubeMesh.Object);
	}

	MotorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorMesh"));
	MotorMesh->SetupAttachment(HullBody);
	MotorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MotorMesh->SetRelativeLocation(FVector(-HullExtent.X - 15.f, 0.f, -10.f));
	MotorMesh->SetRelativeScale3D(FVector(0.25f, 0.25f, 0.9f));
	if (CylinderMesh.Succeeded())
	{
		MotorMesh->SetStaticMesh(CylinderMesh.Object);
	}

	// Propeller sits below the transom, under the waterline when the boat is level.
	Propeller = CreateDefaultSubobject<USceneComponent>(TEXT("Propeller"));
	Propeller->SetupAttachment(HullBody);
	Propeller->SetRelativeLocation(FVector(-HullExtent.X - 15.f, 0.f, -HullExtent.Z - 25.f));

	// Standing at the helm, toward the stern.
	HelmCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HelmCamera"));
	HelmCamera->SetupAttachment(HullBody);
	HelmCamera->SetRelativeLocation(FVector(-HullExtent.X * 0.5f, 0.f, HullExtent.Z + 150.f));
	HelmCamera->bUsePawnControlRotation = false;

	// Pontoons: four corners, two amidships, one at the stern that also tells us if the prop is wet.
	Buoyancy = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("Buoyancy"));
	const float PontoonRadius = 60.f;
	const float PontoonZ = -HullExtent.Z * 0.5f;
	const FVector PontoonOffsets[] = {
		FVector(HullExtent.X * 0.8f, HullExtent.Y * 0.6f, PontoonZ),
		FVector(HullExtent.X * 0.8f, -HullExtent.Y * 0.6f, PontoonZ),
		FVector(0.f, HullExtent.Y * 0.7f, PontoonZ),
		FVector(0.f, -HullExtent.Y * 0.7f, PontoonZ),
		FVector(-HullExtent.X * 0.8f, HullExtent.Y * 0.6f, PontoonZ),
		FVector(-HullExtent.X * 0.8f, -HullExtent.Y * 0.6f, PontoonZ),
		FVector(-HullExtent.X, 0.f, PontoonZ),
	};
	for (const FVector& Offset : PontoonOffsets)
	{
		FSphericalPontoon Pontoon;
		Pontoon.RelativeLocation = Offset;
		Pontoon.Radius = PontoonRadius;
		Buoyancy->BuoyancyData.Pontoons.Add(Pontoon);
	}
	SternPontoonIndex = Buoyancy->BuoyancyData.Pontoons.Num() - 1;
}

void ADeadwaterBoat::BeginPlay()
{
	Super::BeginPlay();

	HullBody->SetMassOverrideInKg(NAME_None, HullMassKg, true);
	if (HasAuthority())
	{
		FuelLiters = FuelCapacityLiters;
	}
}

void ADeadwaterBoat::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADeadwaterBoat, ThrottleLever);
	DOREPLIFETIME(ADeadwaterBoat, EngineOutput);
	DOREPLIFETIME(ADeadwaterBoat, SteerAngleDeg);
	DOREPLIFETIME(ADeadwaterBoat, FuelLiters);
	DOREPLIFETIME(ADeadwaterBoat, EngineHealth);
}

// --- Input ---

void ADeadwaterBoat::BuildInput()
{
	if (HelmMapping)
	{
		return;
	}

	// Built in code so the project runs without any input assets authored in the editor.
	ThrottleAction = NewObject<UInputAction>(this, TEXT("IA_Throttle"));
	ThrottleAction->ValueType = EInputActionValueType::Axis1D;

	SteerAction = NewObject<UInputAction>(this, TEXT("IA_Steer"));
	SteerAction->ValueType = EInputActionValueType::Axis1D;

	CutThrottleAction = NewObject<UInputAction>(this, TEXT("IA_CutThrottle"));
	CutThrottleAction->ValueType = EInputActionValueType::Boolean;

	LookAction = NewObject<UInputAction>(this, TEXT("IA_Look"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	HelmMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Helm"));

	HelmMapping->MapKey(ThrottleAction, EKeys::W);
	FEnhancedActionKeyMapping& ThrottleDown = HelmMapping->MapKey(ThrottleAction, EKeys::S);
	ThrottleDown.Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));

	HelmMapping->MapKey(SteerAction, EKeys::D);
	FEnhancedActionKeyMapping& SteerLeft = HelmMapping->MapKey(SteerAction, EKeys::A);
	SteerLeft.Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));

	HelmMapping->MapKey(CutThrottleAction, EKeys::X);
	HelmMapping->MapKey(LookAction, EKeys::Mouse2D);

	// Gamepad: right trigger / left trigger for throttle, left stick to steer, right stick to look.
	HelmMapping->MapKey(ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	FEnhancedActionKeyMapping& PadReverse = HelmMapping->MapKey(ThrottleAction, EKeys::Gamepad_LeftTriggerAxis);
	PadReverse.Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));
	HelmMapping->MapKey(SteerAction, EKeys::Gamepad_LeftX);
	HelmMapping->MapKey(CutThrottleAction, EKeys::Gamepad_FaceButton_Right);
	HelmMapping->MapKey(LookAction, EKeys::Gamepad_Right2D);
}

void ADeadwaterBoat::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	BuildInput();

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(HelmMapping, 0);
		}
	}

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ADeadwaterBoat::OnThrottle);
		Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ADeadwaterBoat::OnThrottleReleased);
		Input->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ADeadwaterBoat::OnSteer);
		Input->BindAction(SteerAction, ETriggerEvent::Completed, this, &ADeadwaterBoat::OnSteerReleased);
		Input->BindAction(CutThrottleAction, ETriggerEvent::Started, this, &ADeadwaterBoat::OnCutThrottle);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADeadwaterBoat::OnLook);
	}
}

void ADeadwaterBoat::OnThrottle(const FInputActionValue& Value)
{
	ThrottleInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ADeadwaterBoat::OnThrottleReleased(const FInputActionValue& Value)
{
	ThrottleInput = 0.f;
}

void ADeadwaterBoat::OnSteer(const FInputActionValue& Value)
{
	SteerInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ADeadwaterBoat::OnSteerReleased(const FInputActionValue& Value)
{
	SteerInput = 0.f;
}

void ADeadwaterBoat::OnCutThrottle(const FInputActionValue& Value)
{
	bCutThrottleRequested = true;
}

void ADeadwaterBoat::OnLook(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>();
	LookYaw = FMath::Clamp(LookYaw + Delta.X * LookSensitivity, -170.f, 170.f);
	LookPitch = FMath::Clamp(LookPitch + Delta.Y * LookSensitivity, -70.f, 70.f);
	HelmCamera->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));
}

void ADeadwaterBoat::ServerSetControls_Implementation(float InThrottleInput, float InSteerInput, bool bInCutThrottle)
{
	ThrottleInput = FMath::Clamp(InThrottleInput, -1.f, 1.f);
	SteerInput = FMath::Clamp(InSteerInput, -1.f, 1.f);
	bCutThrottleRequested |= bInCutThrottle;
}

// --- Simulation ---

void ADeadwaterBoat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled() && !HasAuthority())
	{
		ServerSetControls(ThrottleInput, SteerInput, bCutThrottleRequested);
		bCutThrottleRequested = false;
	}

	if (HasAuthority())
	{
		UpdateControls(DeltaSeconds);
		UpdateEngine(DeltaSeconds);
		ApplyThrust();
		ApplyHydrodynamics();
	}

	if (IsLocallyControlled())
	{
		DrawDebugHud();
	}
}

void ADeadwaterBoat::UpdateControls(float DeltaSeconds)
{
	if (bCutThrottleRequested)
	{
		ThrottleLever = 0.f;
		bCutThrottleRequested = false;
	}

	// The lever moves while the key is held and stays put when released, like a real throttle.
	ThrottleLever = FMath::Clamp(ThrottleLever + ThrottleInput * ThrottleLeverRate * DeltaSeconds, -1.f, 1.f);

	// The motor swings back to centre when the wheel is let go.
	const float TargetSteer = SteerInput * MaxSteerAngleDeg;
	SteerAngleDeg = FMath::FInterpConstantTo(SteerAngleDeg, TargetSteer, DeltaSeconds, SteerRateDeg);
}

void ADeadwaterBoat::UpdateEngine(float DeltaSeconds)
{
	float Target = ThrottleLever;

	if (FuelLiters <= 0.f || EngineHealth <= 0.f)
	{
		Target = 0.f;
	}
	else if (EngineHealth < 0.5f)
	{
		// A damaged engine cuts out at random, more often the worse it is.
		if (SputterTimeLeft > 0.f)
		{
			SputterTimeLeft -= DeltaSeconds;
			Target = 0.f;
		}
		else if (FMath::FRand() < (0.5f - EngineHealth) * 2.f * DeltaSeconds)
		{
			SputterTimeLeft = FMath::FRandRange(0.3f, 1.5f);
		}
	}

	EngineOutput = FMath::FInterpTo(EngineOutput, Target, DeltaSeconds, EngineSpoolRate);
	FuelLiters = FMath::Max(0.f, FuelLiters - FMath::Abs(EngineOutput) * FuelBurnPerSecond * DeltaSeconds);
}

bool ADeadwaterBoat::IsPropellerSubmerged() const
{
	if (!Buoyancy || !Buoyancy->IsInWaterBody() || !Buoyancy->BuoyancyData.Pontoons.IsValidIndex(SternPontoonIndex))
	{
		return false;
	}

	const FSphericalPontoon& Stern = Buoyancy->BuoyancyData.Pontoons[SternPontoonIndex];
	return Stern.bIsInWater && Propeller->GetComponentLocation().Z < Stern.WaterHeight;
}

void ADeadwaterBoat::ApplyThrust()
{
	if (FMath::IsNearlyZero(EngineOutput, 0.001f) || !IsPropellerSubmerged())
	{
		return;
	}

	const float Scale = EngineOutput > 0.f ? 1.f : ReverseThrustScale;
	const float ThrustN = MaxThrust * EngineOutput * Scale;

	// Steering right swings the prop so it pushes the stern left, which turns the bow right.
	const FVector Up = HullBody->GetUpVector();
	const FVector ThrustDir = HullBody->GetForwardVector().RotateAngleAxis(-SteerAngleDeg, Up);

	HullBody->AddForceAtLocation(ThrustDir * ThrustN * NewtonsToUnreal, Propeller->GetComponentLocation());
}

void ADeadwaterBoat::ApplyHydrodynamics()
{
	if (!Buoyancy || !Buoyancy->IsInWaterBody())
	{
		return;
	}

	// Quadratic water drag in the hull's frame: slippery going forward, stubborn going sideways.
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector LocalVelMs = Xf.InverseTransformVectorNoScale(HullBody->GetPhysicsLinearVelocity()) / 100.f;

	const FVector LocalDragN(
		-ForwardDrag * LocalVelMs.X * FMath::Abs(LocalVelMs.X),
		-LateralDrag * LocalVelMs.Y * FMath::Abs(LocalVelMs.Y),
		0.f);

	HullBody->AddForce(Xf.TransformVectorNoScale(LocalDragN) * NewtonsToUnreal);

	// Resist spinning in place.
	const FVector Up = HullBody->GetUpVector();
	const float YawRate = FVector::DotProduct(HullBody->GetPhysicsAngularVelocityInRadians(), Up);
	HullBody->AddTorqueInRadians(-Up * YawRate * YawDamping, NAME_None, true);
}

float ADeadwaterBoat::GetSpeedKnots() const
{
	return HullBody->GetComponentVelocity().Size() * CmPerSecToKnots;
}

void ADeadwaterBoat::ApplyEngineDamage(float Amount)
{
	if (HasAuthority())
	{
		EngineHealth = FMath::Clamp(EngineHealth - Amount, 0.f, 1.f);
	}
}

void ADeadwaterBoat::DrawDebugHud() const
{
	if (!GEngine)
	{
		return;
	}

	// Temporary readout for tuning the handling; replaced by real gauges later.
	const uint64 KeyBase = 0xDEAD0000ull;
	GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
		FString::Printf(TEXT("Speed %.1f kn   Throttle %+.0f%%   Engine %+.0f%%"),
			GetSpeedKnots(), ThrottleLever * 100.f, EngineOutput * 100.f));
	GEngine->AddOnScreenDebugMessage(KeyBase + 1, 0.f, FColor::White,
		FString::Printf(TEXT("Motor %+.0f deg   Fuel %.1f L   Engine health %.0f%%"),
			SteerAngleDeg, FuelLiters, EngineHealth * 100.f));
	GEngine->AddOnScreenDebugMessage(KeyBase + 2, 0.f, IsPropellerSubmerged() ? FColor::Green : FColor::Red,
		IsPropellerSubmerged() ? TEXT("Prop in water") : TEXT("Prop out of water"));
}
