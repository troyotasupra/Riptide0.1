#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ARiptideBoat;

/** Which instrument an SRiptideGauge draws. */
enum class ERiptideGaugeKind : uint8
{
	Tachometer,     // engine revs, x1000 rpm
	Speedometer,    // speed through the water, knots
	Display         // the chart screen: gear, trim, fuel, heading, warnings
};

/**
 * One of the boat's dash instruments, drawn live from the boat's state every time its widget component redraws. Pure
 * Slate drawing (dials, ticks, needles, text), so the dash needs no textures or widget assets.
 */
class RIPTIDE_API SRiptideGauge : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideGauge) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptideBoat>, Boat)
		SLATE_ARGUMENT(ERiptideGaugeKind, Kind)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(256.f, 256.f); }

private:
	int32 PaintDial(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const ARiptideBoat& Boat) const;
	int32 PaintDisplay(const FGeometry& Geometry, FSlateWindowElementList& Out, int32 Layer, const ARiptideBoat& Boat) const;

	TWeakObjectPtr<ARiptideBoat> Boat;
	ERiptideGaugeKind Kind = ERiptideGaugeKind::Tachometer;
};
