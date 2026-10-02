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
class UProceduralMeshComponent;
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

	/** Damages one motor (0 port, 1 starboard) or both (-1). Below half health it sputters; at 0 it's dead. */
	UFUNCTION(BlueprintCallable, Category = "Boat")
	void ApplyEngineDamage(float Amount, int32 Motor = -1);

	/** Pours fuel into the tank (litres, up to its capacity). Server only. Returns how much went in. */
	UFUNCTION(BlueprintCallable, Category = "Boat")
	float AddFuel(float Liters);

	/** True if the tank has room for this much more fuel. */
	bool HasRoomForFuel(float Liters) const { return FuelCapacityLiters - FuelLiters >= Liters - 0.01f; }

	/** Whoever is on the boarding ladder (one at a time), or null. */
	ARiptideCharacter* GetLadderUser() const { return LadderUser; }

	/** Takes or frees the ladder. Server only. */
	void SetLadderUser(ARiptideCharacter* Crew) { LadderUser = Crew; }

	/** Redraws the mic's cord between the radio and the mic (its holder calls this once they've moved for the
	 * frame, so the cord stays on the hand). */
	void UpdateMicCord();

	/** Where the fuel filler is on the gunwale (starboard, aft), in the world. */
	UFUNCTION(BlueprintPure, Category = "Boat")
	FTransform GetFuelFillerTransform() const;

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetMotorHealth(int32 Motor) const { return Motor == 1 ? EngineHealthStarboard : EngineHealthPort; }

	/** One motor's output, -1 (full astern) to 1 (full ahead). */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetMotorOutput(int32 Motor) const { return Motor == 1 ? MotorOutputStarboard : MotorOutputPort; }

	UFUNCTION(BlueprintPure, Category = "Boat")
	bool IsMotorRunning(int32 Motor) const { return FuelLiters > 0.f && GetMotorHealth(Motor) > 0.f; }

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

	/** The two motors' health averaged (see GetMotorHealth for each). */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetEngineHealth() const { return 0.5f * (EngineHealthPort + EngineHealthStarboard); }

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

	/** True if a crew member at World can grab something solid: the gunwale, the console's grab rails, a T-top leg,
	 * the bow rail or the leaning post's rail. */
	UFUNCTION(BlueprintPure, Category = "Boat|Crew")
	bool IsHandholdNear(FVector World, float Reach) const;

	/** How fast a point on the deck is moving (world cm/s): the hull's velocity plus its spin at that point. */
	FVector GetDeckPointVelocity(const FVector& World) const;

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

	/** True if a world point is inside the hull (below its gunwale, within its sides). */
	bool IsInsideHull(const FVector& World) const;

	/** Roughly how far a world point is outside the hull (cm; negative inside): off its sides and bottom, beyond its
	 * ends, or above its gunwale. */
	float ClearanceFromHull(const FVector& World) const;

	// --- The radio ---
	// The VHF in the overhead box has a hand mic on a coiled cord, hanging on a clip beside it. Anyone within the
	// cord's reach can take it; walking out of reach pulls it from their hand, back onto its clip.

	/** Whoever is holding the radio's hand mic, or null while it hangs on its clip. */
	UFUNCTION(BlueprintPure, Category = "Boat|Radio")
	ARiptideCharacter* GetMicHolder() const { return MicHolder; }

	/** Takes the mic off its clip for a crew member in reach of it. Server only. False if someone else has it. */
	bool GrabMic(ARiptideCharacter* Crew);

	/** Hangs the mic back on its clip. Server only. */
	void HangUpMic();

	/** Where the mic is now (on its clip or in a hand), in the world. */
	UFUNCTION(BlueprintPure, Category = "Boat|Radio")
	FVector GetMicLocation() const;

	/** Where the mic's clip is, in the world. */
	UFUNCTION(BlueprintPure, Category = "Boat|Radio")
	FVector GetMicHookLocation() const;

	/** How far from the radio (cm) the mic can be carried before its cord pulls it back. */
	float GetMicCordReach() const { return MicCordReach; }

	/** The helm's wheel angle (degrees; turned right is positive), following the motors. */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetWheelAngleDeg() const;

	/** How fast each prop is turning (revolutions per second as drawn; negative is astern). */
	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetPropSpinRate(int32 Motor) const { return Motor == 0 || Motor == 1 ? PropSpinRate[Motor] : 0.f; }

	// For the dev mode's physics overlay: where each motor (0 port, 1 starboard) pushes, whether its prop is biting,
	// and which way the motors push (along their shafts, steered and trimmed), in the world.
	FVector GetPropLocation(int32 Motor) const { return (Motor == 1 ? PropellerStarboard : Propeller)->GetComponentLocation(); }
	bool IsPropWet(int32 Motor) const { return bPropWet[Motor == 1 ? 1 : 0]; }
	FVector GetThrustDirection() const { return GetActorQuat() * GetOutboardRotation().RotateVector(FVector::ForwardVector); }

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

	/** The outboards' swivel brackets, hung on the clamp brackets' tilt tubes: they trim with the motors, but don't steer. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorSwivel;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorSwivelStarboard;

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
	TSoftObjectPtr<UStaticMesh> SwivelModel;

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

	/** The props themselves, spinning on each motor's shaft with the engine (the scene components above are where
	 * the thrust pushes). */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> PropMeshStarboard;

	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> PropellerModel;

	/** The steering wheel, which turns with the motors. */
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> WheelMesh;

	UPROPERTY(EditAnywhere, Category = "Boat")
	TSoftObjectPtr<UStaticMesh> WheelModel;

	/** Degrees the wheel turns per degree the motors steer (a hydraulic helm: about three-quarters of a turn each
	 * way to full lock). */
	UPROPERTY(EditAnywhere, Category = "Boat")
	float WheelTurnRatio = 9.f;

	/** The radio's hand mic, and its coiled cord (drawn live between the radio and the mic). */
	UPROPERTY(VisibleAnywhere, Category = "Boat|Radio")
	TObjectPtr<UStaticMeshComponent> MicMesh;

	UPROPERTY(VisibleAnywhere, Category = "Boat|Radio")
	TObjectPtr<UProceduralMeshComponent> MicCord;

	UPROPERTY(EditAnywhere, Category = "Boat|Radio")
	TSoftObjectPtr<UStaticMesh> MicModel;

	UPROPERTY(EditAnywhere, Category = "Boat|Radio")
	float MicCordReach = 190.f;

	UPROPERTY(ReplicatedUsing = OnRep_MicHolder)
	TObjectPtr<ARiptideCharacter> MicHolder;

	UFUNCTION()
	void OnRep_MicHolder();

	/** Puts the mic mesh where MicHolder says: on its clip, or in the holder's hand. */
	void ApplyMicHolder();
	bool bMicCordAtRest = false;
	/** The cord's mesh data, kept between frames: its shape never changes, only where its vertices are. */
	TArray<FVector> CordVerts;
	TArray<FVector> CordNormals;
	TArray<int32> CordTris;

	UPROPERTY(Replicated)
	TObjectPtr<ARiptideCharacter> LadderUser;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Puts the boat back on the water (with its crew) if it has fallen off the edge of the sea. Server only. */
	void RescueIfOffTheSea();

	void UpdatePropsAndWheel(float DeltaSeconds);
	float PropSpinRate[2] = { 0.f, 0.f };
	float PropAngle[2] = { 0.f, 0.f };

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

	/** Spray droplets per second flying off each side's chine at full speed. */
	UPROPERTY(EditAnywhere, Category = "Boat|Wake")
	float BowSprayRate = 1400.f;

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

	/** How far past full revs an engine races when its prop comes out of the water and has nothing to push against,
	 * as a fraction of full revs: up to the rev limiter (full revs are 6000 rpm, the limiter about 6650). The
	 * tachometer and the engine note both follow it into the red. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.5"))
	float PropOutOverRev = 0.12f;

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

	/** Loaded weight: the aluminium hull and console (about 1,900 kg), two 300 hp outboards (570 kg), a two-thirds
	 * full tank, crew and gear. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull", meta = (ClampMin = "50"))
	float HullMassKg = 3200.f;

	// Going ahead, the hull's resistance has three parts, as on a real planing boat:
	// - friction and spray, growing with the square of the speed (ForwardDrag);
	// - wave-making: at low speed the hull climbs its own bow wave, and the resistance builds to a hump around
	//   HumpKnots, the hardest part of getting onto the plane (HumpDragN);
	// - once over the hump it rides on top of the water, but holding its weight up on the bottom's lift still costs a
	//   steady drag (PlaningDragN), so the resistance eases off past the hump rather than vanishing.
	// With MaxThrust these give about 30 knots flat out, 3-4 seconds to get onto the plane and 8-10 to reach 25 knots.

	/** Friction and spray resistance going ahead (N per (m/s)^2). */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float ForwardDrag = 23.5f;

	/** Peak wave-making resistance at the hump (N), at HumpKnots; it builds over about HumpWidthKnots before it. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float HumpDragN = 3500.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float HumpKnots = 11.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float HumpWidthKnots = 4.f;

	/** How hard the hump lifts the bow (N*m at its peak): the bow rises to about 4 degrees getting onto the plane. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float HumpBowRiseNm = 40000.f;

	/** What the wave-making resistance eases off to once on the plane (N). */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningDragN = 2800.f;

	/** Water resistance going astern (N per (m/s)^2). The flat transom and the motors hanging off it shove water
	 * instead of slicing through it, so a boat can't do much over 6 knots backwards. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float AsternDrag = 400.f;

	/** Water resistance moving sideways (N per (m/s)^2). The keel: higher = less sliding in turns. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float LateralDrag = 3200.f;

	/** Water resistance to bobbing up and down (N per m/s). Higher = the hull settles faster after a wave. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float HeaveDamping = 15000.f;

	/** How quickly the hull stops spinning, in 1/s. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float YawDamping = 1.2f;

	/** Bow-up angle a planing hull runs at on its own, in degrees; past it the bottom pushes the bow back down. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningRunningTrimDeg = 3.f;

	/** How hard the bottom pushes the bow back down per degree past the running angle, at planing speed (N*m). */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningPitchStiffness = 19000.f;

	/** How quickly the hull's spin dies out while it's in the air, in 1/s. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float AirRockDamping = 1.5f;

	// How quickly rocking dies out in the water, in 1/s. A hull lying still rolls on for a few gentle swings (little
	// but its own bilges and chines resist rolling); its pitching dies out within a swing or two, since pitching
	// shoves the whole bow and stern up and down through the water. Under way the bottom's planing lift resists
	// rolling too, so the roll damps harder the faster it goes. Much more and the hull feels glued to the sea.

	/** Roll damping lying still or going slowly. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float RollDamping = 0.8f;

	/** Roll damping on the plane (at TrimFullEffectKnots and up). */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float RollDampingPlaning = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PitchDamping = 2.2f;

	/**
	 * Hydrodynamic lift on the forward hull as it moves (N per (m/s)^2 of forward speed). Pushes the bow up
	 * at speed so it rides over swells instead of burying. Only acts where the forward hull is in the water.
	 */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float PlaningLift = 248.f;

	/** Largest planing lift as a fraction of the boat's weight, so jumps off wave crests don't launch it. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxPlaningLiftFraction = 0.5f;

	// --- Engine ---

	/** Thrust of both motors together at full throttle from a standstill, in Newtons (twin 300 hp outboards). Each
	 * motor gives half, and only while its own prop is in the water. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MaxThrust = 12000.f;

	/** How much thrust the props have lost by ThrustFalloffKnots, as a fraction: the faster the water already comes
	 * at a prop, the less each turn of it adds. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.9"))
	float ThrustFalloff = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float ThrustFalloffKnots = 30.f;

	/** Fraction of forward thrust available in reverse (a prop pushes less well backwards). */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "1"))
	float ReverseThrustScale = 0.35f;

	/** The lever's neutral band either side of centre: past it the gear engages (forward or reverse), and the throttle
	 * opens from idle over the rest of the lever's travel. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.5"))
	float NeutralDetent = 0.12f;

	/** Lever travel past the gear detent that stays at idle, like a real binnacle: the gear clicks in, and the throttle
	 * only starts to open a little further on. Leaves room to idle along in gear without touching the throttle. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.3"))
	float GearIdleBand = 0.08f;

	/** Thrust with the gear engaged and the throttle at idle, as a fraction of full: twin outboards idling in gear
	 * push the boat along at about 3.5-4 knots. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "0.3"))
	float IdleThrustInGear = 0.018f;

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
	 * planing lift, so this grows with speed. At 2500, flat out, fully out lifts the bow from about 2 degrees to
	 * about 4 (measured on flat water), where the planing bottom stops giving it any more.
	 */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimMomentPerDeg = 2500.f;

	/** The same for trimming in (bow down). Trimmed in, the bow is pressed onto the water rather than lifted off it, so
	 * the hull doesn't run out of grip and it can work harder: fully in drops the bow about 4 degrees. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimInMomentPerDeg = 4000.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float TrimFullEffectKnots = 22.f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float FuelCapacityLiters = 450.f;

	/** How full the tank is when the boat appears (a crew's boat is rarely brimmed). */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "1"))
	float StartingFuelFraction = 0.7f;

	/** Fuel each motor burns per second at full throttle (a 300 hp outboard flat out: about 112 L an hour) and at idle. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float FuelBurnFullPerMotor = 0.031f;

	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float FuelBurnIdlePerMotor = 0.0006f;

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

	/** Each motor's health: 1 healthy, below 0.5 it sputters, 0 dead. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float EngineHealthPort = 1.f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float EngineHealthStarboard = 1.f;

	/** Each motor's actual output (EngineOutput is their average). */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float MotorOutputPort = 0.f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float MotorOutputStarboard = 0.f;

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

	/** The hull's water resistance along its length at a forward speed (m/s; negative is astern), in Newtons, signed
	 * with the speed. */
	float GetHullResistanceN(float ForwardMs) const;

	/** Moves each engine's revs toward what its throttle and prop ask for (the tachometer and the engine note read
	 * them). Runs on every machine, from replicated state. */
	void UpdateEngineRevs(float DeltaSeconds);
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

	/** The hull's cross-section at X along it, in the boat's frame (riptide_boat_mesh.py's station): the chine's
	 * half-width and height, and the keel's height (cm). */
	static void HullSection(float X, float& OutChineY, float& OutChineZ, float& OutKeelZ, float* OutSheerY = nullptr,
		float* OutSheerZ = nullptr);

	/** Half the hull's width at X and height Z in the boat's frame (0 below the keel). */
	static float HullHalfWidthAt(float X, float Z);

	/** How fast the hull's side widens going aft at X and height Z (cm per cm): 0 along the parallel body aft,
	 * steep toward the stem. The side there is angled atan(this) off the centreline. */
	static float HullSideSlope(float X, float Z);

	/** Where water leaving the hull at a sample starts from, just outside the skin at the sea's height there. */
	FVector SprayOriginAt(float X, float LocalSeaZ, float Side) const;

	/** Where each side's chine meets the sea this frame, sampled bow to stern: the spray comes off there. */
	struct FChineSample
	{
		float X = 0.f;
		FVector Chine = FVector::ZeroVector;   // world
		FVector Keel = FVector::ZeroVector;    // world
		float SeaZ = 0.f;
		float KeelDepth = 0.f;                 // how far the keel there is under the sea (cm); <= 0 is clear of it
		float ChineDepth = 0.f;                // the same for the chine
		float LocalSeaZ = 0.f;                 // the sea's height at the chine, in the boat's frame
	};
	static constexpr int32 ChineSamples = 20;
	void SampleChines(FChineSample (&Out)[2][ChineSamples]) const;

	/** Each station's chine depth last frame, for how fast the hull is driving down into the sea there. */
	float PrevChineDepth[2][ChineSamples] = {};
	bool bHaveChineDepths = false;

	/** The ocean, found once (GetSeaSurfaceZ is asked many times a frame). */
	mutable TWeakObjectPtr<class AWaterBodyOcean> CachedOcean;
	mutable double NextOceanSearch = 0.0;

	float BowSprayOwed[2] = { 0.f, 0.f };

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

	/** The helmsman takes the mic, or hangs it up (M at the helm). */
	UFUNCTION(Server, Reliable)
	void ServerToggleMic();

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MicAction;

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

	/** Leaves the helm, looking where the helmsman was looking (the helm camera only turns on their machine). */
	UFUNCTION(Server, Reliable)
	void ServerLeaveHelm(float InLookYaw, float InLookPitch);

	/** The held controls, sent every frame (a lost one is replaced by the next). */
	UFUNCTION(Server, Unreliable)
	void ServerSetControls(float InThrottleInput, float InSteerInput, float InTrimInput);

	/** Cutting the throttle is a single press, so it goes on its own, reliably. */
	UFUNCTION(Server, Reliable)
	void ServerCutThrottle();

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

	/** How hard the engines (or one motor: 0 port, 1 starboard) are driving the props, 0 (idle or neutral) to 1. */
	float GetDriveFraction(int32 Motor = -1) const;

	UPROPERTY(ReplicatedUsing = OnRep_Helmsman, Transient)
	TObjectPtr<ARiptideCharacter> Helmsman;

	/** The mic moves between the helm camera and the character's own eyes as its holder takes or leaves the helm. */
	UFUNCTION()
	void OnRep_Helmsman() { ApplyMicHolder(); }

	// Raw input from whoever is at the helm. On the server these come from ServerSetControls.
	float ThrottleInput = 0.f;
	float SteerInput = 0.f;
	float TrimInput = 0.f;
	bool bCutThrottleRequested = false;

	float LookYaw = 0.f;
	float LookPitch = 0.f;

	/** Remaining time the engine is cut out by a sputter. */
	float SputterTimeLeft[2] = { 0.f, 0.f };

	/** Whether each prop (port, starboard) is in the water, from UpdatePropImmersion. */
	bool bPropWet[2] = { true, true };

	/** How long each prop has looked like changing state (seconds), before it does. */
	float PropStateTime[2] = { 0.f, 0.f };

	/** How far above the surface (cm) a prop has to rise before it counts as out of the water. */
	float PropDryMargin = 4.f;

	/** Distance falloff shared by the boat's sounds, so other boats fade with distance. */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundFalloff;

	/** The wake simulation this boat created, which it carries along with it. */
	TWeakObjectPtr<AActor> WakeSimulation;

	int32 SternFoamTrail = INDEX_NONE;
	float ChurnLevel = 0.f;

	bool bMotorSoundRunning[2] = { false, false };
	float MotorRevs[2] = { 0.f, 0.f };
	float PrevBowFreeboard = 0.f;
	bool bHaveBowFreeboard = false;
	float SlapCooldownLeft = 0.f;
};
