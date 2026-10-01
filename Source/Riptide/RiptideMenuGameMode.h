#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "GameFramework/GameModeBase.h"
#include "RiptideMenuGameMode.generated.h"

class ARiptideBoat;
class ARiptideCrewPreview;

/**
 * The main menu's live shot, Troy's brief: the patrol boat at night on the swell, its searchlight scanning the water
 * very slowly, seen from low off its quarter, with a crew member in military gear standing as a dark shape against
 * the lit fog and water (and nobody at the helm). The camera floats like it's on a chase boat: it rises and falls
 * with the swell under it, drifts a little and rolls a touch, while keeping the boat framed.
 */
UCLASS(NotPlaceable)
class RIPTIDE_API ARiptideMenuCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	ARiptideMenuCamera();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Puts the scene on this boat: lights on, the crew member at the bow, the searchlight sweeping. */
	void SetBoat(ARiptideBoat* InBoat);

	/** Dresses the crew member on deck in the player's look. */
	void SetCrewLook(const struct FRiptideAppearance& Look);

	/** Where the camera sits, in the boat's frame (yaw only): forward, right, and up from the sea, in cm. Off the
	 * starboard bow, low, looking back across the boat toward the moon: boat and crew dark against the moonlit haze,
	 * the console seen from the side with nobody at it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	FVector CameraOffset = FVector(1400.f, 1000.f, 120.f);

	/** What it looks at, in the boat's frame: abaft the boat, which puts the boat on the right of the screen (the menu
	 * is on the left) with the beam reaching out across the right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	FVector LookAtOffset = FVector(-350.f, -200.f, 160.f);

	/** The searchlight sweeps this far each side of its centre (degrees), centred off the port bow (away from the
	 * camera, so the beam reaches out across the frame behind the boat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	float SweepCentreYaw = -48.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	float SweepHalfArc = 32.f;

	/** Seconds for one sweep across and back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	float SweepPeriod = 52.f;

	/** How far down it points (degrees), to light the sea some 15-20 m out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	float SweepPitch = -10.f;

	/** Where the crew member stands, in the boat's frame (cm), and which way they face (degrees off the bow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	FVector CrewSpot = FVector(185.f, -22.f, 20.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu shot")
	float CrewFacing = -30.f;

	/** For screenshots and tests: puts the sweep at a point in its cycle (0..1). */
	UFUNCTION(BlueprintCallable, Category = "Menu shot")
	void SetSweepPhase(float Phase) { SweepTime = Phase * SweepPeriod; }

private:
	UPROPERTY(Transient)
	TObjectPtr<ARiptideBoat> Boat;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Crew;

	float SweepTime = 0.f;
	float Time = 0.f;
	bool bHaveFrame = false;
	float SmoothedYaw = 0.f;
	FVector SmoothedBoat = FVector::ZeroVector;
	float SmoothedSeaZ = 0.f;
};

/**
 * The main menu's game mode: nobody gets a body (the player is a camera and a menu), and the scene is set up when it
 * starts: the boat launched at the player start, the menu shot's camera, and the crew screen's preview booth.
 */
UCLASS()
class RIPTIDE_API ARiptideMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARiptideMenuGameMode();

	virtual void StartPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	ARiptideMenuCamera* GetMenuCamera() const { return MenuCamera; }
	ARiptideCrewPreview* GetCrewPreview() const { return CrewPreview; }

private:
	void SetUpScene();

	UPROPERTY(Transient)
	TObjectPtr<ARiptideMenuCamera> MenuCamera;

	UPROPERTY(Transient)
	TObjectPtr<ARiptideCrewPreview> CrewPreview;
};
