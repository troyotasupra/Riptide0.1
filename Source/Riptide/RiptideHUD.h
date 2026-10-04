#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Widgets/SCompoundWidget.h"
#include "RiptideHudOverlay.h"
#include "RiptideHUD.generated.h"

class SRiptideButton;
class SRiptideHudOverlay;
class SRiptideSettingsPanel;
class SWidgetSwitcher;
class UInputAction;
class UInputMappingContext;
class URiptideGameInstance;

/**
 * The in-game menu (Esc): Resume, Settings, Invite friends, Leave to main menu, Quit to desktop, over the game, which
 * keeps running underneath (it's multiplayer: nothing pauses).
 */
class RIPTIDE_API SRiptidePauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptidePauseMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URiptideGameInstance>, Game)
		SLATE_EVENT(FSimpleDelegate, OnResume)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void ShowSettings(bool bShow);
	bool IsShowingSettings() const { return bSettings; }
	void FocusFirst();

private:
	FText SessionLine() const;
	void Invite();

	TWeakObjectPtr<URiptideGameInstance> Game;
	FSimpleDelegate OnResume;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SRiptideButton> ResumeButton;
	TSharedPtr<SRiptideSettingsPanel> Settings;
	bool bSettings = false;
	FText Note;
	/** Focus to give once the widget is on screen (Slate can't focus a widget before it's first drawn). */
	TSharedPtr<SWidget> PendingFocus;
};

/**
 * The game's HUD for each player: it owns the in-game menu. Esc, P or the gamepad's Start (Menu) button opens and
 * closes it; in the editor Esc stops play, so use P there. When the inventory is open, Esc closes that first.
 *
 * While the menu is open the player's crew member (or the boat at the helm) gets no input, but the world goes on.
 * The HUD also keeps the camera's field of view at the player's setting.
 */
UCLASS()
class RIPTIDE_API ARiptideHUD : public AHUD
{
	GENERATED_BODY()

public:
	ARiptideHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Opens or closes the in-game menu (what Esc does; also for tests). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void SetMenuOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsMenuOpen() const { return Menu.IsValid(); }

	/** True when the keyboard and mouse drive the game (not a menu): the viewport captures the mouse, has the
	 * keyboard focus and shows no cursor, and the player's movement and look aren't held off. For tests. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsGameInputActive() const;

	// What the in-game overlay shows (SRiptideHudOverlay; posted through the RiptideHud functions).
	struct FPrompt
	{
		FString Text;
		FLinearColor Colour = FLinearColor::White;
		float Progress = -1.f;
		double Time = -1.0;
	};
	struct FNote
	{
		FString Text;
		FLinearColor Colour = FLinearColor::White;
		double Until = 0.0;
		int32 Key = INDEX_NONE;
	};
	FPrompt Prompts[int32(RiptideHud::ESlot::Count)];
	float VitalHealth = -1.f, VitalFood = -1.f, VitalWater = -1.f, VitalCold = 0.f;
	bool bVitalSick = false, bVitalWarm = false;
	float FadeAlpha = 0.f;
	double FadeTime = -1.0;
	double VitalsTime = -1.0;
	TArray<FNote> Notes;

	/** Shows the settings in the open menu (or back to its buttons). */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	void ShowMenuSettings(bool bShow);

private:
	void BuildInput();
	void OnMenuKey();
	/** Holding Left Alt frees the cursor (to reach another monitor) without opening a menu; the view stops turning. */
	void SetCursorFreed(bool bFree);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MenuMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MenuAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FreeCursorAction;

	TSharedPtr<SRiptidePauseMenu> Menu;
	TSharedPtr<SRiptideHudOverlay> Overlay;
	/** Set while the menu holds the player's movement and look input off. */
	bool bIgnoringInput = false;
	/** Set while Left Alt is held and the cursor is free. */
	bool bCursorFreed = false;
};
