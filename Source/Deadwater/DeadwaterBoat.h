#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DeadwaterBoat.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UBuoyancyComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Motorboat driven from the helm in first person.
 *
 * Handling is physics-driven: an outboard motor pushes from the stern only while the
 * propeller is underwater, and steering swings the motor so the thrust itself turns the
 * boat. That means the boat barely turns at idle, loses drive when it catches air off a
 * swell, and slides through turns until the keel bites.
 */
UCLASS()
class DEADWATER_API ADeadwaterBoat : public APawn
{
	GENERATED_BODY()

public:
	ADeadwaterBoat();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Boat")
	float GetSpeedKnots() const;

	UFUNCTION(BlueprintPure, Category = "Boat")
	bool IsPropellerSubmerged() const;

	UFUNCTION(BlueprintCallable, Category = "Boat")
	void ApplyEngineDamage(float Amount);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UBoxComponent> HullBody;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UStaticMeshComponent> MotorMesh;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<USceneComponent> Propeller;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UCameraComponent> HelmCamera;

	UPROPERTY(VisibleAnywhere, Category = "Boat")
	TObjectPtr<UBuoyancyComponent> Buoyancy;

	// --- Hull ---

	UPROPERTY(EditAnywhere, Category = "Boat|Hull", meta = (ClampMin = "50"))
	float HullMassKg = 900.f;

	/** Water resistance moving forward (N per (m/s)^2). Sets top speed. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float ForwardDrag = 55.f;

	/** Water resistance moving sideways (N per (m/s)^2). The keel: higher = less sliding in turns. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float LateralDrag = 900.f;

	/** How quickly the hull stops spinning, in 1/s. */
	UPROPERTY(EditAnywhere, Category = "Boat|Hull")
	float YawDamping = 1.2f;

	// --- Engine ---

	/** Thrust at full throttle, in Newtons (a ~40hp outboard is roughly 3.5 kN). */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine")
	float MaxThrust = 3500.f;

	/** Fraction of forward thrust available in reverse. */
	UPROPERTY(EditAnywhere, Category = "Boat|Engine", meta = (ClampMin = "0", ClampMax = "1"))
	float ReverseThrustScale = 0.4f;

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

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float FuelLiters = 0.f;

	/** 1 = healthy. Below 0.5 the engine sputters; at 0 it's dead. */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Boat|State")
	float EngineHealth = 1.f;

	virtual void BeginPlay() override;

private:
	void BuildInput();
	void UpdateControls(float DeltaSeconds);
	void UpdateEngine(float DeltaSeconds);
	void ApplyThrust();
	void ApplyHydrodynamics();
	void DrawDebugHud() const;

	void OnThrottle(const FInputActionValue& Value);
	void OnThrottleReleased(const FInputActionValue& Value);
	void OnSteer(const FInputActionValue& Value);
	void OnSteerReleased(const FInputActionValue& Value);
	void OnCutThrottle(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);

	UFUNCTION(Server, Unreliable)
	void ServerSetControls(float InThrottleInput, float InSteerInput, bool bInCutThrottle);

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

	// Raw input from whoever is at the helm. On the server these come from ServerSetControls.
	float ThrottleInput = 0.f;
	float SteerInput = 0.f;
	bool bCutThrottleRequested = false;

	float LookYaw = 0.f;
	float LookPitch = 0.f;

	/** Remaining time the engine is cut out by a sputter. */
	float SputterTimeLeft = 0.f;

	/** Index of the buoyancy pontoon nearest the propeller, used to read the water height there. */
	int32 SternPontoonIndex = INDEX_NONE;
};
