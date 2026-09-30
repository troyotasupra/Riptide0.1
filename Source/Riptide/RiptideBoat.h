#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RiptideBoat.generated.h"

class ARiptideCharacter;
class UWidgetComponent;
class UAudioComponent;
class UStaticMesh;
class UMaterialInterface;
class URiptideSprayComponent;
class URiptideStorageComponent;
class UPointLightComponent;
class USpotLightComponent;
class UMaterialInstanceDynamic;
class URiptideWakeFoamComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UBuoyancyComponent;
class UCameraComponent;
class USoundAttenuation;
class USoundBase;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** The gearbox, set by the throttle lever: centred is neutral, pushed forward engages forward, pulled back reverse. */
UENUM(BlueprintType)
enum class ERiptideGear : uint8
{
	Reverse,
	Neutral,
	Forward
};

/**
 * Motorboat driven from the helm in first person.
 *
 * Handling is physics-driven: an outboard motor pushes from the stern only while the
 * propeller is underwater, and steering swings the motor so the thrust itself turns the
 * boat. That means the boat barely turns at idle, loses drive when it catches air off a
 * swell, and slides through turns until the keel bites.
 */
UCLASS()
class RIPTIDE_API ARiptideBoat : public APawn
{
	GENERATED_BODY()

public:
	ARiptideBoat();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetSpeedKnots() const;

	UFUNCTION(BlueprintPure, Category = "Boat")
	bool IsPropellerSubmerged() const;

	UFUNCTION(BlueprintCallable, Category = "Boat")
	void ApplyEngineDamage(float Amount);

	/**
	 * Holds the helm controls as if keys were held: Throttle moves the lever (-1..1), Steer swings the motor (-1..1).
	 * For AI helmsmen and automated handling tests; players drive through input. Server only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boat")
	void SetHelmInput(float Throttle, float Steer);

	/** Holds the trim switch as if pressed: 1 trims the motors out (bow up), -1 trims them in (bow down). Server only. */
	UFUNCTION(BlueprintCallable, Category = "Boat")
	void SetTrimInput(float Trim);

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetThrottleLever() const { return ThrottleLever; }

	/** Which gear the lever is in. */
	UFUNCTION(BlueprintPure, Category = "Boat")
	ERiptideGear GetGear() const;

	/** How far the throttle is open past the gear detent, 0 (idle) to 1 (full). */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetThrottleOpening() const;

	/** Engine speed for the tachometer; 0 when the engine isn't running. */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetEngineRpm() const;

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetFuelFraction() const { return FuelCapacityLiters > 0.f ? FuelLiters / FuelCapacityLiters : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetEngineHealth() const { return EngineHealth; }

	float GetMinTrimDeg() const { return MinTrimDeg; }
	float GetMaxTrimDeg() const { return MaxTrimDeg; }

	/** Motor trim in degrees: positive is trimmed out (bow up), negative trimmed in (bow down). */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetTrimDeg() const { return TrimDeg; }

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetEngineOutput() const { return EngineOutput; }

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetFuelLiters() const { return FuelLiters; }

	/** Height of the sea's surface, waves included, at a point (Z = 0 if there's no ocean). */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetSeaSurfaceZ(FVector Location) const;

	/** Height of the bow's deck edge above the water there, in cm. Negative means the bow is under. */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetBowFreeboardCm() const;

	// --- Crew ---

	/** Where the helmsman stands, behind the console, facing forward: a point on the deck, in the world. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	FTransform GetHelmStandTransform() const;

	/** A spot on the deck to put a crew member (0 = the helm, then the aft deck and the foredeck), in the world. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	FTransform GetDeckSpotTransform(int32 Index) const;

	/** The boat's lockers: the two floor lockers in front of the console, the stern box's hatches and the anchor locker. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	URiptideStorageComponent* GetLockers() const { return Lockers; }

	UFUNCTION(BlueprintCallable, Category = "Boat|Lights")
	void SetSearchlightOn(bool bOn);

	UFUNCTION(BlueprintCallable, Category = "Boat|Lights")
	void SetNavLightsOn(bool bOn);

	UFUNCTION(BlueprintCallable, Category = "Boat|Lights")
	void SetDeckLightsOn(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Boat|Lights")
	bool IsSearchlightOn() const { return bSearchlightOn; }

	UFUNCTION(BlueprintPure, Category = "Boat|Lights")
	bool AreNavLightsOn() const { return bNavLightsOn; }

	/** Points the searchlight: yaw and pitch relative to the boat, in degrees. Server only (the helm aims it). */
	UFUNCTION(BlueprintCallable, Category = "Boat|Lights")
	void AimSearchlight(float YawDeg, float PitchDeg);

	/** The foot of the boarding ladder on the transom (port side), at the waterline, and its top on the stern box. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	FTransform GetLadderFootTransform() const;

	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	FTransform GetLadderTopTransform() const;

	/** Where a climber steps down after coming over the transom: the cockpit deck, port side aft. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	FTransform GetLadderLandingTransform() const;

	/** Whoever is driving, or null. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	ARiptideCharacter* GetHelmsman() const { return Helmsman; }

	/** Puts a crew member at the helm: their player now drives the boat. False if someone already has it. Server only. */
	bool TakeHelm(ARiptideCharacter* Crew);

	/** The helmsman lets go of the wheel and stands back on the deck (the throttle stays where it was left). */
	UFUNCTION(BlueprintCallable, Category = "Boat|Crew")
	void LeaveHelm();

	/** Throws spray off the bow, as when it slaps into a wave. Strength 0..1 sets how big. */
	UFUNCTION(BlueprintCallable, Category = "Boat")
	void SprayAtBow(float Strength);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UBoxComponent> HullBody;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	/** Twin outboards on the transom, port and starboard. They steer together. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorMesh;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorMeshStarboard;

	/** The outboards' clamp brackets, fixed to the transom; the motors tilt and steer on them. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorBracket;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorBracketStarboard;

	/** The twin throttle levers on the console, which move with the throttle and gear. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> ThrottleLeverPort;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> ThrottleLeverStarboard;

	/** Dash instruments: tachometer, speedometer, and the screen (gear, trim, fuel, heading, warnings). */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<URiptideStorageComponent> Lockers;

	// --- Lights ---
	// Navigation lights (masthead all-round white, bow red and green) are lit at the start; the searchlight and the
	// cockpit floods are switched from the helm. The searchlight follows where the helmsman looks.

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<UStaticMeshComponent> SearchlightHead;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<USpotLightComponent> SearchlightBeam;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<UPointLightComponent> MastheadLight;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<USpotLightComponent> BowLightPort;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<USpotLightComponent> BowLightStarboard;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<USpotLightComponent> DeckFloodPort;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Lights")
	TObjectPtr<USpotLightComponent> DeckFloodStarboard;

	UPROPERTY(EditAnywhere, Category = "Boat|Lights")
	TSoftObjectPtr<UStaticMesh> SearchlightModel;

	UPROPERTY(EditAnywhere, Category = "Boat|Lights")
	TSoftObjectPtr<UMaterialInterface> LampOnMaterial;

	/** How fast the searchlight swings to where it's aimed, in degrees per second. */
	UPROPERTY(EditAnywhere, Category = "Boat|Lights")
	float SearchlightSlewDeg = 120.f;

	UPROPERTY(ReplicatedUsing = OnRep_Lights)
	bool bSearchlightOn = false;

	UPROPERTY(ReplicatedUsing = OnRep_Lights)
	bool bNavLightsOn = true;

	UPROPERTY(ReplicatedUsing = OnRep_Lights)
	bool bDeckLightsOn = false;

	/** Where the searchlight is aimed (degrees, relative to the boat). */
	UPROPERTY(Replicated)
	float SearchlightYaw = 0.f;

	UPROPERTY(Replicated)
	float SearchlightPitch = -5.f;

	UFUNCTION()
	void OnRep_Lights();

	/** The open-array radar antenna on the T-top, turning while the engines run. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> RadarArray;

	/** Radar antenna speed, in revolutions per minute. */
	UPROPERTY(EditAnywhere, Category = "Boat")
	float RadarRpm = 24.f;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UWidgetComponent> GaugeTach;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UWidgetComponent> GaugeSpeed;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UWidgetComponent> GaugeDisplay;

	/**
	 * What the crew walks on: the hull model's own triangles (deck, bulwarks, console, T-top legs, rails), invisible.
	 * It's separate from the physics body so it can be the exact concave shape of the deck, and only the crew
	 * collides with it. It rides along with the hull rather than being part of its physics.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> DeckCollision;

	/** The boat and outboard models (generated by Content/Python/riptide_boat_mesh.py). */
	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> HullModel;

	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> OutboardModel;

	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> BracketModel;

	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> LeverModel;

	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> RadarModel;

	/** The two props, where each motor's thrust pushes. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<USceneComponent> Propeller;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<USceneComponent> PropellerStarboard;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UCameraComponent> HelmCamera;

	// The wake is the Water plugin's fluid simulation (BP_FluidSim_01): a ripple solver on a patch of water that
	// follows the player. The hull pushes it with Epic's boat force (a hull-shaped push plus foam), so the waves
	// and foam form a real V behind the boat. The push's strength comes from the hull's speed; how hard that
	// disturbs the water is set by HeightScale and FoamScale on MI_WakeForce (Content/Python/init_unreal.py).

	/** Point at the hull's waterline that pushes the wake simulation. */
	UPROPERTY(VisibleAnywhere, Category = "Boat|Wake")
	TObjectPtr<USceneComponent> WakeSource;

	/** Material the wake simulation draws the hull's push with. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	TSoftObjectPtr<UMaterialInterface> WakeForceMaterial;

	/** Half the length of the hull's footprint on the wake simulation, in cm. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float WakeRadius = 395.f;

	/** Size of the patch of water the wake simulation covers around the player, in cm. Bigger makes a longer
	 * wake at a coarser grid (the simulation is 1024 cells across). */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float WakeSimulationSize = 8192.f;

	/** How fast ripples cross the simulation, in grid cells per step (at most 1). With the patch size this sets
	 * the ripple speed, and the V opens wider the closer that gets to the boat's speed. 0.45 on an 80 m patch is
	 * about 3.2 m/s, giving a V close to a real wake at cruising speed. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float WakeSimulationWaveSpeed = 0.45f;

	/** How quickly the simulation's ripples die out. Low keeps the V's arms trailing well behind the boat;
	 * high (the simulation's own 0.05 and up) smothers them. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float WakeSimulationDamping = 0.02f;

	/** White foam on the water: churned water behind the transom and wash peeling off the bow. */
	UPROPERTY(VisibleAnywhere, Category = "Boat|Wake")
	TObjectPtr<URiptideWakeFoamComponent> WakeFoam;

	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	TSoftObjectPtr<UMaterialInterface> WakeFoamMaterial;

	/** Speed at which the foam is at its thickest. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float FoamFullSpeedKnots = 24.f;

	/** Water thrown into the air off the hull: sheets peeling off the bow wave at speed, and bursts crashing out
	 * when the bow slams into a wave. */
	UPROPERTY(VisibleAnywhere, Category = "Boat|Wake")
	TObjectPtr<URiptideSprayComponent> Spray;

	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	TSoftObjectPtr<UMaterialInterface> SprayMaterial;

	/** Speed where the bow starts throwing spray, and where it's throwing its most. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float SprayStartKnots = 9.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float SprayFullKnots = 28.f;

	/** Spray clouds per second off each side of the bow at full speed. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float BowSprayRate = 450.f;

	/** Whitewater clouds per second boiling up behind the props at full speed and throttle. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float SternSprayRate = 160.f;

	// --- Sound ---
	// Levels are set on the sound assets themselves (see Content/Python/init_unreal.py), so volume 1 here is
	// the loudest each should get: the engine at full throttle, the wash at top speed.

	// The engine is two recordings of a real outboard, one at low revs and one at high revs. Both are pitched to the
	// same engine speed and crossfaded, so the motor sounds right across the whole throttle range. Each of the twin
	// motors plays its own pair, a touch out of tune with the other like two real engines, each at half power so
	// together they're as loud as the single engine the levels were set for.

	UPROPERTY(VisibleAnywhere, Category = "Boat|Sound")
	TObjectPtr<UAudioComponent> EngineAudio;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Sound")
	TObjectPtr<UAudioComponent> EngineHighAudio;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Sound")
	TObjectPtr<UAudioComponent> EngineAudioStarboard;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Sound")
	TObjectPtr<UAudioComponent> EngineHighAudioStarboard;

	/** How far the starboard engine runs off the port one's pitch (1.01 = 1% faster). */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float StarboardEngineDetune = 1.012f;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Sound")
	TObjectPtr<UAudioComponent> WashAudio;

	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	TSoftObjectPtr<USoundBase> EngineSound;

	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	TSoftObjectPtr<USoundBase> EngineHighSound;

	/** The engine's firing pitch in the low- and high-rev recordings (Hz), found from their spectra. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float EngineLowRecordingHz = 25.6f;

	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float EngineHighRecordingHz = 73.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	TSoftObjectPtr<USoundBase> WashSound;

	/** Played at random when the bow slams into a wave. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	TArray<TSoftObjectPtr<USoundBase>> HullSlapSounds;

	/** Engine firing pitch at idle and at full revs (Hz). */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float EngineIdleHz = 25.6f;

	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float EngineFullHz = 73.f;

	/** Engine loop volume at idle, as a fraction of full throttle. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound", meta = (ClampMin = "0", ClampMax = "1"))
	float EngineIdleVolume = 0.4f;

	/** Extra revs when the prop comes out of the water and the engine races, as a fraction of full revs. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound", meta = (ClampMin = "0", ClampMax = "1"))
	float PropOutOverRev = 0.3f;

	/** Speed at which the hull wash reaches full volume. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float WashFullSpeedKnots = 28.f;

	/** How fast the bow must meet the water for a slap (cm/s), and the speed of the loudest slap. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float SlapMinSpeed = 120.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float SlapFullSpeed = 450.f;

	/** Shortest gap between slaps, in seconds. */
	UPROPERTY(EditAnywhere, Category = "Boat|Sound")
	float SlapCooldown = 0.35f;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UBuoyancyComponent> Buoyancy;

	// --- Hull ---

	UPROPERTY(EditAnywhere, Category = "Boat|Hull", meta = (ClampMin = "50"))
	float HullMassKg = 2000.f;

	/** Water resistance moving forward (N per (m/s)^2). Sets top speed. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float ForwardDrag = 59.f;

	/** Water resistance moving sideways (N per (m/s)^2). The keel: higher = less sliding in turns. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float LateralDrag = 2000.f;

	/** Water resistance to bobbing up and down (N per m/s). Higher = the hull settles faster after a wave. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float HeaveDamping = 13300.f;

	/** How quickly the hull stops spinning, in 1/s. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float YawDamping = 1.2f;

	/** Bow-up angle a planing hull runs at on its own, in degrees; past it the bottom pushes the bow back down. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningRunningTrimDeg = 3.f;

	/** How hard the bottom pushes the bow back down per degree past the running angle, at planing speed (N*m). */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningPitchStiffness = 12000.f;

	/** How quickly the hull's spin dies out while it's in the air, in 1/s. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float AirRockDamping = 1.5f;

	/** How quickly roll and pitch rocking dies out in the water, in 1/s. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float RockDamping = 6.f;

	/**
	 * Hydrodynamic lift on the forward hull as it moves (N per (m/s)^2 of forward speed). Pushes the bow up
	 * at speed so it rides over swells instead of burying. Only acts where the forward hull is in the water.
	 */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningLift = 155.f;

	/** Largest planing lift as a fraction of the boat's weight, so jumps off wave crests don't launch it. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxPlaningLiftFraction = 0.5f;

	// --- Engine ---

	/** Thrust of both motors together at full throttle, in Newtons (twin 250 hp outboards). Each motor gives half, and
	 * only while its prop is in the water. With ForwardDrag this sets a top speed of about 30 knots. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MaxThrust = 14000.f;

	/** Fraction of forward thrust available in reverse. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "1"))
	float ReverseThrustScale = 0.4f;

	/** The lever's neutral band either side of centre: past it the gear engages (forward or reverse), and the throttle
	 * opens from idle over the rest of the lever's travel. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.5"))
	float NeutralDetent = 0.12f;

	/** Thrust with the gear engaged and the throttle at idle, as a fraction of full (the boat creeps along in gear). */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.3"))
	float IdleThrustInGear = 0.06f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float IdleRpm = 650.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MaxRpm = 6000.f;

	/** How fast the throttle lever moves while the key is held, in full-range per second. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float ThrottleLeverRate = 0.6f;

	/** How fast the engine spools toward the lever position. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float EngineSpoolRate = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MaxSteerAngleDeg = 30.f;

	/** How fast the motor swings when steering, in degrees per second. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float SteerRateDeg = 70.f;

	// Trim tilts both motors on their transom brackets. Trimmed out, the lower units swing aft and the props push
	// slightly down on the stern, so the bow rides higher (faster, but the props ride nearer the surface and can
	// ventilate). Trimmed in, they push the stern up and hold the bow down (for getting onto the plane, or head seas).

	/** Furthest trimmed in (negative) and out (positive), in degrees. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MinTrimDeg = -6.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MaxTrimDeg = 16.f;

	/** How fast the trim moves while the switch is held, in degrees per second. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimRateDeg = 5.f;

	/**
	 * Pitching moment per degree of trim at TrimFullEffectKnots and full throttle, in N*m (positive trim lifts the
	 * bow). Tilting the thrust alone hardly moves a heavy hull; on a real boat trim mostly works through the hull's
	 * planing lift, so this grows with speed. At 2500 and cruising speed, fully out lifts the bow about 5 degrees
	 * (measured on flat water).
	 */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimMomentPerDeg = 2500.f;

	/** The same for trimming in (bow down). Trimmed in, the bow is pressed onto the water rather than lifted off it, so
	 * the hull doesn't run out of grip and it can work harder: fully in drops the bow about 3 degrees. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimInMomentPerDeg = 5500.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimFullEffectKnots = 22.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float FuelCapacityLiters = 40.f;

	/** Fuel burned per second at full throttle. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float FuelBurnPerSecond = 0.02f;

	// --- Camera ---

	UPROPERTY(EditAnywhere, Category = "Boat|Camera")
	float LookSensitivity = 1.f;

	// --- Replicated state (server-authoritative) ---

	/** Throttle lever position, -1 (full reverse) to 1 (full ahead). The lever stays where you leave it. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float ThrottleLever = 0.f;

	/** Actual engine output, lagging behind the lever. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float EngineOutput = 0.f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float SteerAngleDeg = 0.f;

	/** Motor trim, from MinTrimDeg to MaxTrimDeg. It stays where it's left. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float TrimDeg = 0.f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float FuelLiters = 0.f;

	/** 1 = healthy. Below 0.5 the engine sputters; at 0 it's dead. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float EngineHealth = 1.f;

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Swaps the placeholder shapes for the boat models, if they've been built. */
	void ApplyModels();

private:
	void BuildInput();
	void UpdateControls(float DeltaSeconds);
	void UpdateEngine(float DeltaSeconds);
	void ApplyThrust();
	void ApplyHydrodynamics();
	void DrawDebugHud() const;
	bool IsPropSubmerged(const USceneComponent* Prop) const;

	/** Reads the sea at each prop and decides whether it's biting: out once it's clear of the surface by PropDryMargin,
	 * back in once it's under again, so a prop skimming the surface doesn't flicker in and out every frame. */
	void UpdatePropImmersion();

	/** How the outboards sit on their brackets: tilted by the trim, then swung by the steering. */
	FQuat GetOutboardRotation() const;

	/** Turns and tilts the outboard models, and moves the props with them. */
	void PoseOutboards();
	void RegisterWithWakeSimulation();
	AActor* SpawnWakeSimulation(UClass* SimClass);
	void StartSounds();
	void UpdateSounds(float DeltaSeconds);
	void StartWakeFoam();
	void UpdateWakeFoam(float DeltaSeconds);
	void UpdateSpray(float DeltaSeconds);
	void SetUpLockers();

	/** Half the hull's width at its waterline, at X along it (cm). */
	static float WaterlineHalfBeam(float X);

	float BowSprayOwed = 0.f;

	/** The sea's height under the hull last frame, for how fast it's rising or falling. */
	float PrevSeaZ = 0.f;
	bool bHaveSeaZ = false;
	float SternSprayOwed = 0.f;

	void OnThrottle(const FInputActionValue& Value);
	void OnThrottleReleased(const FInputActionValue& Value);
	void OnSteer(const FInputActionValue& Value);
	void OnSteerReleased(const FInputActionValue& Value);
	void OnCutThrottle(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnLeaveHelm(const FInputActionValue& Value);
	void OnTrim(const FInputActionValue& Value);
	void ApplyLights();
	void UpdateSearchlight(float DeltaSeconds);

	UFUNCTION(Server, Reliable)
	void ServerToggleLight(uint8 Which);

	UFUNCTION(Server, Unreliable)
	void ServerAimSearchlight(float YawDeg, float PitchDeg);

	/** The searchlight's current (smoothed) aim. */
	float SearchlightYawNow = 0.f;
	float SearchlightPitchNow = -5.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LampOffMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> NavLenses;
	void OnTrimReleased(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerLeaveHelm();

	UFUNCTION(Server, Unreliable)
	void ServerSetControls(float InThrottleInput, float InSteerInput, float InTrimInput, bool bInCutThrottle);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> HelmMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CutThrottleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LeaveHelmAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TrimAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DebugHudAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SearchlightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> NavLightsAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DeckLightsAction;

	/** The tuning readout on screen (off by default; H at the helm). */
	bool bShowDebugHud = false;

	/** How hard the engines are driving the props, 0 (idle or neutral) to 1 (full), from the engine output. */
	float GetDriveFraction() const;

	UPROPERTY(Replicated, Transient)
	TObjectPtr<ARiptideCharacter> Helmsman;

	// Raw input from whoever is at the helm. On the server these come from ServerSetControls.
	float ThrottleInput = 0.f;
	float SteerInput = 0.f;
	float TrimInput = 0.f;
	bool bCutThrottleRequested = false;

	float LookYaw = 0.f;
	float LookPitch = 0.f;

	/** Remaining time the engine is cut out by a sputter. */
	float SputterTimeLeft = 0.f;

	/** Whether each prop (port, starboard) is in the water, from UpdatePropImmersion. */
	bool bPropWet[2] = { true, true };

	/** How long each prop has looked like changing state (seconds), before it does. */
	float PropStateTime[2] = { 0.f, 0.f };

	/** How far above the surface (cm) a prop has to rise before it counts as out of the water. */
	float PropDryMargin = 4.f;

	/** Index of the buoyancy pontoon nearest the propeller, used to read the water height there. */
	int32 SternPontoonIndex = INDEX_NONE;

	/** Index of a bow pontoon, used to read the water height at the bow. */
	int32 BowPontoonIndex = INDEX_NONE;

	/** Distance falloff shared by the boat's sounds, so other boats fade with distance. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundFalloff;

	/** The wake simulation this boat created, which it carries along with it. */
	TWeakObjectPtr<AActor> WakeSimulation;

	int32 SternFoamTrail = INDEX_NONE;
	float ChurnLevel = 0.f;

	bool bEngineSoundRunning = false;
	float EngineRevs = 0.f;
	float PrevBowFreeboard = 0.f;
	bool bHaveBowFreeboard = false;
	float SlapCooldownLeft = 0.f;
};
