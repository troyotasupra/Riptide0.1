#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/RiptideFish.h"
#include "RiptideAngler.generated.h"

class ARiptideCharacter;
class UMaterialInstanceDynamic;
class USoundBase;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ERiptideAnglerState : uint8
{
	Idle,
	Charging,       // the left button held: the cast winds up
	Flying,         // the bobber in the air
	Waiting,        // on the water, waiting for a bite
	Bite,           // it's dipping: strike now
	Fight,          // a fish on the line
};

/** How a cast ended, as the angler reports it to the server. */
UENUM(BlueprintType)
enum class ERiptideCastEnd : uint8
{
	Reeled,         // brought in before anything bit
	Landed,
	Snapped,
	Cut,            // the line cut (a shark on it)
	Escaped,
	Missed,         // too slow to strike
};

/** What everyone else sees of a crew member's fishing: the bobber and line, and how far the cast is wound up. */
USTRUCT()
struct FRiptideLineShow
{
	GENERATED_BODY()

	UPROPERTY()
	ERiptideAnglerState State = ERiptideAnglerState::Idle;

	UPROPERTY()
	FVector_NetQuantize10 Bobber = FVector::ZeroVector;

	UPROPERTY()
	uint8 Charge = 0;
};

/**
 * A crew member's fishing, with the rod in hand (ARiptideCharacter::HoldItem). Hold the left button to wind up a
 * cast and let go to throw it; watch the bobber, and when it dips, click to strike. Then play the fish: hold to reel,
 * ease off when it pulls before the line snaps, and don't give it so much slack it throws the hook. The right button
 * picks the bait. In deep water a shark may take the fish on the line: hold on, or cut the line (F).
 *
 * The server decides what's down there (the bait, how deep the water is, the time of day: RiptideFish), when it
 * bites and how hard it fights; the angler plays the fight on their own machine and reports how it ended; the server
 * checks the timing and lands the fish (ARiptideLandedFish, flopping at their feet), or takes the bait. Everyone sees
 * the bobber and line.
 */
UCLASS(ClassGroup = (Riptide), meta = (BlueprintSpawnableComponent))
class RIPTIDE_API URiptideAnglerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiptideAnglerComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// The controls (the local player; the character passes its keys on with the rod in hand).
	UFUNCTION(BlueprintCallable, Category = "Fishing")
	void PrimaryPressed();

	UFUNCTION(BlueprintCallable, Category = "Fishing")
	void PrimaryReleased();

	/** The next bait carried (or a bare hook). */
	UFUNCTION(BlueprintCallable, Category = "Fishing")
	void CycleBait();

	/** Puts a particular bait on (a bait item carried, or none for a bare hook). */
	UFUNCTION(BlueprintCallable, Category = "Fishing")
	void SetBait(FName Item);

	/** Cuts the line with a fish on it. */
	UFUNCTION(BlueprintCallable, Category = "Fishing")
	void CutLine();

	/** Reels in whatever's out, without a catch (the rod put away, the angler swimming...). */
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Fishing")
	ERiptideAnglerState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Fishing")
	FName GetBait() const { return Bait; }

	/** 0..1 through winding up a cast. */
	UFUNCTION(BlueprintPure, Category = "Fishing")
	float GetCharge() const { return Charge; }

	/** In a fight: the line's tension (0..1, 1 snaps) and metres out. */
	UFUNCTION(BlueprintPure, Category = "Fishing")
	float GetTension() const { return Fight.Tension; }

	UFUNCTION(BlueprintPure, Category = "Fishing")
	float GetLineOut() const { return Fight.Distance; }

	UFUNCTION(BlueprintPure, Category = "Fishing")
	bool IsSharkOn() const { return State == ERiptideAnglerState::Fight && Fight.bShark; }

	/** What this crew member's fishing looks like here: their own state on their machine, what they're shown doing
	 * everywhere else. */
	UFUNCTION(BlueprintPure, Category = "Fishing")
	ERiptideAnglerState GetShownState() const { return ShownState; }

	/** Where the bobber is (or was last). */
	UFUNCTION(BlueprintPure, Category = "Fishing")
	FVector GetBobberLocation() const { return Bobber; }

	/** The last cast's end, and how many fish have been landed (for tests). */
	UFUNCTION(BlueprintPure, Category = "Fishing")
	ERiptideCastEnd GetLastEnd() const { return LastEnd; }

	UFUNCTION(BlueprintPure, Category = "Fishing")
	int32 GetLandedCount() const { return LandedCount; }

	/** Developer mode and tests: bites come within a second (server). */
	UFUNCTION(BlueprintCallable, Category = "Fishing")
	void SetFastBites(bool bFast) { bFastBites = bFast; }

	/** How the rod is held, for the body: 0 at rest; up to 1 wound back over the shoulder; negative flicked forward
	 * (just after a cast); a fish on the line holds it up. Everyone's copy follows the shown state. */
	float GetRodSwing() const { return RodSwing; }

private:
	ARiptideCharacter* GetCrew() const;
	bool IsMine() const;
	FVector RodTip() const;

	void Throw();
	void Land();
	void Hook();
	void Finish(ERiptideCastEnd End);
	void PlayFight(float DeltaTime);
	void DrawHud() const;

	UFUNCTION(Server, Reliable)
	void ServerCast(FVector Where, FName BaitItem);

	UFUNCTION(Client, Reliable)
	void ClientCastAck(int32 Id, float InBiteIn, float InPull, float InSpeed, float InSharkAt, FName InSpot);

	UFUNCTION(Server, Reliable)
	void ServerResult(int32 Id, ERiptideCastEnd End);

	UFUNCTION(Server, Unreliable)
	void ServerShowLine(FRiptideLineShow NewShow);

	UFUNCTION(Client, Reliable)
	void ClientNote(const FString& Text);

	/** The line, bobber and sounds as shown (the owner's own state, or what everyone else is sent). */
	void UpdateShown(float DeltaTime);
	void ShownStateChanged(ERiptideAnglerState Was, ERiptideAnglerState Now);
	void Play(USoundBase* Sound, const FVector& Where, float Volume = 1.f) const;
	/** Uses up the bait after a cast (a lure or jig only if bLoseReusable). Server. */
	void UseBait(FName Item, bool bLoseReusable);

	// The owner's fishing.
	ERiptideAnglerState State = ERiptideAnglerState::Idle;
	FName Bait;
	float Charge = 0.f;
	bool bPrimaryHeld = false;
	int32 CastId = 0;
	float BiteIn = 1.0e6f;
	float Pull = 0.3f;
	float FishSpeed = 1.f;
	float SharkAt = -1.f;
	FName Spot;
	FRiptideFishFight Fight;
	float FightTime = 0.f;
	float BiteTimer = 0.f;
	float FlyT = 0.f;
	FVector FlyFrom = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	bool bInWater = false;
	FVector Bobber = FVector::ZeroVector;
	float ShowSend = 0.f;
	ERiptideCastEnd LastEnd = ERiptideCastEnd::Reeled;
	int32 LandedCount = 0;

	// The server's record of the cast under way.
	struct FServerCast
	{
		int32 Id = 0;
		double At = 0.0;
		float Bite = 0.f;
		FName Fish;
		float Kg = 0.f;
		FName Bait;
		float SharkAt = -1.f;
		FVector Where = FVector::ZeroVector;
	};
	TOptional<FServerCast> ServerCastState;
	int32 NextCastId = 1;
	bool bFastBites = false;

	/** What everyone else sees (from the owner, through the server). */
	UPROPERTY(Replicated)
	FRiptideLineShow Show;

	ERiptideAnglerState ShownState = ERiptideAnglerState::Idle;
	float RodSwing = 0.f;
	float SinceCast = 10.f;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BobberMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BobberTop;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LineMesh;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CastSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> PlopSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SplashSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ReelSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SnapSound;

	UPROPERTY(Transient)
	TObjectPtr<class USoundAttenuation> Falloff;

	float ReelClick = 0.f;
};
