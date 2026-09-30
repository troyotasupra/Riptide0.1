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

ARiptideCharacter::ARiptideCharacter()
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
	WalkMapping->MapKey(InteractAction, EKeys::E);
	WalkMapping->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
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
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARiptideCharacter::OnLook);
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ARiptideCharacter::OnJump);
		Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ARiptideCharacter::OnInteract);
	}
}

void ARiptideCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
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
	Jump();
}

void ARiptideCharacter::OnInteract(const FInputActionValue& Value)
{
	TryTakeHelm();
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
		SetActorLocation(Stand.GetLocation() + FVector(0.f, 0.f, HalfHeight + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
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
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
}

void ARiptideCharacter::OnRep_ManningHelm()
{
	ApplyManningHelm();
}

void ARiptideCharacter::OnRep_OverboardCount()
{
	OverboardMessageTimeLeft = 4.f;
}

// --- On deck ---

bool ARiptideCharacter::IsStandingOnBoat() const
{
	const UPrimitiveComponent* Base = GetMovementBase();
	return HomeBoat && Base && Base->GetOwner() == HomeBoat && GetCharacterMovement()->IsMovingOnGround();
}

void ARiptideCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() && !bManningHelm)
	{
		CheckOverboard();
	}
	OverboardMessageTimeLeft -= DeltaSeconds;
	if (IsLocallyControlled())
	{
		DrawHud();
	}
}

void ARiptideCharacter::CheckOverboard()
{
	if (!HomeBoat || IsStandingOnBoat())
	{
		return;
	}

	// In the water: the feet well below the deck (the waterline is 20 cm under it), or drifted far from the boat.
	// There's no swimming yet, so the character is hauled back aboard.
	const FTransform Deck = HomeBoat->GetDeckSpotTransform(0);
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	const float BelowDeck = FVector::DotProduct(Deck.GetLocation() - Feet, Deck.GetUnitAxis(EAxis::Z));
	const bool bInWater = BelowDeck > OverboardDepth || FVector::Dist(Feet, Deck.GetLocation()) > 3000.f;
	if (!bInWater)
	{
		return;
	}

	const FTransform Aft = HomeBoat->GetDeckSpotTransform(1);
	GetCharacterMovement()->StopMovementImmediately();
	SetActorLocation(Aft.GetLocation() + Aft.GetUnitAxis(EAxis::Z) * (HalfHeight + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	++OverboardCount;
	OverboardMessageTimeLeft = 4.f;
	UE_LOG(LogTemp, Log, TEXT("Riptide: %s went overboard and was put back on deck"), *GetName());
}

void ARiptideCharacter::DrawHud() const
{
	if (!GEngine)
	{
		return;
	}
	// Temporary prompts until the real HUD exists (same keys as the boat's readout, which is off while walking).
	const uint64 KeyBase = 0x52495054ull;
	if (IsAtHelm())
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White, TEXT("E  Take the helm"));
	}
	if (OverboardMessageTimeLeft > 0.f)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 1, 0.f, FColor::Orange, TEXT("Man overboard! Swimming isn't in yet, so you're back on deck."));
	}
}
