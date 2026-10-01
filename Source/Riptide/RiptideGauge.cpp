#include "RiptideGauge.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "RiptideBoat.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor Face(0.012f, 0.013f, 0.016f);
	const FLinearColor Bezel(0.25f, 0.26f, 0.28f);
	const FLinearColor Ink(0.92f, 0.93f, 0.9f);
	const FLinearColor Dim(0.45f, 0.47f, 0.48f);
	const FLinearColor Needle(1.f, 0.32f, 0.08f);
	const FLinearColor Red(0.95f, 0.1f, 0.06f);
	const FLinearColor Green(0.2f, 0.9f, 0.35f);
	const FLinearColor Amber(1.f, 0.65f, 0.1f);

	// The dials sweep 240 degrees, from lower left (the dial's zero) clockwise to lower right.
	FVector2f DialPoint(const FVector2f& Centre, float Radius, float Fraction)
	{
		const float Angle = FMath::DegreesToRadians(-120.f + 240.f * FMath::Clamp(Fraction, 0.f, 1.f));
		return Centre + Radius * FVector2f(FMath::Sin(Angle), -FMath::Cos(Angle));
	}

	void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FString& String, int32 Size,
		const FVector2f& Centre, const FLinearColor& Colour, bool bBold = true)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
		FVector2f Extent(Size * 0.6f * String.Len(), Size * 1.2f);
		if (FSlateApplication::IsInitialized())
		{
			Extent = FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Font));
		}
		FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Centre - Extent * 0.5f)),
			String, Font, ESlateDrawEffect::None, Colour);
	}

	void Disc(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Centre, float Radius,
		const FSlateBrush& Brush)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(2.f * Radius), FSlateLayoutTransform(Centre - FVector2f(Radius))),
			&Brush, ESlateDrawEffect::None, Brush.TintColor.GetSpecifiedColor());
	}

	void Bar(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& TopLeft, const FVector2f& Size,
		const FLinearColor& Colour)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Colour);
	}
}

void SRiptideGauge::Construct(const FArguments& InArgs)
{
	Boat = InArgs._Boat;
	Kind = InArgs._Kind;
	// Redrawn every frame with the boat.
	ForceVolatile(true);
}

int32 SRiptideGauge::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const ARiptideBoat* B = Boat.Get();
	if (!B)
	{
		return LayerId;
	}
	return Kind == ERiptideGaugeKind::Display
		? PaintDisplay(AllottedGeometry, OutDrawElements, LayerId, *B)
		: PaintDial(AllottedGeometry, OutDrawElements, LayerId, *B);
}

int32 SRiptideGauge::PaintDial(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const ARiptideBoat& B) const
{
	static const FSlateRoundedBoxBrush BezelBrush(Bezel, 128.f);
	static const FSlateRoundedBoxBrush FaceBrush(Face, 128.f);
	static const FSlateRoundedBoxBrush HubBrush(FLinearColor(0.08f, 0.08f, 0.09f), 128.f);

	const FVector2f Size = FVector2f(G.GetLocalSize());
	const FVector2f C = Size * 0.5f;
	const float R = 0.5f * FMath::Min(Size.X, Size.Y);
	Disc(Out, Layer, G, C, R, BezelBrush);
	Disc(Out, Layer + 1, G, C, R * 0.93f, FaceBrush);
	const int32 L = Layer + 2;

	const bool bTach = Kind == ERiptideGaugeKind::Tachometer;
	const float MaxValue = bTach ? 7.f : 30.f;             // x1000 rpm, or knots
	const float Major = bTach ? 1.f : 5.f;
	const float Minor = bTach ? 0.5f : 1.f;
	// An engine racing on the rev limiter reads into the red, but the needle stops at the end of the dial.
	const float Value = FMath::Min(bTach ? B.GetEngineRpm() / 1000.f : B.GetSpeedKnots(), MaxValue * 1.02f);

	// Red line: the top of the rev range.
	if (bTach)
	{
		TArray<FVector2f> Arc;
		for (float V = 6.f; V <= MaxValue + 0.01f; V += 0.1f)
		{
			Arc.Add(DialPoint(C, R * 0.8f, V / MaxValue));
		}
		FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), Arc, ESlateDrawEffect::None, Red, true, R * 0.07f);
	}
	for (float V = 0.f; V <= MaxValue + 0.001f; V += Minor)
	{
		const bool bMajor = FMath::IsNearlyZero(FMath::Fmod(V + 0.001f, Major), 0.01f);
		const float F = V / MaxValue;
		TArray<FVector2f> Tick = { DialPoint(C, R * (bMajor ? 0.7f : 0.76f), F), DialPoint(C, R * 0.84f, F) };
		FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), Tick, ESlateDrawEffect::None, Ink, true, bMajor ? R * 0.03f : R * 0.015f);
		if (bMajor)
		{
			Text(Out, L, G, FString::FromInt(FMath::RoundToInt(V)), FMath::RoundToInt(R * 0.13f), DialPoint(C, R * 0.55f, F), Ink);
		}
	}
	Text(Out, L, G, bTach ? TEXT("RPM x1000") : TEXT("KNOTS"), FMath::RoundToInt(R * 0.08f), C + FVector2f(0.f, -R * 0.3f), Dim, false);
	Text(Out, L, G, bTach ? FString::Printf(TEXT("%d"), FMath::RoundToInt(B.GetEngineRpm() / 10.f) * 10) : FString::Printf(TEXT("%.1f"), Value),
		FMath::RoundToInt(R * 0.12f), C + FVector2f(0.f, R * 0.45f), Ink);

	// Needle.
	TArray<FVector2f> Hand = { DialPoint(C, -R * 0.14f, Value / MaxValue), DialPoint(C, R * 0.78f, Value / MaxValue) };
	FSlateDrawElement::MakeLines(Out, L + 1, G.ToPaintGeometry(), Hand, ESlateDrawEffect::None, Needle, true, R * 0.045f);
	Disc(Out, L + 2, G, C, R * 0.08f, HubBrush);
	return L + 2;
}

int32 SRiptideGauge::PaintDisplay(const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const ARiptideBoat& B) const
{
	const FVector2f Size = FVector2f(G.GetLocalSize());
	const float U = Size.Y / 100.f;                          // layout unit: 1% of the screen's height
	// Font sizes are in points, which come out about a third bigger than pixels: scaled so each row fits its space.
	auto Pt = [U](float Units) { return FMath::Max(6, FMath::RoundToInt(Units * U * 0.72f)); };
	Bar(Out, Layer, G, FVector2f(0.f), Size, FLinearColor(0.004f, 0.012f, 0.02f));
	const int32 L = Layer + 1;

	// Gear, big, in the middle: F green, N amber, R red.
	const ERiptideGear Gear = B.GetGear();
	const TCHAR* GearText = Gear == ERiptideGear::Forward ? TEXT("F") : Gear == ERiptideGear::Reverse ? TEXT("R") : TEXT("N");
	const FLinearColor GearColour = Gear == ERiptideGear::Forward ? Green : Gear == ERiptideGear::Reverse ? Red : Amber;
	Text(Out, L, G, TEXT("GEAR"), Pt(7.f), FVector2f(Size.X * 0.5f, 17 * U), Dim, false);
	Text(Out, L, G, GearText, Pt(34.f), FVector2f(Size.X * 0.5f, 44 * U), GearColour);
	Text(Out, L, G, FString::Printf(TEXT("THROTTLE %d%%"), FMath::RoundToInt(B.GetThrottleOpening() * 100.f)),
		Pt(6.f), FVector2f(Size.X * 0.5f, 71 * U), Ink, false);

	// Heading along the bottom.
	const int32 Heading = (FMath::RoundToInt(B.GetActorRotation().Yaw) + 450) % 360;   // 0 = north (+Y is east, +X north)
	Text(Out, L, G, FString::Printf(TEXT("HDG %03d"), Heading), Pt(8.f), FVector2f(Size.X * 0.5f, 86 * U), Ink);

	// Trim on the left: a vertical scale, in (down) at the bottom, out (up) at the top, with the neutral mark.
	const float BarW = 7 * U, BarTop = 18 * U, BarH = 64 * U;
	auto VerticalScale = [&](float X, const TCHAR* Label, float Fraction, const FLinearColor& Colour, float Mark)
	{
		Text(Out, L, G, Label, Pt(6.f), FVector2f(X + BarW * 0.5f, 10 * U), Dim, false);
		Bar(Out, L, G, FVector2f(X, BarTop), FVector2f(BarW, BarH), FLinearColor(0.05f, 0.06f, 0.07f));
		const float FillH = BarH * FMath::Clamp(Fraction, 0.f, 1.f);
		Bar(Out, L + 1, G, FVector2f(X, BarTop + BarH - FillH), FVector2f(BarW, FillH), Colour);
		if (Mark >= 0.f)
		{
			Bar(Out, L + 2, G, FVector2f(X - 1.5f * U, BarTop + BarH * (1.f - Mark) - 0.6f * U), FVector2f(BarW + 3 * U, 1.2f * U), Ink);
		}
	};
	const float TrimRange = B.GetMaxTrimDeg() - B.GetMinTrimDeg();
	VerticalScale(8 * U, TEXT("TRIM"), (B.GetTrimDeg() - B.GetMinTrimDeg()) / TrimRange, FLinearColor(0.3f, 0.7f, 1.f),
		-B.GetMinTrimDeg() / TrimRange);
	Text(Out, L, G, FString::Printf(TEXT("%+d"), FMath::RoundToInt(B.GetTrimDeg())), Pt(6.f),
		FVector2f(8 * U + BarW * 0.5f, 90 * U), Ink, false);

	// Fuel on the right, amber under a quarter.
	const float Fuel = B.GetFuelFraction();
	VerticalScale(Size.X - 8 * U - BarW, TEXT("FUEL"), Fuel, Fuel < 0.25f ? Amber : Green, -1.f);
	Text(Out, L, G, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Fuel * 100.f)), Pt(6.f),
		FVector2f(Size.X - 8 * U - BarW * 0.5f, 90 * U), Ink, false);

	// Warnings across the top.
	FString Warning;
	FLinearColor WarningColour = Amber;
	const TCHAR* Sides[2] = { TEXT("PORT"), TEXT("STBD") };
	for (int32 Motor = 0; Motor < 2 && Warning.IsEmpty(); ++Motor)
	{
		if (B.GetMotorHealth(Motor) <= 0.f)
		{
			Warning = FString::Printf(TEXT("%s ENGINE FAILURE"), Sides[Motor]);
			WarningColour = Red;
		}
		else if (B.GetMotorHealth(Motor) < 0.5f)
		{
			Warning = FString::Printf(TEXT("CHECK %s ENGINE"), Sides[Motor]);
		}
	}
	if (Warning.IsEmpty() && B.GetFuelFraction() <= 0.f)
	{
		Warning = TEXT("OUT OF FUEL");
		WarningColour = Red;
	}
	else if (Warning.IsEmpty() && B.GetFuelFraction() < 0.15f)
	{
		Warning = TEXT("LOW FUEL");
	}
	else if (Warning.IsEmpty() && !B.IsPropellerSubmerged() && B.GetEngineRpm() > 0.f)
	{
		Warning = TEXT("PROP VENTILATING");
	}
	if (!Warning.IsEmpty())
	{
		Text(Out, L + 3, G, Warning, Pt(6.f), FVector2f(Size.X * 0.5f, 6 * U), WarningColour);
	}
	return L + 3;
}
