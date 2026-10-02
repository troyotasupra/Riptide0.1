#include "RiptideCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Net/UnrealNetwork.h"
#include "RiptideBoat.h"
#include "RiptideCharacterMovement.h"
#include "RiptideCrewBody.h"
#include "RiptidePlayerState.h"
#include "RiptideInventoryWidget.h"
#include "RiptideSettings.h"
#include "RiptideStorageComponent.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWeakWidget.h"

ARiptideCharacter::ARiptideCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URiptideCharacterMovement>(ACharacter::CharacterMovementComponentName)
		.SetDefaultSubobjectClass<URiptideCrewBodyComponent>(ACharacter::MeshComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// Walking runs after the physics step, once the boat has moved for the frame. Before it, the character would
	// follow where the deck was last frame and lag a hand's width behind it at speed, which shows as jitter.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// Slim enough to pass between the console's T-top legs and the side of the boat (about half a metre).
	GetCapsuleComponent()->InitCapsuleSize(22.f, 88.f);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 70.f));  // eyes about 1.6 m above the deck
	FirstPersonCamera->bUsePawnControlRotation = true;

	Inventory = CreateDefaultSubobject<URiptideStorageComponent>(TEXT("Inventory"));
	BaseEyeHeight = 70.f;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->SetTickGroup(TG_PostPhysics);
	Move->MaxWalkSpeed = 350.f;              // a careful walk on a moving deck
	Move->MaxWalkSpeedCrouched = 150.f;
	Move->BrakingDecelerationWalking = 1400.f;
	Move->GroundFriction = 8.f;
	Move->JumpZVelocity = 380.f;
	Move->AirControl = 0.15f;
	// Steps up onto the foredeck and over small lips, but the gunwale is a wall to walk into: going over the side
	// takes a deliberate jump.
	Move->MaxStepHeight = 25.f;
	Move->SetWalkableFloorAngle(40.f);
	// Stepping off the deck (a jump) keeps the boat's speed and spin, so a jump lands back where it left.
	Move->bImpartBaseVelocityX = true;
	Move->bImpartBaseVelocityY = true;
	Move->bImpartBaseVelocityZ = true;
	Move->bImpartBaseAngularVelocity = true;
	// The boat's turns turn the character (and its view) with it.
	Move->bIgnoreBaseRotation = false;
	// A body nobody is controlling (its player away driving the boat, or a crew member whose player has left) still
	// stands on the deck and rides along with it.
	Move->bRunPhysicsWithNoController = true;

	// The body (built from the player's look in ApplyAppearance): feet on the bottom of the capsule, turned to face
	// forward (the model faces its own +Y). Its own player never sees it (it would fill the first-person view),
	// only its shadow; everyone else does.
	URiptideCrewBodyComponent* Body = GetCrewBody();
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -88.f), FRotator(0.f, -90.f, 0.f));
	Body->SetHiddenFromOwner(true);
}

URiptideCrewBodyComponent* ARiptideCharacter::GetCrewBody() const
{
	return Cast<URiptideCrewBodyComponent>(GetMesh());
}

void ARiptideCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideCharacter, HomeBoat);
	DOREPLIFETIME(ARiptideCharacter, bManningHelm);
	DOREPLIFETIME(ARiptideCharacter, OverboardCount);
	DOREPLIFETIME(ARiptideCharacter, bBracing);
	DOREPLIFETIME(ARiptideCharacter, KnockdownEndTime);
	DOREPLIFETIME(ARiptideCharacter, LadderState);
	DOREPLIFETIME(ARiptideCharacter, LadderFeetZ);
	DOREPLIFETIME(ARiptideCharacter, ClimbOverStart);
	DOREPLIFETIME(ARiptideCharacter, LadderLeavePush);
}

void ARiptideCharacter::BeginPlay()
{
	Super::BeginPlay();
	// A body straight away, in the default look until the player's own arrives (and for a crew member with no
	// player).
	ApplyAppearance();
	if (HasAuthority() && Inventory->Num() == 0)
	{
		// Grids sized like the old build's: pockets, and a small pack until there's gear to wear.
		Inventory->AddStorage(NSLOCTEXT("Riptide", "Pockets", "Pockets"), 5, 2);
		Inventory->AddStorage(NSLOCTEXT("Riptide", "Backpack", "Backpack"), 6, 4);
	}
}

// --- Input ---

void ARiptideCharacter::BuildInput()
{
	if (WalkMapping)
	{
		return;
	}

	// Built in code so the project runs without any input assets authored in the editor.
	MoveAction = NewObject<UInputAction>(this, TEXT("IA_Move"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	LookAction = NewObject<UInputAction>(this, TEXT("IA_Look"));
	LookAction->ValueType = EInputActionValueType::Axis2D;
	JumpAction = NewObject<UInputAction>(this, TEXT("IA_Jump"));
	JumpAction->ValueType = EInputActionValueType::Boolean;
	InteractAction = NewObject<UInputAction>(this, TEXT("IA_Interact"));
	InteractAction->ValueType = EInputActionValueType::Boolean;

	WalkMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Walk"));

	// Move is (right, forward). W/S drive the forward axis (Y), so they're swizzled from X.
	auto Swizzle = [this]()
	{
		UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(WalkMapping);
		S->Order = EInputAxisSwizzle::YXZ;
		return S;
	};
	WalkMapping->MapKey(MoveAction, EKeys::W).Modifiers.Add(Swizzle());
	FEnhancedActionKeyMapping& Back = WalkMapping->MapKey(MoveAction, EKeys::S);
	Back.Modifiers.Add(Swizzle());
	Back.Modifiers.Add(NewObject<UInputModifierNegate>(WalkMapping));
	WalkMapping->MapKey(MoveAction, EKeys::D);
	WalkMapping->MapKey(MoveAction, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(WalkMapping));
	WalkMapping->MapKey(MoveAction, EKeys::Gamepad_Left2D);

	WalkMapping->MapKey(LookAction, EKeys::Mouse2D);
	WalkMapping->MapKey(LookAction, EKeys::Gamepad_Right2D);

	WalkMapping->MapKey(JumpAction, EKeys::SpaceBar);
	WalkMapping->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	// Swimming: Space (held) swims up, C or Ctrl dives.
	DiveAction = NewObject<UInputAction>(this, TEXT("IA_Dive"));
	DiveAction->ValueType = EInputActionValueType::Boolean;
	WalkMapping->MapKey(DiveAction, EKeys::C);
	WalkMapping->MapKey(DiveAction, EKeys::LeftControl);
	WalkMapping->MapKey(DiveAction, EKeys::Gamepad_FaceButton_Right);
	// Hold on: Shift (or the left bumper) near a rail, the gunwale or a T-top leg.
	BraceAction = NewObject<UInputAction>(this, TEXT("IA_Brace"));
	BraceAction->ValueType = EInputActionValueType::Boolean;
	WalkMapping->MapKey(BraceAction, EKeys::LeftShift);
	WalkMapping->MapKey(BraceAction, EKeys::Gamepad_LeftShoulder);
	WalkMapping->MapKey(InteractAction, EKeys::E);
	WalkMapping->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);

	InventoryAction = NewObject<UInputAction>(this, TEXT("IA_Inventory"));
	InventoryAction->ValueType = EInputActionValueType::Boolean;
	WalkMapping->MapKey(InventoryAction, EKeys::Tab);
	WalkMapping->MapKey(InventoryAction, EKeys::Gamepad_Special_Left);   // View; Start (Menu) opens the in-game menu
}

void ARiptideCharacter::SetHomeBoat(ARiptideBoat* Boat)
{
	if (HomeBoat && HasAuthority())
	{
		HomeBoat->OnDestroyed.RemoveDynamic(this, &ARiptideCharacter::OnHomeBoatDestroyed);
	}
	HomeBoat = Boat;
	if (HomeBoat && HasAuthority())
	{
		HomeBoat->OnDestroyed.AddUniqueDynamic(this, &ARiptideCharacter::OnHomeBoatDestroyed);
	}
}

void ARiptideCharacter::OnHomeBoatDestroyed(AActor* Boat)
{
	if (bManningHelm)
	{
		bManningHelm = false;
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		ApplyManningHelm();
		if (AController* Driver = HelmDriver.Get(); Driver && !Driver->GetPawn())
		{
			Driver->Possess(this);
		}
	}
	if (IsClimbing())
	{
		LeaveLadder(FVector::ZeroVector);
	}
}

void ARiptideCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	WatchAppearance();
}

void ARiptideCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	WatchAppearance();
}

void ARiptideCharacter::WatchAppearance()
{
	ARiptidePlayerState* State = GetPlayerState<ARiptidePlayerState>();
	if (!State || State == WatchedState.Get())
	{
		return;
	}
	if (ARiptidePlayerState* Old = WatchedState.Get())
	{
		Old->OnAppearanceChanged.Remove(AppearanceWatch);
	}
	WatchedState = State;
	AppearanceWatch = State->OnAppearanceChanged.AddUObject(this, &ARiptideCharacter::ApplyAppearance);
	ApplyAppearance();
}

void ARiptideCharacter::ApplyAppearance()
{
	// Every machine builds the same body from the replicated look; only what changed is swapped.
	const ARiptidePlayerState* State = WatchedState.Get();
	if (URiptideCrewBodyComponent* Body = GetCrewBody())
	{
		Body->SetAppearance(State ? State->GetAppearance() : FRiptideAppearance());
	}
	UE_LOG(LogTemp, Verbose, TEXT("Riptide: %s's look is %s"), *GetName(), State ? *State->GetAppearance().ToString() : TEXT("(none)"));
}

void ARiptideCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ARiptidePlayerState* Old = WatchedState.Get())
	{
		Old->OnAppearanceChanged.Remove(AppearanceWatch);
	}
	CloseInventory();
	if (HasAuthority() && IsValid(HomeBoat) && HomeBoat->GetLadderUser() == this)
	{
		HomeBoat->SetLadderUser(nullptr);
	}
	Super::EndPlay(EndPlayReason);
}

void ARiptideCharacter::NotifyControllerChanged()
{
	// A body its player has left (to drive the boat, or gone) isn't holding anything any more: keys held as the
	// controls swapped never report being let go.
	if (!Controller)
	{
		bBracing = false;
		LadderInput = 0.f;
		SentLadderInput = 0.f;
		CloseInventory();
	}
	// Swap the walking controls in or out as a player takes or leaves this body (before Super, which forgets
	// the previous controller).
	BuildInput();
	auto InputFor = [](AController* C) -> UEnhancedInputLocalPlayerSubsystem*
	{
		const APlayerController* PC = Cast<APlayerController>(C);
		return PC && PC->IsLocalController() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	};
	if (UEnhancedInputLocalPlayerSubsystem* Old = InputFor(PreviousController))
	{
		Old->RemoveMappingContext(WalkMapping);
	}
	if (UEnhancedInputLocalPlayerSubsystem* New = InputFor(Controller))
	{
		New->AddMappingContext(WalkMapping, 0);
	}
	Super::NotifyControllerChanged();
}

void ARiptideCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnMove);
		Input->BindActionValueLambda(MoveAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetLadderInput(0.f); });
		Input->BindActionValueLambda(MoveAction, ETriggerEvent::Canceled, [this](const FInputActionValue&) { SetLadderInput(0.f); });
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnLook);
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ARiptideCharacter::OnJump);
		Input->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnSwimUp);
		Input->BindAction(DiveAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnDive);
		Input->BindActionValueLambda(BraceAction, ETriggerEvent::Started, [this](const FInputActionValue&) { SetBracing(true); });
		Input->BindActionValueLambda(BraceAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetBracing(false); });
		Input->BindActionValueLambda(BraceAction, ETriggerEvent::Canceled, [this](const FInputActionValue&) { SetBracing(false); });
		Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ARiptideCharacter::OnInteract);
		Input->BindAction(InventoryAction, ETriggerEvent::Started, this, &ARiptideCharacter::OnInventoryKey);
	}
}

void ARiptideCharacter::OnMove(const FInputActionValue& Value)
{
	if (IsKnockedDown())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	if (IsClimbing())
	{
		SetLadderInput(Axis.Y);     // W up, S down, nothing held: hang on
		return;
	}
	// Swimming goes where you look (down to dive, up to surface); walking stays level.
	const FRotator Facing = IsInSea() ? GetControlRotation() : FRotator(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Facing).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Facing).GetUnitAxis(EAxis::Y), Axis.X);
}

void ARiptideCharacter::OnLook(const FInputActionValue& Value)
{
	// Same feel as the helm camera: degrees per unit of mouse or stick, pitch up for mouse up.
	if (AController* C = GetController())
	{
		const FVector2D Delta = Value.Get<FVector2D>() * RiptideSettings::LookScale();     // the player's sensitivity and invert
		FRotator View = C->GetControlRotation();
		View.Yaw += Delta.X * LookSensitivity;
		View.Pitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch + Delta.Y * LookSensitivity), -85.f, 85.f);
		View.Roll = 0.f;
		C->SetControlRotation(View);
	}
}

void ARiptideCharacter::OnJump(const FInputActionValue& Value)
{
	if (IsOnLadder())
	{
		LetGoOfLadder();
	}
	else if (!IsInSea() && !IsClimbing() && !IsKnockedDown())
	{
		Jump();
	}
}

void ARiptideCharacter::OnSwimUp(const FInputActionValue& Value)
{
	if (IsInSea())
	{
		AddMovementInput(FVector::UpVector, 1.f);
	}
}

void ARiptideCharacter::OnDive(const FInputActionValue& Value)
{
	if (IsInSea())
	{
		AddMovementInput(FVector::UpVector, -1.f);
	}
}

// --- In the sea ---

bool ARiptideCharacter::IsInSea() const
{
	const URiptideCharacterMovement* Move = Cast<URiptideCharacterMovement>(GetCharacterMovement());
	return Move && Move->IsSeaSwimming();
}

bool ARiptideCharacter::IsLadderFree() const
{
	return IsValid(HomeBoat) && (!HomeBoat->GetLadderUser() || HomeBoat->GetLadderUser() == this);
}

bool ARiptideCharacter::IsAtLadder() const
{
	return IsValid(HomeBoat) && !IsClimbing() && IsInSea() && IsLadderFree()
		&& FVector::Dist(GetActorLocation(), HomeBoat->GetLadderFootTransform().GetLocation()) <= LadderReach;
}

namespace
{
	// The ladder in the boat's frame, from its foot (ARiptideBoat's GetLadderFootTransform): where a climber's body
	// is (just aft of the rails, facing the transom), and how high their feet go, from hanging off the bottom rung
	// with the head just out of the water to standing on a rung with the transom's top at the hips.
	constexpr float LadderBodyAft = 24.f;
	constexpr float LadderLowestFeet = -120.f;   // below the foot
	constexpr float LadderHighestFeet = 40.f;    // above it
	constexpr float LadderGrabRadius = 45.f;
	constexpr float ClimbOverLeg = 0.45f;        // seconds for each of the two moves over the top
	constexpr uint8 LadderNone = 0;
	constexpr uint8 LadderHanging = 1;
	constexpr uint8 LadderClimbingOver = 2;
}

bool ARiptideCharacter::IsOnLadder() const
{
	return LadderState == LadderHanging;
}

bool ARiptideCharacter::IsClimbing() const
{
	return LadderState != LadderNone;
}

bool ARiptideCharacter::IsLadderLocked() const
{
	return LadderState != LadderNone || LadderGrace > 0.f;
}

bool ARiptideCharacter::IsAtLadderGrab() const
{
	// Swum right into the ladder (and not from deep below it).
	if (!IsValid(HomeBoat) || IsClimbing() || !IsInSea() || LadderRegrabBlock > 0.f || !IsLadderFree())
	{
		return false;
	}
	const FTransform Boat = HomeBoat->GetActorTransform();
	const FVector Foot = Boat.InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation());
	const FVector Me = Boat.InverseTransformPosition(GetActorLocation());
	return FVector2D::Distance(FVector2D(Me.X, Me.Y), FVector2D(Foot.X - LadderBodyAft, Foot.Y)) <= LadderGrabRadius
		&& Me.Z > Foot.Z - 150.f && Me.Z < Foot.Z + 150.f;
}

void ARiptideCharacter::TryClimbAboard()
{
	if (!IsAtLadder())
	{
		return;
	}
	if (HasAuthority())
	{
		GrabLadder();
	}
	else
	{
		ServerClimbAboard();
	}
}

void ARiptideCharacter::ServerClimbAboard_Implementation()
{
	if (!IsClimbing() && IsAtLadder())
	{
		GrabLadder();
	}
}

// The ladder is the server's: it alone takes hold, climbs, goes over the top and lets go, and every machine (the
// climber's own included) places the climber from what it replicates, in the boat's frame. The climber's machine
// sends its W/S; while on the ladder the server doesn't correct the climber's movement (see
// URiptideCharacterMovement::ServerCheckClientError), since the character's own movement is switched off.

void ARiptideCharacter::GrabLadder()
{
	const FTransform Boat = HomeBoat->GetActorTransform();
	const FVector Foot = Boat.InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation());
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	LadderInput = 0.f;
	LadderFeetZ = FMath::Clamp(Boat.InverseTransformPosition(GetActorLocation()).Z - HalfHeight,
		Foot.Z + LadderLowestFeet, Foot.Z + LadderHighestFeet);
	HomeBoat->SetLadderUser(this);
	KnockdownEndTime = -1.f;
	SetLadderState(LadderHanging);
}

void ARiptideCharacter::SetLadderState(uint8 NewState)
{
	const uint8 Previous = LadderState;
	LadderState = NewState;
	ApplyLadderState(Previous);
}

void ARiptideCharacter::OnRep_Ladder(uint8 PreviousState)
{
	ApplyLadderState(PreviousState);
}

void ARiptideCharacter::ApplyLadderState(uint8 PreviousState)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (LadderState != LadderNone && PreviousState == LadderNone)
	{
		// Onto the ladder: the body hangs from it, moved by the climb, not walking or swimming.
		Move->StopMovementImmediately();
		Move->DisableMovement();
		SetActorEnableCollision(false);
		DisplayFeetZ = LadderFeetZ;
		SentLadderInput = 0.f;
		CloseInventory();
	}
	if (LadderState == LadderClimbingOver && PreviousState != LadderClimbingOver)
	{
		ClimbOverTime = 0.f;
	}
	if (LadderState == LadderNone && PreviousState != LadderNone)
	{
		// Off it, onto the deck or back into the sea, moving with the boat (and pushing off, if letting go).
		SetActorEnableCollision(true);
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = (IsValid(HomeBoat) ? HomeBoat->GetDeckPointVelocity(GetActorLocation()) : FVector::ZeroVector) + FVector(LadderLeavePush);
		LadderRegrabBlock = 1.5f;
		LadderInput = 0.f;
		SentLadderInput = 0.f;
	}
}

void ARiptideCharacter::SetLadderInput(float Axis)
{
	LadderInput = FMath::Clamp(Axis, -1.f, 1.f);
	if (!HasAuthority() && IsLocallyControlled() && LadderState == LadderHanging && FMath::Abs(LadderInput - SentLadderInput) > 0.05f)
	{
		SentLadderInput = LadderInput;
		ServerSetLadderInput(LadderInput);
	}
}

void ARiptideCharacter::ServerSetLadderInput_Implementation(float Axis)
{
	LadderInput = FMath::IsFinite(Axis) ? FMath::Clamp(Axis, -1.f, 1.f) : 0.f;
}

void ARiptideCharacter::LetGoOfLadder()
{
	if (LadderState != LadderHanging)
	{
		return;
	}
	if (!HasAuthority())
	{
		ServerLetGoOfLadder();
		return;
	}
	// Pushing off backwards, away from the motors and the ladder.
	LeaveLadder(IsValid(HomeBoat) ? -HomeBoat->GetActorForwardVector() * 120.f : FVector::ZeroVector);
}

void ARiptideCharacter::ServerLetGoOfLadder_Implementation()
{
	LetGoOfLadder();
}

void ARiptideCharacter::LeaveLadder(const FVector& ExtraVelocity)
{
	LadderLeavePush = ExtraVelocity;
	// The climber's own machine catches up a moment later: don't correct it in between.
	LadderGrace = 0.4f;
	if (IsValid(HomeBoat) && HomeBoat->GetLadderUser() == this)
	{
		HomeBoat->SetLadderUser(nullptr);
	}
	SetLadderState(LadderNone);
}

void ARiptideCharacter::StartClimbOver()
{
	ClimbOverStart = HomeBoat->GetActorTransform().InverseTransformPosition(GetActorLocation());
	SetLadderState(LadderClimbingOver);
}

void ARiptideCharacter::UpdateLadder(float DeltaSeconds)
{
	LadderRegrabBlock = FMath::Max(0.f, LadderRegrabBlock - DeltaSeconds);
	LadderGrace = FMath::Max(0.f, LadderGrace - DeltaSeconds);
	GrabRequestCooldown = FMath::Max(0.f, GrabRequestCooldown - DeltaSeconds);
	if (!IsValid(HomeBoat))
	{
		// The boat's gone: nothing to hang on to.
		if (HasAuthority() && IsClimbing())
		{
			LeaveLadder(FVector::ZeroVector);
		}
		return;
	}
	// Swimming into the ladder takes hold of it (the climber's machine asks the server, which checks for itself).
	if (IsAtLadderGrab())
	{
		if (HasAuthority())
		{
			GrabLadder();
		}
		else if (IsLocallyControlled() && GrabRequestCooldown <= 0.f)
		{
			ServerClimbAboard();
			GrabRequestCooldown = 0.5f;
		}
	}
	const FTransform Boat = HomeBoat->GetActorTransform();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Foot = Boat.InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation());
	if (LadderState == LadderHanging)
	{
		if (HasAuthority())
		{
			// Hand over hand while W or S is held; holding on where you are when neither is.
			const float Low = Foot.Z + LadderLowestFeet;
			const float High = Foot.Z + LadderHighestFeet;
			LadderFeetZ = FMath::Clamp(LadderFeetZ + LadderInput * LadderClimbSpeed * DeltaSeconds, Low, High);
			DisplayFeetZ = LadderFeetZ;
			if (LadderInput > 0.1f && LadderFeetZ >= High)
			{
				SetActorLocation(Boat.TransformPosition(FVector(Foot.X - LadderBodyAft, Foot.Y, LadderFeetZ + HalfHeight)), false, nullptr,
					ETeleportType::TeleportPhysics);
				StartClimbOver();
			}
			else if (LadderInput < -0.1f && LadderFeetZ <= Low)
			{
				SetActorLocation(Boat.TransformPosition(FVector(Foot.X - LadderBodyAft, Foot.Y, LadderFeetZ + HalfHeight)), false, nullptr,
					ETeleportType::TeleportPhysics);
				LeaveLadder(FVector::ZeroVector);      // climbed off the bottom, back to swimming
				return;
			}
		}
		else
		{
			// Following the server's height smoothly between its updates.
			DisplayFeetZ = FMath::FInterpTo(DisplayFeetZ, LadderFeetZ, DeltaSeconds, 12.f);
		}
		if (LadderState == LadderHanging)
		{
			SetActorLocation(Boat.TransformPosition(FVector(Foot.X - LadderBodyAft, Foot.Y, DisplayFeetZ + HalfHeight)), false, nullptr,
				ETeleportType::TeleportPhysics);
		}
	}
	if (LadderState == LadderClimbingOver)
	{
		// A leg over the transom onto the stern box, and down into the cockpit (timed on every machine from when it
		// started, so they all show the same climb).
		ClimbOverTime += DeltaSeconds;
		const FVector Start(ClimbOverStart);
		const FVector Top = Boat.InverseTransformPosition(HomeBoat->GetLadderTopTransform().GetLocation()) + FVector(0.f, 0.f, HalfHeight);
		const FVector Landing = Boat.InverseTransformPosition(HomeBoat->GetLadderLandingTransform().GetLocation()) + FVector(0.f, 0.f, HalfHeight + 4.f);
		const FVector Where = ClimbOverTime < ClimbOverLeg
			? FMath::Lerp(Start, Top, FMath::SmoothStep(0.f, 1.f, ClimbOverTime / ClimbOverLeg))
			: FMath::Lerp(Top, Landing, FMath::SmoothStep(0.f, 1.f, FMath::Min(1.f, (ClimbOverTime - ClimbOverLeg) / ClimbOverLeg)));
		SetActorLocation(Boat.TransformPosition(Where), false, nullptr, ETeleportType::TeleportPhysics);
		if (HasAuthority() && ClimbOverTime >= 2.f * ClimbOverLeg)
		{
			LeaveLadder(FVector::ZeroVector);      // aboard, moving with the deck
		}
	}
}

// --- The radio mic ---

bool ARiptideCharacter::IsLookingAt(const FVector& World, float Reach, float Radius) const
{
	const FVector Eye = FirstPersonCamera->GetComponentLocation();
	const FVector View = GetControlRotation().Vector();
	const FVector To = World - Eye;
	const float Along = FVector::DotProduct(To, View);
	return Along > 0.f && Along <= Reach && (To - View * Along).Size() <= Radius;
}

bool ARiptideCharacter::IsHoldingMic() const
{
	return HomeBoat && HomeBoat->GetMicHolder() == this;
}

bool ARiptideCharacter::CanGrabMic() const
{
	return HomeBoat && !HomeBoat->GetMicHolder() && !bManningHelm && !IsInSea() && !IsClimbing()
		&& IsLookingAt(HomeBoat->GetMicHookLocation() - FVector(0.f, 0.f, 6.f), MicReach, 14.f);
}

void ARiptideCharacter::TryToggleMic()
{
	const bool bTake = !IsHoldingMic();
	if (bTake && !CanGrabMic())
	{
		return;
	}
	if (HasAuthority())
	{
		ServerToggleMic_Implementation(bTake);
	}
	else
	{
		ServerToggleMic(bTake);
	}
}

void ARiptideCharacter::ServerToggleMic_Implementation(bool bTake)
{
	if (!HomeBoat)
	{
		return;
	}
	if (bTake)
	{
		// Checked here too, by distance (the server doesn't know exactly where the client was looking).
		if (!HomeBoat->GetMicHolder() && FVector::Dist(FirstPersonCamera->GetComponentLocation(), HomeBoat->GetMicHookLocation()) <= MicReach + 30.f)
		{
			HomeBoat->GrabMic(this);
		}
	}
	else if (IsHoldingMic())
	{
		HomeBoat->HangUpMic();
	}
}

void ARiptideCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);
	if (HasAuthority() && IsInSea() && PrevMovementMode != MOVE_Custom)
	{
		KnockdownEndTime = -1.0;
		if (LadderRegrabBlock <= 0.f && GetWorld()->GetTimeSeconds() - LastInSeaTime > 1.5)
		{
			++OverboardCount;
			UE_LOG(LogTemp, Log, TEXT("Riptide: %s is in the sea at t=%.1f"), *GetName(), GetWorld()->GetTimeSeconds());
		}
	}
}

void ARiptideCharacter::OnInteract(const FInputActionValue& Value)
{
	if (IsAtLadder())
	{
		TryClimbAboard();
	}
	else if (CanGrabMic() || (IsHoldingMic() && HomeBoat && IsLookingAt(HomeBoat->GetMicHookLocation(), MicReach + 60.f, 25.f)))
	{
		TryToggleMic();          // take the mic off its clip, or hang it back on it
	}
	else if (IsAtHelm())
	{
		TryTakeHelm();
	}
	else if (CanRefuel())
	{
		TryRefuel();
	}
	else if (const int32 Locker = GetLockerInReach(); Locker != INDEX_NONE)
	{
		OpenInventory(Locker);
	}
}

void ARiptideCharacter::OnInventoryKey(const FInputActionValue& Value)
{
	OpenInventory(GetLockerInReach());
}

// --- Storage ---

int32 ARiptideCharacter::GetLockerInReach() const
{
	if (!IsValid(HomeBoat) || bManningHelm || !HomeBoat->GetLockers() || IsInSea() || IsClimbing() || IsKnockedDown())
	{
		return INDEX_NONE;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	return HomeBoat->GetLockers()->FindNearest(Feet + FVector(0.f, 0.f, 40.f), LockerReach);
}

void ARiptideCharacter::OpenInventory(int32 Locker)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController() || IsInventoryOpen() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	TWeakObjectPtr<ARiptideCharacter> WeakThis(this);
	InventoryWidget = SNew(SRiptideInventory)
		.Carrying(Inventory)
		.Container(Locker != INDEX_NONE && HomeBoat ? HomeBoat->GetLockers() : nullptr)
		.ContainerIndex(Locker)
		.OnMove(SRiptideInventory::FOnMove::CreateLambda([WeakThis](URiptideStorageComponent* From, int32 FromIndex, int32 Uid,
			URiptideStorageComponent* To, int32 ToIndex, int32 X, int32 Y, bool bRotated, int32 Count)
		{
			if (ARiptideCharacter* Self = WeakThis.Get())
			{
				Self->ServerMoveItem(From, FromIndex, Uid, To, ToIndex, X, Y, bRotated, Count);
			}
		}))
		.OnClose(FSimpleDelegate::CreateLambda([WeakThis]()
		{
			if (ARiptideCharacter* Self = WeakThis.Get())
			{
				Self->CloseInventory();
			}
		}));
	OpenLocker = Locker;
	InventoryWidgetContainer = SNew(SWeakWidget).PossiblyNullContent(InventoryWidget);
	GEngine->GameViewport->AddViewportWidgetContent(InventoryWidgetContainer.ToSharedRef(), 10);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(InventoryWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);    // free to go to another monitor
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	GetCharacterMovement()->StopMovementImmediately();
}

void ARiptideCharacter::CloseInventory()
{
	if (GEngine && GEngine->GameViewport && InventoryWidgetContainer.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(InventoryWidgetContainer.ToSharedRef());
	}
	const bool bWasOpen = InventoryWidget.IsValid();
	InventoryWidget.Reset();
	InventoryWidgetContainer.Reset();
	OpenLocker = INDEX_NONE;
	if (!bWasOpen)
	{
		return;
	}
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

bool ARiptideCharacter::FindFuelDrum(int32& OutGrid, int32& OutUid) const
{
	for (int32 Grid = 0; Grid < Inventory->Num(); ++Grid)
	{
		for (const FRiptideItem& Item : Inventory->GetStorage(Grid)->Grid.Items)
		{
			static const FName FuelDrum(TEXT("fuel_drum"));
			if (Item.Id == FuelDrum)
			{
				OutGrid = Grid;
				OutUid = Item.Uid;
				return true;
			}
		}
	}
	return false;
}

bool ARiptideCharacter::CanRefuel() const
{
	int32 Grid, Uid;
	if (!IsValid(HomeBoat) || bManningHelm || IsInSea() || IsClimbing() || !FindFuelDrum(Grid, Uid) || !HomeBoat->HasRoomForFuel(DrumLiters))
	{
		return false;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	return FVector::Dist(Feet + FVector(0.f, 0.f, 40.f), HomeBoat->GetFuelFillerTransform().GetLocation()) < 140.f;
}

void ARiptideCharacter::TryRefuel()
{
	if (!CanRefuel())
	{
		return;
	}
	if (HasAuthority())
	{
		ServerRefuel_Implementation();
	}
	else
	{
		ServerRefuel();
	}
}

void ARiptideCharacter::ServerRefuel_Implementation()
{
	int32 Grid, Uid;
	if (!CanRefuel() || !FindFuelDrum(Grid, Uid))
	{
		return;
	}
	// A drum holds twenty litres (item table: fuel_drum), poured in whole; the empty drum goes over the side.
	Inventory->GetStorage(Grid)->Grid.Take(Uid, 1);
	Inventory->OnChanged.Broadcast();
	const float Poured = HomeBoat->AddFuel(DrumLiters);
	UE_LOG(LogTemp, Log, TEXT("Riptide: %s poured %.0f L of fuel into %s"), *GetName(), Poured, *HomeBoat->GetName());
}

int32 ARiptideCharacter::TakeFromLocker(int32 Locker)
{
	URiptideStorageComponent* Lockers = HomeBoat ? HomeBoat->GetLockers() : nullptr;
	const FRiptideStorage* Storage = Lockers ? Lockers->GetStorage(Locker) : nullptr;
	if (!HasAuthority() || !Storage || !CanReach(Lockers, Locker))
	{
		return 0;
	}
	TArray<int32> Uids;
	for (const FRiptideItem& Item : Storage->Grid.Items)
	{
		Uids.Add(Item.Uid);
	}
	int32 Moved = 0;
	for (const int32 Uid : Uids)
	{
		for (int32 i = 0; i < Inventory->Num(); ++i)
		{
			if (URiptideStorageComponent::MoveItem(Lockers, Locker, Uid, Inventory, i, -1, -1, false))
			{
				++Moved;
				break;
			}
		}
	}
	return Moved;
}

bool ARiptideCharacter::CanReach(const URiptideStorageComponent* Storage, int32 Index) const
{
	if (Storage == Inventory)
	{
		return true;
	}
	// A locker on the home boat, within reach (with some slack for the boat moving under a lagging client).
	if (IsValid(HomeBoat) && Storage == HomeBoat->GetLockers() && Storage->GetStorage(Index) && !IsInSea() && !IsClimbing())
	{
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		return FVector::Dist(Storage->GetWorldPoint(Index), Feet + FVector(0.f, 0.f, 40.f)) <= LockerReach * 1.6f;
	}
	return false;
}

void ARiptideCharacter::ServerMoveItem_Implementation(URiptideStorageComponent* From, int32 FromIndex, int32 Uid,
	URiptideStorageComponent* To, int32 ToIndex, int32 X, int32 Y, bool bRotated, int32 Count)
{
	if (CanReach(From, FromIndex) && CanReach(To, ToIndex))
	{
		URiptideStorageComponent::MoveItem(From, FromIndex, Uid, To, ToIndex, X, Y, bRotated, Count);
	}
}

// --- Helm ---

bool ARiptideCharacter::IsAtHelm() const
{
	if (!IsValid(HomeBoat) || bManningHelm || IsKnockedDown() || IsInSea() || IsClimbing() || HomeBoat->GetHelmsman())
	{
		return false;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	return FVector::Dist(Feet, HomeBoat->GetHelmStandTransform().GetLocation()) <= HelmReach;
}

void ARiptideCharacter::TryTakeHelm()
{
	if (!IsAtHelm())
	{
		return;
	}
	if (HasAuthority())
	{
		HomeBoat->TakeHelm(this);
	}
	else
	{
		ServerTakeHelm();
	}
}

void ARiptideCharacter::ServerTakeHelm_Implementation()
{
	if (IsAtHelm())
	{
		HomeBoat->TakeHelm(this);
	}
}

void ARiptideCharacter::SetManningHelm(bool bManning)
{
	if (!HomeBoat)
	{
		return;
	}
	bManningHelm = bManning;
	KnockdownEndTime = -1.0;

	// Stands on the helm's spot either way: riding along there while driving, and stepping off from it after.
	const FTransform Stand = HomeBoat->GetHelmStandTransform();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (bManning)
	{
		HelmDriver = GetController();
		ApplyManningHelm();
		SetActorLocationAndRotation(Stand.GetLocation() + Stand.GetUnitAxis(EAxis::Z) * HalfHeight, FRotator(0.f, Stand.Rotator().Yaw, 0.f));
		AttachToComponent(HomeBoat->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		ApplyManningHelm();
	}
}

void ARiptideCharacter::ApplyManningHelm()
{
	SetActorEnableCollision(!bManningHelm);
	SetActorHiddenInGame(bManningHelm);
	if (bManningHelm)
	{
		GetCharacterMovement()->DisableMovement();
	}
	else if (IsValid(HomeBoat))
	{
		// Stepping back onto the deck at the helm's spot, placed from this machine's own view of the boat (on a
		// client it's a moment behind the server's), and a step aft if someone's standing there. Moving with the
		// deck: dropped standing still while the boat runs at speed, the deck would sweep on under the feet and the
		// body would end up in it.
		const FTransform Stand = HomeBoat->GetHelmStandTransform();
		FVector Spot = Stand.GetLocation();
		for (const ARiptideCharacter* Other : TActorRange<ARiptideCharacter>(GetWorld()))
		{
			if (Other != this && !Other->IsManningHelm() && FVector::Dist2D(Other->GetActorLocation(), Spot) < 45.f)
			{
				Spot -= Stand.GetUnitAxis(EAxis::X) * 45.f;
				break;
			}
		}
		SetActorLocation(Spot + Stand.GetUnitAxis(EAxis::Z) * (GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), false, nullptr,
			ETeleportType::TeleportPhysics);
		UCharacterMovementComponent* Move = GetCharacterMovement();
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = HomeBoat->GetDeckPointVelocity(GetActorLocation());
		// The deck's motion felt before taking the helm is long gone: start feeling it afresh, with a moment to
		// find your feet.
		bHavePrevDeckVelocity = false;
		SmoothedDeckAccel = FVector::ZeroVector;
		StaggerCooldown = 0.5f;
	}
}

void ARiptideCharacter::OnRep_ManningHelm()
{
	ApplyManningHelm();
}

void ARiptideCharacter::OnRep_OverboardCount()
{
}

// --- On deck ---

bool ARiptideCharacter::IsStandingOnBoat() const
{
	const UPrimitiveComponent* Base = GetMovementBase();
	return HomeBoat && Base && Base->GetOwner() == HomeBoat && GetCharacterMovement()->IsMovingOnGround();
}

// --- Riding the boat ---

void ARiptideCharacter::SetBracing(bool bHold)
{
	bBracing = bHold;
	if (!HasAuthority())
	{
		ServerSetBracing(bHold);
	}
}

void ARiptideCharacter::ServerSetBracing_Implementation(bool bHold)
{
	bBracing = bHold;
}

bool ARiptideCharacter::IsBraced() const
{
	if (bManningHelm)
	{
		return true;    // hands on the wheel
	}
	return bBracing && HomeBoat && HomeBoat->IsHandholdNear(GetActorLocation(), HandholdReach);
}

void ARiptideCharacter::UpdateBalance(float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	StaggerCooldown -= DeltaSeconds;
	// Holding on slows you to a shuffle; down on the deck you can't move at all.
	Move->MaxWalkSpeed = IsKnockedDown() ? 0.f : IsBraced() ? BracedWalkSpeed : 350.f;

	// Feel the deck: how hard the point under the feet is accelerating (beyond gravity). The body lags behind it:
	// thrown aft when the boat surges, outward in a hard turn, and down onto the knees when the bow slams.
	if (!HomeBoat || !IsStandingOnBoat() || DeltaSeconds <= 0.f)
	{
		bHavePrevDeckVelocity = false;
		return;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector DeckVelocity = HomeBoat->GetDeckPointVelocity(Feet);
	// Smoothed over about a twentieth of a second, whatever the frame rate: the physics step's own jitter isn't a
	// jolt anyone would feel. The acceleration is taken from the smoothed velocity.
	const float Blend = 1.f - FMath::Exp(-DeltaSeconds / 0.05f);
	const FVector Smoothed = bHavePrevDeckVelocity ? FMath::Lerp(SmoothedDeckVelocity, DeckVelocity, Blend) : DeckVelocity;
	SmoothedDeckAccel = bHavePrevDeckVelocity ? (Smoothed - SmoothedDeckVelocity) / DeltaSeconds : FVector::ZeroVector;
	SmoothedDeckVelocity = Smoothed;
	const FVector DeckAccel = SmoothedDeckAccel;
	PrevDeckVelocity = DeckVelocity;
	bHavePrevDeckVelocity = true;
	if (!HasAuthority() || IsBraced() || bSteadyFeet || StaggerCooldown > 0.f)
	{
		return;
	}
	const float G = 980.f;
	const FVector Sideways(DeckAccel.X, DeckAccel.Y, 0.f);
	const float SidewaysG = Sideways.Size() / G;
	const float SlamG = FMath::Max(0.f, DeckAccel.Z) / G;     // the deck driving up into the feet
	// How hard the jolt is against what a body can stand: 1 is a stumble.
	const float Worst = FMath::Max(SidewaysG / StaggerG, SlamG / SlamStaggerG);
	if (Worst < 1.f)
	{
		return;
	}
	// A stumble against the deck's acceleration (a metre or two a second, like a real one): enough to throw you
	// into the bulwark or the console, not over the side. The worst slams take your legs out from under you. The feet
	// stay on the deck: thrown into the air instead, the body leaves the boat's frame, and on a deck pitching along at
	// 30 knots it lands half a metre from where the deck has carried everything else (a violent jump in the view).
	const bool bKnockedDown = SidewaysG >= KnockdownG || SlamG >= SlamKnockdownG;
	const FVector Throw = -Sideways.GetSafeNormal() * FMath::Clamp(FMath::Max(0.f, SidewaysG - StaggerG) * 150.f + 80.f, 0.f, 250.f);
	if (Move->IsMovingOnGround())
	{
		Move->Velocity += FVector(Throw.X, Throw.Y, 0.f);
	}
	++StaggerCount;
	StaggerCooldown = 1.2f;
	if (bKnockedDown)
	{
		KnockdownEndTime = ServerNow() + KnockdownSeconds;
		++KnockdownCount;
	}
	UE_LOG(LogTemp, Verbose, TEXT("Riptide: %s thrown by a jolt of %.2f g sideways, %.2f g up"), *GetName(), SidewaysG, SlamG);
}

void ARiptideCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bManningHelm && !IsClimbing() && !IsInSea())
	{
		UpdateBalance(DeltaSeconds);
	}
	// Knocked down: the view drops to the deck and comes back up as you get to your feet.
	const float EyeZ = IsKnockedDown() ? 5.f : 70.f;
	FVector Cam = FirstPersonCamera->GetRelativeLocation();
	Cam.Z = FMath::FInterpTo(Cam.Z, EyeZ, DeltaSeconds, IsKnockedDown() ? 9.f : 3.f);
	FirstPersonCamera->SetRelativeLocation(Cam);

	UpdateLadder(DeltaSeconds);
	if (IsInSea())
	{
		LastInSeaTime = GetWorld()->GetTimeSeconds();
	}
	if (IsLocallyControlled())
	{
		CloseInventoryIfOutOfReach();
		DrawHud();
	}
	if (IsHoldingMic() && !bManningHelm)
	{
		HomeBoat->UpdateMicCord();
	}
}

double ARiptideCharacter::ServerNow() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

bool ARiptideCharacter::IsKnockedDown() const
{
	return KnockdownEndTime > ServerNow();
}

float ARiptideCharacter::GetKnockdownElapsed() const
{
	return KnockdownEndTime < 0.0 ? 1000.f : float(ServerNow() - (KnockdownEndTime - KnockdownSeconds));
}

void ARiptideCharacter::CloseInventoryIfOutOfReach()
{
	if (IsInventoryOpen() && (IsInSea() || IsClimbing() || bManningHelm
		|| (OpenLocker != INDEX_NONE && (!IsValid(HomeBoat) || !CanReach(HomeBoat->GetLockers(), OpenLocker)))))
	{
		CloseInventory();
	}
}

void ARiptideCharacter::DrawHud() const
{
	if (!GEngine)
	{
		return;
	}
	// Temporary prompts until the real HUD exists (same keys as the boat's readout, which is off while walking).
	const uint64 KeyBase = 0x52495054ull;
	if (IsInventoryOpen())
	{
		return;
	}
	if (IsOnLadder() && IsValid(HomeBoat))
	{
		const float Top = HomeBoat->GetActorTransform().InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation()).Z + LadderHighestFeet;
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, LadderFeetZ >= Top - 1.f
			? TEXT("On the ladder:  W  Climb aboard    S  Climb down    Space  Let go")
			: TEXT("On the ladder:  W  Climb    S  Climb down    Space  Let go"));
	}
	else if (IsClimbing())
	{
	}
	else if (IsAtLadder())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("Swim into the ladder to take hold of it"));
	}
	else if (IsInSea() && IsValid(HomeBoat) && !IsLadderFree()
		&& FVector::Dist(GetActorLocation(), HomeBoat->GetLadderFootTransform().GetLocation()) <= LadderReach)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("Someone's on the ladder"));
	}
	else if (IsKnockedDown())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::Orange, TEXT("Knocked off your feet!  Hold Shift near a rail at speed"));
	}
	else if (IsInSea() && HomeBoat)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("Swimming: the boarding ladder is on the stern, port side.  Space up, C dive"));
	}
	else if (CanGrabMic())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("E  Take the radio mic"));
	}
	else if (IsHoldingMic() && HomeBoat && IsLookingAt(HomeBoat->GetMicHookLocation(), MicReach + 60.f, 25.f))
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("E  Hang up the mic"));
	}
	else if (IsAtHelm())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
			IsHoldingMic() ? TEXT("E  Take the helm    (holding the radio mic: look at its clip and press E to hang it up)") : TEXT("E  Take the helm"));
	}
	else if (IsValid(HomeBoat) && HomeBoat->GetHelmsman() && HomeBoat->GetHelmsman() != this && !bManningHelm
		&& FVector::Dist(GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
			HomeBoat->GetHelmStandTransform().GetLocation()) <= HelmReach)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("Someone's at the helm"));
	}
	else if (CanRefuel())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
			FString::Printf(TEXT("E  Pour the fuel drum in (tank %d%%)"), FMath::RoundToInt(HomeBoat->GetFuelFraction() * 100.f)));
	}
	else if (int32 Grid, Uid; IsValid(HomeBoat) && !bManningHelm && !IsInSea() && FindFuelDrum(Grid, Uid) && !HomeBoat->HasRoomForFuel(DrumLiters)
		&& FVector::Dist(GetActorLocation(), HomeBoat->GetFuelFillerTransform().GetLocation()) < 200.f)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
			FString::Printf(TEXT("The tank's too full for a whole drum (%d%%)"), FMath::RoundToInt(HomeBoat->GetFuelFraction() * 100.f)));
	}
	else if (const int32 Locker = GetLockerInReach(); Locker != INDEX_NONE)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
			FString::Printf(TEXT("E  Open the %s"), *HomeBoat->GetLockers()->GetStorage(Locker)->Title.ToString().ToLower()));
	}
	else if (IsValid(HomeBoat) && IsStandingOnBoat() && HomeBoat->GetSpeedKnots() > 12.f)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, IsBraced() ? FColor::Green : FColor::White,
			IsBraced() ? TEXT("Holding on") : HomeBoat->IsHandholdNear(GetActorLocation(), HandholdReach)
				? TEXT("Shift  Hold on") : TEXT("Get to a rail: the boat's moving fast"));
	}
}
