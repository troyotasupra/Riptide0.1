#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RiptideSettings.generated.h"

class USoundMix;

/**
 * The player's own settings that the engine's GameUserSettings doesn't keep: how the controls feel, the field of
 * view, and the volume of each kind of sound. Saved in the slot "Settings". (Screen resolution, window mode, VSync,
 * the frame-rate limit and the graphics quality are UGameUserSettings', saved in GameUserSettings.ini.)
 *
 * Read anywhere through Get(); the settings screen changes it, then calls Save() and Apply().
 */
UCLASS()
class RIPTIDE_API URiptideSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Multiplies how far the view turns for a given mouse or stick movement (on foot and at the helm). */
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float MouseSensitivity = 1.f;

	/** Pushing the mouse (or stick) forward looks down instead of up. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	bool bInvertY = false;

	/** Horizontal field of view in degrees, in first person and at the helm. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float FieldOfView = 90.f;

	/** Volumes, 0 to 1: everything, the boat and the world's effects (engines, wash, slaps), and the sea's ambience. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float MasterVolume = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float EffectsVolume = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	float AmbientVolume = 1.f;

	/** The microphone to talk into, by its device name; empty for the system's default. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
	FString Microphone;

	/** The player's settings, loaded from disk the first time (defaults if there are none yet). Never null. */
	UFUNCTION(BlueprintPure, Category = "Settings")
	static URiptideSettingsSave* Get();

	/** Writes them to disk. True if it was written. */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	bool Save();

	/** Puts the volumes into effect in WorldContext's world (each world can have its own audio device in the editor). */
	UFUNCTION(BlueprintCallable, Category = "Settings", meta = (WorldContext = "WorldContext"))
	void Apply(const UObject* WorldContext);

	/** Keeps every value in its range (a hand-edited or old save can't break the controls). */
	void Sanitise();

	static const TCHAR* SlotName;

	// The ranges the settings screen offers.
	static constexpr float MinSensitivity = 0.2f;
	static constexpr float MaxSensitivity = 3.f;
	static constexpr float MinFieldOfView = 70.f;
	static constexpr float MaxFieldOfView = 110.f;

private:
	/** Carries the effects and ambience volumes to their sound classes (made by init_unreal.py). */
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> VolumeMix;

	/** The audio devices the mix is pushed on (pushing it twice would only count it twice). */
	TSet<uint32> PushedToDevices;
};

namespace RiptideSettings
{
	/** What to multiply look input by (X turns, Y looks up and down): the player's sensitivity, with the up-down
	 * axis flipped when they invert it. */
	RIPTIDE_API FVector2D LookScale();
}
