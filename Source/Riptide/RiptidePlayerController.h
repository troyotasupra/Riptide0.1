#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RiptideDevCamera.h"
#include "RiptidePlayerController.generated.h"

class ARiptideBoat;
class ARiptideCharacter;
class ADirectionalLight;
class APostProcessVolume;
class AWaterBodyOcean;
class UInputAction;
class UInputMappingContext;
class UWaterWavesBase;
class SRiptideDevPanel;
class SWidget;
struct FRiptideDevPanelRow;

/** Dev mode is for building and playtesting: Shipping builds leave it out. */
#define RIPTIDE_WITH_DEV_MODE (!UE_BUILD_SHIPPING)

/** The dev mode's lighting presets: where the sun stands. */
UENUM(BlueprintType)
enum class ERiptideTimeOfDay : uint8
{
	Day,            // the level's own sun
	GoldenHour,
	Dusk,
	Night
};

/** The dev mode's sea presets. */
UENUM(BlueprintType)
enum class ERiptideSeaState : uint8
{
	Calm,           // a low swell, a few tens of centimetres
	Moderate,       // the level's own 1-1.5 m swell
	Rough,          // 2.5-3 m seas
	Other           // waves set some other way (a test turning them off)
};

/**
 * The player's controller. It drives whatever the player controls (their crew member, or the boat from its helm; the
 * pawns bring their own controls), and carries the dev mode Troy playtests with, keys that work anywhere:
 *
 *   F1 the dev panel (every dev key and live readings)   F2 fly (a noclip camera; again to go back to exactly where
 *   you were, on foot or at the helm)   F3 camera mode (free, ride along with the boat, orbit it, chase it)
 *   F4 drop in where the camera is (flying), or back to the boat's helm (on foot)   F5 time of day   F6 slow motion
 *   Pause or \ freeze the world (the camera still flies)   F7 god mode (never thrown by the deck, fuel never runs out)
 *   F10 photo mode (hides everything on screen)   PgUp the boat's physics overlay   PgDn sea state
 *   Ins or Del refuel and repair   Home right the boat and stop it   End bring the boat to where the camera looks
 *
 * Dev mode runs on the machine that runs the game (single player, or the host); a joining player is told so.
 * The UFUNCTIONs below do what the keys do, for automated tests (Tools/dev_mode_test.py).
 */
UCLASS()
class RIPTIDE_API ARiptidePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARiptidePlayerController();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** A player leaving the game takes their crew member with them, never the boat they were driving (or the fly
	 * camera): the engine destroys whatever pawn the leaving controller has. */
	virtual void PawnLeavingGame() override;

	// --- Panel ---

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetDevPanelShown(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsDevPanelShown() const { return bDevPanelShown; }

	/** True if the panel is actually on screen (shown, and not hidden by photo mode). */
	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsDevPanelOnScreen() const;

	/** What the dev panel says this frame. */
	void GetDevPanelRows(TArray<FRiptideDevPanelRow>& Out) const;

	// --- Flying ---

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetFlying(bool bFly);

	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsFlying() const { return DevCamera != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Dev")
	ARiptideDevCamera* GetDevCamera() const { return DevCamera; }

	/** Next camera mode (starts flying if not already). */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void CycleCameraMode();

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetDevCameraMode(ERiptideDevCameraMode InMode);

	UFUNCTION(BlueprintPure, Category = "Dev")
	ERiptideDevCameraMode GetDevCameraMode() const { return CameraMode; }

	/** Puts the player's crew member where the camera is (on the deck, in the air or in the sea) and stops flying. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void DropInHere();

	/** Puts the player's crew member on the boat's deck at the helm (and stops flying). */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void BackToTheBoat();

	/** The crew member this player plays (on foot, at the helm, or left behind while flying). */
	UFUNCTION(BlueprintPure, Category = "Dev")
	ARiptideCharacter* GetCrewMember() const { return CrewMember.Get(); }

	// --- World ---

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetTimeOfDay(ERiptideTimeOfDay InTime);

	UFUNCTION(BlueprintPure, Category = "Dev")
	ERiptideTimeOfDay GetTimeOfDay() const { return TimeOfDay; }

	/** Game speed: 1 is normal, 0.1 is a tenth. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetTimeScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "Dev")
	float GetTimeScale() const;

	/** Stops the world dead (the fly camera still flies), or lets it go again. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetWorldFrozen(bool bFreeze);

	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsWorldFrozen() const;

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetSeaState(ERiptideSeaState InState);

	UFUNCTION(BlueprintPure, Category = "Dev")
	ERiptideSeaState GetSeaState() const;

	// --- Player and boat ---

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetGodMode(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsGodMode() const { return bGodMode; }

	/** Fills the tank and mends both motors. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void RefuelAndRepair();

	/** Sets the boat upright on the sea where it is, dead in the water (the throttle stays where it is). */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void RightAndStopBoat();

	/** Brings the boat to where the fly camera is looking (on the sea there, heading the way the camera faces). */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void BringBoatHere();

	/** The boat the dev tools work on: the one being driven, or the crew member's own. */
	UFUNCTION(BlueprintPure, Category = "Dev")
	ARiptideBoat* GetDevBoat() const;

	// --- View ---

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetPhotoMode(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsPhotoMode() const { return bPhotoMode; }

	/** Whether prompts and notes printed on screen show at all (photo mode turns them off). */
	UFUNCTION(BlueprintPure, Category = "Dev")
	bool AreScreenMessagesShown() const;

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetPhysicsOverlay(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Dev")
	bool IsPhysicsOverlayOn() const { return bPhysicsOverlay; }

protected:
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

private:
	/** Dev mode works for the player on the machine running the game (single player or the host). */
	bool CanUseDevMode() const;
	bool CheckDevMode();
	void BuildDevInput();

	/** Stops the engine's own debug shortcuts on the dev keys (F1 wireframe, F2-F5 view modes, PgUp/PgDn debug
	 * targets) from firing along with them. */
	void FreeDevKeysFromEngineShortcuts();

	void StartFlying();
	/** Stops flying, controlling Pawn (or whatever was flown from, if null). */
	void StopFlying(APawn* Into = nullptr);

	/** Puts a crew member at a point, standing (feet below it) or falling, moving with the deck if it's over the boat. */
	void PlaceCrewMember(ARiptideCharacter& Crew, const FVector& EyeLocation, const FRotator& View);

	/** Moves the boat to a spot on the sea, upright and still, carrying whoever stands on its deck. */
	void PlaceBoat(ARiptideBoat& Boat, FVector Where, float Yaw);

	ADirectionalLight* FindSun() const;
	AWaterBodyOcean* FindOcean() const;
	UWaterWavesBase* MakeWaves(AWaterBodyOcean& Ocean, ERiptideSeaState State);

	void UpdatePanelVisibility();
	void DrawPhysicsOverlay(const ARiptideBoat& Boat) const;

	/** A short note on screen about what a dev key just did. */
	void DevNote(const FString& Text, const FColor& Colour = FColor(120, 215, 255), float Seconds = 2.5f) const;

	static FString TimeOfDayName(ERiptideTimeOfDay Time);
	static FString SeaStateName(ERiptideSeaState State);

	UPROPERTY(Transient)
	TObjectPtr<ARiptideDevCamera> DevCamera;

	/** What the player controlled before flying (their crew member, or the boat at its helm). */
	TWeakObjectPtr<APawn> FlownFrom;

	TWeakObjectPtr<ARiptideCharacter> CrewMember;

	/** The crew member left on deck while flying keeps moving without a controller (riding the deck, floating);
	 * what that setting was before. */
	TWeakObjectPtr<ARiptideCharacter> RidingCrew;
	bool bRidingCrewRanWithoutController = false;
	float ViewPitchBeforeFlying = 0.f;

	ERiptideDevCameraMode CameraMode = ERiptideDevCameraMode::Free;
	float FlySpeed = 800.f;

	bool bDevPanelShown = false;
	bool bGodMode = false;
	bool bPhotoMode = false;
	bool bScreenMessagesBeforePhoto = true;
	bool bPhysicsOverlay = false;

	ERiptideTimeOfDay TimeOfDay = ERiptideTimeOfDay::Day;
	bool bHaveSunDefaults = false;
	FRotator SunDefaultRotation = FRotator::ZeroRotator;
	float SunDefaultIntensity = 10.f;

	/** Darkens the picture at night (the eye's auto exposure would otherwise turn moonlight back into day). */
	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> NightExposure;
	FLinearColor SunDefaultColour = FLinearColor::White;

	/** The ocean's own waves (the moderate swell) and collision height, and the other presets once made. */
	UPROPERTY(Transient)
	TObjectPtr<UWaterWavesBase> LevelWaves;

	UPROPERTY(Transient)
	TObjectPtr<UWaterWavesBase> CalmWaves;

	UPROPERTY(Transient)
	TObjectPtr<UWaterWavesBase> RoughWaves;

	bool bHaveLevelWaves = false;
	float LevelCollisionHeightOffset = 0.f;

	/** Frame time, smoothed for the panel (seconds). */
	float SmoothedFrameSeconds = 1.f / 60.f;

	TSharedPtr<SRiptideDevPanel> DevPanel;
	TSharedPtr<SWidget> DevPanelContainer;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> DevMapping;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> DevActions;
};
