#include "RiptideHUD.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "RiptideBoat.h"
#include "RiptideCharacter.h"
#include "RiptideGameInstance.h"
#include "RiptideMenuWidgets.h"
#include "RiptideSettings.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RiptideHUD"

using namespace RiptideMenuStyle;

namespace
{
	/** The menu's own input sits above the crew member's and the helm's (0), below the dev mode's (100). */
	constexpr int32 MenuInputPriority = 50;
	constexpr float CardWidth = 460.f;

	bool IsMenuKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::P || Key == EKeys::Gamepad_Special_Right;
	}
}

// --- The menu ---

void SRiptidePauseMenu::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	OnResume = InArgs._OnResume;

	auto Item = [](const FText& Name, const TAttribute<FText>& Detail, TFunction<void()> Action)
	{
		return SNew(SRiptideButton).Text(Name).Detail(Detail).Width(CardWidth).Height(68.f).FontSize(22)
			.OnClicked_Lambda([Action]() { Action(); });
	};

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SRiptideBackdrop).Full(true)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(Switcher, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()
			[
				SNew(SBorder).BorderImage(PanelBrush()).BorderBackgroundColor(Srgb(0.02f, 0.035f, 0.045f, 0.94f)).Padding(FMargin(36.f, 30.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(LOCTEXT("Title", "RIPTIDE")).Font(Font(EFont::Black, 44, 260)).ColorAndOpacity(Ink())
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(2.f, 0.f, 0.f, 4.f)
					[
						SNew(SBox).WidthOverride(56.f).HeightOverride(3.f)
						[
							SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Accent())
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 22.f)
					[
						SNew(STextBlock).Text_Lambda([this]() { return SessionLine(); }).Font(Font(EFont::Regular, 15)).ColorAndOpacity(Dim())
						.WrapTextAt(CardWidth)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						SAssignNew(ResumeButton, SRiptideButton).Text(LOCTEXT("Resume", "Resume")).Width(CardWidth).Height(62.f).FontSize(22)
						.OnClicked_Lambda([this]() { OnResume.ExecuteIfBound(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						Item(LOCTEXT("Settings", "Settings"), FText::GetEmpty(), [this]() { ShowSettings(true); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						Item(LOCTEXT("Invite", "Invite friends"),
							TAttribute<FText>::CreateLambda([this]()
							{
								return Game.IsValid() && Game->IsUsingSteam() ? LOCTEXT("InviteSteam", "Opens the Steam overlay's invite list.")
									: LOCTEXT("InviteIp", "Without Steam, friends join by your IP address.");
							}),
							[this]() { Invite(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						Item(LOCTEXT("Leave", "Leave to main menu"),
							TAttribute<FText>::CreateLambda([this]()
							{
								return Game.IsValid() && Game->IsHosting() ? LOCTEXT("LeaveHost", "You're the host: this ends the game for your crew.")
									: FText::GetEmpty();
							}),
							[this]() { if (Game.IsValid()) { Game->LeaveGame(); } })
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						Item(LOCTEXT("Quit", "Quit to desktop"), FText::GetEmpty(), [this]() { if (Game.IsValid()) { Game->QuitToDesktop(); } })
					]
					// What Invite friends had to say, when it couldn't open the overlay.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text_Lambda([this]() { return Note; }).Font(Font(EFont::Regular, 15)).ColorAndOpacity(Accent())
						.WrapTextAt(CardWidth)
						.Visibility_Lambda([this]() { return Note.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("Hint", "Esc, P or Start: back to the game")).Font(Font(EFont::Regular, 13)).ColorAndOpacity(Dim())
					]
				]
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SBorder).BorderImage(PanelBrush()).BorderBackgroundColor(Srgb(0.02f, 0.035f, 0.045f, 0.94f)).Padding(FMargin(36.f, 26.f))
				[
					SNew(SBox).WidthOverride(720.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock).Text(LOCTEXT("SettingsTitle", "SETTINGS")).Font(Font(EFont::Condensed, 32, 120)).ColorAndOpacity(Ink())
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(Settings, SRiptideSettingsPanel)
							.WorldContext(Game)
							.OnBack_Lambda([this]() { ShowSettings(false); })
						]
					]
				]
			]
		]
	];
}

FText SRiptidePauseMenu::SessionLine() const
{
	const URiptideGameInstance* G = Game.Get();
	const UWorld* World = G ? G->GetWorld() : nullptr;
	if (!World)
	{
		return FText::GetEmpty();
	}
	const int32 Crew = World->GetGameState() ? World->GetGameState()->PlayerArray.Num() : 1;
	const FText CrewText = FText::Format(LOCTEXT("CrewCount", "{0} of {1} crew aboard"), Crew, URiptideGameInstance::MaxCrew);
	switch (World->GetNetMode())
	{
	case NM_ListenServer:
		if (G->IsUsingSteam())
		{
			return FText::Format(G->IsFriendsOnly() ? LOCTEXT("HostFriends", "Hosting for Steam friends · {0}") : LOCTEXT("HostPublic", "Hosting a public game · {0}"), CrewText);
		}
		return FText::Format(LOCTEXT("HostLan", "Hosting · {0} · friends join at {1}"), CrewText, FText::FromString(G->GetLocalAddress()));
	case NM_Client:
		return FText::Format(LOCTEXT("Client", "In a crew · {0}"), CrewText);
	default:
		return LOCTEXT("Solo", "Playing alone (not hosting). Host from the main menu to bring a crew.");
	}
}

void SRiptidePauseMenu::Invite()
{
	URiptideGameInstance* G = Game.Get();
	if (!G)
	{
		return;
	}
	if (G->ShowInviteOverlay())
	{
		Note = FText::GetEmpty();
		return;
	}
	if (G->IsUsingSteam())
	{
		Note = LOCTEXT("NoOverlay", "The Steam overlay didn't open: check it's enabled in Steam's settings (and that you're hosting or in a game).");
	}
	else if (G->IsHosting())
	{
		Note = FText::Format(LOCTEXT("ByIp", "Steam isn't running. Friends can join by IP: {0} on your network (port 7777 forwarded for the internet)."),
			FText::FromString(G->GetLocalAddress()));
	}
	else
	{
		Note = LOCTEXT("NotHosting", "Steam isn't running, and you're not the host: the host can share their IP address.");
	}
}

void SRiptidePauseMenu::ShowSettings(bool bShow)
{
	bSettings = bShow;
	Switcher->SetActiveWidgetIndex(bShow ? 1 : 0);
	if (bShow)
	{
		Settings->FocusFirst();
	}
	else
	{
		FocusFirst();
	}
}

void SRiptidePauseMenu::FocusFirst()
{
	PendingFocus = ResumeButton;
	Focus(ResumeButton);
}

void SRiptidePauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (PendingFocus.IsValid())
	{
		Focus(PendingFocus);
		if (PendingFocus->HasAnyUserFocus().IsSet())
		{
			PendingFocus.Reset();
		}
	}
}

FReply SRiptidePauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Handled();
	}
	const FKey Key = InKeyEvent.GetKey();
	// P and Start always go straight back to the game; Esc and B step back (out of the settings first).
	if (Key == EKeys::P || Key == EKeys::Gamepad_Special_Right || ((Key == EKeys::Escape || IsBackKey(InKeyEvent)) && !bSettings))
	{
		if (bSettings)
		{
			Settings->Close();
		}
		OnResume.ExecuteIfBound();
		return FReply::Handled();
	}
	if (IsBackKey(InKeyEvent) && bSettings)
	{
		Settings->Close();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FNavigationReply SRiptidePauseMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (HasKeyboardFocus())
	{
		bSettings ? Settings->FocusFirst() : FocusFirst();
		return FNavigationReply::Stop();
	}
	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

// --- The HUD ---

ARiptideHUD::ARiptideHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ARiptideHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* Player = GetOwningPlayerController();
	if (!Player || !Player->IsLocalController())
	{
		return;
	}
	// Into the game, the keyboard and mouse are the game's: the main menu left the viewport in its menu-only input
	// mode (cursor shown, keys going to the menu's widgets), and that outlives the menu across the map change.
	Player->SetInputMode(FInputModeGameOnly());
	Player->SetShowMouseCursor(false);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	BuildInput();
	// The in-game overlay: prompts, vitals, crosshair and notes (under the menus).
	if (GEngine && GEngine->GameViewport)
	{
		Overlay = SNew(SRiptideHudOverlay).Hud(this);
		GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 5);
	}
	// Its own input on the player's controller, so it works whatever the player controls (on foot, at the helm, or
	// the dev mode's camera).
	EnableInput(Player);
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		Input->BindAction(MenuAction, ETriggerEvent::Started, this, &ARiptideHUD::OnMenuKey);
		Input->BindActionValueLambda(FreeCursorAction, ETriggerEvent::Started, [this](const FInputActionValue&) { SetCursorFreed(true); });
		Input->BindActionValueLambda(FreeCursorAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { SetCursorFreed(false); });
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MenuMapping, MenuInputPriority);
	}
	URiptideSettingsSave::Get()->Apply(this);
}

bool ARiptideHUD::IsGameInputActive() const
{
	const APlayerController* Player = GetOwningPlayerController();
	const UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	if (!Player || !Viewport || Menu.IsValid() || Player->ShouldShowMouseCursor() || Player->IsMoveInputIgnored() || Player->IsLookInputIgnored())
	{
		return false;
	}
	if (Viewport->GetMouseCaptureMode() != EMouseCaptureMode::CapturePermanently)
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: the viewport isn't capturing the mouse (%d)"), int32(Viewport->GetMouseCaptureMode()));
		return false;
	}
	if (FSlateApplication::IsInitialized() && Viewport->GetGameViewportWidget().IsValid())
	{
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (Focused.Get() != static_cast<const SWidget*>(Viewport->GetGameViewportWidget().Get()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Riptide: the keyboard focus is on %s, not the game"), Focused.IsValid() ? *Focused->GetTypeAsString() : TEXT("nothing"));
			return false;
		}
	}
	return true;
}

void ARiptideHUD::BuildInput()
{
	if (MenuMapping)
	{
		return;
	}
	MenuAction = NewObject<UInputAction>(this, TEXT("IA_Menu"));
	MenuAction->ValueType = EInputActionValueType::Boolean;
	MenuMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Menu"));
	MenuMapping->MapKey(MenuAction, EKeys::Escape);
	MenuMapping->MapKey(MenuAction, EKeys::P);              // in the editor, where Esc stops play
	MenuMapping->MapKey(MenuAction, EKeys::Gamepad_Special_Right);
	FreeCursorAction = NewObject<UInputAction>(this, TEXT("IA_FreeCursor"));
	FreeCursorAction->ValueType = EInputActionValueType::Boolean;
	MenuMapping->MapKey(FreeCursorAction, EKeys::LeftAlt);
}

void ARiptideHUD::SetCursorFreed(bool bFree)
{
	APlayerController* Player = GetOwningPlayerController();
	// A menu already frees the cursor, and looks after the input itself.
	if (bFree == bCursorFreed || !Player || (bFree && Menu.IsValid()))
	{
		return;
	}
	bCursorFreed = bFree;
	if (bFree)
	{
		// The game still gets the keyboard (so letting go of Alt is seen, and the boat can still be driven), but
		// the mouse is a plain cursor that can leave the window.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		Player->SetInputMode(Mode);
		Player->SetShowMouseCursor(true);
		Player->SetIgnoreLookInput(true);
	}
	else if (!Menu.IsValid())
	{
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
		Player->SetIgnoreLookInput(false);
	}
}

void ARiptideHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetMenuOpen(false);
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	APlayerController* Player = GetOwningPlayerController();
	if (Player && Player->GetLocalPlayer() && MenuMapping)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(MenuMapping);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ARiptideHUD::OnMenuKey()
{
	SetMenuOpen(!IsMenuOpen());
}

void ARiptideHUD::SetMenuOpen(bool bOpen)
{
	APlayerController* Player = GetOwningPlayerController();
	if (bOpen == IsMenuOpen() || !Player || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (bOpen)
	{
		// The inventory is closed first (Esc there closes it; this is for Start, or a script).
		if (ARiptideCharacter* Crew = Cast<ARiptideCharacter>(Player->GetPawn()); Crew && Crew->IsInventoryOpen())
		{
			Crew->CloseInventory();
		}
		TWeakObjectPtr<ARiptideHUD> WeakThis(this);
		Menu = SNew(SRiptidePauseMenu)
			.Game(GetGameInstance<URiptideGameInstance>())
			.OnResume_Lambda([WeakThis]() { if (WeakThis.IsValid()) { WeakThis->SetMenuOpen(false); } });
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), 20);
		// Keys held as it opens are let go (the boat's throttle, a run, holding on), then nothing reaches the
		// player's crew member until it closes.
		Player->FlushPressedKeys();
		if (bCursorFreed)
		{
			bCursorFreed = false;
			Player->SetIgnoreLookInput(false);
		}
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Menu);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);    // free to go to another monitor
		Player->SetInputMode(Mode);
		Player->SetShowMouseCursor(true);
		Player->SetIgnoreMoveInput(true);
		Player->SetIgnoreLookInput(true);
		bIgnoringInput = true;
		Menu->FocusFirst();
	}
	else
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		Menu.Reset();
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
		// The keyboard focus went with the menu's widgets: give it back to the game's viewport.
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
		if (bIgnoringInput)
		{
			Player->SetIgnoreMoveInput(false);
			Player->SetIgnoreLookInput(false);
			bIgnoringInput = false;
		}
	}
}

void ARiptideHUD::ShowMenuSettings(bool bShow)
{
	if (Menu.IsValid())
	{
		Menu->ShowSettings(bShow);
	}
}

void ARiptideHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double RealNow = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	Notes.RemoveAll([RealNow](const FNote& N) { return N.Until < RealNow; });
	// The player's field of view, on their own eyes and at the helm (not on other cameras, like the dev mode's).
	const APlayerController* Player = GetOwningPlayerController();
	AActor* Target = Player ? Player->GetViewTarget() : nullptr;
	if (Target && (Target->IsA<ARiptideCharacter>() || Target->IsA<ARiptideBoat>()))
	{
		if (UCameraComponent* Camera = Target->FindComponentByClass<UCameraComponent>())
		{
			const float FieldOfView = URiptideSettingsSave::Get()->FieldOfView;
			if (!FMath::IsNearlyEqual(Camera->FieldOfView, FieldOfView))
			{
				Camera->SetFieldOfView(FieldOfView);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
