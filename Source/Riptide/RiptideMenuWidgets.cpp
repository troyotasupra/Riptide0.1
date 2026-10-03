#include "RiptideMenuWidgets.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Rendering/DrawElements.h"
#include "RiptideSettings.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RiptideMenu"

// --- Style ---

FLinearColor RiptideMenuStyle::Srgb(float R, float G, float B, float A)
{
	FLinearColor C(FColor(uint8(FMath::Clamp(R, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(G, 0.f, 1.f) * 255.f + 0.5f),
		uint8(FMath::Clamp(B, 0.f, 1.f) * 255.f + 0.5f)));
	C.A = A;
	return C;
}

FLinearColor RiptideMenuStyle::Ink() { return Srgb(0.92f, 0.94f, 0.96f); }
FLinearColor RiptideMenuStyle::Dim() { return Srgb(0.56f, 0.62f, 0.68f); }
FLinearColor RiptideMenuStyle::Heading() { return Srgb(0.75f, 0.85f, 0.95f); }
FLinearColor RiptideMenuStyle::Accent() { return Srgb(1.f, 0.7f, 0.26f); }
FLinearColor RiptideMenuStyle::Edge() { return Srgb(0.35f, 0.42f, 0.48f, 0.8f); }
FLinearColor RiptideMenuStyle::Warning() { return Srgb(1.f, 0.45f, 0.3f); }

FSlateFontInfo RiptideMenuStyle::Font(EFont Face, int32 Size, int32 LetterSpacing)
{
	static const FName Faces[] = { "Regular", "Bold", "BoldCondensed", "Black", "Light" };
	FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Faces[uint8(Face)], Size);
	Info.LetterSpacing = LetterSpacing;
	return Info;
}

const FSlateBrush* RiptideMenuStyle::PanelBrush()
{
	static const FSlateRoundedBoxBrush Brush(Srgb(0.02f, 0.035f, 0.045f, 0.88f), 6.f, Edge(), 1.f);
	return &Brush;
}

const FSlateBrush* RiptideMenuStyle::CardBrush()
{
	static const FSlateRoundedBoxBrush Brush(Srgb(0.05f, 0.07f, 0.09f, 0.9f), 4.f, Srgb(0.3f, 0.36f, 0.42f, 0.7f), 1.f);
	return &Brush;
}

const FSlateBrush* RiptideMenuStyle::CardHotBrush()
{
	static const FSlateRoundedBoxBrush Brush(Srgb(0.1f, 0.12f, 0.13f, 0.95f), 4.f, Accent(), 2.f);
	return &Brush;
}

const FSlateBrush* RiptideMenuStyle::White()
{
	return FCoreStyle::Get().GetBrush("WhiteBrush");
}

FVector2f RiptideMenuStyle::Measure(const FString& String, const FSlateFontInfo& FontInfo)
{
	if (FSlateApplication::IsInitialized())
	{
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, FontInfo));
	}
	return FVector2f(FontInfo.Size * 0.6f * String.Len(), FontInfo.Size * 1.3f);
}

void RiptideMenuStyle::FillRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
	const FLinearColor& Colour)
{
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), White(), ESlateDrawEffect::None, Colour);
}

void RiptideMenuStyle::Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
	const FSlateBrush* Brush, const FLinearColor& Tint)
{
	// (A rounded box is filled with the tint it's drawn with, not its brush's colour.)
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), Brush, ESlateDrawEffect::None, Tint);
}

void RiptideMenuStyle::DrawString(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& String, const FSlateFontInfo& FontInfo,
	const FVector2f& Pos, const FLinearColor& Colour, float AlignX)
{
	const FVector2f Size = Measure(String, FontInfo);
	FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos - FVector2f(Size.X * AlignX, 0.f))), String,
		FontInfo, ESlateDrawEffect::None, Colour);
}

void RiptideMenuStyle::Chevron(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Centre, float Size, int32 Direction,
	const FLinearColor& Colour)
{
	const float D = float(Direction) * Size * 0.35f;
	TArray<FVector2f> Points = { Centre + FVector2f(-D, -Size * 0.5f), Centre + FVector2f(D, 0.f), Centre + FVector2f(-D, Size * 0.5f) };
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, 2.5f);
}

TSharedRef<SEditableTextBox> RiptideMenuStyle::MakeTextField(const FText& Hint, int32 FontSize, FOnTextChanged OnChanged,
	FOnTextCommitted OnCommitted)
{
	static FEditableTextBoxStyle Style = []()
	{
		FEditableTextBoxStyle S = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		const FSlateRoundedBoxBrush Normal(Srgb(0.03f, 0.045f, 0.055f, 0.95f), 4.f, Srgb(0.3f, 0.36f, 0.42f, 0.8f), 1.f);
		const FSlateRoundedBoxBrush Hot(Srgb(0.05f, 0.065f, 0.075f, 0.97f), 4.f, Srgb(0.55f, 0.6f, 0.65f), 1.f);
		const FSlateRoundedBoxBrush Focused(Srgb(0.05f, 0.065f, 0.075f, 0.97f), 4.f, Accent(), 2.f);
		S.SetBackgroundImageNormal(Normal).SetBackgroundImageHovered(Hot).SetBackgroundImageFocused(Focused).SetBackgroundImageReadOnly(Normal);
		S.SetForegroundColor(FSlateColor(Ink()));
		S.SetFocusedForegroundColor(FSlateColor(Ink()));
		S.SetPadding(FMargin(14.f, 10.f));
		return S;
	}();
	return SNew(SEditableTextBox)
		.Style(&Style)
		.Font(Font(EFont::Regular, FontSize))
		.HintText(Hint)
		.SelectAllTextWhenFocused(true)
		.ClearKeyboardFocusOnCommit(false)
		.OnTextChanged(OnChanged)
		.OnTextCommitted(OnCommitted);
}

bool RiptideMenuStyle::IsBackKey(const FKeyEvent& Key)
{
	return Key.GetKey() == EKeys::Escape || Key.GetKey() == EKeys::Gamepad_FaceButton_Right
		|| (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetNavigationActionFromKey(Key) == EUINavigationAction::Back);
}

void RiptideMenuStyle::Focus(const TSharedPtr<SWidget>& Widget)
{
	if (Widget.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Widget, EFocusCause::SetDirectly);
	}
}

namespace
{
	using namespace RiptideMenuStyle;

	/** The mouse moving onto a control focuses it, so the mouse and the keyboard or pad never light two at once
	 * (unless someone is typing in a text field, which keeps its focus). */
	void FocusOnHover(const TSharedRef<SWidget>& Widget)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(0);
		if (Focused.IsValid() && Focused->GetType() == FName(TEXT("SEditableText")))
		{
			return;
		}
		FSlateApplication::Get().SetUserFocus(0, Widget, EFocusCause::Mouse);
	}
}

// --- SRiptideButton ---

void SRiptideButton::Construct(const FArguments& InArgs)
{
	Text = InArgs._Text;
	Detail = InArgs._Detail;
	Columns = InArgs._Columns;
	ColumnPositions = InArgs._ColumnPositions;
	Width = InArgs._Width;
	Height = InArgs._Height;
	FontSize = InArgs._FontSize;
	bCentred = InArgs._Centred;
	bPrimary = InArgs._Primary;
	Selected = InArgs._Selected;
	OnClicked = InArgs._OnClicked;
	OnDoubleClicked = InArgs._OnDoubleClicked;
}

bool SRiptideButton::IsLit() const
{
	return IsHovered() || HasAnyUserFocus().IsSet() || bPressed;
}

int32 SRiptideButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FGeometry& G = AllottedGeometry;
	const FVector2f Size = FVector2f(G.GetLocalSize());
	const bool bEnabled = IsEnabled() && bParentEnabled;
	const bool bLit = bEnabled && IsLit();
	const bool bChosen = Selected.Get(false);
	const float Alpha = bEnabled ? 1.f : 0.4f;

	// The card, lit with the amber edge while hovered or focused; a chosen option keeps a thin amber stripe.
	const FSlateBrush* Brush = bLit ? CardHotBrush() : CardBrush();
	FLinearColor Fill = bPrimary ? Srgb(0.42f, 0.27f, 0.08f, 0.95f) : bLit ? Srgb(0.1f, 0.12f, 0.13f, 0.95f) : Srgb(0.05f, 0.07f, 0.09f, 0.88f);
	if (bChosen && !bPrimary)
	{
		Fill = bLit ? Srgb(0.16f, 0.14f, 0.1f, 0.96f) : Srgb(0.12f, 0.1f, 0.07f, 0.92f);
	}
	if (bPressed)
	{
		Fill = Fill * 1.25f;
	}
	Fill.A *= Alpha;
	Box(Out, LayerId, G, FVector2f(0.f), Size, Brush, Fill);
	if (bLit || bChosen)
	{
		FillRect(Out, LayerId + 1, G, FVector2f(0.f, 6.f), FVector2f(4.f, Size.Y - 12.f), Accent().CopyWithNewOpacity(bLit ? 1.f : 0.7f));
	}

	const FSlateFontInfo LabelFont = Font(EFont::Condensed, FontSize, 60);
	const FString Label = Text.Get().ToString().ToUpper();
	const FString DetailText = Detail.Get().ToString();
	const FSlateFontInfo DetailFont = Font(EFont::Regular, FMath::Max(11, FontSize / 2 + 2));
	const float LabelH = LabelFont.Size * 1.3f;
	const float DetailH = DetailText.IsEmpty() ? 0.f : DetailFont.Size * 1.45f;
	const float Top = (Size.Y - LabelH - DetailH) * 0.5f;
	const FLinearColor LabelColour = (bLit ? FLinearColor::White : Ink()).CopyWithNewOpacity(Alpha);
	const float X = bCentred ? Size.X * 0.5f : 22.f;
	const float Align = bCentred ? 0.5f : 0.f;
	DrawString(Out, LayerId + 2, G, Label, LabelFont, FVector2f(X, Top), LabelColour, Align);
	if (!DetailText.IsEmpty())
	{
		DrawString(Out, LayerId + 2, G, DetailText, DetailFont, FVector2f(X, Top + LabelH), Dim().CopyWithNewOpacity(Alpha), Align);
	}
	const TArray<FText> ColumnTexts = Columns.Get(TArray<FText>());
	for (int32 i = 0; i < ColumnTexts.Num() && i < ColumnPositions.Num(); ++i)
	{
		const FSlateFontInfo ColumnFont = Font(EFont::Regular, FMath::Max(12, FontSize - 4));
		DrawString(Out, LayerId + 2, G, ColumnTexts[i].ToString(), ColumnFont, FVector2f(Size.X * ColumnPositions[i], (Size.Y - ColumnFont.Size * 1.3f) * 0.5f),
			(bLit ? Ink() : Dim()).CopyWithNewOpacity(Alpha));
	}
	return LayerId + 3;
}

FReply SRiptideButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SRiptideButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		OnClicked.ExecuteIfBound();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SRiptideButton::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (OnDoubleClicked.IsBound() && IsEnabled())
	{
		OnDoubleClicked.Execute();
		return FReply::Handled();
	}
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

void SRiptideButton::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsEnabled())
	{
		FocusOnHover(SharedThis(this));
	}
}

FReply SRiptideButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsEnabled() && !InKeyEvent.IsRepeat() && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
	{
		// Pressing Accept on a selected row (a game in the browser) is the same as double-clicking it.
		if (OnDoubleClicked.IsBound() && Selected.Get(false))
		{
			OnDoubleClicked.Execute();
		}
		else
		{
			OnClicked.ExecuteIfBound();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// --- SRiptideStepper ---

void SRiptideStepper::Construct(const FArguments& InArgs)
{
	Label = InArgs._Label;
	Value = InArgs._Value;
	Fraction = InArgs._Fraction;
	Width = InArgs._Width;
	Height = InArgs._Height;
	OnStep = InArgs._OnStep;
	OnSetFraction = InArgs._OnSetFraction;
}

int32 SRiptideStepper::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FGeometry& G = AllottedGeometry;
	const FVector2f Size = FVector2f(G.GetLocalSize());
	const bool bLit = IsHovered() || HasAnyUserFocus().IsSet() || bDragging;
	Box(Out, LayerId, G, FVector2f(0.f), Size, bLit ? CardHotBrush() : CardBrush(),
		bLit ? Srgb(0.1f, 0.12f, 0.13f, 0.95f) : Srgb(0.04f, 0.06f, 0.075f, 0.8f));
	if (bLit)
	{
		FillRect(Out, LayerId + 1, G, FVector2f(0.f, 6.f), FVector2f(4.f, Size.Y - 12.f), Accent());
	}

	const FSlateFontInfo LabelFont = Font(EFont::Condensed, 17, 40);
	DrawString(Out, LayerId + 2, G, Label.Get().ToString().ToUpper(), LabelFont, FVector2f(20.f, (Size.Y - LabelFont.Size * 1.3f) * 0.5f),
		bLit ? FLinearColor::White : Ink());

	// The value between its chevrons; it flashes amber for a moment when it changes.
	const float Left = Size.X * 0.5f, Right = Size.X - 16.f;
	const TOptional<float> Bar = Fraction.Get(TOptional<float>());
	const FLinearColor ChevronColour = bLit ? Accent() : Dim();
	Chevron(Out, LayerId + 2, G, FVector2f(Left + 8.f, Size.Y * 0.5f), 14.f, -1, ChevronColour);
	Chevron(Out, LayerId + 2, G, FVector2f(Right - 8.f, Size.Y * 0.5f), 14.f, 1, ChevronColour);
	const float Flash = FMath::Clamp(1.f - float(FSlateApplication::Get().GetCurrentTime() - LastStepTime) / 0.35f, 0.f, 1.f);
	const FLinearColor ValueColour = FMath::Lerp(bLit ? FLinearColor::White : Ink(), Accent(), Flash);
	const FSlateFontInfo ValueFont = Font(EFont::Bold, 16);
	if (Bar.IsSet())
	{
		// Value on the left of the area, the bar filling the rest.
		const float ValueW = 64.f;
		DrawString(Out, LayerId + 2, G, Value.Get().ToString(), ValueFont, FVector2f(Left + 24.f + ValueW * 0.5f, (Size.Y - ValueFont.Size * 1.3f) * 0.5f),
			ValueColour, 0.5f);
		const FVector2f BarPos(Left + 24.f + ValueW + 8.f, Size.Y * 0.5f - 3.f);
		const FVector2f BarSize(Right - 24.f - BarPos.X, 6.f);
		FillRect(Out, LayerId + 2, G, BarPos, BarSize, Srgb(0.2f, 0.24f, 0.28f, 0.9f));
		FillRect(Out, LayerId + 3, G, BarPos, FVector2f(BarSize.X * FMath::Clamp(Bar.GetValue(), 0.f, 1.f), BarSize.Y), bLit ? Accent() : Heading());
	}
	else
	{
		DrawString(Out, LayerId + 2, G, Value.Get().ToString(), ValueFont, FVector2f((Left + Right) * 0.5f, (Size.Y - ValueFont.Size * 1.3f) * 0.5f),
			ValueColour, 0.5f);
	}
	return LayerId + 4;
}

FNavigationReply SRiptideStepper::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// Left and right change the value (from the arrow keys, the D-pad or the stick alike); up and down move on.
	const EUINavigation Direction = InNavigationEvent.GetNavigationType();
	if (Direction == EUINavigation::Left || Direction == EUINavigation::Right)
	{
		OnStep.ExecuteIfBound(Direction == EUINavigation::Left ? -1 : 1);
		LastStepTime = FSlateApplication::Get().GetCurrentTime();
		return FNavigationReply::Stop();
	}
	return SLeafWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

void SRiptideStepper::SetFractionAt(float LocalX)
{
	const float Left = Width * 0.5f + 24.f + 64.f + 8.f;
	const float Right = Width - 16.f - 24.f;
	OnSetFraction.ExecuteIfBound(FMath::Clamp((LocalX - Left) / FMath::Max(1.f, Right - Left), 0.f, 1.f));
}

FReply SRiptideStepper::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	const float X = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X;
	const float Left = MyGeometry.GetLocalSize().X * 0.5f, Right = MyGeometry.GetLocalSize().X - 16.f;
	FReply Reply = FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	if (X >= Left - 8.f && X <= Left + 28.f)
	{
		OnStep.ExecuteIfBound(-1);
		LastStepTime = FSlateApplication::Get().GetCurrentTime();
	}
	else if (X >= Right - 28.f && X <= Right + 12.f)
	{
		OnStep.ExecuteIfBound(1);
		LastStepTime = FSlateApplication::Get().GetCurrentTime();
	}
	else if (Fraction.Get(TOptional<float>()).IsSet() && X > Left + 28.f && OnSetFraction.IsBound())
	{
		bDragging = true;
		Width = MyGeometry.GetLocalSize().X;
		SetFractionAt(X);
		Reply.CaptureMouse(SharedThis(this));
	}
	else if (X > Left)
	{
		// The middle of a list's value: next choice.
		OnStep.ExecuteIfBound(1);
		LastStepTime = FSlateApplication::Get().GetCurrentTime();
	}
	return Reply;
}

FReply SRiptideStepper::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		SetFractionAt(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SRiptideStepper::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SRiptideStepper::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
	FocusOnHover(SharedThis(this));
}

// --- SRiptideSpinner, SRiptideBackdrop ---

int32 SRiptideSpinner::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const FVector2f Centre = Size * 0.5f;
	const float Radius = FMath::Min(Size.X, Size.Y) * 0.4f;
	const float Start = float(FSlateApplication::Get().GetCurrentTime()) * 5.f;
	TArray<FVector2f> Points;
	for (int32 i = 0; i <= 16; ++i)
	{
		const float A = Start + i / 16.f * UE_PI * 1.4f;
		Points.Add(Centre + Radius * FVector2f(FMath::Cos(A), FMath::Sin(A)));
	}
	FSlateDrawElement::MakeLines(Out, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Accent(), true, 2.5f);
	return LayerId + 1;
}

int32 SRiptideBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const float S = FMath::Clamp(Strength.Get(1.f), 0.f, 1.f);
	const FLinearColor Night = Srgb(0.005f, 0.015f, 0.025f);
	if (bFull)
	{
		FillRect(Out, LayerId, AllottedGeometry, FVector2f(0.f), Size, Night.CopyWithNewOpacity(0.72f * S));
		return LayerId + 1;
	}
	// From the left edge (where the menu is) to clear by the middle of the screen, and a band along the bottom for
	// the footer.
	TArray<FSlateGradientStop> Across = {
		FSlateGradientStop(FVector2f(0.f, 0.f), Night.CopyWithNewOpacity(0.86f * S)),
		FSlateGradientStop(FVector2f(Size.X * 0.3f, 0.f), Night.CopyWithNewOpacity(0.62f * S)),
		FSlateGradientStop(FVector2f(Size.X * 0.58f, 0.f), Night.CopyWithNewOpacity(0.f)),
	};
	FSlateDrawElement::MakeGradient(Out, LayerId, AllottedGeometry.ToPaintGeometry(), Across, Orient_Vertical);
	TArray<FSlateGradientStop> Down = {
		FSlateGradientStop(FVector2f(0.f, Size.Y * 0.8f), Night.CopyWithNewOpacity(0.f)),
		FSlateGradientStop(FVector2f(0.f, Size.Y), Night.CopyWithNewOpacity(0.7f * S)),
	};
	FSlateDrawElement::MakeGradient(Out, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Down, Orient_Horizontal);
	return LayerId + 2;
}

// --- SRiptideSettingsPanel ---

namespace
{
	const FText QualityNames[] = { LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"), LOCTEXT("Epic", "Epic"),
		LOCTEXT("Cinematic", "Cinematic") };
	const float FrameLimits[] = { 30.f, 60.f, 90.f, 120.f, 144.f, 165.f, 240.f, 0.f };     // 0 = no limit

	UGameUserSettings* Graphics()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	FText OnOff(bool b)
	{
		return b ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off");
	}

	FText Percent(float Fraction)
	{
		return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Fraction * 100.f)));
	}

	/** Steps through a list of values, wrapping round. */
	int32 Wrap(int32 Index, int32 Num)
	{
		return Num > 0 ? ((Index % Num) + Num) % Num : 0;
	}
}

void SRiptideSettingsPanel::Construct(const FArguments& InArgs)
{
	WorldContext = InArgs._WorldContext;
	OnBack = InArgs._OnBack;

	// Resolutions the monitor offers (largest last), for full screen; any of them works in a window too.
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	if (Resolutions.IsEmpty())
	{
		UKismetSystemLibrary::GetConvenientWindowedResolutions(Resolutions);
	}
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y < B.X * B.Y || (A.X * A.Y == B.X * B.Y && A.X < B.X); });
	for (int32 i = Resolutions.Num() - 1; i > 0; --i)
	{
		if (Resolutions[i] == Resolutions[i - 1])
		{
			Resolutions.RemoveAt(i);
		}
	}

	URiptideSettingsSave* S = URiptideSettingsSave::Get();
	auto Volume = [this, S](float URiptideSettingsSave::* Field, const FText& Name)
	{
		return SNew(SRiptideStepper)
			.Label(Name)
			.Value_Lambda([S, Field]() { return Percent(S->*Field); })
			.Fraction_Lambda([S, Field]() { return TOptional<float>(S->*Field); })
			.OnStep_Lambda([this, S, Field](int32 Dir) { S->*Field = FMath::Clamp(FMath::RoundToFloat((S->*Field + Dir * 0.05f) * 20.f) / 20.f, 0.f, 1.f); ApplyGameplay(); })
			.OnSetFraction_Lambda([this, S, Field](float F) { S->*Field = F; ApplyGameplay(); });
	};

	TSharedPtr<SRiptideStepper> First;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ Section(LOCTEXT("Controls", "Controls")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SAssignNew(First, SRiptideStepper)
			.Label(LOCTEXT("Sensitivity", "Look sensitivity"))
			.Value_Lambda([S]() { return FText::FromString(FString::Printf(TEXT("%.1fx"), S->MouseSensitivity)); })
			.Fraction_Lambda([S]() { return TOptional<float>((S->MouseSensitivity - URiptideSettingsSave::MinSensitivity)
				/ (URiptideSettingsSave::MaxSensitivity - URiptideSettingsSave::MinSensitivity)); })
			.OnStep_Lambda([this, S](int32 Dir) { S->MouseSensitivity = FMath::RoundToFloat((S->MouseSensitivity + Dir * 0.1f) * 10.f) / 10.f; ApplyGameplay(); })
			.OnSetFraction_Lambda([this, S](float F)
			{
				S->MouseSensitivity = FMath::RoundToFloat(FMath::Lerp(URiptideSettingsSave::MinSensitivity, URiptideSettingsSave::MaxSensitivity, F) * 10.f) / 10.f;
				ApplyGameplay();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("InvertY", "Invert look up and down"))
			.Value_Lambda([S]() { return OnOff(S->bInvertY); })
			.OnStep_Lambda([this, S](int32) { S->bInvertY = !S->bInvertY; ApplyGameplay(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("Fov", "Field of view"))
			.Value_Lambda([S]() { return FText::FromString(FString::Printf(TEXT("%d°"), FMath::RoundToInt(S->FieldOfView))); })
			.Fraction_Lambda([S]() { return TOptional<float>((S->FieldOfView - URiptideSettingsSave::MinFieldOfView)
				/ (URiptideSettingsSave::MaxFieldOfView - URiptideSettingsSave::MinFieldOfView)); })
			.OnStep_Lambda([this, S](int32 Dir) { S->FieldOfView += Dir * 5.f; ApplyGameplay(); })
			.OnSetFraction_Lambda([this, S](float F)
			{
				S->FieldOfView = FMath::RoundToFloat(FMath::Lerp(URiptideSettingsSave::MinFieldOfView, URiptideSettingsSave::MaxFieldOfView, F));
				ApplyGameplay();
			})
		]
		+ SVerticalBox::Slot().AutoHeight()[ Section(LOCTEXT("Audio", "Audio")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[ Volume(&URiptideSettingsSave::MasterVolume, LOCTEXT("Master", "Master volume")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[ Volume(&URiptideSettingsSave::EffectsVolume, LOCTEXT("Effects", "Effects: the boat, the water")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)[ Volume(&URiptideSettingsSave::AmbientVolume, LOCTEXT("Ambient", "Ambience: the sea")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Section(LOCTEXT("GraphicsSection", "Graphics")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("Quality", "Graphics quality"))
			.Value_Lambda([]()
			{
				const int32 Level = Graphics() ? Graphics()->GetOverallScalabilityLevel() : -1;
				return Level >= 0 && Level < int32(UE_ARRAY_COUNT(QualityNames)) ? QualityNames[Level] : LOCTEXT("Custom", "Custom");
			})
			.OnStep_Lambda([this](int32 Dir)
			{
				if (UGameUserSettings* G = Graphics())
				{
					const int32 Level = G->GetOverallScalabilityLevel();
					G->SetOverallScalabilityLevel(Level < 0 ? 2 : Wrap(Level + Dir, UE_ARRAY_COUNT(QualityNames)));
					ApplyGraphics(false);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("WindowMode", "Display"))
			.Value_Lambda([]()
			{
				switch (Graphics() ? Graphics()->GetFullscreenMode() : EWindowMode::Windowed)
				{
				case EWindowMode::Fullscreen: return LOCTEXT("Fullscreen", "Full screen");
				case EWindowMode::WindowedFullscreen: return LOCTEXT("Borderless", "Borderless window");
				default: return LOCTEXT("Windowed", "Window");
				}
			})
			.OnStep_Lambda([this](int32 Dir)
			{
				if (UGameUserSettings* G = Graphics())
				{
					G->SetFullscreenMode(EWindowMode::ConvertIntToWindowMode(Wrap(int32(G->GetFullscreenMode()) + Dir, 3)));
					ApplyGraphics(true);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("Resolution", "Resolution"))
			.Value_Lambda([]()
			{
				const FIntPoint R = Graphics() ? Graphics()->GetScreenResolution() : FIntPoint::ZeroValue;
				return FText::FromString(FString::Printf(TEXT("%d × %d"), R.X, R.Y));
			})
			.OnStep_Lambda([this](int32 Dir)
			{
				UGameUserSettings* G = Graphics();
				if (!G || Resolutions.IsEmpty())
				{
					return;
				}
				int32 Index = Resolutions.IndexOfByKey(G->GetScreenResolution());
				Index = Index == INDEX_NONE ? Resolutions.Num() - 1 : Wrap(Index + Dir, Resolutions.Num());
				G->SetScreenResolution(Resolutions[Index]);
				ApplyGraphics(true);
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("VSync", "VSync"))
			.Value_Lambda([]() { return OnOff(Graphics() && Graphics()->IsVSyncEnabled()); })
			.OnStep_Lambda([this](int32) { if (UGameUserSettings* G = Graphics()) { G->SetVSyncEnabled(!G->IsVSyncEnabled()); ApplyGraphics(false); } })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SRiptideStepper)
			.Label(LOCTEXT("FrameLimit", "Frame-rate limit"))
			.Value_Lambda([]()
			{
				const float Limit = Graphics() ? Graphics()->GetFrameRateLimit() : 0.f;
				return Limit <= 0.f ? LOCTEXT("Unlimited", "Unlimited") : FText::FromString(FString::Printf(TEXT("%d fps"), FMath::RoundToInt(Limit)));
			})
			.OnStep_Lambda([this](int32 Dir)
			{
				if (UGameUserSettings* G = Graphics())
				{
					int32 Index = INDEX_NONE;
					for (int32 i = 0; i < int32(UE_ARRAY_COUNT(FrameLimits)); ++i)
					{
						Index = FMath::IsNearlyEqual(FrameLimits[i], G->GetFrameRateLimit(), 0.5f) ? i : Index;
					}
					G->SetFrameRateLimit(FrameLimits[Index == INDEX_NONE ? 1 : Wrap(Index + Dir, UE_ARRAY_COUNT(FrameLimits))]);
					ApplyGraphics(false);
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SRiptideButton).Text(LOCTEXT("Back", "Back")).Width(220.f).Height(52.f).FontSize(20).Centred(true)
				.OnClicked_Lambda([this]() { Close(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SRiptideButton).Text(LOCTEXT("Defaults", "Reset to defaults")).Width(260.f).Height(52.f).FontSize(20).Centred(true)
				.OnClicked_Lambda([this]() { ResetToDefaults(); })
			]
		]
	];
	FirstRow = First;
}

TSharedRef<SWidget> SRiptideSettingsPanel::Section(const FText& Title)
{
	return SNew(SBox).Padding(FMargin(2.f, 10.f, 0.f, 4.f))
	[
		SNew(STextBlock).Text(FText::FromString(Title.ToString().ToUpper())).Font(Font(EFont::Condensed, 15, 220)).ColorAndOpacity(Heading())
	];
}

void SRiptideSettingsPanel::FocusFirst()
{
	Focus(FirstRow);
}

void SRiptideSettingsPanel::ApplyGameplay()
{
	URiptideSettingsSave* S = URiptideSettingsSave::Get();
	S->Sanitise();
	S->Apply(WorldContext.Get());
}

void SRiptideSettingsPanel::ApplyGraphics(bool bResolution)
{
	UGameUserSettings* G = Graphics();
	if (!G)
	{
		return;
	}
	G->ApplyNonResolutionSettings();
	// In the editor the game shares the editor's window: its size and mode aren't the game's to change.
	if (bResolution && !GIsEditor)
	{
		G->ApplyResolutionSettings(false);
		G->ConfirmVideoMode();
	}
}

void SRiptideSettingsPanel::ResetToDefaults()
{
	URiptideSettingsSave* S = URiptideSettingsSave::Get();
	const URiptideSettingsSave* Defaults = GetDefault<URiptideSettingsSave>();
	S->MouseSensitivity = Defaults->MouseSensitivity;
	S->bInvertY = Defaults->bInvertY;
	S->FieldOfView = Defaults->FieldOfView;
	S->MasterVolume = Defaults->MasterVolume;
	S->EffectsVolume = Defaults->EffectsVolume;
	S->AmbientVolume = Defaults->AmbientVolume;
	ApplyGameplay();
	if (UGameUserSettings* G = Graphics())
	{
		// The engine's own pick for this machine's graphics, and its defaults for the rest.
		G->SetVSyncEnabled(false);
		G->SetFrameRateLimit(0.f);
		G->SetOverallScalabilityLevel(3);
		ApplyGraphics(false);
	}
}

void SRiptideSettingsPanel::Close()
{
	URiptideSettingsSave::Get()->Save();
	if (UGameUserSettings* G = Graphics())
	{
		G->SaveSettings();
	}
	OnBack.ExecuteIfBound();
}

FReply SRiptideSettingsPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsBackKey(InKeyEvent))
	{
		Close();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
