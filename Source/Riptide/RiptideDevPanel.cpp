#include "RiptideDevPanel.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "RiptidePlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	/** Colours as a colour picker shows them (sRGB); Slate draws in linear colour. */
	FLinearColor Srgb(float R, float G, float B, float A = 1.f)
	{
		FLinearColor C(FColor(uint8(R * 255.f + 0.5f), uint8(G * 255.f + 0.5f), uint8(B * 255.f + 0.5f)));
		C.A = A;
		return C;
	}

	// The inventory's palette (RiptideInventoryWidget.cpp).
	const FLinearColor Heading = Srgb(0.75f, 0.85f, 0.95f);
	const FLinearColor Ink = Srgb(0.92f, 0.94f, 0.96f);
	const FLinearColor Muted = Srgb(0.6f, 0.66f, 0.72f);
	const FLinearColor Rule = Srgb(0.35f, 0.42f, 0.48f, 0.55f);

	constexpr float CardWidth = 452.f;
	constexpr float Margin = 24.f;
	// Below the editor's own viewport badges when playing in the editor.
	constexpr float TopMargin = 56.f;
	constexpr float Pad = 16.f;

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	FVector2f Measure(const FString& Text, const FSlateFontInfo& FontInfo)
	{
		if (FSlateApplication::IsInitialized())
		{
			return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo));
		}
		return FVector2f(FontInfo.Size * 0.6f * Text.Len(), FontInfo.Size * 1.3f);
	}

	void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& String, const FSlateFontInfo& FontInfo,
		const FVector2f& Pos, const FLinearColor& Colour)
	{
		const FVector2f Size = Measure(String, FontInfo);
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), String, FontInfo, ESlateDrawEffect::None, Colour);
	}

	void FillRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& Colour)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None, Colour);
	}

	/** Breaks text into lines no wider than Width. */
	TArray<FString> Wrap(const FString& String, const FSlateFontInfo& FontInfo, float Width)
	{
		TArray<FString> Words, Lines;
		String.ParseIntoArray(Words, TEXT(" "));
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Try = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			if (!Line.IsEmpty() && Measure(Try, FontInfo).X > Width)
			{
				Lines.Add(Line);
				Line = Word;
			}
			else
			{
				Line = Try;
			}
		}
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
		return Lines;
	}

	float RowHeight(const FRiptideDevPanelRow& Row, int32 HintLines)
	{
		switch (Row.Kind)
		{
		case FRiptideDevPanelRow::EKind::Heading: return 30.f;
		case FRiptideDevPanelRow::EKind::Key: return 25.f;
		case FRiptideDevPanelRow::EKind::Hint: return 16.f * HintLines + 4.f;
		case FRiptideDevPanelRow::EKind::Legend: return 19.f;
		default: return 21.f;
		}
	}
}

void SRiptideDevPanel::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	// Live readings: redrawn every frame.
	ForceVolatile(true);
	SetVisibility(EVisibility::HitTestInvisible);
	CardBrush = MakeUnique<FSlateRoundedBoxBrush>(Srgb(0.02f, 0.035f, 0.05f, 0.84f), 8.f, Srgb(0.35f, 0.42f, 0.48f, 0.7f), 1.f);
	KeycapBrush = MakeUnique<FSlateRoundedBoxBrush>(Srgb(0.13f, 0.16f, 0.19f, 0.95f), 4.f, Srgb(0.42f, 0.48f, 0.54f), 1.f);
}

int32 SRiptideDevPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const ARiptidePlayerController* PC = Owner.Get();
	if (!PC)
	{
		return LayerId;
	}
	TArray<FRiptideDevPanelRow> Rows;
	PC->GetDevPanelRows(Rows);

	const FGeometry& G = AllottedGeometry;
	const FVector2f Screen = FVector2f(G.GetLocalSize());
	const FSlateFontInfo TitleFont = Font(15, true), HeadingFont = Font(9, true), KeyFont = Font(9, true), LabelFont = Font(11),
		ValueFont = Font(11, true), StatFont = Font(10), HintFont = Font(10);
	const float Inner = CardWidth - 2.f * Pad;

	// Measure first, so the card fits its rows (and scales down to fit a short window).
	TArray<TArray<FString>> HintLines;
	HintLines.SetNum(Rows.Num());
	float Height = Pad + 26.f;
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		if (Rows[i].Kind == FRiptideDevPanelRow::EKind::Hint)
		{
			HintLines[i] = Wrap(Rows[i].Label, HintFont, Inner);
		}
		Height += RowHeight(Rows[i], HintLines[i].Num());
	}
	Height += Pad - 4.f;

	const float Scale = FMath::Min(1.f, (Screen.Y - TopMargin - Margin) / Height);
	const FGeometry Card = G.MakeChild(FVector2f(CardWidth, Height), FSlateLayoutTransform(Scale, FVector2f(Screen.X - Margin - CardWidth * Scale, TopMargin)));
	int32 L = LayerId;
	FSlateDrawElement::MakeBox(Out, L, Card.ToPaintGeometry(FVector2f(CardWidth, Height), FSlateLayoutTransform()), CardBrush.Get(),
		ESlateDrawEffect::None, CardBrush->TintColor.GetSpecifiedColor());

	// Title.
	Text(Out, L + 1, Card, TEXT("DEV MODE"), TitleFont, FVector2f(Pad, Pad - 4.f), Ink);
	const FString Close = TEXT("F1 hides this");
	Text(Out, L + 1, Card, Close, HintFont, FVector2f(CardWidth - Pad - Measure(Close, HintFont).X, Pad + 1.f), Muted);

	float Y = Pad + 26.f;
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FRiptideDevPanelRow& Row = Rows[i];
		const float H = RowHeight(Row, HintLines[i].Num());
		const FLinearColor LabelColour = Row.bDimmed ? Srgb(0.45f, 0.5f, 0.55f) : Ink;
		switch (Row.Kind)
		{
		case FRiptideDevPanelRow::EKind::Heading:
			FillRect(Out, L + 1, Card, FVector2f(Pad, Y + 8.f), FVector2f(Inner, 1.f), Rule);
			Text(Out, L + 2, Card, Row.Label.ToUpper(), HeadingFont, FVector2f(Pad, Y + 13.f), Heading);
			break;
		case FRiptideDevPanelRow::EKind::Key:
		{
			// The key as a keycap, then what it does, then its state on the right.
			const FVector2f CapSize(FMath::Max(40.f, Measure(Row.Key, KeyFont).X + 12.f), 19.f);
			FSlateDrawElement::MakeBox(Out, L + 1, Card.ToPaintGeometry(CapSize, FSlateLayoutTransform(FVector2f(Pad, Y + 2.f))), KeycapBrush.Get(),
				ESlateDrawEffect::None, KeycapBrush->TintColor.GetSpecifiedColor());
			const FVector2f KeySize = Measure(Row.Key, KeyFont);
			Text(Out, L + 2, Card, Row.Key, KeyFont, FVector2f(Pad + (CapSize.X - KeySize.X) * 0.5f, Y + 2.f + (CapSize.Y - KeySize.Y) * 0.5f),
				Row.bDimmed ? Muted : Ink);
			Text(Out, L + 2, Card, Row.Label, LabelFont, FVector2f(Pad + FMath::Max(52.f, CapSize.X + 10.f), Y + 2.f), LabelColour);
			if (!Row.Value.IsEmpty())
			{
				Text(Out, L + 2, Card, Row.Value, ValueFont, FVector2f(CardWidth - Pad - Measure(Row.Value, ValueFont).X, Y + 2.f), Row.Colour);
			}
			break;
		}
		case FRiptideDevPanelRow::EKind::Stat:
			Text(Out, L + 2, Card, Row.Label, StatFont, FVector2f(Pad, Y + 2.f), Muted);
			Text(Out, L + 2, Card, Row.Value, ValueFont, FVector2f(CardWidth - Pad - Measure(Row.Value, ValueFont).X, Y + 1.f), Row.Colour);
			break;
		case FRiptideDevPanelRow::EKind::Hint:
		{
			float LineY = Y + 2.f;
			for (const FString& Line : HintLines[i])
			{
				Text(Out, L + 2, Card, Line, HintFont, FVector2f(Pad, LineY), Muted);
				LineY += 16.f;
			}
			break;
		}
		case FRiptideDevPanelRow::EKind::Legend:
			FillRect(Out, L + 2, Card, FVector2f(Pad + 2.f, Y + 5.f), FVector2f(10.f, 10.f), Row.Colour);
			Text(Out, L + 2, Card, Row.Label, StatFont, FVector2f(Pad + 20.f, Y + 1.f), Muted);
			break;
		}
		Y += H;
	}
	return L + 3;
}
