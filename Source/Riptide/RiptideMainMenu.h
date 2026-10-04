#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RiptideAppearance.h"
#include "Widgets/SCompoundWidget.h"
#include "RiptideMainMenu.generated.h"

class ARiptideCrewPreview;
class ARiptideMenuCamera;
class SEditableTextBox;
class SRiptideSettingsPanel;
class SVerticalBox;
class SWidgetSwitcher;
class URiptideGameInstance;

/** The main menu's screens. */
UENUM(BlueprintType)
enum class ERiptideMenuScreen : uint8
{
	Home,
	Host,
	Join,
	Crew,
	Settings
};

/**
 * The main menu, over the live shot of the boat: the big RIPTIDE title and Host, Join, Crew, Settings and Quit, each
 * opening its screen on the left (the crew screen shows the crew member turning on the right). The footer has the
 * player's callsign, whether Steam is up, and the controls. Messages (a failed join, a lost connection) show as a
 * banner; while hosting or joining, a status card covers the menu.
 */
class RIPTIDE_API SRiptideMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URiptideGameInstance>, Game)
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptideMenuCamera>, MenuCamera)
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptideCrewPreview>, Preview)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;

	void ShowScreen(ERiptideMenuScreen Screen);
	ERiptideMenuScreen GetScreen() const { return Current; }

	/** Puts keyboard and gamepad focus on the current screen's first control. */
	void FocusScreen();

	/** Shows a message in the banner for a while. */
	void ShowMessage(const FText& Message);

private:
	TSharedRef<SWidget> MakeTitle();
	TSharedRef<SWidget> MakeHome();
	TSharedRef<SWidget> MakeHost();
	TSharedRef<SWidget> MakeJoin();
	TSharedRef<SWidget> MakeCrew();
	TSharedRef<SWidget> MakeFooter();
	TSharedRef<SWidget> MakeBusy();
	TSharedRef<SWidget> ScreenHeader(const FText& Title, const FText& Blurb);

	void RebuildGameList();
	void JoinSelected();
	void JoinTypedAddress();
	void OpenCrew();
	void SaveCrew();
	bool IsBusy() const;

	TWeakObjectPtr<URiptideGameInstance> Game;
	TWeakObjectPtr<ARiptideMenuCamera> MenuCamera;
	TWeakObjectPtr<ARiptideCrewPreview> Preview;

	ERiptideMenuScreen Current = ERiptideMenuScreen::Home;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TMap<ERiptideMenuScreen, TSharedPtr<SWidget>> FirstControl;

	// Host: carry on with the saved world or start a new one, and who can join.
	bool bFriendsOnly = true;
	bool bContinue = true;
	bool bHaveSave = false;
	FText SaveSummary;
	/** Reads what's saved, for the host screen. */
	void RefreshSave();

	// Join
	TSharedPtr<SVerticalBox> GameList;
	TSharedPtr<SEditableTextBox> AddressField;
	int32 ShownListVersion = -1;
	int32 SelectedGame = INDEX_NONE;

	// Crew: the look being edited (saved on Save, dropped on Back).
	FRiptideAppearance EditingLook;
	TSharedPtr<SEditableTextBox> CallsignField;
	FSlateBrush PreviewBrush;

	// The banner.
	FText Message;
	double MessageUntil = 0.0;

	TSharedPtr<SRiptideSettingsPanel> Settings;
	TSharedPtr<SWidget> BusyCancel;
	/** Focus to give once the widget is on screen (Slate can't focus a widget before it's first drawn). */
	TSharedPtr<SWidget> PendingFocus;
	bool bWasBusy = false;
};

/**
 * The main menu map's HUD: puts the main menu on screen and hands it the mouse, keyboard and gamepad.
 */
UCLASS()
class RIPTIDE_API ARiptideMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Opens a screen of the menu (for scripts and screenshots; players use the buttons). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void ShowScreen(ERiptideMenuScreen Screen);

	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsMenuShown() const { return Menu.IsValid(); }

private:
	TSharedPtr<SRiptideMainMenu> Menu;
};
