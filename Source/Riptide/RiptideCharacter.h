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
	ARiptideCharacter();

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

	/** How many times this character has gone overboard and been put back on deck. */
	UFUNCTION(BlueprintPure, Category = "Crew")
	int32 GetOverboardCount() const { return OverboardCount; }

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

	/** How far below the deck (cm) the feet can drop before the character counts as in the water. */
	UPROPERTY(EditAnywhere, Category = "Crew")
	float OverboardDepth = 60.f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Crew")
	TObjectPtr<ARiptideBoat> HomeBoat;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void BuildInput();
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void CheckOverboard();
	void DrawHud() const;

	UFUNCTION(Server, Reliable)
	void ServerTakeHelm();

	/** Moves an item between grids this crew member can reach (their own, or a locker nearby). */
	UFUNCTION(Server, Reliable)
	void ServerMoveItem(URiptideStorageComponent* From, int32 FromIndex, int32 Uid, URiptideStorageComponent* To, int32 ToIndex,
		int32 X, int32 Y, bool bRotated, int32 Count);

	bool CanReach(const URiptideStorageComponent* Storage, int32 Index) const;
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

	TSharedPtr<SRiptideInventory> InventoryWidget;
	TSharedPtr<class SWidget> InventoryWidgetContainer;

	UPROPERTY(ReplicatedUsing = OnRep_ManningHelm)
	bool bManningHelm = false;

	UPROPERTY(ReplicatedUsing = OnRep_OverboardCount)
	int32 OverboardCount = 0;

	float OverboardMessageTimeLeft = 0.f;
};
