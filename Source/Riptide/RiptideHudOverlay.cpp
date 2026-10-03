#include "RiptideHudOverlay.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "RiptideHUD.h"
#include "RiptideMenuWidgets.h"
#include "RiptideSkyClock.h"

namespace
{
	/** A prompt not posted again for this long is gone. */
	constexpr double PromptLife = 0.15;

	double Now(const UWorld* World)
	{
		return World ? World->GetRealTimeSeconds() : 0.0;
	}

	/** Splits a prompt into (key, action) pairs; a part without a key comes back with an empty key. */
	TArray<TPair<FString, FString>> Pairs(const FString& Text)
	{
		TArray<TPair<FString, FString>> Out;
		// Pairs are four or more spaces apart.
		TArray<FString> Parts;
		FString Rest = Text;
		while (!Rest.IsEmpty())
		{
			int32 At = Rest.Find(TEXT("    "));
			if (At == INDEX_NONE)
			{
				Parts.Add(Rest.TrimStartAndEnd());
				break;
			}
			Parts.Add(Rest.Left(At).TrimStartAndEnd());
			Rest = Rest.Mid(At).TrimStart();
		}
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}
			// "Key  action": a short key before two spaces (E, Shift, Hold V, [ ], Hold right mouse).
			const int32 Gap = Part.Find(TEXT("  "));
			if (Gap > 0 && Gap <= 16)
			{
				Out.Emplace(Part.Left(Gap), Part.Mid(Gap).TrimStart());
			}
			else
			{
				Out.Emplace(FString(), Part);
			}
		}
		return Out;
	}
}

ARiptideHUD* RiptideHud::HudFor(const AActor* For)
{
	const APlayerController* PC = Cast<APlayerController>(For);
	if (!PC)
	{
		if (const APawn* Pawn = Cast<APawn>(For))
		{
			PC = Cast<APlayerController>(Pawn->GetController());
		}
	}
	return PC && PC->IsLocalController() ? Cast<ARiptideHUD>(PC->GetHUD()) : nullptr;
}

void RiptideHud::Prompt(const AActor* For, ESlot Slot, const FString& Text, const FLinearColor& Colour, float Progress)
{
	if (ARiptideHUD* Hud = HudFor(For))
	{
		ARiptideHUD::FPrompt& P = Hud->Prompts[int32(Slot)];
		P.Text = Text;
		P.Colour = Colour;
		P.Progress = Progress;
		P.Time = Now(Hud->GetWorld());
	}
}

void RiptideHud::Vitals(const AActor* For, float Health, float Food, float Water, bool bSick, float Cold, bool bWarm)
{
	if (ARiptideHUD* Hud = HudFor(For))
	{
		Hud->VitalHealth = Health;
		Hud->VitalFood = Food;
		Hud->VitalWater = Water;
		Hud->bVitalSick = bSick;
		Hud->VitalCold = Cold;
		Hud->bVitalWarm = bWarm;
		Hud->VitalsTime = Now(Hud->GetWorld());
	}
}

void RiptideHud::Fade(const AActor* For, float Alpha)
{
	if (ARiptideHUD* Hud = HudFor(For))
	{
		Hud->FadeAlpha = Alpha;
		Hud->FadeTime = Now(Hud->GetWorld());
	}
}

void RiptideHud::Note(const APlayerController* Player, const FString& Text, float Seconds, const FLinearColor& Colour, int32 Key)
{
	ARiptideHUD* Hud = HudFor(Player);
	if (!Hud)
	{
		return;
	}
	const double Until = Now(Hud->GetWorld()) + Seconds;
	if (Key != INDEX_NONE)
	{
		for (ARiptideHUD::FNote& N : Hud->Notes)
		{
			if (N.Key == Key)
			{
				N.Text = Text;
				N.Colour = Colour;
				N.Until = Until;
				return;
			}
		}
	}
	Hud->Notes.Add({ Text, Colour, Until, Key });
	if (Hud->Notes.Num() > 6)
	{
		Hud->Notes.RemoveAt(0);
	}
}

void SRiptideHudOverlay::Construct(const FArguments& InArgs)
{
	Hud = InArgs._Hud;
	SetVisibility(EVisibility::HitTestInvisible);
}

float SRiptideHudOverlay::DrawPromptLine(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float Scale, const FString& Text,
	const FLinearColor& Colour, float CentreX, float Y, float Progress) const
{
	using namespace RiptideMenuStyle;
	const FSlateFontInfo TextFont = Font(EFont::Regular, FMath::RoundToInt(17 * Scale));
	const FSlateFontInfo KeyFont = Font(EFont::Bold, FMath::RoundToInt(14 * Scale));
	const float PadX = 12.f * Scale, KeyPad = 7.f * Scale, Gap = 22.f * Scale, Height = 34.f * Scale;
	const TArray<TPair<FString, FString>> Items = Pairs(Text);
	// Measure, then lay out centred.
	float Width = 0.f;
	for (const auto& Item : Items)
	{
		if (!Item.Key.IsEmpty())
		{
			Width += Measure(Item.Key, KeyFont).X + KeyPad * 2.f + 8.f * Scale;
		}
		Width += Measure(Item.Value, TextFont).X + Gap;
	}
	Width -= Gap;
	const float Left = CentreX - Width * 0.5f - PadX;
	FillRect(Out, Layer, G, FVector2f(Left, Y), FVector2f(Width + PadX * 2.f, Height), Srgb(0.02f, 0.03f, 0.04f, 0.62f));
	float X = Left + PadX;
	for (const auto& Item : Items)
	{
		if (!Item.Key.IsEmpty())
		{
			const float KeyW = Measure(Item.Key, KeyFont).X + KeyPad * 2.f;
			const float KeyH = 22.f * Scale;
			const float KeyY = Y + (Height - KeyH) * 0.5f;
			FillRect(Out, Layer + 1, G, FVector2f(X, KeyY), FVector2f(KeyW, KeyH), Srgb(0.92f, 0.92f, 0.9f, 0.95f));
			DrawString(Out, Layer + 2, G, Item.Key, KeyFont, FVector2f(X + KeyPad, KeyY + 2.f * Scale), Srgb(0.05f, 0.05f, 0.06f));
			X += KeyW + 8.f * Scale;
		}
		DrawString(Out, Layer + 1, G, Item.Value, TextFont, FVector2f(X, Y + 6.f * Scale), Colour);
		X += Measure(Item.Value, TextFont).X + Gap;
	}
	if (Progress >= 0.f)
	{
		const float BarY = Y + Height + 3.f * Scale;
		FillRect(Out, Layer, G, FVector2f(Left, BarY), FVector2f(Width + PadX * 2.f, 5.f * Scale), Srgb(0.f, 0.f, 0.f, 0.5f));
		FillRect(Out, Layer + 1, G, FVector2f(Left, BarY), FVector2f((Width + PadX * 2.f) * FMath::Clamp(Progress, 0.f, 1.f), 5.f * Scale), Accent());
		return Height + 10.f * Scale;
	}
	return Height;
}

int32 SRiptideHudOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace RiptideMenuStyle;
	const ARiptideHUD* H = Hud.Get();
	if (!H || H->IsMenuOpen())
	{
		return LayerId;
	}
	const FVector2f Size = G.GetLocalSize();
	const float Scale = FMath::Clamp(Size.Y / 1080.f, 0.6f, 2.f);
	const double T = Now(H->GetWorld());

	// Asleep: the screen goes dark (the prompts still show over it).
	if (H->FadeTime >= 0.0 && T - H->FadeTime < 0.25 && H->FadeAlpha > 0.f)
	{
		FillRect(Out, LayerId, G, FVector2f(0.f), Size, Srgb(0.f, 0.f, 0.02f, FMath::Clamp(H->FadeAlpha, 0.f, 1.f)));
		LayerId += 1;
	}
	// The time of day, top right.
	if (const ARiptideSkyClock* Clock = ARiptideSkyClock::Get(H))
	{
		const float Hours = Clock->GetHours();
		const FString Time = FString::Printf(TEXT("%s  %02d:%02d"), Clock->IsNight() ? TEXT("NIGHT") : TEXT("DAY"), int32(Hours), int32(FMath::Frac(Hours) * 60.f));
		const FSlateFontInfo ClockFont = Font(EFont::Bold, FMath::RoundToInt(16 * Scale));
		DrawString(Out, LayerId + 1, G, Time, ClockFont, FVector2f(Size.X - 30.f * Scale, 26.f * Scale), Srgb(1.f, 1.f, 1.f, 0.8f), 1.f);
	}

	// A small crosshair dot, so it's clear what E acts on.
	const float Dot = 4.f * Scale;
	FillRect(Out, LayerId, G, Size * 0.5f - FVector2f(Dot * 0.5f), FVector2f(Dot), Srgb(1.f, 1.f, 1.f, 0.7f));

	// Prompts, stacked up from the bottom middle: what's under the crosshair nearest the centre.
	float Y = Size.Y - 70.f * Scale;
	using RiptideHud::ESlot;
	for (const ESlot Slot : { ESlot::HelmReadout, ESlot::Helm, ESlot::Radio, ESlot::Context, ESlot::Focus })
	{
		const ARiptideHUD::FPrompt& P = H->Prompts[int32(Slot)];
		if (P.Time < 0.0 || T - P.Time > PromptLife || P.Text.IsEmpty())
		{
			continue;
		}
		const float Height = 34.f * Scale + (P.Progress >= 0.f ? 10.f * Scale : 0.f);
		Y -= Height + 8.f * Scale;
		DrawPromptLine(Out, LayerId + 1, G, Scale, P.Text, P.Colour, Size.X * 0.5f, Y, P.Progress);
	}

	// Vitals, bottom left: three bars, the low ones in the warning colour.
	if (H->VitalsTime >= 0.0 && T - H->VitalsTime < 1.0)
	{
		const FSlateFontInfo Label = Font(EFont::Bold, FMath::RoundToInt(16 * Scale));
		const float W = 220.f * Scale, BarH = 11.f * Scale, Row = 28.f * Scale;
		float BY = Size.Y - 40.f * Scale - Row * 3.f;
		const float BX = 34.f * Scale;
		const TPair<const TCHAR*, float> Bars[] = { { TEXT("HEALTH"), H->VitalHealth }, { TEXT("FOOD"), H->VitalFood }, { TEXT("WATER"), H->VitalWater } };
		const FLinearColor Fills[] = { Srgb(0.85f, 0.25f, 0.22f), Srgb(0.9f, 0.6f, 0.2f), Srgb(0.3f, 0.6f, 0.95f) };
		for (int32 i = 0; i < 3; ++i)
		{
			const float V = FMath::Clamp(Bars[i].Value / 100.f, 0.f, 1.f);
			const bool bLow = Bars[i].Value < (i == 0 ? 30.f : 20.f);
			DrawString(Out, LayerId + 1, G, Bars[i].Key, Label, FVector2f(BX, BY), bLow ? Warning() : Srgb(1.f, 1.f, 1.f, 0.85f));
			const float Bx = BX + 84.f * Scale;
			FillRect(Out, LayerId, G, FVector2f(Bx, BY + 5.f * Scale), FVector2f(W, BarH), Srgb(0.f, 0.f, 0.f, 0.45f));
			FillRect(Out, LayerId + 1, G, FVector2f(Bx, BY + 5.f * Scale), FVector2f(W * V, BarH), Fills[i]);
			BY += Row;
		}
		// Under the bars: sick, and how the cold is (warm by a fire or in a shelter; cold, then freezing, out at night).
		FString Status;
		if (H->bVitalSick)
		{
			Status += TEXT("SICK   ");
		}
		if (H->VitalCold >= 75.f)
		{
			Status += TEXT("FREEZING");
		}
		else if (H->VitalCold >= 35.f)
		{
			Status += TEXT("COLD");
		}
		else if (H->bVitalWarm)
		{
			Status += TEXT("WARM");
		}
		if (!Status.IsEmpty())
		{
			const bool bBad = H->bVitalSick || H->VitalCold >= 35.f;
			DrawString(Out, LayerId + 1, G, Status, Label, FVector2f(BX, BY), bBad ? Warning() : Srgb(1.f, 0.75f, 0.4f));
		}
	}

	// Notes, top left, fading out over their last second.
	const FSlateFontInfo NoteFont = Font(EFont::Regular, FMath::RoundToInt(16 * Scale));
	float NY = 30.f * Scale;
	for (const ARiptideHUD::FNote& N : H->Notes)
	{
		const double Left = N.Until - T;
		if (Left <= 0.0)
		{
			continue;
		}
		FLinearColor C = N.Colour;
		C.A *= float(FMath::Clamp(Left, 0.0, 1.0));
		const FVector2f TextSize = Measure(N.Text, NoteFont);
		FillRect(Out, LayerId, G, FVector2f(24.f * Scale, NY - 4.f * Scale), FVector2f(TextSize.X + 20.f * Scale, TextSize.Y + 8.f * Scale),
			Srgb(0.02f, 0.03f, 0.04f, 0.5f * C.A));
		DrawString(Out, LayerId + 1, G, N.Text, NoteFont, FVector2f(34.f * Scale, NY), C);
		NY += TextSize.Y + 12.f * Scale;
	}
	return LayerId + 3;
}
