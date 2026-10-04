#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideInteractable.h"
#include "RiptideRaft.generated.h"

class ARiptideCharacter;
class UBoxComponent;
class UBuoyancyComponent;
class UStaticMeshComponent;

/**
 * A log raft: six logs lashed under a plank deck, 3.4 m long and 2.6 m across, launched from a finished raft site
 * (ARiptideStructure). It floats on the sea and rocks under the crew standing on it. With a pair of oars fitted in
 * its oarlocks, a crew member kneels amidships and rows: W and S pull ahead and back, A and D turn. Taking the oars
 * out is the key: nobody rows the raft away without them.
 *
 * E on it: fit the oars (two carried), take them up to row, or (held) take them out again; climb aboard from the
 * water; push it off the sand when it's beached. The server runs its physics; everyone sees where it is and who's
 * rowing.
 */
UCLASS()
class RIPTIDE_API ARiptideRaft : public AActor, public IRiptideInteractable
{
	GENERATED_BODY()

public:
	ARiptideRaft();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Launches a raft onto the water from a raft site at Where: out along the shortest way to water deep enough to
	 * float it, up to MaxLaunchDistance. Null if there's none that close. Server. */
	static ARiptideRaft* Launch(UWorld* World, const FTransform& Where);

	/** Places a raft as it was saved. Server. */
	static ARiptideRaft* Restore(UWorld* World, const FTransform& Where, bool bOars);

	/** The same, for tests (Python can't spawn into a running game). */
	UFUNCTION(BlueprintCallable, Category = "Raft", meta = (WorldContext = "WorldContextObject"))
	static ARiptideRaft* SpawnRaft(UObject* WorldContextObject, FTransform Where, bool bOars);

	UFUNCTION(BlueprintPure, Category = "Raft")
	bool HasOars() const { return bOarsFitted; }

	/** Who's rowing (null when nobody is). */
	UFUNCTION(BlueprintPure, Category = "Raft")
	ARiptideCharacter* GetRower() const { return Rower; }

	/** Afloat: in the water deep enough that the oars bite. */
	UFUNCTION(BlueprintPure, Category = "Raft")
	bool IsAfloat() const;

	/** Whether a crew member is standing on the deck. */
	bool IsAboard(const ARiptideCharacter* Who) const;

	/** Fits a pair of oars from Who's pockets, or takes them out into them (server). */
	UFUNCTION(BlueprintCallable, Category = "Raft")
	bool FitOars(ARiptideCharacter* Who);

	UFUNCTION(BlueprintCallable, Category = "Raft")
	bool TakeOutOars(ARiptideCharacter* Who);

	/** Who takes up the oars (or puts them down with null). Server; the character kneels amidships (SetRowing). */
	UFUNCTION(BlueprintCallable, Category = "Raft")
	bool SetRower(ARiptideCharacter* Who);

	/** The rower's strokes: Forward (-1 back-rowing .. 1 pulling) and Turn (-1 left .. 1 right). Server. */
	void SetRowInput(float Forward, float Turn);

	/** The rowing input as everyone sees it (for the oars' and rower's motion). */
	FVector2D GetRowInput() const { return FVector2D(RowForward / 127.f, RowTurn / 127.f); }

	/** Where the rower kneels, and where their hands hold the oars' handles this moment, in the world. */
	FTransform GetSeatTransform() const;
	void GetOarHandles(FVector& OutLeft, FVector& OutRight) const;

	/** Speed through the water, m/s (for tests). */
	UFUNCTION(BlueprintPure, Category = "Raft")
	float GetSpeedMs() const;

	// IRiptideInteractable
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const override;
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) override;

	enum EVerb : uint8 { VerbFitOars, VerbRow, VerbTakeOutOars, VerbClimbAboard, VerbPush };

	// The raft: length (x), beam (y) and height from the logs' bottoms to the deck (riptide_item_models.py's
	// build_raft); its mass; where the oarlocks stand (cm off the centreline, on each side, above the deck).
	static constexpr float Length = 340.f;
	static constexpr float Beam = 260.f;
	static constexpr float Height = 50.f;
	static constexpr float MassKg = 350.f;
	static constexpr float OarlockY = 124.f;
	static constexpr float MaxLaunchDistance = 2500.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UBuoyancyComponent> Buoyancy;

	/** The oars, shown lying in their locks when fitted and swinging with the strokes when rowed. */
	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UStaticMeshComponent> OarLeft;

	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UStaticMeshComponent> OarRight;

	/** Unseen boxes round each oarlock, to look at. */
	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UBoxComponent> LockLeft;

	UPROPERTY(VisibleAnywhere, Category = "Raft")
	TObjectPtr<UBoxComponent> LockRight;

	UPROPERTY(ReplicatedUsing = OnRep_Oars)
	bool bOarsFitted = false;

	UPROPERTY(Replicated)
	TObjectPtr<ARiptideCharacter> Rower;

	/** The rower's input, -127..127 each. */
	UPROPERTY(Replicated)
	int8 RowForward = 0;

	UPROPERTY(Replicated)
	int8 RowTurn = 0;

	UFUNCTION()
	void OnRep_Oars();

	void ApplyWater(float DeltaSeconds);
	void PoseOars(float DeltaSeconds);

	/** How far through a stroke each oar is (radians, a turn per stroke), run on every machine from the input. */
	float StrokeLeft = 0.f;
	float StrokeRight = 0.f;
	/** The oar model's length and where on it the oarlock holds it (from its bounds). */
	float OarLength = 197.f;
};
