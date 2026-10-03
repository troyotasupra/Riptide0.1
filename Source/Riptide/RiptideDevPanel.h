#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"

class ARiptidePlayerController;

/** One line of the dev panel. */
struct FRiptideDevPanelRow
{
	enum class EKind : uint8
	{
		Heading,    // a section's name, with a rule above it
		Key,        // a dev key: its keycap, what it does, and its state
		Stat,       // a live reading: name and value
		Hint,       // a line of small print
		Legend      // a colour swatch and what it means (the physics overlay's colours)
	};

	EKind Kind = EKind::Stat;
	FString Key;
	FString Label;
	FString Value;
	FLinearColor Colour = FLinearColor::White;   // the value's colour, or the legend's swatch
	bool bDimmed = false;                         // a key that does nothing right now
};

/**
 * The dev mode's panel (F1): a dark card in the top-right corner listing every dev key with its current state, and live
 * readings (frame rate, spray, the boat, the camera, time). Drawn entirely here in the inventory's style; what it says
 * comes from the player controller every frame. It never takes the mouse or keyboard.
 */
class RIPTIDE_API SRiptideDevPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideDevPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptidePlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }

private:
	TWeakObjectPtr<ARiptidePlayerController> Owner;
	TUniquePtr<FSlateRoundedBoxBrush> CardBrush;
	TUniquePtr<FSlateRoundedBoxBrush> KeycapBrush;
};
