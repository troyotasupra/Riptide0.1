#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RiptideDevCamera.generated.h"

class ARiptideBoat;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** How the dev mode's fly camera moves. */
UENUM(BlueprintType)
enum class ERiptideDevCameraMode : uint8
{
	Free,           // flying anywhere, through anything
	RideAlong,      // flying in the boat's frame: the camera is carried along with it underway
	Orbit,          // circling the boat, always looking at it
	Chase           // following behind the boat the way it's travelling
};

/**
 * The dev mode's fly camera (ARiptidePlayerController possesses it while flying). No body and no collision: it flies
 * through the hull, the sea and everything else. It runs on real time, not game time, so it flies at full speed in slow
 * motion and while the world is frozen.
 *
 * WASD fly where you look, E or Space rise, Q or C sink, Shift goes fast and Ctrl slow, the mouse looks round and the
 * wheel sets the speed. Circling or chasing the boat, the mouse swings the camera round it and the wheel zooms.
 */
UCLASS(NotPlaceable)
class RIPTIDE_API ARiptideDevCamera : public APawn
{
	GENERATED_BODY()

public:
	ARiptideDevCamera();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** The boat the ride-along, orbit and chase modes follow. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetBoat(ARiptideBoat* InBoat);

	UFUNCTION(BlueprintPure, Category = "Dev")
	ARiptideBoat* GetBoat() const { return Boat.Get(); }

	/** Switches mode, gliding from where the camera is now. Modes that follow the boat stay free without one. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetMode(ERiptideDevCameraMode InMode);

	UFUNCTION(BlueprintPure, Category = "Dev")
	ERiptideDevCameraMode GetMode() const { return Mode; }

	static FString GetModeName(ERiptideDevCameraMode InMode);

	/** Holds the flying controls as if keys were held: X forward, Y right, Z up (each -1..1). For automated tests. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetFlyInput(FVector Move);

	/** Turns the view (or swings the camera round the boat) as the mouse would, in degrees. */
	UFUNCTION(BlueprintCallable, Category = "Dev")
	void AddLookInput(float YawDeg, float PitchDeg);

	/** Flying speed without Shift or Ctrl, in cm/s. */
	UFUNCTION(BlueprintPure, Category = "Dev")
	float GetFlySpeed() const { return FlySpeed; }

	UFUNCTION(BlueprintCallable, Category = "Dev")
	void SetFlySpeed(float CmPerSecond);

	/** How far the orbit or chase camera sits from the boat, in cm. */
	UFUNCTION(BlueprintPure, Category = "Dev")
	float GetZoomDistance() const { return Mode == ERiptideDevCameraMode::Chase ? ChaseDistance : OrbitDistance; }

	/** Keeps the camera where it is in the world after the boat has jumped somewhere (a dev mode teleport). */
	void KeepWorldPlaceAfterBoatMoved();

	/** Speed (fly modes) or zoom (orbit and chase), as the dev panel shows it. */
	FString GetSpeedText() const;

	/** Whether the camera follows the boat (anything but free flight). */
	bool IsFollowingBoat() const { return Mode != ERiptideDevCameraMode::Free && Boat.IsValid(); }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Dev")
	TObjectPtr<UCameraComponent> Camera;

	/** Degrees the view turns per unit of mouse or stick (the crew's own look feel). */
	UPROPERTY(EditAnywhere, Category = "Dev")
	float LookSensitivity = 1.f;

	/** Speed multipliers while Shift (fast) or Ctrl (slow) is held. */
	UPROPERTY(EditAnywhere, Category = "Dev")
	float FastMultiplier = 4.f;

	UPROPERTY(EditAnywhere, Category = "Dev")
	float SlowMultiplier = 0.25f;

	/** How quickly flying speeds up and slows down (1/s): high is snappy, low floats. */
	UPROPERTY(EditAnywhere, Category = "Dev")
	float FlyResponse = 8.f;

private:
	void BuildInput();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnWheel(const FInputActionValue& Value);

	/** The boat's heading frame: where it is and which way it points, level (its roll and pitch left out). */
	float BoatYaw() const;

	/** Takes up a mode from the camera's current place in the world, so switching doesn't jump. */
	void AdoptCurrentPlace();

	/** Where each mode puts the camera this frame (world). */
	void UpdateFree(float Dt, FVector& OutLoc, FRotator& OutRot);
	void UpdateRideAlong(float Dt, FVector& OutLoc, FRotator& OutRot);
	void UpdateOrbit(float Dt, FVector& OutLoc, FRotator& OutRot);
	void UpdateChase(float Dt, FVector& OutLoc, FRotator& OutRot);

	/** The point on the boat the orbit and chase cameras look at: over the console. */
	FVector BoatLookPoint() const;

	/** Lifts a camera circling or chasing the boat out of the sea (it can dip to just over the waves). */
	void KeepAboveSea(const FVector& LookAt, FVector& Loc, FRotator& Rot) const;

	/** The current fly speed with Shift or Ctrl applied. */
	float CurrentSpeed() const;

	/** The movement keys held, turned into a world direction for a view (forward and right follow the view,
	 * up is straight up). */
	FVector WishDirection(const FRotator& View) const;

	TWeakObjectPtr<ARiptideBoat> Boat;
	ERiptideDevCameraMode Mode = ERiptideDevCameraMode::Free;

	/** Controls held (X forward, Y right, Z up), mouse movement since last frame, and the speed keys. */
	FVector MoveInput = FVector::ZeroVector;
	FVector2D LookInput = FVector2D::ZeroVector;
	bool bFast = false;
	bool bSlow = false;

	float FlySpeed = 800.f;

	/** Free flight: where the camera is, the view, and its velocity (world, cm/s). */
	FVector FreeLocation = FVector::ZeroVector;
	FRotator FreeView = FRotator::ZeroRotator;
	FVector FreeVelocity = FVector::ZeroVector;

	/** Riding along: where the camera is on the boat (its frame, rolling and pitching with it), which way it looks
	 * (yaw from the bow, pitch from the horizon: the view stays level while the boat rocks), and its velocity (boat
	 * frame). */
	FVector RideOffset = FVector::ZeroVector;
	float RideYaw = 0.f;
	float RidePitch = 0.f;
	FVector RideVelocity = FVector::ZeroVector;

	/** Orbiting: the camera's angle round the boat (yaw from the bow, pitch from the horizon) and distance (cm). */
	float OrbitYaw = 180.f;
	float OrbitPitch = -15.f;
	float OrbitDistance = 1400.f;

	/** Chasing: the heading followed (smoothed), the camera's angle off it and its height angle, its distance, and
	 * where the camera is (it lags behind where it wants to be, like a chase camera on a spring). */
	float ChaseHeading = 0.f;
	float ChaseYawOffset = 0.f;
	float ChasePitch = -12.f;
	float ChaseDistance = 1600.f;
	FVector ChaseLocation = FVector::ZeroVector;

	/** Gliding from the last mode's view into this one's over BlendTime seconds. */
	FVector BlendFromLocation = FVector::ZeroVector;
	FQuat BlendFromRotation = FQuat::Identity;
	float BlendElapsed = 0.f;
	static constexpr float BlendTime = 0.4f;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FlyMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FastAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SlowAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> WheelAction;
};
