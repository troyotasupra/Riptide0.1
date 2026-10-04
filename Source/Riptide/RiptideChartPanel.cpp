#include "RiptideChartPanel.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Rendering/DrawElements.h"
#include "RiptideChart.h"
#include "RiptideCharacter.h"
#include "RiptideMenuWidgets.h"
#include "RiptidePond.h"
#include "RiptideRaft.h"
#include "RiptideStructure.h"

using namespace RiptideMenuStyle;

namespace
{
	const FLinearColor ChartInk(0.28f, 0.2f, 0.13f);
	const FLinearColor YouRed(0.8f, 0.15f, 0.1f);
	const FLinearColor CrewBlue(0.2f, 0.45f, 0.85f);

	void Lines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& Points, const FLinearColor& Colour, float Thickness)
	{
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, Thickness);
	}
}

void SRiptideChartPanel::Construct(const FArguments& InArgs)
{
	Crew = InArgs._Crew;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SRiptideChartPanel::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	ARiptideCharacter* Me = Crew.Get();
	ARiptideChart* Chart = Me ? ARiptideChart::Get(Me) : nullptr;
	if (!Me || !Chart)
	{
		return LayerId;
	}
	const FVector2f Screen = G.GetLocalSize();
	const float Scale = FMath::Clamp(Screen.Y / 1080.f, 0.6f, 2.f);
	// What's shown: everything the crew has seen (and the islands, once a sea chart's been read), with a little sea
	// round it; never less than a quarter of a kilometre across. Early on it's a close-up of the beach; it widens as
	// the crew goes further.
	const FIntPoint PictureSize = Chart->GetPictureSize();
	FBox2D View = Chart->GetSeenBounds();
	if (Chart->IsRead())
	{
		for (const FRiptideChartIsland& Island : Chart->GetIslands())
		{
			View += FBox2D(Island.Centre - FVector2D(Island.Radius), Island.Centre + FVector2D(Island.Radius));
		}
	}
	if (!View.bIsValid)
	{
		View = FBox2D(FVector2D(Me->GetActorLocation()), FVector2D(Me->GetActorLocation()));
	}
	View = View.ExpandBy(4000.f);
	const FVector2D Middle = View.GetCenter();
	const double Half = FMath::Max3(View.GetExtent().X, View.GetExtent().Y, 12500.0);
	View = FBox2D(Middle - FVector2D(Half), Middle + FVector2D(Half));
	// On the picture (north-west corner to south-east), kept inside it.
	const FVector2D Bound(PictureSize);
	FVector2D TopLeft = Chart->WorldToPixel(FVector2D(View.Max.X, View.Min.Y));
	FVector2D BottomRight = Chart->WorldToPixel(FVector2D(View.Min.X, View.Max.Y));
	TopLeft = FVector2D(FMath::Clamp(TopLeft.X, 0.0, Bound.X - 1.0), FMath::Clamp(TopLeft.Y, 0.0, Bound.Y - 1.0));
	BottomRight = FVector2D(FMath::Clamp(BottomRight.X, TopLeft.X + 1.0, Bound.X), FMath::Clamp(BottomRight.Y, TopLeft.Y + 1.0, Bound.Y));
	const FVector2D Shown = BottomRight - TopLeft;
	const FVector2f Room(640.f * Scale, 600.f * Scale);
	const float Fit = FMath::Min(Room.X / Shown.X, Room.Y / Shown.Y);
	const FVector2f Drawn(Shown.X * Fit, Shown.Y * Fit);
	const float Pad = 22.f * Scale;
	const FVector2f SheetSize = Drawn + FVector2f(Pad * 2.f, Pad * 2.f + 58.f * Scale);
	const FVector2f Sheet = (Screen - SheetSize) * 0.5f;
	FillRect(Out, LayerId, G, Sheet - FVector2f(3.f * Scale), SheetSize + FVector2f(6.f * Scale), Srgb(0.18f, 0.12f, 0.07f, 0.9f));
	FillRect(Out, LayerId + 1, G, Sheet, SheetSize, Srgb(0.86f, 0.8f, 0.66f));
	const FSlateFontInfo Title = Font(EFont::Condensed, FMath::RoundToInt(26 * Scale), 120);
	DrawString(Out, LayerId + 2, G, TEXT("CHART"), Title, Sheet + FVector2f(Pad, 10.f * Scale), ChartInk);
	const FVector2f Origin = Sheet + FVector2f(Pad, Pad + 34.f * Scale);

	if (UTexture2D* Picture = Chart->GetPicture())
	{
		PictureBrush.SetResourceObject(Picture);
		PictureBrush.ImageSize = Shown;
		PictureBrush.DrawAs = ESlateBrushDrawType::Image;
		PictureBrush.SetUVRegion(FBox2f(FVector2f(TopLeft / Bound), FVector2f(BottomRight / Bound)));
		FSlateDrawElement::MakeBox(Out, LayerId + 2, G.ToPaintGeometry(FVector2f(Drawn), FSlateLayoutTransform(Origin)), &PictureBrush,
			ESlateDrawEffect::None, FLinearColor::White);
	}
	// North is up.
	const FSlateFontInfo Small = Font(EFont::Bold, FMath::RoundToInt(13 * Scale));
	DrawString(Out, LayerId + 3, G, TEXT("N"), Font(EFont::Bold, FMath::RoundToInt(16 * Scale)), Origin + FVector2f(Drawn.X - 14.f * Scale, 6.f * Scale), ChartInk, 0.5f);
	Lines(Out, LayerId + 3, G, { Origin + FVector2f(Drawn.X - 14.f * Scale, 44.f * Scale), Origin + FVector2f(Drawn.X - 14.f * Scale, 26.f * Scale) }, ChartInk, 2.f * Scale);

	auto ToSheet = [&](const FVector& World)
	{
		const FVector2D Pixel = Chart->WorldToPixel(FVector2D(World)) - TopLeft;
		return Origin + FVector2f(Pixel.X * Fit, Pixel.Y * Fit);
	};
	auto OnSheet = [&](const FVector2f& P) { return P.X >= Origin.X && P.Y >= Origin.Y && P.X <= Origin.X + Drawn.X && P.Y <= Origin.Y + Drawn.Y; };
	auto Mark = [&](const FVector& World, const FString& Label, const FLinearColor& Colour, float Size)
	{
		const FVector2f At = ToSheet(World);
		if (!OnSheet(At) || !Chart->IsSeen(FVector2D(World)))
		{
			return;
		}
		FillRect(Out, LayerId + 4, G, At - FVector2f(Size * 0.5f * Scale), FVector2f(Size * Scale), Colour);
		if (!Label.IsEmpty())
		{
			DrawString(Out, LayerId + 4, G, Label, Small, At + FVector2f(7.f * Scale, -8.f * Scale), Colour);
		}
	};

	UWorld* World = Me->GetWorld();
	// The islands, named once a sea chart's been read (or where their middles have been seen).
	for (const FRiptideChartIsland& Island : Chart->GetIslands())
	{
		const FVector Centre(Island.Centre, 0.f);
		if (Chart->IsRead() || Chart->IsSeen(Island.Centre))
		{
			const FVector2f At = ToSheet(Centre);
			if (OnSheet(At))
			{
				DrawString(Out, LayerId + 4, G, Island.Name.ToString().ToUpper(), Font(EFont::Condensed, FMath::RoundToInt(18 * Scale), 80),
					At - FVector2f(0.f, 30.f * Scale), ChartInk, 0.5f);
			}
		}
	}
	// The camp (the first thing built), the pool, the rafts.
	for (TActorIterator<ARiptideStructure> It(World); It; ++It)
	{
		Mark(It->GetActorLocation(), TEXT("Camp"), ChartInk, 8.f);
		break;
	}
	for (TActorIterator<ARiptidePond> It(World); It; ++It)
	{
		Mark(It->GetActorLocation(), TEXT("Pool"), Srgb(0.15f, 0.35f, 0.6f), 7.f);
	}
	for (TActorIterator<ARiptideRaft> It(World); It; ++It)
	{
		Mark(It->GetActorLocation(), TEXT("Raft"), Srgb(0.45f, 0.3f, 0.15f), 8.f);
	}
	// The rest of the crew.
	for (TActorIterator<ARiptideCharacter> It(World); It; ++It)
	{
		if (*It != Me && It->GetPlayerState())
		{
			const FVector2f At = ToSheet(It->GetActorLocation());
			if (OnSheet(At))
			{
				FillRect(Out, LayerId + 5, G, At - FVector2f(5.f * Scale), FVector2f(10.f * Scale), CrewBlue);
				DrawString(Out, LayerId + 5, G, It->GetPlayerState()->GetPlayerName(), Small, At + FVector2f(8.f * Scale, -8.f * Scale), CrewBlue);
			}
		}
	}
	// You: an arrow the way you face (kept on the edge of the sheet if you're off it).
	FVector2f At = ToSheet(Me->GetActorLocation());
	At.X = FMath::Clamp(At.X, Origin.X + 8.f, Origin.X + Drawn.X - 8.f);
	At.Y = FMath::Clamp(At.Y, Origin.Y + 8.f, Origin.Y + Drawn.Y - 8.f);
	const float Yaw = FMath::DegreesToRadians(Me->GetControlRotation().Yaw);
	const FVector2f Facing(FMath::Sin(Yaw), -FMath::Cos(Yaw));            // north up, east right
	const FVector2f Side(-Facing.Y, Facing.X);
	const float A = 12.f * Scale;
	const TArray<FVector2f> Arrow = { At + Facing * A, At - Facing * A * 0.6f + Side * A * 0.6f, At - Facing * A * 0.25f, At - Facing * A * 0.6f - Side * A * 0.6f,
		At + Facing * A };
	Lines(Out, LayerId + 6, G, Arrow, YouRed, 3.f * Scale);
	Lines(Out, LayerId + 6, G, { At - Facing * A * 0.25f, At + Facing * A * 0.7f }, YouRed, 4.f * Scale);

	// What it is, and how to put it away.
	const FSlateFontInfo Body = Font(EFont::Regular, FMath::RoundToInt(14 * Scale));
	const float Drawing = Chart->GetDrawnFraction();
	const FString Footer = Drawing < 1.f ? FString::Printf(TEXT("Drawing the chart... %d%%"), FMath::RoundToInt(Drawing * 100.f))
		: FString(TEXT("What the crew has seen.      M  Put it away"));
	DrawString(Out, LayerId + 3, G, Footer, Body, FVector2f(Sheet.X + Pad, Sheet.Y + SheetSize.Y - 28.f * Scale), ChartInk);
	return LayerId + 7;
}
