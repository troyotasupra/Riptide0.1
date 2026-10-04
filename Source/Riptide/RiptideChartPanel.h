#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class ARiptideCharacter;

/**
 * The chart, opened with M (ARiptideChart's picture): parchment where nobody has been, the islands' ground where the
 * crew has, and on it you (a red arrow the way you face), the rest of the crew, rafts, the camp and the pool. With a
 * sea chart read, the islands are named. It's held up while you go on moving; M closes it.
 */
class RIPTIDE_API SRiptideChartPanel : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideChartPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptideCharacter>, Crew)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f, 1.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<ARiptideCharacter> Crew;
	mutable FSlateBrush PictureBrush;
};
