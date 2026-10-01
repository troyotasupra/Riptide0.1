#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RiptideCharacter.generated.h"

class ARiptideBoat;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class URiptideStorageComponent;
class SRiptideInventory;
struct FInputActionValue;

/**
 * A crew member on foot, in first person.
 *
 * Walks the deck of a moving, rolling boat: the character stands on the boat's deck collision and is carried
 * along with it, so it keeps its footing at full speed and through turns. Its movement runs after the physics
 * step, once the boat has moved for the frame, so the deck never slides out from under it. At the console it
 * takes the helm (the player then drives the boat) and steps back onto the deck when leaving it.
 */
UCLASS()
class RIPTIDE_API ARiptideCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARiptideCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	/** The boat this character belongs to: the one it stands on, returns to after going overboard, and drives. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetHomeBoat(ARiptideBoat* Boat) { HomeBoat = Boat; }

	UFUNCTION(BlueprintPure, Category = "Crew")
	ARiptideBoat* GetHomeBoat() const { return HomeBoat; }

	/** True while standing close enough to the helm to take it. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsAtHelm() const;

	/** Takes the boat's helm if standing at it. Called by the interact key; also for automated tests. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void TryTakeHelm();

	/** True if standing on the home boat's deck (not in the air or in the water). */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsStandingOnBoat() const;

	/** How many times this character has ended up in the sea. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	int32 GetOverboardCount() const { return OverboardCount; }

	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsInSea() const;

	// The boarding ladder: swim into it and you take hold. W climbs, S climbs down, and letting go of both keeps you
	// hanging where you are (to look over the transom before committing). At the top W takes you over onto the
	// boat; at the bottom S, or Space anywhere, lets go into the sea.

	/** True while swimming near the boarding ladder. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsAtLadder() const;

	/** Takes hold of the ladder if swimming at it (swimming into it does the same). For tests and AI. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void TryClimbAboard();

	/** On the ladder: holding on, climbing, or going over the top onto the boat. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsClimbing() const { return bOnLadder || bClimbingOver; }

	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsOnLadder() const { return bOnLadder; }

	/** How high the feet are on the ladder, in the boat's frame (cm). */
	UFUNCTION(BlueprintPure, Category = "Crew")
	float GetLadderFeetZ() const { return LadderFeetZ; }

	/** Climbs as if W (1) or S (-1) were held; 0 hangs on. For tests and AI. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetLadderInput(float Axis);

	/** Lets go of the ladder, into the sea. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void LetGoOfLadder();

	/** Holding the boat radio's hand mic. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsHoldingMic() const;

	/** Looking at the radio's hand mic on its clip, close enough to take it. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool CanGrabMic() const;

	/** Takes the radio mic (if it can), or hangs it back up (if holding it). */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void TryToggleMic();

	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsManningHelm() const { return bManningHelm; }

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	/** Holding on to something solid (Shift near a handhold), or at the helm: slams and turns don't throw you. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsBraced() const;

	/** Holds (or lets go) as if the brace key were held. For AI crews and automated tests. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetBracing(bool bHold);

	UFUNCTION(BlueprintPure, Category = "Crew")
	bool IsKnockedDown() const { return KnockdownTimeLeft > 0.f; }

	/** The dev mode's god mode: the deck's jolts never throw this crew member, braced or not (and it's back on its feet). */
	void SetSteadyFeet(bool bSteady) { bSteadyFeet = bSteady; KnockdownTimeLeft = bSteady ? 0.f : KnockdownTimeLeft; }

	/** How many times the boat's motion has thrown this crew member off balance, and knocked them down. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	int32 GetStaggerCount() const { return StaggerCount; }

	UFUNCTION(BlueprintPure, Category = "Crew")
	int32 GetKnockdownCount() const { return KnockdownCount; }

	/** What this crew member carries: pockets and a backpack. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	URiptideStorageComponent* GetInventory() const { return Inventory; }

	/** The home boat's locker within reach, or -1. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	int32 GetLockerInReach() const;

	/** Moves everything that fits from one of the home boat's lockers into this crew member's pockets and pack.
	 * Returns how many stacks moved. Server only (for AI crews and automated tests; players use the menu). */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	int32 TakeFromLocker(int32 Locker);

	/** True when standing at the home boat's fuel filler with a fuel drum to pour in. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	bool CanRefuel() const;

	/** Pours a fuel drum from this crew member's inventory into the boat's tank. Called by the interact key; also
	 * for tests. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void TryRefuel();

	/** Opens the inventory screen, with one of the home boat's lockers alongside (or none). Local player only. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void OpenInventory(int32 Locker);

	UFUNCTION(BlueprintCallable, Category = "Crew")
	void CloseInventory();
	bool IsInventoryOpen() const { return InventoryWidget.IsValid(); }

	/** Hides the character at the helm while its player drives, or brings it back onto the deck. Server only. */
	void SetManningHelm(bool bManning);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Crew")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, Category = "Crew")
	TObjectPtr<URiptideStorageComponent> Inventory;

	/** How close (cm) a locker's lid has to be to open it. */
	UPROPERTY(EditAnywhere, Category = "Crew")
	float LockerReach = 150.f;

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Crew")
	float LookSensitivity = 1.f;

	/** How far from the helm's standing spot (cm) the helm can be taken. */
	UPROPERTY(EditAnywhere, Category = "Crew")
	float HelmReach = 110.f;

	// Riding the boat: the deck's jolts (from slams, hard turns and big throttle changes) throw an unbraced crew member.
	// Past StaggerG they stumble the way they're thrown; past KnockdownG they go down for a moment.

	/** Deck acceleration (in g, beyond gravity) that throws you off balance, and that knocks you down. */
	UPROPERTY(EditAnywhere, Category = "Crew|Balance")
	float StaggerG = 0.9f;

	UPROPERTY(EditAnywhere, Category = "Crew|Balance")
	float KnockdownG = 2.2f;

	/** How far a handhold can be to hold on to it (cm). */
	UPROPERTY(EditAnywhere, Category = "Crew|Balance")
	float HandholdReach = 85.f;

	/** Walking speed while holding on (shuffling along a rail). */
	UPROPERTY(EditAnywhere, Category = "Crew|Balance")
	float BracedWalkSpeed = 150.f;

	/** How close (cm) to the foot of the boarding ladder counts as at it (for the prompt). */
	UPROPERTY(EditAnywhere, Category = "Crew")
	float LadderReach = 160.f;

	/** Climbing speed on the ladder (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Crew")
	float LadderClimbSpeed = 60.f;

	/** How close (cm) to the radio's hand mic you have to be to take it off its clip. */
	UPROPERTY(EditAnywhere, Category = "Crew")
	float MicReach = 140.f;

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Crew")
	TObjectPtr<ARiptideBoat> HomeBoat;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void BuildInput();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnSwimUp(const FInputActionValue& Value);
	void UpdateBalance(float DeltaSeconds);

	UFUNCTION(Server, Reliable)
	void ServerSetBracing(bool bHold);
	void OnDive(const FInputActionValue& Value);
	bool IsAtLadderGrab() const;
	void GrabLadder();
	void UpdateLadder(float DeltaSeconds);
	void StartClimbOver();
	/** Back on the boat's deck or in the sea: movement and collision on, moving with the deck. */
	void LeaveLadder(const FVector& ExtraVelocity);

	UFUNCTION(Server, Reliable)
	void ServerClimbAboard();

	UFUNCTION(Server, Unreliable)
	void ServerSetLadderInput(float Axis);

	UFUNCTION(Server, Reliable)
	void ServerLetGoOfLadder();

	UFUNCTION(Server, Reliable)
	void ServerToggleMic(bool bTake);

	/** True if the view is on World (within Radius cm of the line of sight) no further than Reach away. */
	bool IsLookingAt(const FVector& World, float Reach, float Radius) const;
	void DrawHud() const;

	UFUNCTION(Server, Reliable)
	void ServerTakeHelm();

	/** Moves an item between grids this crew member can reach (their own, or a locker nearby). */
	UFUNCTION(Server, Reliable)
	void ServerMoveItem(URiptideStorageComponent* From, int32 FromIndex, int32 Uid, URiptideStorageComponent* To, int32 ToIndex,
		int32 X, int32 Y, bool bRotated, int32 Count);

	bool CanReach(const URiptideStorageComponent* Storage, int32 Index) const;

	UFUNCTION(Server, Reliable)
	void ServerRefuel();

	/** Finds a fuel drum in what this crew member carries: which grid and which item, or false. */
	bool FindFuelDrum(int32& OutGrid, int32& OutUid) const;
	void OnInventoryKey(const FInputActionValue& Value);

	/** Applies bManningHelm on the machines that didn't set it (hidden or not is replicated by the engine). */
	UFUNCTION()
	void OnRep_ManningHelm();

	UFUNCTION()
	void OnRep_OverboardCount();

	void ApplyManningHelm();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> WalkMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InventoryAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DiveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BraceAction;

	UPROPERTY(Replicated)
	bool bBracing = false;

	bool bSteadyFeet = false;

	UPROPERTY(ReplicatedUsing = OnRep_Knockdown)
	float KnockdownTimeLeft = 0.f;

	UFUNCTION()
	void OnRep_Knockdown() {}

	/** The deck point under the feet's velocity last frame (to feel its acceleration), and whether it's valid. */
	FVector PrevDeckVelocity = FVector::ZeroVector;
	bool bHavePrevDeckVelocity = false;
	float StaggerCooldown = 0.f;
	int32 StaggerCount = 0;
	int32 KnockdownCount = 0;
	FVector SmoothedDeckAccel = FVector::ZeroVector;

	/** On the ladder: the feet's height on it (boat frame), the climb input, and going over the top (time into it,
	 * where it started on the boat). After letting go it can't be grabbed again for a moment. */
	bool bOnLadder = false;
	float LadderFeetZ = 0.f;
	float LadderInput = 0.f;
	float SentLadderInput = 0.f;
	bool bClimbingOver = false;
	float ClimbOverTime = 0.f;
	FVector ClimbOverStart = FVector::ZeroVector;
	float LadderRegrabBlock = 0.f;

	TSharedPtr<SRiptideInventory> InventoryWidget;
	TSharedPtr<class SWidget> InventoryWidgetContainer;

	UPROPERTY(ReplicatedUsing = OnRep_ManningHelm)
	bool bManningHelm = false;

	UPROPERTY(ReplicatedUsing = OnRep_OverboardCount)
	int32 OverboardCount = 0;

};
