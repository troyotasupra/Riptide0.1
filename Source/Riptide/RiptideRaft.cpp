#include "RiptideRaft.h"

#include "BuoyancyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"
#include "RiptideItemIcons.h"
#include "RiptideSea.h"
#include "RiptideStorageComponent.h"

#define LOCTEXT_NAMESPACE "RiptideRaft"

namespace
{
	// Floating: spherical pontoons at the corners and amidships, sized in BeginPlay to hold the raft up with its
	// logs half under (a log is 21.7 cm across... in radius: the deck stands 28 cm clear of the water).
	constexpr float PontoonRadius = 45.f;
	constexpr float PontoonRestDepth = 24.f;
	constexpr float WaterlineAboveBottom = 22.f;

	// Rowing, after the Godot build's rafts (boats/boat.gd, row_math.gd): about a metre and a half a second flat out.
	constexpr float RowForceN = 900.f;
	constexpr float RowTorqueNm = 450.f;     // about 20 degrees a second, one oar alone
	constexpr float ForwardDrag = 400.f;      // N per (m/s)^2 along its length
	constexpr float LateralDrag = 2000.f;     // and sideways: logs don't slip sideways easily
	constexpr float YawDamping = 1200.f;      // N m per rad/s
	constexpr float NewtonsToUnreal = 100.f;  // kg cm/s^2

	// The oars: where the oarlock holds one along it (cm from its handle end), how far a stroke sweeps, how fast.
	constexpr float OarInboard = 90.f;
	constexpr float StrokeSweepDeg = 32.f;
	constexpr float StrokesPerSecond = 0.75f;
	// How far the oars slope down from their locks to the water (the blade dips in, the handles come up to the hands).
	constexpr float OarDipDeg = -22.f;

	const TCHAR* RaftMeshPath = TEXT("/Game/Riptide/Items/SM_Vessel_raft.SM_Vessel_raft");
}

ARiptideRaft::ARiptideRaft()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetCullDistanceSquared(FMath::Square(100000.f));

	// The body is the raft's own box: it floats, the crew stand on its top (the deck) and it grounds on the sand.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetBoxExtent(FVector(Length * 0.5f, Beam * 0.5f, Height * 0.5f));
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Body->SetSimulatePhysics(true);
	Body->BodyInstance.SetMassOverride(MassKg, true);
	Body->SetLinearDamping(0.05f);
	Body->SetAngularDamping(0.6f);
	Body->BodyInstance.SleepFamily = ESleepFamily::Custom;
	Body->BodyInstance.CustomSleepThresholdMultiplier = 0.f;
	// Weight low (wet logs), so it rights itself.
	Body->BodyInstance.COMNudge = FVector(0.f, 0.f, -12.f);
	RootComponent = Body;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Body);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, -Height * 0.5f));

	auto MakeOar = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Oar = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Oar->SetupAttachment(Body);
		Oar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Oar->SetVisibility(false);
		return Oar;
	};
	OarLeft = MakeOar(TEXT("OarLeft"));
	OarRight = MakeOar(TEXT("OarRight"));

	auto MakeLock = [this](const TCHAR* Name, float Side)
	{
		UBoxComponent* Lock = CreateDefaultSubobject<UBoxComponent>(Name);
		Lock->SetupAttachment(Body);
		Lock->SetCollisionProfileName(TEXT("RiptideHarvest"));
		Lock->SetBoxExtent(FVector(40.f, 18.f, 25.f));
		Lock->SetRelativeLocation(FVector(0.f, Side * OarlockY, Height * 0.5f + 15.f));
		return Lock;
	};
	LockLeft = MakeLock(TEXT("LockLeft"), -1.f);
	LockRight = MakeLock(TEXT("LockRight"), 1.f);

	Buoyancy = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("Buoyancy"));
	Buoyancy->BuoyancyData.bCenterPontoonsOnCOM = false;
	const float PontoonZ = -Height * 0.5f + WaterlineAboveBottom - PontoonRestDepth + PontoonRadius;
	for (const FVector2D& At : { FVector2D(125.f, 85.f), FVector2D(125.f, -85.f), FVector2D(-125.f, 85.f), FVector2D(-125.f, -85.f), FVector2D(0.f, 0.f) })
	{
		FSphericalPontoon Pontoon;
		Pontoon.RelativeLocation = FVector(At.X, At.Y, PontoonZ);
		Pontoon.Radius = PontoonRadius;
		Buoyancy->BuoyancyData.Pontoons.Add(Pontoon);
	}
}

void ARiptideRaft::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideRaft, bOarsFitted);
	DOREPLIFETIME(ARiptideRaft, Rower);
	DOREPLIFETIME(ARiptideRaft, RowForward);
	DOREPLIFETIME(ARiptideRaft, RowTurn);
}

void ARiptideRaft::BeginPlay()
{
	// Lift sized to the raft's weight before the buoyancy component starts (inside Super): one pontoon's submerged
	// cap times the coefficient holds it all up (the engine shares the lift out between the pontoons).
	{
		const float R = PontoonRadius;
		const float D = PontoonRestDepth;
		const float RestVolumeCm3 = (UE_PI / 3.f) * D * D * (3.f * R - D);
		const float WeightUnreal = MassKg * FMath::Abs(GetWorld()->GetGravityZ());
		Buoyancy->BuoyancyData.BuoyancyCoefficient = WeightUnreal / RestVolumeCm3;
		Buoyancy->BuoyancyData.MaxBuoyantForce = WeightUnreal * 20.f;
	}
	Super::BeginPlay();
	Body->SetMassOverrideInKg(NAME_None, MassKg, true);
	if (UStaticMesh* Model = LoadObject<UStaticMesh>(nullptr, RaftMeshPath))
	{
		Mesh->SetStaticMesh(Model);
	}
	if (UStaticMesh* Oar = URiptideItemIconSubsystem::MeshFor(TEXT("oar")))
	{
		OarLeft->SetStaticMesh(Oar);
		OarRight->SetStaticMesh(Oar);
		OarLength = Oar->GetBoundingBox().GetSize().X;
	}
	OnRep_Oars();
}

ARiptideRaft* ARiptideRaft::Launch(UWorld* World, const FTransform& Where)
{
	if (!World)
	{
		return nullptr;
	}
	// The nearest water deep enough to float it (the logs draw about a quarter metre), out from the site.
	const FVector From = Where.GetLocation();
	float BestDistance = MaxLaunchDistance + 1.f;
	FVector BestSpot = FVector::ZeroVector;
	float BestYaw = 0.f;
	for (int32 Bearing = 0; Bearing < 16; ++Bearing)
	{
		const float Yaw = Bearing * 22.5f;
		const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
		for (float Out = 200.f; Out <= MaxLaunchDistance && Out < BestDistance; Out += 50.f)
		{
			const FVector Spot = From + Dir * Out;
			if (URiptideSeaSubsystem::GroundHeightAt(World, Spot) < -70.f)
			{
				// A little further, so the whole raft is in the water and not on the shelving sand.
				BestDistance = Out;
				BestSpot = From + Dir * (Out + Length * 0.6f);
				BestYaw = Yaw;
				break;
			}
		}
	}
	if (BestDistance > MaxLaunchDistance)
	{
		return nullptr;
	}
	BestSpot.Z = URiptideSeaSubsystem::SeaSurfaceAt(World, BestSpot) + Height * 0.5f;
	return Restore(World, FTransform(FRotator(0.f, BestYaw, 0.f), BestSpot), false);
}

ARiptideRaft* ARiptideRaft::SpawnRaft(UObject* WorldContextObject, FTransform Where, bool bOars)
{
	return Restore(WorldContextObject ? WorldContextObject->GetWorld() : nullptr, Where, bOars);
}

ARiptideRaft* ARiptideRaft::Restore(UWorld* World, const FTransform& Where, bool bOars)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideRaft* Raft = World ? World->SpawnActor<ARiptideRaft>(Where.GetLocation(), Where.Rotator(), Params) : nullptr;
	if (Raft)
	{
		Raft->bOarsFitted = bOars;
		Raft->OnRep_Oars();
	}
	return Raft;
}

bool ARiptideRaft::IsAfloat() const
{
	if (!Buoyancy || !Buoyancy->IsInWaterBody())
	{
		return false;
	}
	return URiptideSeaSubsystem::GroundHeightAt(this, GetActorLocation()) < GetActorLocation().Z - Height * 0.5f - 15.f;
}

bool ARiptideRaft::IsAboard(const ARiptideCharacter* Who) const
{
	if (!Who)
	{
		return false;
	}
	if (Who->GetRowingRaft() == this)
	{
		return true;
	}
	const UCharacterMovementComponent* Move = Who->GetCharacterMovement();
	if (Move && Move->GetMovementBase() && Move->GetMovementBase()->GetOwner() == this)
	{
		return true;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Who->GetActorLocation());
	return FMath::Abs(Local.X) < Length * 0.5f + 10.f && FMath::Abs(Local.Y) < Beam * 0.5f + 10.f && Local.Z > 0.f && Local.Z < 250.f;
}

bool ARiptideRaft::FitOars(ARiptideCharacter* Who)
{
	if (!HasAuthority() || !Who || bOarsFitted || Who->GetInventory()->CountOf(TEXT("oar")) < 2)
	{
		return false;
	}
	URiptideStorageComponent* Inventory = Who->GetInventory();
	int32 Need = 2;
	for (int32 Grid = 0; Grid < Inventory->Num() && Need > 0; ++Grid)
	{
		Need = Inventory->GetStorage(Grid)->Grid.Remove(TEXT("oar"), Need);
	}
	Inventory->OnChanged.Broadcast();
	bOarsFitted = true;
	OnRep_Oars();
	Who->StartAction(ERiptideCrewAction::Reach);
	return true;
}

bool ARiptideRaft::TakeOutOars(ARiptideCharacter* Who)
{
	if (!HasAuthority() || !Who || !bOarsFitted)
	{
		return false;
	}
	SetRower(nullptr);
	bOarsFitted = false;
	OnRep_Oars();
	Who->GiveItem(TEXT("oar"), 2);
	Who->StartAction(ERiptideCrewAction::Reach);
	return true;
}

bool ARiptideRaft::SetRower(ARiptideCharacter* Who)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (Who && (!bOarsFitted || (Rower && Rower != Who)))
	{
		return false;
	}
	ARiptideCharacter* Was = Rower;
	Rower = Who;
	RowForward = 0;
	RowTurn = 0;
	if (Was && Was != Who)
	{
		Was->SetRowing(nullptr);
	}
	if (Who)
	{
		Who->SetRowing(this);
	}
	ForceNetUpdate();
	return true;
}

void ARiptideRaft::SetRowInput(float Forward, float Turn)
{
	RowForward = int8(FMath::RoundToInt(FMath::Clamp(Forward, -1.f, 1.f) * 127.f));
	RowTurn = int8(FMath::RoundToInt(FMath::Clamp(Turn, -1.f, 1.f) * 127.f));
}

FTransform ARiptideRaft::GetSeatTransform() const
{
	// Kneeling amidships just aft of the oarlocks, facing the bow and pushing the oars (as gondoliers and Asian
	// boatmen row), so the way the rower looks is the way the raft goes.
	return FTransform(FRotator::ZeroRotator, FVector(-35.f, 0.f, Height * 0.5f)) * GetActorTransform();
}

void ARiptideRaft::OnRep_Oars()
{
	OarLeft->SetVisibility(bOarsFitted);
	OarRight->SetVisibility(bOarsFitted);
	PoseOars(0.f);
}

float ARiptideRaft::GetSpeedMs() const
{
	return Body ? Body->GetPhysicsLinearVelocity().Size2D() / 100.f : 0.f;
}

void ARiptideRaft::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		// The rower gone (left, swimming, down): nobody's rowing.
		if (Rower && (!IsValid(Rower) || Rower->GetRowingRaft() != this))
		{
			Rower = nullptr;
			RowForward = RowTurn = 0;
		}
		ApplyWater(DeltaSeconds);
	}
	PoseOars(DeltaSeconds);
}

void ARiptideRaft::ApplyWater(float DeltaSeconds)
{
	if (!Buoyancy || !Buoyancy->IsInWaterBody())
	{
		return;
	}
	const FTransform& Xf = Body->GetComponentTransform();
	const FVector LocalVel = Xf.InverseTransformVectorNoScale(Body->GetPhysicsLinearVelocity()) / 100.f;
	// The water's grip: along its length and sideways.
	const FVector Drag(-ForwardDrag * LocalVel.X * FMath::Abs(LocalVel.X), -LateralDrag * LocalVel.Y * FMath::Abs(LocalVel.Y), 0.f);
	Body->AddForce(Xf.TransformVectorNoScale(Drag) * NewtonsToUnreal);
	const float YawRate = Body->GetPhysicsAngularVelocityInRadians().Z;
	Body->AddTorqueInRadians(FVector(0.f, 0.f, -YawRate * YawDamping * NewtonsToUnreal * 100.f));
	// The oars: each pulls on its own side; together they drive it ahead, one alone turns it away from that side.
	if (!Rower || !bOarsFitted || !IsAfloat())
	{
		return;
	}
	const FVector2D Input = GetRowInput();
	const float Left = FMath::Clamp(Input.X + Input.Y, -1.f, 1.f);
	const float Right = FMath::Clamp(Input.X - Input.Y, -1.f, 1.f);
	const float Ahead = (Left + Right) * 0.5f;
	const float Turn = (Left - Right) * 0.5f;
	FVector Forward = Xf.GetUnitAxis(EAxis::X);
	Forward.Z = 0.f;
	Body->AddForce(Forward.GetSafeNormal() * Ahead * RowForceN * NewtonsToUnreal);
	Body->AddTorqueInRadians(FVector(0.f, 0.f, Turn * RowTorqueNm * NewtonsToUnreal * 100.f));
}

void ARiptideRaft::GetOarHandles(FVector& OutLeft, FVector& OutRight) const
{
	// Each oar's handle end, swept with its stroke (the same motion PoseOars draws the oars with).
	auto Handle = [this](float Side, float Stroke)
	{
		const float Sweep = FMath::DegreesToRadians(StrokeSweepDeg) * FMath::Sin(Stroke);
		const float Dip = FMath::DegreesToRadians(OarDipDeg - 6.f * FMath::Cos(Stroke));
		const FVector Out(FMath::Sin(Sweep) * FMath::Cos(Dip), Side * FMath::Cos(Sweep) * FMath::Cos(Dip), FMath::Sin(Dip));
		const FVector Lock(0.f, Side * OarlockY, Height * 0.5f + 12.f);
		return GetActorTransform().TransformPosition(Lock - Out * OarInboard);
	};
	OutLeft = Handle(-1.f, StrokeLeft);
	OutRight = Handle(1.f, StrokeRight);
}

void ARiptideRaft::PoseOars(float DeltaSeconds)
{
	if (!bOarsFitted || !OarLeft || !OarRight)
	{
		return;
	}
	const FVector2D Input = GetRowInput();
	const bool bRowing = Rower != nullptr;
	const float Left = FMath::Clamp(Input.X + Input.Y, -1.f, 1.f);
	const float Right = FMath::Clamp(Input.X - Input.Y, -1.f, 1.f);
	StrokeLeft = FMath::Fmod(StrokeLeft + DeltaSeconds * StrokesPerSecond * 2.f * UE_PI * Left, 2.f * UE_PI);
	StrokeRight = FMath::Fmod(StrokeRight + DeltaSeconds * StrokesPerSecond * 2.f * UE_PI * Right, 2.f * UE_PI);
	for (const float Side : { -1.f, 1.f })
	{
		UStaticMeshComponent* Oar = Side < 0.f ? OarLeft : OarRight;
		const UStaticMesh* Model = Oar->GetStaticMesh();
		const FBox Box = Model ? Model->GetBoundingBox() : FBox(FVector(-98.f, -10.f, 0.f), FVector(98.f, 10.f, 4.f));
		// The oar model lies along x, handle (low x) to blade; the oarlock holds it OarInboard from the handle.
		const FVector Pivot(Box.Min.X + OarInboard, 0.f, (Box.Min.Z + Box.Max.Z) * 0.5f);
		const FVector Lock(0.f, Side * OarlockY, Height * 0.5f + 12.f);
		FVector Out;
		if (bRowing)
		{
			const float Stroke = Side < 0.f ? StrokeLeft : StrokeRight;
			const float Sweep = FMath::DegreesToRadians(StrokeSweepDeg) * FMath::Sin(Stroke);
			const float Dip = FMath::DegreesToRadians(OarDipDeg - 6.f * FMath::Cos(Stroke));
			Out = FVector(FMath::Sin(Sweep) * FMath::Cos(Dip), Side * FMath::Cos(Sweep) * FMath::Cos(Dip), FMath::Sin(Dip));
		}
		else
		{
			// Shipped: lying fore and aft in the lock, blade aft, along the raft's edge.
			Out = FVector(-1.f, Side * 0.06f, 0.f).GetSafeNormal();
		}
		const FQuat Turn = FRotationMatrix::MakeFromXZ(Out, FVector::UpVector).ToQuat();
		Oar->SetRelativeTransform(FTransform(-Pivot) * FTransform(Turn, Lock));
	}
}

// --- Using it ---

bool ARiptideRaft::GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const
{
	if (!Who)
	{
		return false;
	}
	if (Who->IsInSea())
	{
		Out.Prompt = LOCTEXT("ClimbAboard", "Climb aboard the raft");
		Out.Verb = VerbClimbAboard;
		return true;
	}
	if (!IsAboard(Who))
	{
		if (!IsAfloat())
		{
			Out.Prompt = LOCTEXT("Push", "Push the raft");
			Out.Verb = VerbPush;
			return true;
		}
		return false;
	}
	const bool bAtLock = Hit.GetComponent() == LockLeft || Hit.GetComponent() == LockRight;
	if (!bOarsFitted)
	{
		Out.Verb = VerbFitOars;
		Out.Prompt = LOCTEXT("FitOars", "Fit a pair of oars");
		if (Who->GetInventory()->CountOf(TEXT("oar")) < 2)
		{
			Out.bEnabled = false;
			Out.WhyNot = LOCTEXT("NeedOars", "The oarlocks are empty: fitting oars takes two (make them in the crafting book, B)");
		}
		return true;
	}
	if (bAtLock && Rower == nullptr)
	{
		// A long press at an oarlock takes the oars out (the raft's key); a tap takes them up to row.
		Out.Verb = VerbTakeOutOars;
		Out.Prompt = LOCTEXT("TakeOutOars", "Take the oars out");
		Out.HoldSeconds = 1.f;
		return true;
	}
	if (Rower == nullptr)
	{
		Out.Verb = VerbRow;
		Out.Prompt = LOCTEXT("Row", "Take up the oars");
		return true;
	}
	return false;
}

void ARiptideRaft::Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb)
{
	if (!Who || !HasAuthority())
	{
		return;
	}
	switch (Verb)
	{
	case VerbFitOars:
		FitOars(Who);
		break;
	case VerbTakeOutOars:
		TakeOutOars(Who);
		break;
	case VerbRow:
		SetRower(Who);
		break;
	case VerbClimbAboard:
	{
		// Up over the nearest edge onto the deck.
		const FVector Local = GetActorTransform().InverseTransformPosition(Who->GetActorLocation());
		const FVector OnDeck(FMath::Clamp(Local.X, -Length * 0.5f + 45.f, Length * 0.5f - 45.f), FMath::Clamp(Local.Y, -Beam * 0.5f + 45.f, Beam * 0.5f - 45.f),
			Height * 0.5f + Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 6.f);
		Who->SetActorLocation(GetActorTransform().TransformPosition(OnDeck), false, nullptr, ETeleportType::TeleportPhysics);
		Who->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		Who->GetCharacterMovement()->Velocity = Body->GetPhysicsLinearVelocity();
		Who->StartAction(ERiptideCrewAction::Reach);
		break;
	}
	case VerbPush:
	{
		// A shove down the sand, the way they're looking.
		FVector Dir = Who->GetControlRotation().Vector();
		Dir.Z = 0.f;
		Body->AddImpulse(Dir.GetSafeNormal() * MassKg * 140.f, NAME_None, false);
		Who->StartAction(ERiptideCrewAction::Reach);
		break;
	}
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE
