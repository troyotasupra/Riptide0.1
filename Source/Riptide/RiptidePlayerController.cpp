#include "RiptidePlayerController.h"

#include "BuoyancyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerInput.h"
#include "GerstnerWaterWaves.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "RiptideBoat.h"
#include "RiptideCharacter.h"
#include "RiptideDevPanel.h"
#include "RiptideSprayComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"
#include "Widgets/SWeakWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogRiptideDev, Log, All);

namespace
{
	// The dev mode's notes on screen, apart from the game's own prompts.
	constexpr uint64 DevNoteKey = 0x44455630ull;
	constexpr float CmPerSecToKnots = 0.0194384f;

	// Slow motion steps (F6), and how high the sun stands for each time of day (degrees above the horizon; the day
	// is the level's own sun).
	const float TimeScales[] = { 1.f, 0.5f, 0.25f, 0.1f };
	constexpr float GoldenHourSunDeg = 6.f;
	constexpr float DuskSunDeg = -3.f;
	// At night the sun is well down and the same light plays the moon: high in the other half of the sky, a little
	// over a thousandth as bright (a full moon is about 0.1-0.3 lux against the sun's tens of thousands), and cool.
	// Without it the sea and sky go pitch black and only the boat's lights show anything.
	constexpr float MoonDeg = 40.f;
	constexpr float MoonBrightness = 0.012f;
	const FLinearColor MoonColour(0.55f, 0.68f, 1.f);
	// The camera's auto exposure would brighten moonlight right back up to day; held this many stops down, night
	// looks like night and the boat's lights stand out.
	constexpr float NightExposureBias = -4.f;

	// How far the ocean's collision reaches over the tallest wave (init_unreal.py's OCEAN_COLLISION_ABOVE_WAVES): the
	// Water plugin only counts the boat as in the sea while it overlaps it.
	constexpr float OceanCollisionAboveWaves = 300.f;

	const FLinearColor On(FColor(110, 230, 130));
	const FLinearColor Off(FColor(140, 152, 165));
	const FLinearColor Lit(FColor(120, 205, 255));
	const FLinearColor Warn(FColor(255, 185, 70));
	const FLinearColor Plain(FColor(235, 240, 245));

	// The physics overlay's colours.
	const FColor PontoonWet(40, 170, 255);
	const FColor PontoonDry(255, 150, 40);
	const FColor PropWet(60, 230, 90);
	const FColor PropDry(255, 60, 50);
	const FColor ThrustColour(255, 230, 40);
	const FColor VelocityColour(255, 255, 255);
	const FColor HullBoxColour(120, 140, 160);
}

ARiptidePlayerController::ARiptidePlayerController()
{
	// Ticks while the world is frozen (as every player controller does), to keep the panel and overlay up to date.
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

// --- Input ---

bool ARiptidePlayerController::CanUseDevMode() const
{
#if RIPTIDE_WITH_DEV_MODE
	// It moves pawns, the boat and the world's clock, which only the machine running the game can do.
	return IsLocalController() && HasAuthority();
#else
	return false;
#endif
}

bool ARiptidePlayerController::CheckDevMode()
{
	if (CanUseDevMode())
	{
		return true;
	}
#if RIPTIDE_WITH_DEV_MODE
	DevNote(TEXT("Dev mode only works in single player, or for the host"), FColor::Orange);
#endif
	return false;
}

void ARiptidePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
#if RIPTIDE_WITH_DEV_MODE
	if (!IsLocalController())
	{
		return;
	}
	BuildDevInput();
	if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		// Above the crew's, the helm's and the fly camera's controls (none of them use these keys anyway).
		Input->AddMappingContext(DevMapping, 100);
	}
	FreeDevKeysFromEngineShortcuts();
	DevNote(TEXT("Dev mode: F1 shows the dev keys"), FColor(120, 215, 255), 6.f);
#endif
}

void ARiptidePlayerController::BuildDevInput()
{
	if (DevMapping)
	{
		return;
	}
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}
	DevMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Dev"));

	// Each dev key, built in code like the rest of the game's controls. They all work while the world is frozen.
	auto Bind = [this, Input](const TCHAR* Name, std::initializer_list<FKey> Keys, TFunction<void()> Action)
	{
		UInputAction* IA = NewObject<UInputAction>(this, Name);
		IA->ValueType = EInputActionValueType::Boolean;
		IA->bTriggerWhenPaused = true;
		DevActions.Add(IA);
		for (const FKey& Key : Keys)
		{
			DevMapping->MapKey(IA, Key);
		}
		Input->BindActionValueLambda(IA, ETriggerEvent::Started, [this, Action](const FInputActionValue&)
		{
			if (CheckDevMode())
			{
				Action();
			}
		});
	};
	Bind(TEXT("IA_DevPanel"), { EKeys::F1 }, [this]() { SetDevPanelShown(!bDevPanelShown); });
	Bind(TEXT("IA_DevFly"), { EKeys::F2 }, [this]() { SetFlying(!IsFlying()); });
	Bind(TEXT("IA_DevCameraMode"), { EKeys::F3 }, [this]() { CycleCameraMode(); });
	Bind(TEXT("IA_DevDropIn"), { EKeys::F4 }, [this]() { IsFlying() ? DropInHere() : BackToTheBoat(); });
	Bind(TEXT("IA_DevTimeOfDay"), { EKeys::F5 }, [this]() { SetTimeOfDay(ERiptideTimeOfDay((uint8(TimeOfDay) + 1) % 4)); });
	Bind(TEXT("IA_DevSlowMotion"), { EKeys::F6 }, [this]()
	{
		// The next step down, and back to full speed after the slowest.
		int32 Step = 0;
		for (int32 i = 0; i < UE_ARRAY_COUNT(TimeScales); ++i)
		{
			if (FMath::IsNearlyEqual(GetTimeScale(), TimeScales[i], 0.01f))
			{
				Step = i;
			}
		}
		SetTimeScale(TimeScales[(Step + 1) % UE_ARRAY_COUNT(TimeScales)]);
	});
	Bind(TEXT("IA_DevGodMode"), { EKeys::F7 }, [this]() { SetGodMode(!bGodMode); });
	Bind(TEXT("IA_DevPhotoMode"), { EKeys::F10 }, [this]() { SetPhotoMode(!bPhotoMode); });
	// Pause, or backslash on keyboards without one (Macs). (Esc and P belong to the game's own menu.)
	Bind(TEXT("IA_DevFreeze"), { EKeys::Pause, EKeys::Backslash }, [this]() { SetWorldFrozen(!IsWorldFrozen()); });
	Bind(TEXT("IA_DevOverlay"), { EKeys::PageUp }, [this]() { SetPhysicsOverlay(!bPhysicsOverlay); });
	Bind(TEXT("IA_DevSeaState"), { EKeys::PageDown }, [this]()
	{
		const ERiptideSeaState Now = GetSeaState();
		SetSeaState(Now == ERiptideSeaState::Calm ? ERiptideSeaState::Moderate : Now == ERiptideSeaState::Moderate ? ERiptideSeaState::Rough : ERiptideSeaState::Calm);
	});
	// Delete as well as Insert: Mac keyboards have no Insert key.
	Bind(TEXT("IA_DevRefuel"), { EKeys::Insert, EKeys::Delete }, [this]() { RefuelAndRepair(); });
	Bind(TEXT("IA_DevRightBoat"), { EKeys::Home }, [this]() { RightAndStopBoat(); });
	Bind(TEXT("IA_DevBoatHere"), { EKeys::End }, [this]() { BringBoatHere(); });
}

void ARiptidePlayerController::FreeDevKeysFromEngineShortcuts()
{
	// Development builds bind F1-F5 to view modes (F1 wireframe) and PgUp/PgDn to debug targets. Left on, pressing
	// F1 for the panel would also turn the world to wireframe.
	if (!PlayerInput || !DevMapping)
	{
		return;
	}
	for (FKeyBind& Bind : PlayerInput->DebugExecBindings)
	{
		for (const FEnhancedActionKeyMapping& Mapping : DevMapping->GetMappings())
		{
			if (Bind.Key == Mapping.Key)
			{
				Bind.bDisabled = true;
			}
		}
	}
}

void ARiptidePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// Keep track of the player's crew member through the helm and flying (the boat's helmsman is set before the
	// boat is possessed).
	if (ARiptideCharacter* Crew = Cast<ARiptideCharacter>(InPawn))
	{
		CrewMember = Crew;
	}
	else if (const ARiptideBoat* Boat = Cast<ARiptideBoat>(InPawn); Boat && Boat->GetHelmsman())
	{
		CrewMember = Boat->GetHelmsman();
	}
	if (bGodMode && CrewMember.IsValid())
	{
		CrewMember->SetSteadyFeet(true);
	}
}

void ARiptidePlayerController::DevNote(const FString& Text, const FColor& Colour, float Seconds) const
{
	if (GEngine && IsLocalController())
	{
		GEngine->AddOnScreenDebugMessage(DevNoteKey, Seconds, Colour, Text);
	}
	UE_LOG(LogRiptideDev, Log, TEXT("Dev: %s"), *Text);
}

// --- Panel ---

void ARiptidePlayerController::SetDevPanelShown(bool bShow)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	bDevPanelShown = bShow;
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (bShow && !DevPanel.IsValid() && Viewport)
	{
		DevPanel = SNew(SRiptideDevPanel).Owner(this);
		DevPanelContainer = SNew(SWeakWidget).PossiblyNullContent(DevPanel);
		// Over the game, under the inventory screen.
		Viewport->AddViewportWidgetContent(DevPanelContainer.ToSharedRef(), 5);
	}
	UpdatePanelVisibility();
#endif
}

bool ARiptidePlayerController::IsDevPanelOnScreen() const
{
	return DevPanel.IsValid() && DevPanel->GetVisibility() != EVisibility::Collapsed;
}

void ARiptidePlayerController::UpdatePanelVisibility()
{
	if (DevPanel.IsValid())
	{
		DevPanel->SetVisibility(bDevPanelShown && !bPhotoMode ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

void ARiptidePlayerController::GetDevPanelRows(TArray<FRiptideDevPanelRow>& Out) const
{
	using EKind = FRiptideDevPanelRow::EKind;
	auto Heading = [&Out](const TCHAR* Label) { FRiptideDevPanelRow& R = Out.AddDefaulted_GetRef(); R.Kind = EKind::Heading; R.Label = Label; };
	auto Key = [&Out](const FString& K, const FString& Label, const FString& Value = FString(), const FLinearColor& Colour = Plain, bool bDimmed = false)
	{
		FRiptideDevPanelRow& R = Out.AddDefaulted_GetRef();
		R.Kind = EKind::Key;
		R.Key = K;
		R.Label = Label;
		R.Value = Value;
		R.Colour = Colour;
		R.bDimmed = bDimmed;
	};
	auto Stat = [&Out](const FString& Label, const FString& Value, const FLinearColor& Colour = Plain)
	{
		FRiptideDevPanelRow& R = Out.AddDefaulted_GetRef();
		R.Kind = EKind::Stat;
		R.Label = Label;
		R.Value = Value;
		R.Colour = Colour;
	};
	auto Hint = [&Out](const FString& Label) { FRiptideDevPanelRow& R = Out.AddDefaulted_GetRef(); R.Kind = EKind::Hint; R.Label = Label; };
	auto Legend = [&Out](const FColor& Colour, const FString& Label)
	{
		FRiptideDevPanelRow& R = Out.AddDefaulted_GetRef();
		R.Kind = EKind::Legend;
		R.Colour = FLinearColor(Colour);
		R.Label = Label;
	};
	auto OnOff = [](bool b) { return b ? FString(TEXT("On")) : FString(TEXT("Off")); };

	const bool bFlying = IsFlying();
	const ARiptideBoat* Boat = GetDevBoat();

	Heading(TEXT("Camera"));
	Key(TEXT("F2"), TEXT("Fly"), bFlying ? TEXT("Flying") : TEXT("Off"), bFlying ? On : Off);
	Key(TEXT("F3"), TEXT("Camera mode"), ARiptideDevCamera::GetModeName(bFlying ? DevCamera->GetMode() : CameraMode), bFlying ? Lit : Off);
	Key(TEXT("Wheel"), bFlying && DevCamera->IsFollowingBoat() && DevCamera->GetMode() != ERiptideDevCameraMode::RideAlong ? TEXT("Zoom") : TEXT("Fly speed"),
		bFlying ? DevCamera->GetSpeedText() : FString::Printf(TEXT("%.1f m/s"), FlySpeed / 100.f), bFlying ? Plain : Off, !bFlying);
	Key(TEXT("F4"), bFlying ? TEXT("Drop in here") : TEXT("Back to the boat's helm"));
	if (bFlying)
	{
		Hint(DevCamera->IsFollowingBoat() && DevCamera->GetMode() != ERiptideDevCameraMode::RideAlong
			? TEXT("Mouse swings round the boat · A D circle · W S zoom · E Q up and down · Shift faster · Ctrl slower")
			: TEXT("WASD fly where you look · E or Space up · Q or C down · Shift fast · Ctrl slow · mouse look"));
	}

	Heading(TEXT("World"));
	Key(TEXT("F5"), TEXT("Time of day"), TimeOfDayName(TimeOfDay), TimeOfDay == ERiptideTimeOfDay::Day ? Plain : Lit);
	const float Scale = GetTimeScale();
	Key(TEXT("F6"), TEXT("Slow motion"), FMath::IsNearlyEqual(Scale, 1.f) ? FString(TEXT("Off")) : FString::Printf(TEXT("%g×"), Scale),
		FMath::IsNearlyEqual(Scale, 1.f) ? Off : Lit);
	Key(TEXT("Pause / \\"), TEXT("Freeze the world"), IsWorldFrozen() ? TEXT("Frozen") : TEXT("Off"), IsWorldFrozen() ? Lit : Off);
	Key(TEXT("PgDn"), TEXT("Sea state"), SeaStateName(GetSeaState()), GetSeaState() == ERiptideSeaState::Moderate ? Plain : Lit);

	Heading(TEXT("Crew and boat"));
	Key(TEXT("F7"), TEXT("God mode (steady feet, endless fuel)"), OnOff(bGodMode), bGodMode ? On : Off);
	Key(TEXT("Ins / Del"), TEXT("Refuel and repair the boat"));
	Key(TEXT("Home"), TEXT("Right the boat and stop it"));
	Key(TEXT("End"), TEXT("Bring the boat to where you look"), bFlying ? FString() : TEXT("fly first"), Off, !bFlying);

	Heading(TEXT("View"));
	Key(TEXT("F10"), TEXT("Photo mode (hides everything)"), OnOff(bPhotoMode), bPhotoMode ? On : Off);
	Key(TEXT("PgUp"), TEXT("Physics overlay"), OnOff(bPhysicsOverlay), bPhysicsOverlay ? On : Off);
	if (bPhysicsOverlay)
	{
		Legend(PontoonWet, TEXT("Buoyancy pontoon in the sea (line to the surface)"));
		Legend(PontoonDry, TEXT("Pontoon out of the water"));
		Legend(PropWet, TEXT("Prop biting  ·  red: prop out of the water"));
		Legend(ThrustColour, TEXT("Thrust (each motor's push)  ·  white: velocity"));
	}

	Heading(TEXT("Live"));
	const float Ms = SmoothedFrameSeconds * 1000.f;
	Stat(TEXT("Frame rate"), FString::Printf(TEXT("%.0f fps  ·  %.1f ms"), 1.f / FMath::Max(SmoothedFrameSeconds, 1e-4f), Ms),
		Ms > 20.f ? Warn : On);
	if (Boat)
	{
		const URiptideSprayComponent* Spray = Boat->FindComponentByClass<URiptideSprayComponent>();
		Stat(TEXT("Spray in the air"), FString::Printf(TEXT("%s clouds"), *FText::AsNumber(Spray ? Spray->GetCloudCount() : 0).ToString()));
		const int32 HeadingDeg = (FMath::RoundToInt(Boat->GetActorRotation().Yaw) + 450) % 360;   // as the dash shows it
		Stat(TEXT("Boat"), FString::Printf(TEXT("%.1f kn  ·  HDG %03d  ·  trim %+.0f°"), Boat->GetSpeedKnots(), HeadingDeg, Boat->GetTrimDeg()));
		Stat(TEXT("Fuel"), FString::Printf(TEXT("%.0f L  (%.0f%%)"), Boat->GetFuelLiters(), Boat->GetFuelFraction() * 100.f),
			Boat->GetFuelFraction() < 0.15f ? Warn : Plain);
		const float Port = Boat->GetMotorHealth(0), Starboard = Boat->GetMotorHealth(1);
		Stat(TEXT("Engines"), FString::Printf(TEXT("port %.0f%%  ·  starboard %.0f%%"), Port * 100.f, Starboard * 100.f),
			FMath::Min(Port, Starboard) < 0.5f ? Warn : Plain);
	}
	FVector ViewLoc;
	FRotator ViewRot;
	GetPlayerViewPoint(ViewLoc, ViewRot);
	Stat(TEXT("Camera (m)"), FString::Printf(TEXT("%.1f,  %.1f,  %.1f"), ViewLoc.X / 100.f, ViewLoc.Y / 100.f, ViewLoc.Z / 100.f));
	Stat(TEXT("Camera mode"), bFlying ? ARiptideDevCamera::GetModeName(DevCamera->GetMode()) + TEXT("  ·  ") + DevCamera->GetSpeedText()
		: TEXT("First person"));
	Stat(TEXT("Time"), FString::Printf(TEXT("%s  ·  %g× speed%s"), *TimeOfDayName(TimeOfDay), Scale, IsWorldFrozen() ? TEXT("  ·  frozen") : TEXT("")),
		IsWorldFrozen() ? Lit : Plain);
}

// --- Flying ---

void ARiptidePlayerController::SetFlying(bool bFly)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode() || bFly == IsFlying())
	{
		return;
	}
	if (bFly)
	{
		StartFlying();
	}
	else
	{
		StopFlying();
	}
#endif
}

void ARiptidePlayerController::StartFlying()
{
	APawn* Current = GetPawn();
	FlownFrom = Current;
	ViewPitchBeforeFlying = FRotator::NormalizeAxis(GetControlRotation().Pitch);

	// Left on the deck without a controller, a character stops moving altogether: the boat would sail out from under
	// it. Keep it riding the deck (or floating) while nobody controls it.
	if (ARiptideCharacter* Crew = Cast<ARiptideCharacter>(Current))
	{
		RidingCrew = Crew;
		bRidingCrewRanWithoutController = Crew->GetCharacterMovement()->bRunPhysicsWithNoController;
		Crew->GetCharacterMovement()->bRunPhysicsWithNoController = true;
	}

	// The camera starts exactly where the view is now.
	FVector ViewLoc;
	FRotator ViewRot;
	GetPlayerViewPoint(ViewLoc, ViewRot);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideDevCamera* Camera = GetWorld()->SpawnActor<ARiptideDevCamera>(ARiptideDevCamera::StaticClass(), ViewLoc,
		FRotator(ViewRot.Pitch, ViewRot.Yaw, 0.f), Params);
	if (!Camera)
	{
		return;
	}
	Camera->SetFlySpeed(FlySpeed);
	Camera->SetBoat(GetDevBoat());
	DevCamera = Camera;
	Possess(Camera);
	Camera->SetMode(CameraMode);
	DevNote(FString::Printf(TEXT("Flying (%s): F2 goes back"), *ARiptideDevCamera::GetModeName(Camera->GetMode())));
}

void ARiptidePlayerController::StopFlying(APawn* Into)
{
	ARiptideDevCamera* Camera = DevCamera;
	if (!Camera)
	{
		return;
	}
	FlySpeed = Camera->GetFlySpeed();
	CameraMode = Camera->GetMode();

	// Back to exactly what was flown from: the crew member, or the boat's helm if they're still at it.
	APawn* Target = Into ? Into : FlownFrom.Get();
	if (const ARiptideBoat* Boat = Cast<ARiptideBoat>(Target); Boat && (!Boat->GetHelmsman() || Boat->GetController()))
	{
		Target = CrewMember.Get();
	}
	if (ARiptideCharacter* Crew = Cast<ARiptideCharacter>(Target); Crew && Crew->IsManningHelm() && Crew->GetHomeBoat()
		&& Crew->GetHomeBoat()->GetHelmsman() == Crew && !Crew->GetHomeBoat()->GetController())
	{
		Target = Crew->GetHomeBoat();
	}

	if (ARiptideCharacter* Crew = RidingCrew.Get())
	{
		Crew->GetCharacterMovement()->bRunPhysicsWithNoController = bRidingCrewRanWithoutController;
	}
	RidingCrew.Reset();
	FlownFrom.Reset();

	DevCamera = nullptr;
	if (Target)
	{
		// Being possessed stops a pawn dead; a crew member dropped in over the moving deck (or stepping off it) has to
		// keep the boat's motion, or the deck sails on from under them before they land.
		ACharacter* Body = Cast<ACharacter>(Target);
		const FVector KeepVelocity = Body ? Body->GetCharacterMovement()->Velocity : FVector::ZeroVector;
		const EMovementMode KeepMode = Body ? Body->GetCharacterMovement()->MovementMode.GetValue() : MOVE_None;
		const uint8 KeepCustomMode = Body ? Body->GetCharacterMovement()->CustomMovementMode : 0;
		Possess(Target);
		if (Body && KeepMode != MOVE_None)
		{
			Body->GetCharacterMovement()->SetMovementMode(KeepMode, KeepCustomMode);
			Body->GetCharacterMovement()->Velocity = KeepVelocity;
		}
	}
	else
	{
		UnPossess();
		if (AGameModeBase* Mode = GetWorld()->GetAuthGameMode())
		{
			Mode->RestartPlayer(this);
		}
	}
	if (ARiptideCharacter* Crew = Cast<ARiptideCharacter>(GetPawn()); Crew && !Into)
	{
		// Looking the way the body faces now (the boat may have turned it while flying), at the same height as before.
		SetControlRotation(FRotator(ViewPitchBeforeFlying, Crew->GetActorRotation().Yaw, 0.f));
	}
	Camera->Destroy();
}

void ARiptidePlayerController::CycleCameraMode()
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	if (!IsFlying())
	{
		SetFlying(true);
		return;
	}
	SetDevCameraMode(ERiptideDevCameraMode((uint8(DevCamera->GetMode()) + 1) % 4));
#endif
}

void ARiptidePlayerController::SetDevCameraMode(ERiptideDevCameraMode InMode)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	CameraMode = InMode;
	if (DevCamera)
	{
		DevCamera->SetBoat(GetDevBoat());
		DevCamera->SetMode(InMode);
		CameraMode = DevCamera->GetMode();
		DevNote(FString::Printf(TEXT("Camera: %s"), *ARiptideDevCamera::GetModeName(CameraMode)));
	}
#endif
}

void ARiptidePlayerController::DropInHere()
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	ARiptideCharacter* Crew = CrewMember.Get();
	if (!IsFlying() || !Crew)
	{
		DevNote(TEXT("Fly (F2) to where you want to be, then F4 drops you in"));
		return;
	}
	if (Crew->IsClimbing() && !Crew->IsOnLadder())
	{
		DevNote(TEXT("Wait a moment: climbing over the transom"));
		return;
	}
	// Off the helm first (with nobody driving, the boat keeps its throttle), or off the ladder.
	if (Crew->IsManningHelm() && Crew->GetHomeBoat())
	{
		Crew->GetHomeBoat()->LeaveHelm();
	}
	Crew->LetGoOfLadder();

	const FVector Eye = DevCamera->GetActorLocation();
	const FRotator View = DevCamera->GetActorRotation();
	PlaceCrewMember(*Crew, Eye, View);
	StopFlying(Crew);
	SetControlRotation(FRotator(FMath::Clamp(FRotator::NormalizeAxis(View.Pitch), -85.f, 85.f), View.Yaw, 0.f));
	DevNote(TEXT("Dropped in"));
#endif
}

void ARiptidePlayerController::BackToTheBoat()
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	ARiptideCharacter* Crew = CrewMember.Get();
	ARiptideBoat* Boat = Crew && Crew->GetHomeBoat() ? Crew->GetHomeBoat() : GetDevBoat();
	if (!Crew || !Boat)
	{
		return;
	}
	if (Crew->IsManningHelm())
	{
		StopFlying();
		DevNote(TEXT("Already at the helm"));
		return;
	}
	if (Crew->IsClimbing() && !Crew->IsOnLadder())
	{
		DevNote(TEXT("Wait a moment: climbing over the transom"));
		return;
	}
	Crew->LetGoOfLadder();

	// Standing at the helm's spot, facing forward, moving with the deck.
	const FTransform Stand = Boat->GetHelmStandTransform();
	const float HalfHeight = Crew->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Location = Stand.GetLocation() + Stand.GetUnitAxis(EAxis::Z) * (HalfHeight + 4.f);
	const float Yaw = Stand.Rotator().Yaw;
	Crew->SetActorLocationAndRotation(Location, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	UCharacterMovementComponent* Move = Crew->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = Boat->GetDeckPointVelocity(Location);
	if (IsFlying())
	{
		StopFlying(Crew);
	}
	SetControlRotation(FRotator(0.f, Yaw, 0.f));
	DevNote(TEXT("Back at the helm (E takes it)"));
#endif
}

void ARiptidePlayerController::PlaceCrewMember(ARiptideCharacter& Crew, const FVector& EyeLocation, const FRotator& View)
{
	const float HalfHeight = Crew.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Location = EyeLocation - FVector(0.f, 0.f, Crew.BaseEyeHeight);
	bool bOverBoat = false;
	ARiptideBoat* Boat = Crew.GetHomeBoat() ? Crew.GetHomeBoat() : GetDevBoat();
	if (Boat)
	{
		// Inside the hull below its deck (the camera down in the bilge): up onto the deck there.
		const FTransform Frame = Boat->GetActorTransform();
		const float DeckZ = Frame.InverseTransformPosition(Boat->GetHelmStandTransform().GetLocation()).Z;
		FVector Local = Frame.InverseTransformPosition(Location);
		if (Boat->IsInsideHull(Frame.TransformPosition(Local - FVector(0.f, 0.f, HalfHeight))) && Local.Z - HalfHeight < DeckZ)
		{
			Local.Z = DeckZ + HalfHeight + 5.f;
			Location = Frame.TransformPosition(Local);
		}
		// Over the deck, the body falls with the boat's motion, so it lands on the deck rather than being left behind.
		FHitResult Hit;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(RiptideDropIn), true, &Crew);
		bOverBoat = GetWorld()->LineTraceSingleByChannel(Hit, Location, Location - FVector(0.f, 0.f, 3000.f), ECC_Pawn, Query)
			&& Hit.GetActor() == Boat;
	}
	// Clear of anything solid right there (the console, a rail).
	GetWorld()->FindTeleportSpot(&Crew, Location, FRotator(0.f, View.Yaw, 0.f));
	Crew.SetActorLocationAndRotation(Location, FRotator(0.f, View.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	UCharacterMovementComponent* Move = Crew.GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = bOverBoat ? Boat->GetDeckPointVelocity(Location) : FVector::ZeroVector;
}

// --- World ---

FString ARiptidePlayerController::TimeOfDayName(ERiptideTimeOfDay Time)
{
	switch (Time)
	{
	case ERiptideTimeOfDay::GoldenHour: return TEXT("Golden hour");
	case ERiptideTimeOfDay::Dusk: return TEXT("Dusk");
	case ERiptideTimeOfDay::Night: return TEXT("Night");
	default: return TEXT("Day");
	}
}

ADirectionalLight* ARiptidePlayerController::FindSun() const
{
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ARiptidePlayerController::SetTimeOfDay(ERiptideTimeOfDay InTime)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	ADirectionalLight* Sun = FindSun();
	if (!Sun || !Sun->GetLightComponent())
	{
		DevNote(TEXT("This level has no sun to move"), FColor::Orange);
		return;
	}
	if (!bHaveSunDefaults)
	{
		SunDefaultRotation = Sun->GetActorRotation();
		SunDefaultIntensity = Sun->GetLightComponent()->Intensity;
		SunDefaultColour = Sun->GetLightComponent()->GetLightColor();
		bHaveSunDefaults = true;
	}
	// The level's sun is Stationary (its lighting is partly baked for it); it has to be Movable to turn at runtime.
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	float Elevation = -SunDefaultRotation.Pitch;
	switch (InTime)
	{
	case ERiptideTimeOfDay::GoldenHour: Elevation = GoldenHourSunDeg; break;
	case ERiptideTimeOfDay::Dusk: Elevation = DuskSunDeg; break;
	default: break;
	}
	ULightComponent* Light = Sun->GetLightComponent();
	if (InTime == ERiptideTimeOfDay::Night)
	{
		Sun->SetActorRotation(FRotator(-MoonDeg, SunDefaultRotation.Yaw + 180.f, 0.f));
		Light->SetIntensity(SunDefaultIntensity * MoonBrightness);
		Light->SetLightColor(MoonColour);
	}
	else
	{
		// Setting in the same quarter of the sky it shines from all day.
		Sun->SetActorRotation(FRotator(-Elevation, SunDefaultRotation.Yaw, 0.f));
		Light->SetIntensity(SunDefaultIntensity);
		Light->SetLightColor(SunDefaultColour);
	}
	if (!NightExposure)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		NightExposure = GetWorld()->SpawnActor<APostProcessVolume>(Params);
		if (NightExposure)
		{
			NightExposure->bUnbound = true;
			NightExposure->Priority = 1000.f;
			NightExposure->Settings.bOverride_AutoExposureBias = true;
			NightExposure->Settings.AutoExposureBias = NightExposureBias;
		}
	}
	if (NightExposure)
	{
		NightExposure->bEnabled = InTime == ERiptideTimeOfDay::Night;
	}
	// The sky atmosphere, clouds and fog follow the sun by themselves; a sky light that doesn't capture in real time
	// needs telling.
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		USkyLightComponent* Sky = It->GetLightComponent();
		if (Sky && !Sky->IsRealTimeCaptureEnabled())
		{
			Sky->RecaptureSky();
		}
	}
	TimeOfDay = InTime;
	DevNote(FString::Printf(TEXT("Time of day: %s"), *TimeOfDayName(InTime)));
#endif
}

void ARiptidePlayerController::SetTimeScale(float Scale)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	UGameplayStatics::SetGlobalTimeDilation(this, FMath::Clamp(Scale, 0.01f, 1.f));
	DevNote(FMath::IsNearlyEqual(GetTimeScale(), 1.f) ? FString(TEXT("Normal speed")) : FString::Printf(TEXT("Slow motion %g×"), GetTimeScale()));
#endif
}

float ARiptidePlayerController::GetTimeScale() const
{
	return UGameplayStatics::GetGlobalTimeDilation(this);
}

void ARiptidePlayerController::SetWorldFrozen(bool bFreeze)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode() || bFreeze == IsWorldFrozen())
	{
		return;
	}
	// Frozen, the controller still updates the camera (the fly camera flies, the view follows it).
	bShouldPerformFullTickWhenPaused = bFreeze;
	SetPause(bFreeze);
	bShouldPerformFullTickWhenPaused = IsWorldFrozen();
	DevNote(IsWorldFrozen() ? TEXT("Frozen: Pause (or backslash) lets the world go (F2 flies round it)") : TEXT("Unfrozen"));
#endif
}

bool ARiptidePlayerController::IsWorldFrozen() const
{
	return GetWorld() && GetWorld()->IsPaused();
}

AWaterBodyOcean* ARiptidePlayerController::FindOcean() const
{
	for (TActorIterator<AWaterBodyOcean> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FString ARiptidePlayerController::SeaStateName(ERiptideSeaState State)
{
	switch (State)
	{
	case ERiptideSeaState::Calm: return TEXT("Calm");
	case ERiptideSeaState::Moderate: return TEXT("Moderate swell");
	case ERiptideSeaState::Rough: return TEXT("Rough");
	default: return TEXT("Custom");
	}
}

UWaterWavesBase* ARiptidePlayerController::MakeWaves(AWaterBodyOcean& Ocean, ERiptideSeaState State)
{
	// The same kind of waves init_unreal.py gives the level (its SWELL: 16 long, rolling waves from the same wind),
	// smaller or bigger. Steepness goes with height, as there: steeper small waves fold over and show dark spots.
	UGerstnerWaterWaves* Waves = NewObject<UGerstnerWaterWaves>(&Ocean, NAME_None, RF_Transient);
	UGerstnerWaterWaveGeneratorSimple* Generator = NewObject<UGerstnerWaterWaveGeneratorSimple>(Waves);
	Generator->NumWaves = 16;
	Generator->WindAngleDeg = -30.f;
	if (State == ERiptideSeaState::Calm)
	{
		// A low, lazy swell of a few tens of centimetres.
		Generator->MinWavelength = 450.f;
		Generator->MaxWavelength = 5000.f;
		Generator->MinAmplitude = 0.5f;
		Generator->MaxAmplitude = 8.f;
		Generator->SmallWaveSteepness = 0.06f;
		Generator->LargeWaveSteepness = 0.03f;
	}
	else
	{
		// Seas of 2.5-3 m, longer and steeper.
		Generator->MinWavelength = 700.f;
		Generator->MaxWavelength = 9000.f;
		Generator->MinAmplitude = 4.f;
		Generator->MaxAmplitude = 62.f;
		Generator->SmallWaveSteepness = 0.2f;
		Generator->LargeWaveSteepness = 0.1f;
	}
	Waves->GerstnerWaveGenerator = Generator;
	Waves->RecomputeWaves(/* bAllowBPScript = */ false);
	return Waves;
}

void ARiptidePlayerController::SetSeaState(ERiptideSeaState InState)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode() || InState == ERiptideSeaState::Other)
	{
		return;
	}
	AWaterBodyOcean* Ocean = FindOcean();
	UWaterBodyComponent* Body = Ocean ? Ocean->GetWaterBodyComponent() : nullptr;
	if (!Body)
	{
		DevNote(TEXT("This level has no ocean"), FColor::Orange);
		return;
	}
	if (!bHaveLevelWaves)
	{
		LevelWaves = Ocean->GetWaterWaves();
		LevelCollisionHeightOffset = Body->CollisionHeightOffset;
		bHaveLevelWaves = true;
	}
	UWaterWavesBase* Waves = LevelWaves;
	if (InState == ERiptideSeaState::Calm)
	{
		Waves = CalmWaves ? CalmWaves.Get() : (CalmWaves = MakeWaves(*Ocean, InState)).Get();
	}
	else if (InState == ERiptideSeaState::Rough)
	{
		Waves = RoughWaves ? RoughWaves.Get() : (RoughWaves = MakeWaves(*Ocean, InState)).Get();
	}
	// The ocean's collision has to reach over the new wave tops, or the boat would count as out of the water on
	// every crest (init_unreal.py's _cover_waves). Set before the waves: changing them rebuilds the collision.
	Body->CollisionHeightOffset = FMath::Max(LevelCollisionHeightOffset, (Waves ? Waves->GetMaxWaveHeight() : 0.f) + OceanCollisionAboveWaves);
	// This updates the waves on the GPU (what's drawn) and on the CPU (what the boat's buoyancy, the spray and the
	// swimmers read), together.
	Ocean->SetWaterWaves(Waves);
	DevNote(FString::Printf(TEXT("Sea: %s (waves up to %.1f m)"), *SeaStateName(InState), Body->GetMaxWaveHeight() / 100.f));
#endif
}

ERiptideSeaState ARiptidePlayerController::GetSeaState() const
{
	const AWaterBodyOcean* Ocean = FindOcean();
	const UWaterWavesBase* Waves = Ocean ? Ocean->GetWaterWaves() : nullptr;
	if (!bHaveLevelWaves || Waves == LevelWaves)
	{
		return bHaveLevelWaves || Waves ? ERiptideSeaState::Moderate : ERiptideSeaState::Other;
	}
	return Waves == CalmWaves ? ERiptideSeaState::Calm : Waves == RoughWaves ? ERiptideSeaState::Rough : ERiptideSeaState::Other;
}

// --- Crew and boat ---

ARiptideBoat* ARiptidePlayerController::GetDevBoat() const
{
	if (ARiptideBoat* Driven = Cast<ARiptideBoat>(GetPawn()))
	{
		return Driven;
	}
	if (const ARiptideCharacter* Crew = CrewMember.Get(); Crew && Crew->GetHomeBoat())
	{
		return Crew->GetHomeBoat();
	}
	for (TActorIterator<ARiptideBoat> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ARiptidePlayerController::SetGodMode(bool bOn)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	bGodMode = bOn;
	if (ARiptideCharacter* Crew = CrewMember.Get())
	{
		Crew->SetSteadyFeet(bOn);
	}
	DevNote(bOn ? TEXT("God mode on: the deck never throws you, and the tank never empties") : TEXT("God mode off"));
#endif
}

void ARiptidePlayerController::RefuelAndRepair()
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	if (ARiptideBoat* Boat = GetDevBoat())
	{
		Boat->AddFuel(1e6f);
		// Engine health is held between 0 and 1, so negative damage mends a motor fully.
		Boat->ApplyEngineDamage(-1.f, -1);
		DevNote(FString::Printf(TEXT("Tank full (%.0f L), both engines mended"), Boat->GetFuelLiters()));
	}
#endif
}

void ARiptidePlayerController::RightAndStopBoat()
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	if (ARiptideBoat* Boat = GetDevBoat())
	{
		PlaceBoat(*Boat, Boat->GetActorLocation(), Boat->GetActorRotation().Yaw);
		DevNote(TEXT("Boat righted and stopped (the throttle stays where it was: X cuts it)"));
	}
#endif
}

void ARiptidePlayerController::BringBoatHere()
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	ARiptideBoat* Boat = GetDevBoat();
	if (!IsFlying() || !Boat)
	{
		DevNote(TEXT("Fly (F2) to where you want the boat, look there, and press End"));
		return;
	}
	// Where the view meets the sea (if that's within 80 m), or 15 m ahead; never right on top of the camera.
	const FVector Eye = DevCamera->GetActorLocation();
	const FVector Look = DevCamera->GetActorRotation().Vector();
	const FVector Ahead = FVector(Look.X, Look.Y, 0.f).GetSafeNormal();
	const float SeaZ = Boat->GetSeaSurfaceZ(Eye);
	FVector Target = Eye + Ahead * 1500.f;
	if (Look.Z < -0.05f && Eye.Z > SeaZ)
	{
		const float Along = (Eye.Z - SeaZ) / -Look.Z;
		if (Along < 8000.f)
		{
			Target = Eye + Look * Along;
		}
	}
	const FVector2D Offset(Target.X - Eye.X, Target.Y - Eye.Y);
	if (Offset.Size() < 1000.f)
	{
		Target = Eye + Ahead * 1000.f;
	}
	PlaceBoat(*Boat, Target, DevCamera->GetActorRotation().Yaw);
	DevNote(TEXT("Boat brought here"));
#endif
}

void ARiptidePlayerController::PlaceBoat(ARiptideBoat& Boat, FVector Where, float Yaw)
{
	// Whoever stands on the deck goes with it, where they stood (the helmsman is fixed to the boat already).
	TArray<TPair<ARiptideCharacter*, FVector>> Aboard;
	for (TActorIterator<ARiptideCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetHomeBoat() == &Boat && !It->IsManningHelm() && It->IsStandingOnBoat())
		{
			Aboard.Emplace(*It, Boat.GetActorTransform().InverseTransformPosition(It->GetActorLocation()));
		}
	}
	// Upright on the sea, its origin 15 cm over the water as when it's launched (ARiptideGameMode), and dead still.
	Where.Z = Boat.GetSeaSurfaceZ(Where) + 15.f;
	Boat.SetActorLocationAndRotation(Where, FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::ResetPhysics);
	if (UPrimitiveComponent* Hull = Cast<UPrimitiveComponent>(Boat.GetRootComponent()))
	{
		Hull->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Hull->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	for (const TPair<ARiptideCharacter*, FVector>& Crew : Aboard)
	{
		Crew.Key->SetActorLocation(Boat.GetActorTransform().TransformPosition(Crew.Value) + FVector(0.f, 0.f, 2.f), false, nullptr,
			ETeleportType::TeleportPhysics);
		// Already where the deck has gone: the movement mustn't carry them by the boat's jump a second time.
		Crew.Key->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Crew.Key->GetCharacterMovement()->SaveBaseLocation();
	}
	// A camera riding along or circling stays where it is, rather than jumping with the boat.
	if (DevCamera)
	{
		DevCamera->KeepWorldPlaceAfterBoatMoved();
	}
}

// --- View ---

void ARiptidePlayerController::SetPhotoMode(bool bOn)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode() || bOn == bPhotoMode || !GEngine)
	{
		return;
	}
	bPhotoMode = bOn;
	// The prompts are the engine's on-screen messages: switched off entirely, then back as they were.
	if (bOn)
	{
		bScreenMessagesBeforePhoto = GEngine->bEnableOnScreenDebugMessages;
		GEngine->ClearOnScreenDebugMessages();
		GEngine->bEnableOnScreenDebugMessages = false;
	}
	else
	{
		GEngine->bEnableOnScreenDebugMessages = bScreenMessagesBeforePhoto;
		DevNote(TEXT("Photo mode off"));
	}
	UpdatePanelVisibility();
#endif
}

bool ARiptidePlayerController::AreScreenMessagesShown() const
{
	return GEngine && GEngine->bEnableOnScreenDebugMessages;
}

void ARiptidePlayerController::SetPhysicsOverlay(bool bOn)
{
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	bPhysicsOverlay = bOn;
	DevNote(bOn ? TEXT("Physics overlay on (F1 shows what the colours mean)") : TEXT("Physics overlay off"));
#endif
}

void ARiptidePlayerController::DrawPhysicsOverlay(const ARiptideBoat& Boat) const
{
	UWorld* World = GetWorld();
	// Drawn over everything: the pontoons and props are under the hull and the sea.
	const uint8 Depth = SDPG_Foreground;

	// The hull's physics body (a box) and its centre of mass.
	const UBoxComponent* Hull = Cast<UBoxComponent>(Boat.GetRootComponent());
	FVector Centre = Boat.GetActorLocation();
	if (Hull)
	{
		DrawDebugBox(World, Hull->GetComponentLocation(), Hull->GetScaledBoxExtent(), Hull->GetComponentQuat(), HullBoxColour, false, -1.f, Depth, 1.f);
		Centre = Hull->GetCenterOfMass();
		DrawDebugPoint(World, Centre, 10.f, FColor::White, false, -1.f, Depth);
		// Where it's going, and how fast: the arrow reaches where it'll be in half a second.
		const FVector Velocity = Boat.GetVelocity();
		if (Velocity.SizeSquared() > 100.f)
		{
			DrawDebugDirectionalArrow(World, Centre, Centre + Velocity * 0.5f, 60.f, VelocityColour, false, -1.f, Depth, 2.f);
		}
	}

	// Buoyancy pontoons: blue in the sea, orange out of it, each with a line up (or down) to the sea's surface over it.
	if (const UBuoyancyComponent* Buoyancy = Boat.FindComponentByClass<UBuoyancyComponent>())
	{
		for (const FSphericalPontoon& Pontoon : Buoyancy->BuoyancyData.Pontoons)
		{
			const FColor Colour = Pontoon.bIsInWater ? PontoonWet : PontoonDry;
			DrawDebugSphere(World, Pontoon.CenterLocation, Pontoon.Radius, 12, Colour, false, -1.f, Depth, 1.f);
			const FVector Surface(Pontoon.CenterLocation.X, Pontoon.CenterLocation.Y, Pontoon.WaterHeight);
			DrawDebugLine(World, Pontoon.CenterLocation, Surface, Colour, false, -1.f, Depth, 2.f);
			DrawDebugCircle(World, Surface, 22.f, 16, Colour, false, -1.f, Depth, 2.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		}
	}

	// The props: green biting, red out of the water, with each motor's thrust (longer for more; backwards astern).
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		const FVector Prop = Boat.GetPropLocation(Motor);
		const bool bWet = Boat.IsPropWet(Motor);
		DrawDebugSphere(World, Prop, 14.f, 10, bWet ? PropWet : PropDry, false, -1.f, Depth, 2.f);
		const float Output = Boat.GetMotorOutput(Motor);
		if (FMath::Abs(Output) > 0.01f)
		{
			DrawDebugDirectionalArrow(World, Prop, Prop + Boat.GetThrustDirection() * Output * 350.f, 40.f,
				bWet ? ThrustColour : FColor(130, 130, 130), false, -1.f, Depth, 3.f);
		}
	}
}

// --- Every frame ---

void ARiptidePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if RIPTIDE_WITH_DEV_MODE
	if (!CanUseDevMode())
	{
		return;
	}
	// Real frame time (not slowed or frozen), smoothed over about half a second.
	const float Frame = FApp::GetDeltaTime();
	SmoothedFrameSeconds = FMath::Lerp(SmoothedFrameSeconds, Frame, 1.f - FMath::Exp(-Frame / 0.5f));

	// The fly camera gone from under us (the level unloading, say): back to the crew member.
	if (DevCamera && !IsValid(DevCamera))
	{
		DevCamera = nullptr;
	}

	ARiptideBoat* Boat = GetDevBoat();
	if (bGodMode && Boat && Boat->GetFuelFraction() < 1.f)
	{
		Boat->AddFuel(1e6f);
	}
	if (bPhotoMode && GEngine)
	{
		// Anything that turned the messages back on while taking pictures.
		GEngine->bEnableOnScreenDebugMessages = false;
	}
	if (bPhysicsOverlay && !bPhotoMode && Boat)
	{
		DrawPhysicsOverlay(*Boat);
	}
#endif
}

void ARiptidePlayerController::PawnLeavingGame()
{
	// The engine destroys the leaving player's pawn. Flying, that's only the camera: land back in whatever was flown
	// from first. At the helm it would be the boat, under everyone else aboard: step off it, so it's the crew member
	// that goes.
	if (DevCamera)
	{
		StopFlying();
	}
	if (ARiptideBoat* Boat = Cast<ARiptideBoat>(GetPawn()); Boat && Boat->GetHelmsman())
	{
		Boat->LeaveHelm();
	}
	if (Cast<ARiptideBoat>(GetPawn()))
	{
		// Driving without a crew member at the helm (never in play, but never lose the boat): just let go of it.
		UnPossess();
	}
	Super::PawnLeavingGame();
}

void ARiptidePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The on-screen messages switch is the engine's, not this world's: hand it back.
	if (bPhotoMode && GEngine)
	{
		GEngine->bEnableOnScreenDebugMessages = bScreenMessagesBeforePhoto;
		bPhotoMode = false;
	}
	if (DevPanelContainer.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
	{
		GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(DevPanelContainer.ToSharedRef());
	}
	DevPanel.Reset();
	DevPanelContainer.Reset();
	Super::EndPlay(EndPlayReason);
}
