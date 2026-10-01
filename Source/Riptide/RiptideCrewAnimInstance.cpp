#include "RiptideCrewAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RiptideBoat.h"
#include "RiptideCharacter.h"
#include "RiptideCrewBody.h"
#include "TwoBoneIK.h"

namespace
{
	// The clips, in the order of GetClipTable(). Asset names are under /Game/Riptide/Characters/Animations; the
	// sources are Quaternius's Universal Animation Libraries (Standard), imported by init_unreal.py.
	enum EClip : int32
	{
		Idle, Walk, Jog, CrouchIdle, CrouchWalk, JumpStart, JumpLoop, JumpLand, SwimIdle, SwimForward, ClimbUp, Rail,
		Knockback, GetUp, RifleReady, ClipCount
	};

	// How fast each clip's feet travel over the ground at its own speed (cm/s), measured from the clips (the
	// distance a planted foot slides back over its stance): walking and running play faster or slower to match.
	constexpr float WalkClipSpeed = 105.f;
	constexpr float JogClipSpeed = 320.f;
	constexpr float CrouchClipSpeed = 70.f;

	// The animations' rig: its legs (thigh + shin) are 82.9 cm, and its pelvis height is for those.
	constexpr float ClipLegLength = 82.9f;

	// The knockdown: thrown onto the back (Hit_Knockback, sped up) then getting up (LayToIdle).
	constexpr float KnockbackRate = 1.3f;
	constexpr float KnockbackEnd = 0.83f / KnockbackRate;
	constexpr float GetUpRate = 1.5f;
	constexpr float KnockdownAnimEnd = KnockbackEnd + 1.53f / GetUpRate;

	// The boarding ladder, in its own frame from ARiptideBoat::GetLadderFootTransform (riptide_boat_mesh.py's
	// LADDER_RUNGS): treads every 16 cm from 40 cm below the foot, nine of them, 2 cm toward the boat from it; the
	// rails' grab handle 110 cm above it. Hands grip near the rails, feet stand toward the middle.
	constexpr float RungBelowFoot = -40.f;
	constexpr float RungPitch = 16.f;
	constexpr int32 RungCount = 9;
	constexpr float RungIn = 2.f;
	constexpr float HandleHeight = 110.f;
	constexpr float HandReach = 140.f;     // hands grip this far above the feet (chest to head height)
	constexpr float HandSpread = 7.5f;
	constexpr float FootSpread = 6.5f;

	// The menu pose's rifle: its grip is in the right hand, and the left hand holds the handguard this far along
	// the barrel from it (see URiptideCrewBodyComponent's rifle).
	constexpr float HandguardReach = 25.f;

	// The body's own frame (the skeletal mesh component's): it faces +Y, its left is +X, up is +Z.
	const FVector BodyForward(0.f, 1.f, 0.f);
	const FVector BodyLeft(1.f, 0.f, 0.f);

	// Where the body sits relative to its capsule in each state (cm, body frame): swimming, the root rides at the
	// sea's surface (the clips swim with the head above it); leaning on a rail, the clip's body leans 47 cm back
	// from its hands, so it's brought forward over the feet.
	FVector RootOffset(ERiptideCrewAnimState State)
	{
		switch (State)
		{
		case ERiptideCrewAnimState::Swimming: return FVector(0.f, 0.f, 118.f);
		case ERiptideCrewAnimState::Braced: return FVector(0.f, 30.f, 0.f);
		default: return FVector::ZeroVector;
		}
	}
}

const TArray<TPair<FString, FString>>& URiptideCrewAnimInstance::GetClipTable()
{
	static const TArray<TPair<FString, FString>> Table = {
		{ TEXT("A_Idle"), TEXT("UAL1/Idle_Loop") },
		{ TEXT("A_Walk"), TEXT("UAL1/Walk_Loop") },
		{ TEXT("A_Jog"), TEXT("UAL1/Jog_Fwd_Loop") },
		{ TEXT("A_CrouchIdle"), TEXT("UAL1/Crouch_Idle_Loop") },
		{ TEXT("A_CrouchWalk"), TEXT("UAL1/Crouch_Fwd_Loop") },
		{ TEXT("A_JumpStart"), TEXT("UAL1/Jump_Start") },
		{ TEXT("A_JumpLoop"), TEXT("UAL1/Jump_Loop") },
		{ TEXT("A_JumpLand"), TEXT("UAL1/Jump_Land") },
		{ TEXT("A_SwimIdle"), TEXT("UAL1/Swim_Idle_Loop") },
		{ TEXT("A_SwimForward"), TEXT("UAL1/Swim_Fwd_Loop") },
		{ TEXT("A_ClimbUp"), TEXT("UAL2/ClimbUp_1m") },
		{ TEXT("A_RailHold"), TEXT("UAL2/Idle_Rail_Loop") },
		{ TEXT("A_Knockback"), TEXT("UAL2/Hit_Knockback") },
		{ TEXT("A_GetUp"), TEXT("UAL2/LayToIdle") },
		{ TEXT("A_RifleReady"), TEXT("UAL1/Pistol_Idle_Loop") },
	};
	check(Table.Num() == ClipCount);
	return Table;
}

void URiptideCrewAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (Clips.Num() == ClipCount)
	{
		return;
	}
	Clips.Reset();
	for (const TPair<FString, FString>& Clip : GetClipTable())
	{
		const FString Path = FString::Printf(TEXT("/Game/Riptide/Characters/Animations/%s.%s"), *Clip.Key, *Clip.Key);
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UE_CLOG(!Sequence, LogTemp, Warning, TEXT("Riptide: crew animation %s is missing (the editor builds it on launch)"), *Path);
		Clips.Add(Sequence);
	}
}

FAnimInstanceProxy* URiptideCrewAnimInstance::CreateAnimInstanceProxy()
{
	return new FRiptideCrewAnimProxy(this);
}

void URiptideCrewAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void URiptideCrewAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const URiptideCrewBodyComponent* Body = Cast<URiptideCrewBodyComponent>(GetSkelMeshComponent());
	const ERiptideCrewPose Pose = Body ? Body->GetPose() : ERiptideCrewPose::Gameplay;
	ARiptideCharacter* Crew = Cast<ARiptideCharacter>(GetOwningActor());
	if (Pose == ERiptideCrewPose::Gameplay && Crew)
	{
		GatherCharacter(Crew, DeltaSeconds);
		return;
	}
	Inputs = FRiptideCrewAnimInputs();
	Inputs.State = Pose == ERiptideCrewPose::RifleReady ? ERiptideCrewAnimState::MenuRifle : ERiptideCrewAnimState::MenuIdle;
}

void URiptideCrewAnimInstance::GatherCharacter(ARiptideCharacter* Crew, float DeltaSeconds)
{
	// Everything read here replicates (movement, the ladder, the knockdown, bracing, the helm, where the player
	// looks), so every machine shows the same animation.
	FRiptideCrewAnimInputs N;
	const UCharacterMovementComponent* Move = Crew->GetCharacterMovement();
	ARiptideBoat* Boat = Crew->GetHomeBoat();
	const FVector Feet = Crew->GetActorLocation() - FVector(0.f, 0.f, Crew->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	// Walking speed relative to what the feet stand on: on the boat's deck its own speed is taken off (at 30 knots
	// the crew member stands still on it).
	FVector Velocity = Crew->GetVelocity();
	if (Boat && Crew->IsStandingOnBoat())
	{
		Velocity -= Boat->GetDeckPointVelocity(Feet);
	}
	const float Blend = DeltaSeconds > 0.f ? 1.f - FMath::Exp(-DeltaSeconds / 0.08f) : 1.f;
	SmoothedVelocity = FMath::Lerp(SmoothedVelocity, Velocity, Blend);
	const FVector Local = Crew->GetActorTransform().InverseTransformVectorNoScale(SmoothedVelocity);
	N.Speed = FVector2D(Local.X, Local.Y).Size();
	N.Direction = N.Speed > 5.f ? FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)) : 0.f;
	N.VerticalSpeed = Velocity.Z;
	N.bCrouched = Crew->bIsCrouched;
	N.AimPitch = FRotator::NormalizeAxis(Crew->GetBaseAimRotation().Pitch);

	const bool bFalling = Move && Move->IsFalling() && !Crew->IsInSea() && !Crew->IsClimbing();
	N.FallTime = bFalling ? Inputs.FallTime + DeltaSeconds : 0.f;
	N.LandedTime = bFalling ? 0.f : (bWasFalling && Inputs.FallTime > 0.35f ? 0.f : Inputs.LandedTime + DeltaSeconds);
	bWasFalling = bFalling;
	N.KnockdownTime = Crew->GetKnockdownElapsed();
	N.ClimbOverTime = Crew->GetClimbOverTime();

	if (Crew->IsManningHelm())
	{
		N.State = ERiptideCrewAnimState::Helm;
	}
	else if (N.KnockdownTime < KnockdownAnimEnd)
	{
		N.State = ERiptideCrewAnimState::KnockedDown;
	}
	else if (Crew->IsClimbing())
	{
		N.State = Crew->IsOnLadder() ? ERiptideCrewAnimState::Ladder : ERiptideCrewAnimState::ClimbingOver;
	}
	else if (Crew->IsInSea())
	{
		N.State = ERiptideCrewAnimState::Swimming;
		N.Speed = Velocity.Size();
	}
	else if (bFalling && N.FallTime > 0.15f)
	{
		N.State = ERiptideCrewAnimState::Falling;
	}
	else if (Crew->IsBraced())
	{
		N.State = ERiptideCrewAnimState::Braced;
	}
	else
	{
		N.State = ERiptideCrewAnimState::Ground;
	}

	if (Crew->IsClimbing() && IsValid(Boat))
	{
		// The ladder, seen from the body: the body turns to face it, whichever way its player is looking.
		const FTransform Component = GetSkelMeshComponent()->GetComponentTransform();
		const FTransform Foot = Boat->GetLadderFootTransform();
		N.LadderFoot = Component.InverseTransformPosition(Foot.GetLocation());
		N.LadderUp = Component.InverseTransformVectorNoScale(Foot.GetRotation().GetUpVector());
		N.LadderIn = Component.InverseTransformVectorNoScale(Foot.GetRotation().GetForwardVector());
		N.LadderLeft = Component.InverseTransformVectorNoScale(-Foot.GetRotation().GetRightVector());
		N.bHaveLadder = true;
		N.BodyYaw = FRotator::NormalizeAxis(Boat->GetActorRotation().Yaw - Crew->GetActorRotation().Yaw);
	}
	Inputs = N;
}

// --- The proxy (animation worker thread) ---

void FRiptideCrewAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const URiptideCrewAnimInstance* Instance = CastChecked<URiptideCrewAnimInstance>(InAnimInstance);
	In = Instance->Inputs;
	Clips.SetNum(ClipCount);
	for (int32 i = 0; i < ClipCount; ++i)
	{
		Clips[i] = Instance->Clips.IsValidIndex(i) ? Instance->Clips[i].Get() : nullptr;
	}
}

void FRiptideCrewAnimProxy::Update(float DeltaSeconds)
{
	// Crossfade toward the current state (quicker into a knockdown, instant at the hidden helm).
	const int32 Target = int32(In.State);
	const float BlendTime = In.State == ERiptideCrewAnimState::KnockedDown ? 0.08f
		: In.State == ERiptideCrewAnimState::Falling || In.State == ERiptideCrewAnimState::ClimbingOver ? 0.15f : 0.25f;
	float Total = 0.f;
	for (int32 i = 0; i < UE_ARRAY_COUNT(Weights); ++i)
	{
		const float Goal = i == Target ? 1.f : 0.f;
		Weights[i] = In.State == ERiptideCrewAnimState::Helm ? Goal : FMath::FInterpConstantTo(Weights[i], Goal, DeltaSeconds, 1.f / BlendTime);
		Total += Weights[i];
	}
	for (float& W : Weights)
	{
		W = Total > 0.f ? W / Total : 0.f;
	}

	LoopTime += DeltaSeconds;
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, In.Speed, DeltaSeconds, 10.f);
	// Walking backwards plays the cycle backwards; sideways, the legs turn toward the way they go.
	const bool bBackwards = FMath::Abs(In.Direction) > 100.f && SmoothedSpeed > 20.f;
	const float WantYaw = SmoothedSpeed < 20.f ? 0.f
		: FMath::Clamp(bBackwards ? FRotator::NormalizeAxis(In.Direction - 180.f) : In.Direction, -70.f, 70.f);
	LegYaw = FMath::FInterpTo(LegYaw, WantYaw, DeltaSeconds, 8.f);
	const UAnimSequence* WalkClip = Clips.IsValidIndex(Walk) ? Clips[Walk] : nullptr;
	const UAnimSequence* JogClip = Clips.IsValidIndex(Jog) ? Clips[Jog] : nullptr;
	if (WalkClip && JogClip)
	{
		const float JogW = FMath::Clamp((SmoothedSpeed - 160.f) / 140.f, 0.f, 1.f);
		const float Speed = FMath::Max(SmoothedSpeed, 40.f);
		const float Cycles = In.bCrouched
			? Speed / CrouchClipSpeed / (Clips[CrouchWalk] ? Clips[CrouchWalk]->GetPlayLength() : 2.f)
			: FMath::Lerp(Speed / WalkClipSpeed / WalkClip->GetPlayLength(), Speed / JogClipSpeed / JogClip->GetPlayLength(), JogW);
		StridePhase = FMath::Frac(StridePhase + (bBackwards ? -1.f : 1.f) * Cycles * DeltaSeconds + 1.f);
	}
	const float SwimGoal = FMath::Clamp((In.Speed - 40.f) / 110.f, 0.f, 1.f);
	SwimWeight = FMath::FInterpTo(SwimWeight, SwimGoal, DeltaSeconds, 4.f);
	SwimPhase = FMath::Frac(SwimPhase + DeltaSeconds * FMath::Clamp(In.Speed / 110.f, 0.7f, 1.4f) / 1.33f);
}

void FRiptideCrewAnimProxy::CacheBoneIndices(const FBoneContainer& Bones)
{
	if (CachedSerial == Bones.GetSerialNumber())
	{
		return;
	}
	CachedSerial = Bones.GetSerialNumber();
	Bone.Reset();
	static const TCHAR* Names[] = { TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"),
		TEXT("neck_01"), TEXT("Head"), TEXT("clavicle_l"), TEXT("clavicle_r"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
		TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), TEXT("middle_01_r"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"),
		TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r") };
	for (const TCHAR* Name : Names)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		Bone.Add(FName(Name), MeshIndex == INDEX_NONE ? INDEX_NONE : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)).GetInt());
	}
	// The upper body: spine_01 and everything above it (for holding the rail while the legs shuffle).
	const FCompactPoseBoneIndex Spine = B(TEXT("spine_01"));
	UpperBody.SetNumZeroed(Bones.GetCompactPoseNumBones());
	for (int32 i = 0; i < UpperBody.Num(); ++i)
	{
		for (FCompactPoseBoneIndex B(i); B.IsValid(); B = Bones.GetParentBoneIndex(B))
		{
			if (B == Spine)
			{
				UpperBody[i] = 1.f;
				break;
			}
		}
	}
	// This body's leg length, from its reference pose (thigh to calf to foot).
	const TArray<FTransform>& Ref = Bones.GetRefPoseArray();
	auto RefLength = [&](const TCHAR* Name)
	{
		const int32 Index = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return Ref.IsValidIndex(Index) ? Ref[Index].GetTranslation().Size() : 0.f;
	};
	const float Legs = RefLength(TEXT("calf_l")) + RefLength(TEXT("foot_l"));
	LegScale = Legs > 10.f ? Legs / ClipLegLength : 1.f;
}

void FRiptideCrewAnimProxy::Sample(int32 Clip, float Time, bool bLoop, FPoseContext& Out) const
{
	const UAnimSequence* Sequence = Clips.IsValidIndex(Clip) ? Clips[Clip] : nullptr;
	if (!Sequence)
	{
		Out.ResetToRefPose();
		return;
	}
	const float Length = Sequence->GetPlayLength();
	const float T = bLoop ? FMath::Fmod(FMath::Max(0.f, Time), FMath::Max(Length, 0.01f)) : FMath::Clamp(Time, 0.f, Length);
	FAnimationPoseData Data(Out);
	Sequence->GetAnimationPose(Data, FAnimExtractContext(double(T), false, FDeltaTimeRecord(), bLoop));
}

namespace
{
	void BlendInto(FPoseContext& Acc, float& AccWeight, FPoseContext& Pose, float Weight)
	{
		if (AccWeight <= 0.f)
		{
			Acc.Pose.CopyBonesFrom(Pose.Pose);
			Acc.Curve.CopyFrom(Pose.Curve);
			AccWeight = Weight;
			return;
		}
		FPoseContext Result(Acc);
		const float Alpha = Weight / (AccWeight + Weight);
		FAnimationPoseData A(Acc), B(Pose), Out(Result);
		FAnimationRuntime::BlendTwoPosesTogether(A, B, 1.f - Alpha, Out);
		Acc.Pose.CopyBonesFrom(Result.Pose);
		Acc.Curve.CopyFrom(Result.Curve);
		AccWeight += Weight;
	}

	/** Turns a bone in component space about an axis through its own joint (its children go with it). */
	void TurnBone(FCSPose<FCompactPose>& CS, FCompactPoseBoneIndex Index, const FQuat& Turn)
	{
		if (!Index.IsValid())
		{
			return;
		}
		FTransform T = CS.GetComponentSpaceTransform(Index);
		T.SetRotation(Turn * T.GetRotation());
		CS.SafeSetCSBoneTransforms({ FBoneTransform(Index, T) });
	}

	/** Reaches a limb (root, joint, end) to a target with two-bone IK, the middle joint bending toward Pole. The
	 * end keeps its turn relative to the middle bone (a hand following its forearm) or its own (a foot staying
	 * level). */
	void Reach(FCSPose<FCompactPose>& CS, FCompactPoseBoneIndex A, FCompactPoseBoneIndex B, FCompactPoseBoneIndex C,
		const FVector& Target, const FVector& Pole, bool bEndFollowsJoint)
	{
		if (!A.IsValid() || !B.IsValid() || !C.IsValid())
		{
			return;
		}
		FTransform TA = CS.GetComponentSpaceTransform(A);
		FTransform TB = CS.GetComponentSpaceTransform(B);
		FTransform TC = CS.GetComponentSpaceTransform(C);
		const FQuat OldB = TB.GetRotation();
		const FQuat OldC = TC.GetRotation();
		AnimationCore::SolveTwoBoneIK(TA, TB, TC, Pole, Target, false, 1.0, 1.0);
		TC.SetRotation(bEndFollowsJoint ? TB.GetRotation() * OldB.Inverse() * OldC : OldC);
		TArray<FBoneTransform> Set = { FBoneTransform(A, TA), FBoneTransform(B, TB), FBoneTransform(C, TC) };
		Set.Sort([](const FBoneTransform& X, const FBoneTransform& Y) { return X.BoneIndex < Y.BoneIndex; });
		CS.SafeSetCSBoneTransforms(Set);
	}
}

void FRiptideCrewAnimProxy::Locomotion(FPoseContext& Out) const
{
	// Standing still to walking to running, blended by speed, the walk and run in step.
	const float S = SmoothedSpeed;
	const int32 StandClip = In.bCrouched ? CrouchIdle : Idle;
	const float MoveW = FMath::Clamp(S / 60.f, 0.f, 1.f);
	if (MoveW <= 0.01f)
	{
		Sample(StandClip, LoopTime, true, Out);
		return;
	}
	FPoseContext Moving(Out);
	if (In.bCrouched)
	{
		const UAnimSequence* Clip = Clips[CrouchWalk];
		Sample(CrouchWalk, StridePhase * (Clip ? Clip->GetPlayLength() : 1.f), true, Moving);
	}
	else
	{
		const float JogW = FMath::Clamp((S - 160.f) / 140.f, 0.f, 1.f);
		const UAnimSequence* WalkClip = Clips[Walk];
		const UAnimSequence* JogClip = Clips[Jog];
		Sample(Walk, StridePhase * (WalkClip ? WalkClip->GetPlayLength() : 1.f), true, Moving);
		if (JogW > 0.01f)
		{
			FPoseContext Running(Out);
			Sample(Jog, StridePhase * (JogClip ? JogClip->GetPlayLength() : 1.f), true, Running);
			float W = 1.f - JogW;
			BlendInto(Moving, W, Running, JogW);
		}
	}
	if (MoveW >= 0.99f)
	{
		Out.Pose.CopyBonesFrom(Moving.Pose);
		Out.Curve.CopyFrom(Moving.Curve);
		return;
	}
	Sample(StandClip, LoopTime, true, Out);
	float W = 1.f - MoveW;
	BlendInto(Out, W, Moving, MoveW);
}

void FRiptideCrewAnimProxy::Ladder(FPoseContext& Out) const
{
	// No ladder clip in the libraries: the body stands as at rest, and two-bone IK puts its hands and feet on the
	// ladder's actual treads. Climbing, they move a tread at a time, one hand with the opposite foot, then the
	// other pair; hanging still, they stay where they grip.
	Sample(Idle, 0.f, false, Out);
	if (!In.bHaveLadder)
	{
		return;
	}
	FCSPose<FCompactPose> CS;
	CS.InitPose(Out.Pose);
	// The body is turned to face the ladder afterwards (BodyYaw on the root), so the ladder is brought into the
	// unturned frame here.
	const FQuat Unturn(FVector::UpVector, FMath::DegreesToRadians(-In.BodyYaw));
	const FVector Foot = Unturn.RotateVector(In.LadderFoot);
	const FVector Up = Unturn.RotateVector(In.LadderUp);
	const FVector Inward = Unturn.RotateVector(In.LadderIn);
	const FVector Left = Unturn.RotateVector(In.LadderLeft);
	const float FeetAbove = -FVector::DotProduct(Foot, Up);

	// Lean in toward the ladder a little, hips close to it.
	TurnBone(CS, B(TEXT("spine_01")), FQuat(FVector::CrossProduct(Up, Inward).GetSafeNormal(), FMath::DegreesToRadians(-8.f)));

	auto Grip = [&](float Reach, float Phase, float Spread, bool bHand, FVector& OutPoint)
	{
		// Which tread this limb is on as the feet climb: the limbs move in pairs, each every second tread.
		const float Tread = (FeetAbove + Reach - RungBelowFoot) / RungPitch;
		const float Cycle = (Tread + Phase) * 0.5f;
		const float Step = FMath::SmoothStep(0.55f, 0.95f, FMath::Frac(Cycle));
		const float Index = FMath::FloorToFloat(Cycle) * 2.f - Phase + 2.f * Step;
		const float Height = bHand && Index > RungCount - 1
			? FMath::Lerp(RungBelowFoot + RungPitch * (RungCount - 1), HandleHeight, FMath::Clamp(Index - (RungCount - 1), 0.f, 1.f))
			: RungBelowFoot + RungPitch * FMath::Clamp(Index, 0.f, float(RungCount - 1));
		const float Lift = FMath::Sin(Step * PI) * 7.f;
		OutPoint = Foot + Up * Height + Inward * (RungIn - Lift) + Left * Spread;
	};
	const FVector Shoulder = CS.GetComponentSpaceTransform(B(TEXT("spine_03"))).GetLocation();
	const FVector Hips = CS.GetComponentSpaceTransform(B(TEXT("pelvis"))).GetLocation();
	for (const float Side : { 1.f, -1.f })
	{
		const bool bLeft = Side > 0.f;
		FVector Hand, FootPoint;
		Grip(HandReach, bLeft ? 1.f : 0.f, Side * HandSpread, true, Hand);
		Grip(0.f, bLeft ? 0.f : 1.f, Side * FootSpread, false, FootPoint);
		// The hand's bone is the wrist, a little under and behind where the palm closes on the tread; the foot's is
		// the ankle, above and behind the tread the sole stands on.
		Hand += -Inward * 4.f - Up * 6.f;
		FootPoint += Up * 8.f - Inward * 10.f;
		Reach(CS, B(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r")), B(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r")),
			B(bLeft ? TEXT("hand_l") : TEXT("hand_r")), Hand, Shoulder + Left * Side * 35.f - Up * 30.f - Inward * 10.f, true);
		Reach(CS, B(bLeft ? TEXT("thigh_l") : TEXT("thigh_r")), B(bLeft ? TEXT("calf_l") : TEXT("calf_r")),
			B(bLeft ? TEXT("foot_l") : TEXT("foot_r")), FootPoint, Hips + Inward * 45.f + Left * Side * 12.f, false);
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Out.Pose);
}

void FRiptideCrewAnimProxy::Knockdown(FPoseContext& Out) const
{
	// Thrown onto the back, then getting up.
	const float T = In.KnockdownTime;
	if (T < KnockbackEnd)
	{
		Sample(Knockback, T * KnockbackRate, false, Out);
	}
	else
	{
		Sample(GetUp, (T - KnockbackEnd) * GetUpRate, false, Out);
	}
}

void FRiptideCrewAnimProxy::MenuRifle(FPoseContext& Out) const
{
	// Shouldered: the two-handed aim, the rifle's grip in the right hand (URiptideCrewBodyComponent places it), and
	// the left hand moved forward onto its handguard.
	Sample(RifleReady, LoopTime, true, Out);
	FCSPose<FCompactPose> CS;
	CS.InitPose(Out.Pose);
	const FCompactPoseBoneIndex HandR = B(TEXT("hand_r"));
	const FCompactPoseBoneIndex Knuckle = B(TEXT("middle_01_r"));
	if (!HandR.IsValid() || !Knuckle.IsValid())
	{
		return;
	}
	const FVector Grip = FMath::Lerp(CS.GetComponentSpaceTransform(HandR).GetLocation(), CS.GetComponentSpaceTransform(Knuckle).GetLocation(),
		URiptideCrewBodyComponent::RifleGripAlongHand);
	const FVector Barrel = URiptideCrewBodyComponent::RifleAim().RotateVector(BodyForward);
	const FVector Handguard = Grip + Barrel * HandguardReach + FVector(0.f, 0.f, URiptideCrewBodyComponent::RifleBoreHeight - 4.f) + BodyLeft * 1.5f;
	const FVector Shoulder = CS.GetComponentSpaceTransform(B(TEXT("upperarm_l"))).GetLocation();
	Reach(CS, B(TEXT("upperarm_l")), B(TEXT("lowerarm_l")), B(TEXT("hand_l")),
		Handguard - Barrel * 7.f, Shoulder + BodyLeft * 20.f - FVector(0.f, 0.f, 40.f), true);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Out.Pose);
}

void FRiptideCrewAnimProxy::StatePose(ERiptideCrewAnimState State, FPoseContext& Out) const
{
	switch (State)
	{
	case ERiptideCrewAnimState::Ground:
		Locomotion(Out);
		break;
	case ERiptideCrewAnimState::Falling:
		// Off the ground: the end of the jump's take-off when going up, then the fall.
		if (In.VerticalSpeed > 80.f && In.FallTime < 0.45f)
		{
			Sample(JumpStart, 0.75f + In.FallTime, false, Out);
		}
		else
		{
			Sample(JumpLoop, LoopTime, true, Out);
		}
		break;
	case ERiptideCrewAnimState::Swimming:
	{
		Sample(SwimIdle, LoopTime, true, Out);
		if (SwimWeight > 0.01f)
		{
			FPoseContext Stroke(Out);
			const UAnimSequence* Clip = Clips[SwimForward];
			Sample(SwimForward, SwimPhase * (Clip ? Clip->GetPlayLength() : 1.f), true, Stroke);
			float W = 1.f - SwimWeight;
			BlendInto(Out, W, Stroke, SwimWeight);
		}
		break;
	}
	case ERiptideCrewAnimState::Ladder:
		Ladder(Out);
		break;
	case ERiptideCrewAnimState::ClimbingOver:
		// Over the top and down onto the deck: the climb-up clip, timed to the climb's two moves (0.9 s).
		Sample(ClimbUp, In.ClimbOverTime * 0.67f / 0.9f, false, Out);
		break;
	case ERiptideCrewAnimState::KnockedDown:
		Knockdown(Out);
		break;
	case ERiptideCrewAnimState::Braced:
	{
		// Leaning on the rail with both hands; shuffling along it, the legs walk under the held upper body.
		Sample(Rail, LoopTime, true, Out);
		if (SmoothedSpeed > 20.f)
		{
			FPoseContext Legs(Out);
			Locomotion(Legs);
			FPoseContext Result(Out);
			FAnimationPoseData A(Legs), B(Out), R(Result);
			FAnimationRuntime::BlendTwoPosesTogetherPerBone(A, B, UpperBody, R);
			Out.Pose.CopyBonesFrom(Result.Pose);
		}
		break;
	}
	case ERiptideCrewAnimState::MenuRifle:
		MenuRifle(Out);
		break;
	case ERiptideCrewAnimState::Helm:
	case ERiptideCrewAnimState::MenuIdle:
	default:
		Sample(Idle, LoopTime, true, Out);
		break;
	}
}

bool FRiptideCrewAnimProxy::Evaluate(FPoseContext& Output)
{
	CacheBoneIndices(Output.Pose.GetBoneContainer());
	if (Clips.Num() != ClipCount)
	{
		Output.ResetToRefPose();
		return true;
	}
	// Each state that's showing (more than one while crossfading), blended by weight.
	float Have = 0.f;
	FVector Offset = FVector::ZeroVector;
	for (int32 i = 0; i < UE_ARRAY_COUNT(Weights); ++i)
	{
		const float W = Weights[i];
		if (W < 0.01f)
		{
			continue;
		}
		Offset += RootOffset(ERiptideCrewAnimState(i)) * W;
		if (Have <= 0.f)
		{
			StatePose(ERiptideCrewAnimState(i), Output);
			Have = W;
			continue;
		}
		FPoseContext Pose(Output);
		StatePose(ERiptideCrewAnimState(i), Pose);
		BlendInto(Output, Have, Pose, W);
	}
	if (Have <= 0.f)
	{
		Output.ResetToRefPose();
		return true;
	}

	// The clips' hips are at the height for their rig's legs: raised or lowered for this body's.
	const FCompactPoseBoneIndex Pelvis = B(TEXT("pelvis"));
	if (Pelvis.IsValid())
	{
		Output.Pose[Pelvis].SetTranslation(Output.Pose[Pelvis].GetTranslation() * LegScale);
	}

	const float Upright = Weights[int32(ERiptideCrewAnimState::Ground)] + Weights[int32(ERiptideCrewAnimState::Braced)];
	FCSPose<FCompactPose> CS;
	CS.InitPose(Output.Pose);
	// Walking sideways or backwards: the hips and legs turn toward the way they go, the chest stays facing ahead.
	if (FMath::Abs(LegYaw) > 0.5f && Upright > 0.01f)
	{
		const FCompactPoseBoneIndex Spine = B(TEXT("spine_01"));
		const FQuat Chest = Spine.IsValid() ? CS.GetComponentSpaceTransform(Spine).GetRotation() : FQuat::Identity;
		TurnBone(CS, Pelvis, FQuat(FVector::UpVector, FMath::DegreesToRadians(LegYaw * Upright)));
		if (Spine.IsValid())
		{
			FTransform T = CS.GetComponentSpaceTransform(Spine);
			T.SetRotation(Chest);
			CS.SafeSetCSBoneTransforms({ FBoneTransform(Spine, T) });
		}
	}
	// Looking up and down: the chest, neck and head share the look's pitch (only on their feet: swimming,
	// climbing or down on the deck the pose is the clip's).
	const float Pitch = FMath::Clamp(In.AimPitch, -70.f, 70.f) * Upright;
	if (FMath::Abs(Pitch) > 0.5f)
	{
		for (const TPair<const TCHAR*, float>& Share : { TPair<const TCHAR*, float>(TEXT("spine_02"), 0.2f),
			TPair<const TCHAR*, float>(TEXT("spine_03"), 0.25f), TPair<const TCHAR*, float>(TEXT("neck_01"), 0.2f),
			TPair<const TCHAR*, float>(TEXT("Head"), 0.3f) })
		{
			TurnBone(CS, B(Share.Key), FQuat(BodyLeft, FMath::DegreesToRadians(Pitch * Share.Value)));
		}
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);

	// Where the body sits on its capsule, and turned to face the ladder.
	const FCompactPoseBoneIndex Root = B(TEXT("root"));
	if (Root.IsValid())
	{
		FTransform& T = Output.Pose[Root];
		const float Yaw = In.BodyYaw * (Weights[int32(ERiptideCrewAnimState::Ladder)] + Weights[int32(ERiptideCrewAnimState::ClimbingOver)]);
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		T.SetRotation(Turn * T.GetRotation());
		T.SetTranslation(Turn.RotateVector(T.GetTranslation()) + Turn.RotateVector(Offset));
	}
	return true;
}
