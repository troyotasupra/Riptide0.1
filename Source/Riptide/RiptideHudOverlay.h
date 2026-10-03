#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AActor;
class APlayerController;
class ARiptideHUD;

/**
 * What the game shows on screen while playing (not the menus): prompts for what the keys do, the body's vitals, a
 * crosshair, and notes that come and go (radio lines, the dev mode's messages).
 *
 * Gameplay code posts to it every frame it wants something shown (Prompt, Vitals); a prompt not posted again for a
 * moment disappears by itself. Notes stay for their given time. A prompt is plain text, or key-and-action pairs
 * written "E  Take the helm      H  Tuning readout" (two spaces after the key, four or more between pairs), which are
 * drawn with each key in a keycap.
 *
 * Unlike the engine's on-screen debug messages (which shipping builds don't draw), this is part of the game.
 */
namespace RiptideHud
{
	/** Where a prompt goes: the line for what's under the crosshair, the place-specific prompts, the boat's readout. */
	enum class ESlot : uint8 { Focus, Context, Helm, HelmReadout, Radio, Count };

	/** Shows Text in Slot this frame, for the player controlling (or riding as) For. Progress 0-1 draws a bar under it. */
	RIPTIDE_API void Prompt(const AActor* For, ESlot Slot, const FString& Text, const FLinearColor& Colour = FLinearColor::White, float Progress = -1.f);

	/** The body's state this frame (each 0-100; Cold 0 warm to 100 freezing, bWarm by a fire or in a shelter). */
	RIPTIDE_API void Vitals(const AActor* For, float Health, float Food, float Water, bool bSick, float Cold = 0.f, bool bWarm = false);

	/** Darkens the whole screen this frame (asleep): 0 clear, 1 black. */
	RIPTIDE_API void Fade(const AActor* For, float Alpha);

	/** A note for Seconds (radio lines, dev messages), newest at the bottom; Key replaces an earlier note with it. */
	RIPTIDE_API void Note(const APlayerController* Player, const FString& Text, float Seconds, const FLinearColor& Colour = FLinearColor::White,
		int32 Key = INDEX_NONE);

	/** The HUD of the local player controlling For (a pawn, or a controller), if it's this machine's. */
	RIPTIDE_API ARiptideHUD* HudFor(const AActor* For);
}

/** The overlay drawing an ARiptideHUD's prompts, vitals and notes. */
class RIPTIDE_API SRiptideHudOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideHudOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptideHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.f, 1.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** Draws a prompt line centred on CentreX with its top at Y; returns its height. */
	float DrawPromptLine(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float Scale, const FString& Text, const FLinearColor& Colour,
		float CentreX, float Y, float Progress) const;

	TWeakObjectPtr<ARiptideHUD> Hud;
};
