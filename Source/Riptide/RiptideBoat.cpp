#include "RiptideBoat.h"

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
#include "Kismet/GameplayStatics.h"
#include "UObject/StructOnScope.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Unreal works in centimetres, so forces in Newtons are scaled by 100 (kg*cm/s^2).
	constexpr float NewtonsToUnreal = 100.f;
	constexpr float CmPerSecToKnots = 0.0194384f;

	// Hull size in cm (length, beam, depth) for the placeholder skiff.
	const FVector HullExtent(300.f, 110.f, 35.f);

	// Buoyancy pontoons, in cm. They sit only a quarter of their height in the water at rest, so the hull
	// has about six times its resting lift in reserve: a bow driven into a swell gets pushed back up hard
	// instead of burying. The waterline sits 20 cm up the hull.
	constexpr float PontoonRadius = 60.f;
	constexpr float PontoonRestDepth = 30.f;
	const float WaterlineZ = -HullExtent.Z + 20.f;
}

ARiptideBoat::ARiptideBoat()
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
	// Set the override directly: SetMassOverrideInKg recalculates mass, which can't run during CDO construction.
	HullBody->BodyInstance.SetMassOverride(HullMassKg, true);
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

	// Under the hull bottom, so the wake simulation sees it below the surface.
	WakeSource = CreateDefaultSubobject<USceneComponent>(TEXT("WakeSource"));
	WakeSource->SetupAttachment(HullBody);
	WakeSource->SetRelativeLocation(FVector(0.f, 0.f, -HullExtent.Z));

	// Weight sits low and aft (engine, fuel, crew on the floor), which keeps the hull from rolling over.
	HullBody->BodyInstance.COMNudge = FVector(-30.f, 0.f, -30.f);

	// Pontoons run down both sides, where the hull's width resists rolling, plus one at the stern
	// that also tells us if the prop is wet. BeginPlay sizes their lift so they float PontoonRestDepth deep.
	Buoyancy = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("Buoyancy"));
	const float PontoonZ = WaterlineZ - PontoonRestDepth + PontoonRadius;
	const float ChineY = HullExtent.Y * 0.6f;
	const FVector PontoonOffsets[] = {
		FVector(HullExtent.X * 0.8f, ChineY, PontoonZ),
		FVector(HullExtent.X * 0.8f, -ChineY, PontoonZ),
		FVector(HullExtent.X * 0.27f, ChineY, PontoonZ),
		FVector(HullExtent.X * 0.27f, -ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.27f, ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.27f, -ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.8f, ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.8f, -ChineY, PontoonZ),
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
	BowPontoonIndex = 0;
}

void ARiptideBoat::BeginPlay()
{
	// Size buoyancy to the hull's mass before the buoyancy component starts (it begins play inside Super).
	// The engine spreads one pontoon's worth of lift across all pontoons (their coefficients sum to 1),
	// so lift = submerged volume of one pontoon * BuoyancyCoefficient. Pick the coefficient that holds the
	// boat up with the pontoons PontoonRestDepth under (a spherical cap), which puts the waterline at WaterlineZ.
	{
		const float R = PontoonRadius;
		const float D = PontoonRestDepth;
		const float RestVolumeCm3 = (UE_PI / 3.f) * D * D * (3.f * R - D);
		const float WeightUnreal = HullMassKg * FMath::Abs(GetWorld()->GetGravityZ());
		Buoyancy->BuoyancyData.BuoyancyCoefficient = WeightUnreal / RestVolumeCm3;
		// The engine clamps each pontoon's force; leave room for a fully buried pontoon's full reserve.
		Buoyancy->BuoyancyData.MaxBuoyantForce = WeightUnreal * 20.f;
	}

	Super::BeginPlay();

	HullBody->SetMassOverrideInKg(NAME_None, HullMassKg, true);
	if (HasAuthority())
	{
		FuelLiters = FuelCapacityLiters;
	}

	// The wake is purely visual, so every machine registers its own copy of the boat.
	RegisterWithWakeSimulation();
}

void ARiptideBoat::RegisterWithWakeSimulation()
{
	// The Water plugin's fluid simulation (BP_FluidSim_01) ripples the water surface around the local player.
	// It's a Blueprint, so its "Register Dynamic Force" function and FluidForceDynamic struct are reached
	// through reflection, matching the struct's fields by their name prefix.
	static const TCHAR* SimClassPath = TEXT("/Water/FluidSimulation/Blueprints/BP_FluidSim_01.BP_FluidSim_01_C");
	UClass* SimClass = LoadClass<AActor>(nullptr, SimClassPath);
	AActor* Sim = SimClass ? UGameplayStatics::GetActorOfClass(this, SimClass) : nullptr;
	if (!Sim)
	{
		return;
	}

	UFunction* Register = Sim->FindFunction(TEXT("Register Dynamic Force"));
	if (!Register)
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: wake simulation has no 'Register Dynamic Force'; no wake for %s"), *GetName());
		return;
	}

	auto SetNumber = [](FProperty* Prop, void* Container, double Value)
	{
		if (FNumericProperty* Num = CastField<FNumericProperty>(Prop))
		{
			void* Ptr = Num->ContainerPtrToValuePtr<void>(Container);
			if (Num->IsFloatingPoint())
			{
				Num->SetFloatingPointPropertyValue(Ptr, Value);
			}
		}
	};
	auto SetObject = [](FProperty* Prop, void* Container, UObject* Value)
	{
		if (FObjectPropertyBase* Obj = CastField<FObjectPropertyBase>(Prop))
		{
			Obj->SetObjectPropertyValue(Obj->ContainerPtrToValuePtr<void>(Container), Value);
		}
	};

	FStructOnScope Params(Register);
	uint8* ParamMemory = Params.GetStructMemory();
	bool bFilledForce = false;
	for (TFieldIterator<FProperty> It(Register); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		const FString ParamName = Param->GetName();
		if (FStructProperty* ForceParam = CastField<FStructProperty>(Param))
		{
			void* Force = ForceParam->ContainerPtrToValuePtr<void>(ParamMemory);
			for (TFieldIterator<FProperty> Field(ForceParam->Struct); Field; ++Field)
			{
				const FString FieldName = Field->GetName();
				if (FieldName.StartsWith(TEXT("ForceRadius")))
				{
					SetNumber(*Field, Force, WakeRadius);
				}
				else if (FieldName.StartsWith(TEXT("ForceStrength")))
				{
					SetNumber(*Field, Force, WakeStrength);
				}
				else if (FieldName.StartsWith(TEXT("ForceComponent")))
				{
					SetObject(*Field, Force, WakeSource);
					bFilledForce = true;
				}
			}
		}
		else if (ParamName.StartsWith(TEXT("Tracked")))
		{
			SetObject(Param, ParamMemory, WakeSource);
		}
		else if (ParamName.StartsWith(TEXT("WaterLevel")))
		{
			// Sea level. The test maps put the ocean surface at Z = 0.
			SetNumber(Param, ParamMemory, 0.0);
		}
	}

	if (!bFilledForce)
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: wake simulation's force struct didn't match; no wake for %s"), *GetName());
		return;
	}
	Sim->ProcessEvent(Register, ParamMemory);
	UE_LOG(LogTemp, Log, TEXT("Riptide: %s registered with the wake simulation"), *GetName());
}

void ARiptideBoat::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARiptideBoat, ThrottleLever);
	DOREPLIFETIME(ARiptideBoat, EngineOutput);
	DOREPLIFETIME(ARiptideBoat, SteerAngleDeg);
	DOREPLIFETIME(ARiptideBoat, FuelLiters);
	DOREPLIFETIME(ARiptideBoat, EngineHealth);
}

// --- Input ---

void ARiptideBoat::BuildInput()
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

void ARiptideBoat::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
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
		Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnThrottle);
		Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnThrottleReleased);
		Input->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnSteer);
		Input->BindAction(SteerAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnSteerReleased);
		Input->BindAction(CutThrottleAction, ETriggerEvent::Started, this, &ARiptideBoat::OnCutThrottle);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnLook);
	}
}

void ARiptideBoat::OnThrottle(const FInputActionValue& Value)
{
	ThrottleInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ARiptideBoat::OnThrottleReleased(const FInputActionValue& Value)
{
	ThrottleInput = 0.f;
}

void ARiptideBoat::OnSteer(const FInputActionValue& Value)
{
	SteerInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ARiptideBoat::OnSteerReleased(const FInputActionValue& Value)
{
	SteerInput = 0.f;
}

void ARiptideBoat::OnCutThrottle(const FInputActionValue& Value)
{
	bCutThrottleRequested = true;
}

void ARiptideBoat::OnLook(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>();
	LookYaw = FMath::Clamp(LookYaw + Delta.X * LookSensitivity, -170.f, 170.f);
	LookPitch = FMath::Clamp(LookPitch + Delta.Y * LookSensitivity, -70.f, 70.f);
	HelmCamera->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));
}

void ARiptideBoat::ServerSetControls_Implementation(float InThrottleInput, float InSteerInput, bool bInCutThrottle)
{
	ThrottleInput = FMath::Clamp(InThrottleInput, -1.f, 1.f);
	SteerInput = FMath::Clamp(InSteerInput, -1.f, 1.f);
	bCutThrottleRequested |= bInCutThrottle;
}

// --- Simulation ---

void ARiptideBoat::Tick(float DeltaSeconds)
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

void ARiptideBoat::UpdateControls(float DeltaSeconds)
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

void ARiptideBoat::UpdateEngine(float DeltaSeconds)
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

bool ARiptideBoat::IsPropellerSubmerged() const
{
	if (!Buoyancy || !Buoyancy->IsInWaterBody() || !Buoyancy->BuoyancyData.Pontoons.IsValidIndex(SternPontoonIndex))
	{
		return false;
	}

	// Compare the prop with the water surface at the stern directly. The stern pontoon itself can sit
	// clear of the water while the prop, which hangs lower, is still under.
	const FSphericalPontoon& Stern = Buoyancy->BuoyancyData.Pontoons[SternPontoonIndex];
	return Propeller->GetComponentLocation().Z < Stern.WaterHeight;
}

void ARiptideBoat::ApplyThrust()
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

void ARiptideBoat::ApplyHydrodynamics()
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
		-HeaveDamping * LocalVelMs.Z);

	HullBody->AddForce(Xf.TransformVectorNoScale(LocalDragN) * NewtonsToUnreal);

	// Planing lift: water striking the forward hull bottom pushes up ahead of the centre of mass,
	// so the bow trims up with speed. Only going forward, and capped so crests don't launch the boat.
	// It only exists where the hull is in the water: scaled by how deep the forward bottom sits compared
	// with its resting draft, so a bow lifting clear (or leaving a crest) loses the lift and settles back.
	if (LocalVelMs.X > 0.f)
	{
		const FVector LiftPoint = Xf.TransformPosition(FVector(HullExtent.X * 0.3f, 0.f, -HullExtent.Z));

		float WaterHeightSum = 0.f;
		int32 NumForward = 0;
		for (const FSphericalPontoon& Pontoon : Buoyancy->BuoyancyData.Pontoons)
		{
			if (Pontoon.RelativeLocation.X > 0.f)
			{
				WaterHeightSum += Pontoon.WaterHeight;
				++NumForward;
			}
		}
		const float RestDraft = WaterlineZ + HullExtent.Z;
		const float Wetness = NumForward > 0
			? FMath::Clamp((WaterHeightSum / NumForward - LiftPoint.Z) / RestDraft, 0.f, 1.f)
			: 0.f;

		const float WeightN = HullMassKg * FMath::Abs(GetWorld()->GetGravityZ()) / 100.f;
		const float LiftN = Wetness * FMath::Min(PlaningLift * LocalVelMs.X * LocalVelMs.X, WeightN * MaxPlaningLiftFraction);
		HullBody->AddForceAtLocation(Xf.GetUnitAxis(EAxis::Z) * LiftN * NewtonsToUnreal, LiftPoint);
	}

	// Resist spinning in place, and resist rocking so the hull settles after a wave instead of building up a roll.
	const FVector Up = HullBody->GetUpVector();
	const FVector AngVel = HullBody->GetPhysicsAngularVelocityInRadians();
	const float YawRate = FVector::DotProduct(AngVel, Up);
	const FVector RockRate = AngVel - Up * YawRate;
	HullBody->AddTorqueInRadians(-Up * YawRate * YawDamping - RockRate * RockDamping, NAME_None, true);
}

float ARiptideBoat::GetBowFreeboardCm() const
{
	if (!Buoyancy || !Buoyancy->BuoyancyData.Pontoons.IsValidIndex(BowPontoonIndex))
	{
		return 0.f;
	}
	const FSphericalPontoon& Bow = Buoyancy->BuoyancyData.Pontoons[BowPontoonIndex];
	const FVector BowDeckEdge = HullBody->GetComponentTransform().TransformPosition(FVector(HullExtent.X, 0.f, HullExtent.Z));
	return BowDeckEdge.Z - Bow.WaterHeight;
}

float ARiptideBoat::GetSpeedKnots() const
{
	return HullBody->GetComponentVelocity().Size() * CmPerSecToKnots;
}

void ARiptideBoat::ApplyEngineDamage(float Amount)
{
	if (HasAuthority())
	{
		EngineHealth = FMath::Clamp(EngineHealth - Amount, 0.f, 1.f);
	}
}

void ARiptideBoat::SetHelmInput(float Throttle, float Steer)
{
	if (HasAuthority())
	{
		ThrottleInput = FMath::Clamp(Throttle, -1.f, 1.f);
		SteerInput = FMath::Clamp(Steer, -1.f, 1.f);
	}
}

void ARiptideBoat::DrawDebugHud() const
{
	if (!GEngine)
	{
		return;
	}

	// Temporary readout for tuning the handling; replaced by real gauges later.
	const uint64 KeyBase = 0x52495054ull;
	GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
		FString::Printf(TEXT("Speed %.1f kn   Throttle %+.0f%%   Engine %+.0f%%"),
			GetSpeedKnots(), ThrottleLever * 100.f, EngineOutput * 100.f));
	GEngine->AddOnScreenDebugMessage(KeyBase + 1, 0.f, FColor::White,
		FString::Printf(TEXT("Motor %+.0f deg   Fuel %.1f L   Engine health %.0f%%"),
			SteerAngleDeg, FuelLiters, EngineHealth * 100.f));
	GEngine->AddOnScreenDebugMessage(KeyBase + 2, 0.f, IsPropellerSubmerged() ? FColor::Green : FColor::Red,
		IsPropellerSubmerged() ? TEXT("Prop in water") : TEXT("Prop out of water"));
}
