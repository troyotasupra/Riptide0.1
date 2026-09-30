#include "RiptideCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
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
#include "RiptideInventoryWidget.h"
#include "RiptideStorageComponent.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SWeakWidget.h"

ARiptideCharacter::ARiptideCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URiptideCharacterMovement>(ACharacter::CharacterMovementComponentName))
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
}

void ARiptideCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideCharacter, HomeBoat);
	DOREPLIFETIME(ARiptideCharacter, bManningHelm);
	DOREPLIFETIME(ARiptideCharacter, OverboardCount);
	DOREPLIFETIME(ARiptideCharacter, bBracing);
	DOREPLIFETIME(ARiptideCharacter, KnockdownTimeLeft);
}

void ARiptideCharacter::BeginPlay()
{
	Super::BeginPlay();
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
	WalkMapping->MapKey(InventoryAction, EKeys::Gamepad_Special_Right);
}

void ARiptideCharacter::NotifyControllerChanged()
{
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
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnLook);
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ARiptideCharacter::OnJump);
		Input->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnSwimUp);
		Input->BindAction(DiveAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnDive);
		Input->BindActionValueLambda(BraceAction, ETriggerEvent::Started, [this](const FInputActionValue&) { SetBracing(true); });
		Input->BindActionValueLambda(BraceAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetBracing(false); });
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
		const FVector2D Delta = Value.Get<FVector2D>();
		FRotator View = C->GetControlRotation();
		View.Yaw += Delta.X * LookSensitivity;
		View.Pitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch + Delta.Y * LookSensitivity), -85.f, 85.f);
		View.Roll = 0.f;
		C->SetControlRotation(View);
	}
}

void ARiptideCharacter::OnJump(const FInputActionValue& Value)
{
	if (bOnLadder)
	{
		LetGoOfLadder();
	}
	else if (!IsInSea() && !bClimbingOver)
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

bool ARiptideCharacter::IsAtLadder() const
{
	return HomeBoat && !IsClimbing() && IsInSea()
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
}

bool ARiptideCharacter::IsAtLadderGrab() const
{
	// Swum right into the ladder.
	if (!HomeBoat || IsClimbing() || !IsInSea() || LadderRegrabBlock > 0.f)
	{
		return false;
	}
	const FTransform Boat = HomeBoat->GetActorTransform();
	const FVector Foot = Boat.InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation());
	const FVector Me = Boat.InverseTransformPosition(GetActorLocation());
	return FVector2D::Distance(FVector2D(Me.X, Me.Y), FVector2D(Foot.X - LadderBodyAft, Foot.Y)) <= LadderGrabRadius
		&& Me.Z > Foot.Z - 250.f && Me.Z < Foot.Z + 150.f;
}

void ARiptideCharacter::TryClimbAboard()
{
	if (!IsAtLadder())
	{
		return;
	}
	GrabLadder();
	if (!HasAuthority())
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

void ARiptideCharacter::GrabLadder()
{
	const FTransform Boat = HomeBoat->GetActorTransform();
	const FVector Foot = Boat.InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation());
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	bOnLadder = true;
	bClimbingOver = false;
	LadderInput = 0.f;
	LadderFeetZ = FMath::Clamp(Boat.InverseTransformPosition(GetActorLocation()).Z - HalfHeight,
		Foot.Z + LadderLowestFeet, Foot.Z + LadderHighestFeet);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
}

void ARiptideCharacter::SetLadderInput(float Axis)
{
	LadderInput = FMath::Clamp(Axis, -1.f, 1.f);
	if (!HasAuthority() && IsLocallyControlled() && FMath::Abs(LadderInput - SentLadderInput) > 0.05f)
	{
		SentLadderInput = LadderInput;
		ServerSetLadderInput(LadderInput);
	}
}

void ARiptideCharacter::ServerSetLadderInput_Implementation(float Axis)
{
	LadderInput = FMath::Clamp(Axis, -1.f, 1.f);
}

void ARiptideCharacter::LetGoOfLadder()
{
	if (!bOnLadder)
	{
		return;
	}
	// Pushing off backwards, away from the motors and the ladder.
	LeaveLadder(-HomeBoat->GetActorForwardVector() * 120.f);
	LadderRegrabBlock = 1.5f;
	if (!HasAuthority())
	{
		ServerLetGoOfLadder();
	}
}

void ARiptideCharacter::ServerLetGoOfLadder_Implementation()
{
	LetGoOfLadder();
}

void ARiptideCharacter::LeaveLadder(const FVector& ExtraVelocity)
{
	bOnLadder = false;
	bClimbingOver = false;
	LadderInput = 0.f;
	SetActorEnableCollision(true);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = (HomeBoat ? HomeBoat->GetDeckPointVelocity(GetActorLocation()) : FVector::ZeroVector) + ExtraVelocity;
}

void ARiptideCharacter::StartClimbOver()
{
	bOnLadder = false;
	bClimbingOver = true;
	ClimbOverTime = 0.f;
	ClimbOverStart = HomeBoat->GetActorTransform().InverseTransformPosition(GetActorLocation());
}

void ARiptideCharacter::UpdateLadder(float DeltaSeconds)
{
	LadderRegrabBlock = FMath::Max(0.f, LadderRegrabBlock - DeltaSeconds);
	if (!HomeBoat)
	{
		return;
	}
	if (IsAtLadderGrab())
	{
		GrabLadder();
		if (!HasAuthority())
		{
			ServerClimbAboard();
		}
	}
	const FTransform Boat = HomeBoat->GetActorTransform();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Foot = Boat.InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation());
	if (bOnLadder)
	{
		// Hand over hand while W or S is held; holding on where you are when neither is.
		const float Low = Foot.Z + LadderLowestFeet;
		const float High = Foot.Z + LadderHighestFeet;
		LadderFeetZ += LadderInput * LadderClimbSpeed * DeltaSeconds;
		if (LadderInput > 0.1f && LadderFeetZ >= High)
		{
			LadderFeetZ = High;
			StartClimbOver();
		}
		else if (LadderInput < -0.1f && LadderFeetZ <= Low)
		{
			LadderFeetZ = Low;
			SetActorLocation(Boat.TransformPosition(FVector(Foot.X - LadderBodyAft, Foot.Y, LadderFeetZ + HalfHeight)), false, nullptr,
				ETeleportType::TeleportPhysics);
			LeaveLadder(FVector::ZeroVector);      // climbed off the bottom, back to swimming
			LadderRegrabBlock = 1.5f;
			return;
		}
		LadderFeetZ = FMath::Clamp(LadderFeetZ, Low, High);
		SetActorLocation(Boat.TransformPosition(FVector(Foot.X - LadderBodyAft, Foot.Y, LadderFeetZ + HalfHeight)), false, nullptr,
			ETeleportType::TeleportPhysics);
	}
	if (bClimbingOver)
	{
		// A leg over the transom onto the stern box, and down into the cockpit.
		ClimbOverTime += DeltaSeconds;
		const FVector Top = Boat.InverseTransformPosition(HomeBoat->GetLadderTopTransform().GetLocation()) + FVector(0.f, 0.f, HalfHeight);
		const FVector Landing = Boat.InverseTransformPosition(HomeBoat->GetLadderLandingTransform().GetLocation()) + FVector(0.f, 0.f, HalfHeight + 4.f);
		FVector Where;
		if (ClimbOverTime < ClimbOverLeg)
		{
			Where = FMath::Lerp(ClimbOverStart, Top, FMath::SmoothStep(0.f, 1.f, ClimbOverTime / ClimbOverLeg));
		}
		else
		{
			Where = FMath::Lerp(Top, Landing, FMath::SmoothStep(0.f, 1.f, FMath::Min(1.f, (ClimbOverTime - ClimbOverLeg) / ClimbOverLeg)));
		}
		SetActorLocation(Boat.TransformPosition(Where), false, nullptr, ETeleportType::TeleportPhysics);
		if (ClimbOverTime >= 2.f * ClimbOverLeg)
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
		++OverboardCount;
		UE_LOG(LogTemp, Log, TEXT("Riptide: %s is in the sea at t=%.1f"), *GetName(), GetWorld()->GetTimeSeconds());
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
	if (!HomeBoat || bManningHelm || !HomeBoat->GetLockers())
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
	InventoryWidgetContainer = SNew(SWeakWidget).PossiblyNullContent(InventoryWidget);
	GEngine->GameViewport->AddViewportWidgetContent(InventoryWidgetContainer.ToSharedRef(), 10);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(InventoryWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
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
	InventoryWidget.Reset();
	InventoryWidgetContainer.Reset();
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
			if (Item.Id == FName(TEXT("fuel_drum")))
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
	if (!HomeBoat || bManningHelm || IsInSea() || !FindFuelDrum(Grid, Uid))
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
	if (!CanRefuel() || !FindFuelDrum(Grid, Uid) || HomeBoat->GetFuelFraction() >= 1.f)
	{
		return;
	}
	// A drum holds twenty litres (item table: fuel_drum); the empty drum goes over the side.
	Inventory->GetStorage(Grid)->Grid.Take(Uid, 1);
	Inventory->OnChanged.Broadcast();
	const float Poured = HomeBoat->AddFuel(20.f);
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
	if (HomeBoat && Storage == HomeBoat->GetLockers() && Storage->GetStorage(Index))
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
	if (!HomeBoat || bManningHelm)
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

	// Stands on the helm's spot either way: riding along there while driving, and stepping off from it after.
	const FTransform Stand = HomeBoat->GetHelmStandTransform();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (bManning)
	{
		ApplyManningHelm();
		SetActorLocationAndRotation(Stand.GetLocation() + Stand.GetUnitAxis(EAxis::Z) * HalfHeight, FRotator(0.f, Stand.Rotator().Yaw, 0.f));
		AttachToComponent(HomeBoat->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorLocation(Stand.GetLocation() + Stand.GetUnitAxis(EAxis::Z) * (HalfHeight + 4.f), false, nullptr, ETeleportType::TeleportPhysics);
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
	else
	{
		// Stepping off moving with the deck: dropped standing still while the boat runs at speed, the deck would
		// sweep on under the feet and the body would end up in it.
		UCharacterMovementComponent* Move = GetCharacterMovement();
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = HomeBoat ? HomeBoat->GetDeckPointVelocity(GetActorLocation()) : FVector::ZeroVector;
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
	KnockdownTimeLeft = FMath::Max(0.f, KnockdownTimeLeft - DeltaSeconds);
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
	// Smoothed over a few frames: the physics step's own jitter isn't a jolt anyone would feel.
	const FVector RawAccel = bHavePrevDeckVelocity ? (DeckVelocity - PrevDeckVelocity) / DeltaSeconds : FVector::ZeroVector;
	SmoothedDeckAccel = bHavePrevDeckVelocity ? FMath::Lerp(SmoothedDeckAccel, RawAccel, FMath::Min(1.f, DeltaSeconds * 20.f)) : FVector::ZeroVector;
	const FVector DeckAccel = SmoothedDeckAccel;
	PrevDeckVelocity = DeckVelocity;
	bHavePrevDeckVelocity = true;
	if (!HasAuthority() || IsBraced() || StaggerCooldown > 0.f)
	{
		return;
	}
	const float G = 980.f;
	const FVector Sideways(DeckAccel.X, DeckAccel.Y, 0.f);
	const float SidewaysG = Sideways.Size() / G;
	const float SlamG = FMath::Max(0.f, DeckAccel.Z) / G;     // the deck driving up into the feet
	const float Worst = FMath::Max(SidewaysG, SlamG * 0.7f);
	if (Worst < StaggerG)
	{
		return;
	}
	// A stumble against the deck's acceleration (a metre or two a second, like a real one): enough to throw you
	// into the bulwark or the console, not over the side. The worst slams take your legs out from under you.
	const bool bKnockedDown = Worst >= KnockdownG;
	const FVector Throw = -Sideways.GetSafeNormal() * FMath::Clamp((SidewaysG - StaggerG) * 150.f + 80.f, 0.f, 250.f);
	LaunchCharacter(FVector(Throw.X, Throw.Y, bKnockedDown ? 40.f : 0.f), false, false);
	++StaggerCount;
	StaggerCooldown = 0.6f;
	if (bKnockedDown)
	{
		KnockdownTimeLeft = 1.4f;
		++KnockdownCount;
	}
	UE_LOG(LogTemp, Verbose, TEXT("Riptide: %s thrown by a %.1f g jolt"), *GetName(), Worst);
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

	if (HasAuthority() || IsLocallyControlled())
	{
		UpdateLadder(DeltaSeconds);
	}
	if (IsLocallyControlled())
	{
		DrawHud();
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
	if (bOnLadder && HomeBoat)
	{
		const float Top = HomeBoat->GetActorTransform().InverseTransformPosition(HomeBoat->GetLadderFootTransform().GetLocation()).Z + LadderHighestFeet;
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, LadderFeetZ >= Top - 1.f
			? TEXT("On the ladder:  W  Climb aboard    S  Climb down    Space  Let go")
			: TEXT("On the ladder:  W  Climb    S  Climb down    Space  Let go"));
	}
	else if (bClimbingOver)
	{
	}
	else if (IsAtLadder())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("Swim into the ladder to take hold of it"));
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
	else if (HomeBoat && IsStandingOnBoat() && HomeBoat->GetSpeedKnots() > 12.f)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, IsBraced() ? FColor::Green : FColor::White,
			IsBraced() ? TEXT("Holding on") : HomeBoat->IsHandholdNear(GetActorLocation(), HandholdReach)
				? TEXT("Shift  Hold on") : TEXT("Get to a rail: the boat's moving fast"));
	}
	else if (CanRefuel())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
			FString::Printf(TEXT("E  Pour the fuel drum in (tank %d%%)"), FMath::RoundToInt(HomeBoat->GetFuelFraction() * 100.f)));
	}
	else if (const int32 Locker = GetLockerInReach(); Locker != INDEX_NONE)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
			FString::Printf(TEXT("E  Open the %s"), *HomeBoat->GetLockers()->GetStorage(Locker)->Title.ToString().ToLower()));
	}
}
