#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "RiptideCrewAnimInstance.generated.h"

class UAnimSequence;

/** What a crew member's body is doing, as the animation shows it. */
UENUM(BlueprintType)
enum class ERiptideCrewAnimState : uint8
{
	Ground,         // standing, walking, running (or crouching)
	Falling,
	Swimming,
	Ladder,         // on the boarding ladder (hand over hand, or hanging still)
	ClimbingOver,   // going over the top of the ladder onto the deck
	KnockedDown,    // thrown down by a slam, and getting up
	Braced,         // holding on to a rail
	Helm,           // at the helm (the body is hidden then)
	MenuIdle,       // standing at ease, for the menus
	MenuRifle,      // rifle shouldered, for the main menu's silhouette
};

/** What a crew body is asked to show: driven by its character in play, or a fixed pose (the menus' mannequin). */
UENUM(BlueprintType)
enum class ERiptideCrewPose : uint8
{
	Gameplay,       // follow the ARiptideCharacter that owns the body
	Idle,           // stand at ease
	RifleReady,     // rifle shouldered
};

/** Everything the animation needs from the game, gathered on the game thread each frame. */
struct FRiptideCrewAnimInputs
{
	ERiptideCrewAnimState State = ERiptideCrewAnimState::MenuIdle;
	/** Ground speed (cm/s) relative to what the feet stand on (the deck moves), and its direction relative to the
	 * way the body faces (degrees, + to the right). */
	float Speed = 0.f;
	float Direction = 0.f;
	float VerticalSpeed = 0.f;
	bool bCrouched = false;
	bool bSprinting = false;
	/** A one-shot action over the upper body (ERiptideCrewAction as a number; 0 none) and seconds into it. */
	uint8 Action = 0;
	float ActionTime = 0.f;
	/** Where the crew member looks, up (+) or down (degrees): the upper body and head follow it. */
	float AimPitch = 0.f;
	/** Turns the whole body about the vertical (degrees), on the ladder to face it whichever way its player looks. */
	float BodyYaw = 0.f;
	/** Seconds into a fall, since landing, into a knockdown, and into the climb over the top of the ladder. */
	float FallTime = 0.f;
	float LandedTime = 10.f;
	float KnockdownTime = 10.f;
	float ClimbOverTime = 0.f;
	/** The boarding ladder in the body's (component) space: the foot of its tread line, and its up, inward (toward
	 * the boat) and left directions. */
	FVector LadderFoot = FVector::ZeroVector;
	FVector LadderUp = FVector::UpVector;
	FVector LadderIn = FVector::ForwardVector;
	FVector LadderLeft = FVector::RightVector;
	bool bHaveLadder = false;
	/** A fishing rod in hand, and how it's being worked (URiptideAnglerComponent::GetRodSwing). */
	bool bHoldingRod = false;
	float RodSwing = 0.f;
};

/**
 * Evaluates a crew member's pose on the animation worker thread: samples the Quaternius clips, blends them by
 * state and speed, and adjusts the result in component space (turning the legs toward the way they walk, aiming
 * the upper body with the look, placing hands and feet on the ladder's rungs, the rifle's handguard in the left
 * hand). No AnimBP asset: everything here is code.
 */
struct FRiptideCrewAnimProxy : public FAnimInstanceProxy
{
	FRiptideCrewAnimProxy() = default;
	explicit FRiptideCrewAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	/** The clips, by FRiptideCrewClip index (copied from the anim instance, which keeps them loaded). */
	TArray<const UAnimSequence*> Clips;
	FRiptideCrewAnimInputs In;

private:
	void CacheBoneIndices(const FBoneContainer& Bones);
	void Sample(int32 Clip, float Time, bool bLoop, FPoseContext& Out) const;
	void Locomotion(FPoseContext& Out) const;
	void Ladder(FPoseContext& Out) const;
	void Knockdown(FPoseContext& Out) const;
	void MenuRifle(FPoseContext& Out) const;
	/** Both hands on a held fishing rod (URiptideCrewBodyComponent::RodInComponent). */
	void HoldRod(FCSPose<FCompactPose>& CS, float Weight) const;
	void StatePose(ERiptideCrewAnimState State, FPoseContext& Out) const;

	/** How much each state shows (crossfading toward the current one), and the clips' play positions. */
	float Weights[10] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };
	float StridePhase = 0.f;      // 0..1 through the walk/jog cycle (shared, so they blend in step)
	float LoopTime = 0.f;         // for the looping poses that just play (idle, swim, rail, falling)
	float SwimPhase = 0.f;
	float LegYaw = 0.f;           // the legs turned toward the way they walk, smoothed (degrees)
	float SmoothedSpeed = 0.f;
	float SwimWeight = 0.f;       // idle to stroking, smoothed
	float ActionWeight = 0.f;     // how much of the upper-body action shows, fading in and out

	/** Bone indices in the current bone container (rebuilt when it changes), and which bones are upper body. */
	uint16 CachedSerial = MAX_uint16;
	TMap<FName, int32> Bone;
	FCompactPoseBoneIndex B(const TCHAR* Name) const { const int32* I = Bone.Find(FName(Name)); return FCompactPoseBoneIndex(I ? *I : INDEX_NONE); }
	TArray<float> UpperBody;
	/** The animations' pelvis height is for their own rig's legs: scaled to this body's leg length. */
	float LegScale = 1.f;
};

/**
 * A crew member's animation, in code: picks the state from the ARiptideCharacter that owns the mesh (or the pose
 * the menus ask for) on every machine, from replicated state only, so other players see the same thing.
 */
UCLASS(Transient, NotBlueprintable)
class RIPTIDE_API URiptideCrewAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** What the body is doing now (for tests and debugging). */
	UFUNCTION(BlueprintPure, Category = "Crew")
	ERiptideCrewAnimState GetAnimState() const { return Inputs.State; }

	/** The asset path of each clip the crew uses, and the Quaternius clip it was imported from (for the import
	 * script and the credits). */
	static const TArray<TPair<FString, FString>>& GetClipTable();

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FRiptideCrewAnimProxy;

	void GatherCharacter(class ARiptideCharacter* Crew, float DeltaSeconds);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Clips;

	FRiptideCrewAnimInputs Inputs;
	/** Where the feet were on the deck last frame (in the deck's frame), to measure walking speed on a moving boat. */
	FVector LastDeckFeet = FVector::ZeroVector;
	bool bHaveLastDeckFeet = false;
	FVector SmoothedVelocity = FVector::ZeroVector;
	bool bWasFalling = false;
};
