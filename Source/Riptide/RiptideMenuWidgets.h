#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/SlateDelegates.h"

class SEditableTextBox;
struct FSlateBrush;

/**
 * The menus' look, shared by the main menu and the in-game menu: the inventory's dark military cards (see
 * SRiptideInventory) with an amber edge on whatever is selected, Roboto in condensed capitals, and the building
 * blocks below, drawn here in C++ like the rest of the game's UI.
 *
 * Every control works with the mouse, the keyboard (arrows to move, Enter to choose, Esc to go back) and a gamepad
 * (stick or D-pad to move, A to choose, B to go back): they take Slate's focus, and show it the way they show the
 * mouse hovering.
 */
namespace RiptideMenuStyle
{
	/** sRGB, as a colour picker shows it, to the linear colour Slate draws in. */
	RIPTIDE_API FLinearColor Srgb(float R, float G, float B, float A = 1.f);

	// The palette.
	RIPTIDE_API FLinearColor Ink();         // main text
	RIPTIDE_API FLinearColor Dim();         // secondary text
	RIPTIDE_API FLinearColor Heading();     // section headings (the inventory's)
	RIPTIDE_API FLinearColor Accent();      // what's selected: a searchlight amber
	RIPTIDE_API FLinearColor Edge();        // card edges
	RIPTIDE_API FLinearColor Warning();

	enum class EFont : uint8 { Regular, Bold, Condensed, Black, Light };
	RIPTIDE_API FSlateFontInfo Font(EFont Face, int32 Size, int32 LetterSpacing = 0);

	/** Brushes: a menu panel, a card at rest, and a card that's selected. */
	RIPTIDE_API const FSlateBrush* PanelBrush();
	RIPTIDE_API const FSlateBrush* CardBrush();
	RIPTIDE_API const FSlateBrush* CardHotBrush();
	RIPTIDE_API const FSlateBrush* White();

	RIPTIDE_API FVector2f Measure(const FString& Text, const FSlateFontInfo& Font);

	// Painting helpers for the widgets that draw themselves.
	RIPTIDE_API void FillRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
		const FLinearColor& Colour);
	RIPTIDE_API void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
		const FSlateBrush* Brush, const FLinearColor& Tint);
	RIPTIDE_API void DrawString(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& String, const FSlateFontInfo& Font,
		const FVector2f& Pos, const FLinearColor& Colour, float AlignX = 0.f);
	/** A chevron pointing left (-1) or right (1), centred on Centre. */
	RIPTIDE_API void Chevron(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Centre, float Size, int32 Direction,
		const FLinearColor& Colour);

	/** A styled single-line text field (the callsign, an IP address). */
	RIPTIDE_API TSharedRef<SEditableTextBox> MakeTextField(const FText& Hint, int32 FontSize = 18, FOnTextChanged OnChanged = FOnTextChanged(),
		FOnTextCommitted OnCommitted = FOnTextCommitted());

	/** True if the key event or navigation is "back" (Esc, gamepad B). */
	RIPTIDE_API bool IsBackKey(const FKeyEvent& Key);

	/** Moves keyboard and gamepad focus to a widget (no-op if it can't take it). */
	RIPTIDE_API void Focus(const TSharedPtr<SWidget>& Widget);
}

/**
 * A menu button drawn as a card: a label in condensed capitals, maybe a line of detail under it, and maybe columns
 * of text on the right (the game browser's rows). Lit with an amber edge while hovered or focused; Selected keeps it
 * lit (a chosen option, a selected game). Click, Enter, Space or gamepad A presses it.
 */
class RIPTIDE_API SRiptideButton : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideButton)
		: _Width(420.f), _Height(60.f), _FontSize(22), _Centred(false), _Primary(false), _Selected(false) {}
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ATTRIBUTE(FText, Detail)
		/** Columns of text at fractions of the width (the browser's crew and ping), each with its fraction. */
		SLATE_ATTRIBUTE(TArray<FText>, Columns)
		SLATE_ARGUMENT(TArray<float>, ColumnPositions)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_ARGUMENT(int32, FontSize)
		SLATE_ARGUMENT(bool, Centred)
		/** The screen's main action: drawn with an amber fill. */
		SLATE_ARGUMENT(bool, Primary)
		SLATE_ATTRIBUTE(bool, Selected)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_EVENT(FSimpleDelegate, OnDoubleClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	void SetText(const FText& NewText) { Text.Set(NewText); }

private:
	bool IsLit() const;

	TAttribute<FText> Text;
	TAttribute<FText> Detail;
	TAttribute<TArray<FText>> Columns;
	TArray<float> ColumnPositions;
	float Width = 420.f;
	float Height = 60.f;
	int32 FontSize = 22;
	bool bCentred = false;
	bool bPrimary = false;
	TAttribute<bool> Selected;
	FSimpleDelegate OnClicked;
	FSimpleDelegate OnDoubleClicked;
	bool bPressed = false;
};

/**
 * A setting or a part of the crew member's look: its name on the left, its value on the right between two chevrons.
 * Left and right (arrow keys, D-pad, stick, or clicking a chevron) step through the choices. With Fraction bound it
 * also draws a bar (a volume, the sensitivity) that can be clicked or dragged to a value.
 */
class RIPTIDE_API SRiptideStepper : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnStep, int32 /*Direction: -1 or 1*/);
	DECLARE_DELEGATE_OneParam(FOnSetFraction, float);

	SLATE_BEGIN_ARGS(SRiptideStepper) : _Width(720.f), _Height(40.f) {}
		SLATE_ATTRIBUTE(FText, Label)
		SLATE_ATTRIBUTE(FText, Value)
		SLATE_ATTRIBUTE(TOptional<float>, Fraction)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)
		SLATE_EVENT(FOnStep, OnStep)
		SLATE_EVENT(FOnSetFraction, OnSetFraction)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, Height); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; }
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	/** Where things sit across the row (local X): the value's area between the chevrons, and the bar. */
	float ValueLeft() const { return Width * 0.5f; }
	float ValueRight() const { return Width - 16.f; }
	void SetFractionAt(float LocalX);

	TAttribute<FText> Label;
	TAttribute<FText> Value;
	TAttribute<TOptional<float>> Fraction;
	float Width = 720.f;
	float Height = 40.f;
	FOnStep OnStep;
	FOnSetFraction OnSetFraction;
	bool bDragging = false;
	/** When the last step happened (the value flashes briefly). */
	double LastStepTime = -10.0;
};

/** A thin spinning arc: something is under way. */
class RIPTIDE_API SRiptideSpinner : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideSpinner) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { SetCanTick(false); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(22.f, 22.f); }
};

/** The darkening behind the menus: heavy on the left where the buttons are, clear over the scene on the right. */
class RIPTIDE_API SRiptideBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideBackdrop) : _Strength(1.f), _Full(false) {}
		SLATE_ATTRIBUTE(float, Strength)
		/** Even all over (the in-game menu over the game), instead of from the left. */
		SLATE_ARGUMENT(bool, Full)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { Strength = InArgs._Strength; bFull = InArgs._Full; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }

private:
	TAttribute<float> Strength;
	bool bFull = false;
};

/**
 * The settings screen, the same in the main menu and the in-game menu: controls (sensitivity, invert Y, field of
 * view), audio (master, effects, ambience) and graphics (quality, resolution, window mode, VSync, frame-rate limit).
 * Every change takes effect at once; leaving the screen saves them.
 */
class RIPTIDE_API SRiptideSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideSettingsPanel) {}
		/** The world to apply the volumes in. */
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, WorldContext)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Focuses the first row (for keyboard and gamepad). */
	void FocusFirst();

	/** Saves everything and calls OnBack. */
	void Close();

private:
	TSharedRef<SWidget> Section(const FText& Title);
	void ApplyGameplay();
	void ApplyGraphics(bool bResolution);
	void ResetToDefaults();

	TWeakObjectPtr<UObject> WorldContext;
	FSimpleDelegate OnBack;
	TSharedPtr<SWidget> FirstRow;
	TArray<FIntPoint> Resolutions;
};
