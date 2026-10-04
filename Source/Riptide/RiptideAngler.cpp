#include "RiptideAngler.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"
#include "RiptideCrewBody.h"
#include "RiptideHudOverlay.h"
#include "RiptideItems.h"
#include "RiptideLandedFish.h"
#include "RiptideSea.h"
#include "RiptideSkyClock.h"
#include "RiptideStorageComponent.h"
#include "RiptideWorldItem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	constexpr float FlySeconds = 0.6f;
	constexpr float BobberRadius = 4.5f;           // cm: big and bright enough to watch for a dip at full distance
	const FLinearColor BobberRed(1.f, 0.12f, 0.05f);
	const FLinearColor BobberWhite(0.95f, 0.95f, 0.92f);
	const FLinearColor LineColour(0.9f, 0.9f, 0.86f);

	USoundBase* LoadSound(const TCHAR* Name)
	{
		return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Riptide/Audio/%s.%s"), Name, Name));
	}
}

URiptideAnglerComponent::URiptideAnglerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void URiptideAnglerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(URiptideAnglerComponent, Show, COND_SkipOwner);
}

ARiptideCharacter* URiptideAnglerComponent::GetCrew() const
{
	return Cast<ARiptideCharacter>(GetOwner());
}

bool URiptideAnglerComponent::IsMine() const
{
	const ARiptideCharacter* Crew = GetCrew();
	return Crew && Crew->IsLocallyControlled();
}

void URiptideAnglerComponent::BeginPlay()
{
	Super::BeginPlay();
	// The bobber (red below, white above) and the line: drawn on every machine, from the owner's state or what's shown.
	AActor* Owner = GetOwner();
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Make = [&](UStaticMesh* Mesh, const FLinearColor& Colour, const FVector& Scale)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		Part->SetWorldScale3D(Scale);
		if (Plain)
		{
			UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(Plain, Part);
			Paint->SetVectorParameterValue(TEXT("Color"), Colour);
			Part->SetMaterial(0, Paint);
		}
		Part->SetupAttachment(Owner->GetRootComponent());
		Part->RegisterComponent();
		Part->SetVisibility(false);
		return Part;
	};
	if (Owner && Sphere && Cylinder)
	{
		BobberMesh = Make(Sphere, BobberRed, FVector(BobberRadius * 2.f / 100.f));
		BobberTop = Make(Sphere, BobberWhite, FVector(BobberRadius * 1.3f / 100.f));
		LineMesh = Make(Cylinder, LineColour, FVector(0.004f, 0.004f, 0.01f));
	}
	CastSound = LoadSound(TEXT("S_FishCast"));
	PlopSound = LoadSound(TEXT("S_FishPlop"));
	SplashSound = LoadSound(TEXT("S_FishSplash"));
	ReelSound = LoadSound(TEXT("S_FishReel"));
	SnapSound = LoadSound(TEXT("S_FishSnap"));
	// Heard where they happen, fading out over thirty metres.
	Falloff = NewObject<USoundAttenuation>(this);
	Falloff->Attenuation.bAttenuate = true;
	Falloff->Attenuation.bSpatialize = true;
	Falloff->Attenuation.AttenuationShapeExtents = FVector(200.f, 0.f, 0.f);
	Falloff->Attenuation.FalloffDistance = 3000.f;
}

void URiptideAnglerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (UStaticMeshComponent* Part : { BobberMesh.Get(), BobberTop.Get(), LineMesh.Get() })
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Super::EndPlay(EndPlayReason);
}

FVector URiptideAnglerComponent::RodTip() const
{
	const ARiptideCharacter* Crew = GetCrew();
	FVector Tip;
	if (const URiptideCrewBodyComponent* Body = Crew ? Crew->GetCrewBody() : nullptr; Body && Body->GetHeldTip(Tip))
	{
		return Tip;
	}
	// No rod drawn (no body yet): about where it would be.
	const FRotator View = Crew ? Crew->GetBaseAimRotation() : FRotator::ZeroRotator;
	const FVector Eye = Crew ? Crew->GetPawnViewLocation() : FVector::ZeroVector;
	return Eye + FRotator(0.f, View.Yaw, 0.f).RotateVector(FVector(125.f, 35.f, 30.f));
}

// --- Controls ---

void URiptideAnglerComponent::PrimaryPressed()
{
	bPrimaryHeld = true;
	switch (State)
	{
	case ERiptideAnglerState::Idle:
		State = ERiptideAnglerState::Charging;
		Charge = 0.f;
		break;
	case ERiptideAnglerState::Waiting:
		Finish(ERiptideCastEnd::Reeled);
		break;
	case ERiptideAnglerState::Bite:
		Hook();
		break;
	default:
		break;
	}
}

void URiptideAnglerComponent::PrimaryReleased()
{
	bPrimaryHeld = false;
	if (State == ERiptideAnglerState::Charging)
	{
		Throw();
	}
}

void URiptideAnglerComponent::CycleBait()
{
	if (State != ERiptideAnglerState::Idle)
	{
		return;
	}
	const ARiptideCharacter* Crew = GetCrew();
	TArray<FName> Options = { NAME_None };
	for (const FName& Item : RiptideFish::BaitItems())
	{
		if (Crew && Crew->GetInventory()->CountOf(Item) > 0)
		{
			Options.Add(Item);
		}
	}
	const int32 Index = Options.IndexOfByKey(Bait);
	Bait = Options[(Index + 1) % Options.Num()];
}

void URiptideAnglerComponent::SetBait(FName Item)
{
	Bait = RiptideFish::IsBait(Item) ? Item : NAME_None;
}

void URiptideAnglerComponent::CutLine()
{
	if (State == ERiptideAnglerState::Fight)
	{
		Finish(ERiptideCastEnd::Cut);
	}
}

void URiptideAnglerComponent::Stop()
{
	if (IsMine())
	{
		if (State != ERiptideAnglerState::Idle)
		{
			Finish(ERiptideCastEnd::Reeled);
		}
	}
	else if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerCastState.Reset();
	}
	bPrimaryHeld = false;
}

// --- The cast ---

void URiptideAnglerComponent::Throw()
{
	const ARiptideCharacter* Crew = GetCrew();
	if (!Crew)
	{
		return;
	}
	FlyFrom = RodTip();
	const FVector Forward = FRotator(0.f, Crew->GetControlRotation().Yaw, 0.f).Vector();
	Target = FVector(FlyFrom.X, FlyFrom.Y, 0.f) + Forward * FMath::Lerp(RiptideFish::MinCast, RiptideFish::MaxCast, Charge) * 100.f;
	const float Ground = URiptideSeaSubsystem::GroundHeightAt(this, Target);
	bInWater = Ground < -50.f;
	Target.Z = bInWater ? URiptideSeaSubsystem::SeaSurfaceAt(this, Target) : Ground + 5.f;
	FlyT = 0.f;
	State = ERiptideAnglerState::Flying;
	SinceCast = 0.f;
}

void URiptideAnglerComponent::Land()
{
	if (!bInWater)
	{
		RiptideHud::Note(Cast<APlayerController>(GetCrew()->GetController()), TEXT("That landed on dry ground: cast out over deeper water"), 3.f,
			FLinearColor(0.9f, 0.9f, 0.9f), 0xF15A);
		State = ERiptideAnglerState::Idle;
		return;
	}
	State = ERiptideAnglerState::Waiting;
	CastId = 0;
	BiteIn = 1.0e6f;
	if (Bait.IsNone() || GetCrew()->GetInventory()->CountOf(Bait) <= 0)
	{
		Bait = NAME_None;
	}
	ServerCast(Target, Bait);
}

void URiptideAnglerComponent::ServerCast_Implementation(FVector Where, FName BaitItem)
{
	ARiptideCharacter* Crew = GetCrew();
	if (!Crew || Crew->GetHeldItem() != TEXT("fishing_rod") || !Crew->CanUseHands())
	{
		return;
	}
	if (FVector::Dist2D(Crew->GetActorLocation(), Where) > (RiptideFish::MaxCast + 4.f) * 100.f)
	{
		return;
	}
	const float Ground = URiptideSeaSubsystem::GroundHeightAt(this, Where);
	if (Ground > -50.f)
	{
		ClientNote(TEXT("That's not deep enough to fish."));
		return;
	}
	if (!RiptideFish::IsBait(BaitItem) || Crew->GetInventory()->CountOf(BaitItem) <= 0)
	{
		BaitItem = NAME_None;
	}
	// What's down there for this bait, at this depth and hour.
	const float Depth = Ground < -100000.f ? 99.f : -Ground / 100.f;
	const FName CastSpot = RiptideFish::SpotOf(Depth);
	const ARiptideSkyClock* Clock = ARiptideSkyClock::Get(this);
	const FName Band = RiptideFish::TimeBand(Clock ? Clock->GetHours() : 12.f);
	const TMap<FName, float> Odds = RiptideFish::Weights(CastSpot, RiptideFish::BaitKind(BaitItem), Band);
	const FName Kind = RiptideFish::Pick(Odds, FMath::FRand());
	float Bite = RiptideFish::BiteSeconds(Odds, FMath::FRand());
	if (bFastBites)
	{
		Bite = FMath::Min(Bite, 1.f);
	}
	const FRiptideFishDef* Def = RiptideFish::Find(Kind);
	FServerCast Record;
	Record.Id = NextCastId++;
	Record.At = GetWorld()->GetTimeSeconds();
	Record.Bite = Bite;
	Record.Fish = Kind;
	Record.Kg = Def ? RiptideFish::RollKg(*Def, FMath::FRand()) : 0.f;
	Record.Bait = BaitItem;
	Record.SharkAt = CastSpot == TEXT("deep") && FMath::FRand() < RiptideFish::SharkChance ? FMath::FRandRange(2.f, 6.f) : -1.f;
	Record.Where = Where;
	ServerCastState = Record;
	ClientCastAck(Record.Id, Def ? Bite : 1.0e6f, Def ? RiptideFish::Strength(*Def, Record.Kg) : 0.f, Def ? Def->Speed : 1.f, Record.SharkAt, CastSpot);
	UE_LOG(LogTemp, Log, TEXT("Riptide: %s cast into %s water (%.1f m) with %s at %s: %s, %.1f kg, bites in %.1f s"), *Crew->GetName(), *CastSpot.ToString(),
		Depth, BaitItem.IsNone() ? TEXT("a bare hook") : *BaitItem.ToString(), *Band.ToString(), Kind.IsNone() ? TEXT("nothing") : *Kind.ToString(),
		Record.Kg, Bite);
}

void URiptideAnglerComponent::ClientCastAck_Implementation(int32 Id, float InBiteIn, float InPull, float InSpeed, float InSharkAt, FName InSpot)
{
	if (State != ERiptideAnglerState::Waiting || CastId != 0)
	{
		return;
	}
	CastId = Id;
	BiteIn = InBiteIn;
	Pull = InPull;
	FishSpeed = InSpeed;
	SharkAt = InSharkAt;
	Spot = InSpot;
}

void URiptideAnglerComponent::Hook()
{
	const FVector Tip = RodTip();
	Fight = RiptideFish::NewFight(FMath::Max(FVector::Dist2D(Tip, Target) / 100.f, 3.f));
	FightTime = 0.f;
	State = ERiptideAnglerState::Fight;
}

void URiptideAnglerComponent::PlayFight(float DeltaTime)
{
	FightTime += DeltaTime;
	if (SharkAt >= 0.f && !Fight.bShark && FightTime >= SharkAt)
	{
		Fight.bShark = true;
		RiptideHud::Note(Cast<APlayerController>(GetCrew()->GetController()), TEXT("A shark's on your fish! Hold on, or cut the line (F)"), 4.f,
			FLinearColor(1.f, 0.55f, 0.3f), 0xF15B);
	}
	const ERiptideFightResult Result = RiptideFish::Step(Fight, bPrimaryHeld, DeltaTime, RiptideFish::SurgeAt(FightTime, FishSpeed), Pull);
	// The bobber is dragged along the line toward the rod as it comes in.
	const FVector Tip = RodTip();
	const FVector Flat(Tip.X, Tip.Y, 0.f);
	const FVector Toward = FVector(Target.X, Target.Y, 0.f) - Flat;
	const FVector Dir = Toward.Size2D() > 10.f ? Toward.GetSafeNormal2D() : FVector::ForwardVector;
	FVector P = Flat + Dir * FMath::Max(Fight.Distance, 0.5f) * 100.f;
	const float T = GetWorld()->GetTimeSeconds();
	P.Z = URiptideSeaSubsystem::SeaSurfaceAt(this, P) - 8.f - FMath::Sin(T * 17.f) * 5.f;
	Bobber = P;
	switch (Result)
	{
	case ERiptideFightResult::Landed:
		Finish(ERiptideCastEnd::Landed);
		break;
	case ERiptideFightResult::Snapped:
		Finish(ERiptideCastEnd::Snapped);
		break;
	case ERiptideFightResult::Escaped:
		Finish(ERiptideCastEnd::Escaped);
		break;
	default:
		break;
	}
}

void URiptideAnglerComponent::Finish(ERiptideCastEnd End)
{
	if (CastId != 0)
	{
		ServerResult(CastId, End);
	}
	if (End == ERiptideCastEnd::Snapped)
	{
		Play(SnapSound, RodTip(), 0.9f);
	}
	if (End == ERiptideCastEnd::Escaped || End == ERiptideCastEnd::Missed)
	{
		RiptideHud::Note(Cast<APlayerController>(GetCrew()->GetController()), End == ERiptideCastEnd::Missed ? TEXT("Too slow: it spat the hook")
			: TEXT("It got away"), 3.f, FLinearColor(0.9f, 0.9f, 0.9f), 0xF15A);
	}
	LastEnd = End;
	State = ERiptideAnglerState::Idle;
	CastId = 0;
	Fight = FRiptideFishFight();
	Charge = 0.f;
}

void URiptideAnglerComponent::UseBait(FName Item, bool bLoseReusable)
{
	ARiptideCharacter* Crew = GetCrew();
	if (Item.IsNone() || !Crew || (RiptideFish::IsBaitReusable(Item) && !bLoseReusable))
	{
		return;
	}
	URiptideStorageComponent* Inventory = Crew->GetInventory();
	for (int32 Grid = 0; Grid < Inventory->Num(); ++Grid)
	{
		if (Inventory->GetStorage(Grid)->Grid.Remove(Item, 1) == 0)
		{
			Inventory->OnChanged.Broadcast();
			return;
		}
	}
}

void URiptideAnglerComponent::ServerResult_Implementation(int32 Id, ERiptideCastEnd End)
{
	ARiptideCharacter* Crew = GetCrew();
	if (!Crew || !ServerCastState.IsSet() || ServerCastState->Id != Id)
	{
		return;
	}
	const FServerCast Record = ServerCastState.GetValue();
	ServerCastState.Reset();
	const FRiptideFishDef* Def = RiptideFish::Find(Record.Fish);
	switch (End)
	{
	case ERiptideCastEnd::Landed:
	{
		const float Elapsed = GetWorld()->GetTimeSeconds() - Record.At;
		if (!Def || Elapsed < Record.Bite + (bFastBites ? 0.3f : RiptideFish::MinFightSeconds))
		{
			ClientNote(TEXT("The line comes up empty."));
			return;
		}
		UseBait(Record.Bait, false);
		const bool bSharkTook = Record.SharkAt >= 0.f && Elapsed >= Record.Bite + Record.SharkAt && FMath::FRand() < 0.5f;
		if (bSharkTook)
		{
			// Only the head comes over the side.
			if (const int32 Left = Crew->GiveItem(TEXT("cut_bait"), 2); Left > 0)
			{
				ARiptideWorldItem::Drop(GetWorld(), FRiptideItemGrid::NewStack(TEXT("cut_bait"), Left), Crew->GetActorLocation());
			}
			ClientNote(FString::Printf(TEXT("A shark tore your %s away: all you land is the head (cut bait x2)"), *Def->Name.ToString().ToLower()));
		}
		else
		{
			// Out of the water and onto the ground at your feet, still alive.
			ARiptideLandedFish::Land(Crew, Def->Id, Record.Kg);
			ClientNote(FString::Printf(TEXT("A %.1f kg %s is flopping at your feet: kill it, then take it"), Record.Kg, *Def->Name.ToString().ToLower()));
			++LandedCount;
		}
		break;
	}
	case ERiptideCastEnd::Snapped:
	case ERiptideCastEnd::Cut:
		UseBait(Record.Bait, true);
		ClientNote(End == ERiptideCastEnd::Cut ? FString(TEXT("You cut the line."))
			: FString::Printf(TEXT("The line snapped: the fish is gone%s"), RiptideFish::IsBaitReusable(Record.Bait) ? TEXT(", and your lure with it") : TEXT("")));
		break;
	case ERiptideCastEnd::Escaped:
	case ERiptideCastEnd::Missed:
		if (FMath::FRand() < 0.5f)
		{
			UseBait(Record.Bait, false);
		}
		break;
	default:
		break;
	}
}

void URiptideAnglerComponent::ClientNote_Implementation(const FString& Text)
{
	const ARiptideCharacter* Crew = GetCrew();
	RiptideHud::Note(Crew ? Cast<APlayerController>(Crew->GetController()) : nullptr, Text, 4.f, FLinearColor(0.85f, 0.95f, 1.f), 0xF15C);
}

// --- Every frame ---

void URiptideAnglerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ARiptideCharacter* Crew = GetCrew();
	if (!Crew)
	{
		return;
	}
	SinceCast += DeltaTime;
	if (IsMine())
	{
		// The rod put away, or the hands needed for something else (swimming, the ladder, the helm): reeled in.
		if (State != ERiptideAnglerState::Idle && (Crew->GetHeldItem() != TEXT("fishing_rod") || !Crew->CanUseHands()))
		{
			Finish(ERiptideCastEnd::Reeled);
		}
		if (!Bait.IsNone() && Crew->GetInventory()->CountOf(Bait) <= 0)
		{
			Bait = NAME_None;
		}
		const float T = GetWorld()->GetTimeSeconds();
		switch (State)
		{
		case ERiptideAnglerState::Charging:
			Charge = FMath::Min(1.f, Charge + DeltaTime / RiptideFish::ChargeSeconds);
			break;
		case ERiptideAnglerState::Flying:
		{
			FlyT = FMath::Min(1.f, FlyT + DeltaTime / FlySeconds);
			FVector P = FMath::Lerp(FlyFrom, Target, FlyT);
			P.Z += FMath::Sin(FlyT * UE_PI) * (150.f + FVector::Dist(FlyFrom, Target) * 0.15f);
			Bobber = P;
			if (FlyT >= 1.f)
			{
				Land();
			}
			break;
		}
		case ERiptideAnglerState::Waiting:
			BiteIn -= DeltaTime;
			Bobber = FVector(Target.X, Target.Y, URiptideSeaSubsystem::SeaSurfaceAt(this, Target) + FMath::Sin(T * 2.2f) * 2.f);
			if (BiteIn <= 0.f)
			{
				State = ERiptideAnglerState::Bite;
				BiteTimer = RiptideFish::HookWindow;
			}
			break;
		case ERiptideAnglerState::Bite:
			BiteTimer -= DeltaTime;
			Bobber = FVector(Target.X, Target.Y, URiptideSeaSubsystem::SeaSurfaceAt(this, Target) - 8.f - 8.f * FMath::Sin(T * 30.f));
			if (BiteTimer <= 0.f)
			{
				Finish(ERiptideCastEnd::Missed);
			}
			break;
		case ERiptideAnglerState::Fight:
			PlayFight(DeltaTime);
			break;
		default:
			break;
		}
		// Everyone else is shown the bobber a few times a second.
		FRiptideLineShow Now;
		Now.State = State;
		Now.Bobber = Bobber;
		Now.Charge = uint8(FMath::RoundToInt(Charge * 255.f));
		ShowSend -= DeltaTime;
		if (Now.State != Show.State || ShowSend <= 0.f)
		{
			ShowSend = 0.1f;
			Show = Now;
			if (Crew->HasAuthority())
			{
				Crew->ForceNetUpdate();
			}
			else
			{
				ServerShowLine(Now);
			}
		}
		DrawHud();
	}
	UpdateShown(DeltaTime);
}

void URiptideAnglerComponent::ServerShowLine_Implementation(FRiptideLineShow NewShow)
{
	Show = NewShow;
}

void URiptideAnglerComponent::UpdateShown(float DeltaTime)
{
	const ARiptideCharacter* Crew = GetCrew();
	const bool bMine = IsMine();
	const ERiptideAnglerState Now = bMine ? State : Show.State;
	const float ShownCharge = bMine ? Charge : Show.Charge / 255.f;
	if (!bMine)
	{
		// Others' bobbers glide between updates.
		Bobber = Now == ERiptideAnglerState::Idle ? FVector(Show.Bobber) : FMath::VInterpTo(Bobber, FVector(Show.Bobber), DeltaTime, 12.f);
	}
	if (Now != ShownState)
	{
		ShownStateChanged(ShownState, Now);
		ShownState = Now;
	}
	// How the rod is held: wound back over the shoulder with the cast, flicked out as it goes, held up with a fish on.
	float Want = 0.f;
	switch (Now)
	{
	case ERiptideAnglerState::Charging:
		Want = ShownCharge;
		break;
	case ERiptideAnglerState::Flying:
		Want = SinceCast < 0.15f ? 0.4f : -0.6f;
		break;
	case ERiptideAnglerState::Waiting:
		Want = -0.25f;
		break;
	case ERiptideAnglerState::Bite:
		Want = -0.35f;
		break;
	case ERiptideAnglerState::Fight:
		Want = 0.45f + 0.08f * FMath::Sin(GetWorld()->GetTimeSeconds() * 9.f);
		break;
	default:
		break;
	}
	RodSwing = FMath::FInterpTo(RodSwing, Want, DeltaTime, Now == ERiptideAnglerState::Flying ? 18.f : 6.f);

	const bool bLine = Crew && Crew->GetHeldItem() == TEXT("fishing_rod") && (Now == ERiptideAnglerState::Flying || Now == ERiptideAnglerState::Waiting
		|| Now == ERiptideAnglerState::Bite || Now == ERiptideAnglerState::Fight);
	for (UStaticMeshComponent* Part : { BobberMesh.Get(), BobberTop.Get(), LineMesh.Get() })
	{
		if (Part)
		{
			Part->SetVisibility(bLine);
		}
	}
	if (bLine && BobberMesh && BobberTop && LineMesh)
	{
		BobberMesh->SetWorldLocation(Bobber);
		BobberTop->SetWorldLocation(Bobber + FVector(0.f, 0.f, BobberRadius * 0.9f));
		// The line from the rod's tip to the bobber: a thin cylinder (the engine's is 1 m tall, centred).
		const FVector Tip = RodTip();
		const FVector Span = Bobber - Tip;
		const float Length = FMath::Max(Span.Size(), 1.f);
		LineMesh->SetWorldLocationAndRotation((Tip + Bobber) * 0.5f, FRotationMatrix::MakeFromZ(Span / Length).Rotator());
		LineMesh->SetWorldScale3D(FVector(0.004f, 0.004f, Length / 100.f));
	}
	// The reel's clicking while line comes in.
	if (Now == ERiptideAnglerState::Fight && (bMine ? bPrimaryHeld : true))
	{
		ReelClick -= DeltaTime;
		if (ReelClick <= 0.f)
		{
			ReelClick = 0.11f;
			Play(ReelSound, RodTip(), 0.35f);
		}
	}
}

void URiptideAnglerComponent::ShownStateChanged(ERiptideAnglerState Was, ERiptideAnglerState Now)
{
	if (Now == ERiptideAnglerState::Flying)
	{
		SinceCast = 0.f;
		Play(CastSound, RodTip(), 0.6f);
	}
	if (Was == ERiptideAnglerState::Flying && Now == ERiptideAnglerState::Waiting)
	{
		Play(PlopSound, Bobber, 0.8f);
	}
	if (Now == ERiptideAnglerState::Bite || (Was == ERiptideAnglerState::Bite && Now == ERiptideAnglerState::Fight))
	{
		Play(SplashSound, Bobber, Now == ERiptideAnglerState::Bite ? 0.7f : 1.f);
	}
}

void URiptideAnglerComponent::Play(USoundBase* Sound, const FVector& Where, float Volume) const
{
	if (!Sound || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UGameplayStatics::PlaySoundAtLocation(this, Sound, Where, Volume, 1.f, 0.f, Falloff);
}

void URiptideAnglerComponent::DrawHud() const
{
	const ARiptideCharacter* Crew = GetCrew();
	if (!Crew || Crew->GetHeldItem() != TEXT("fishing_rod") || Crew->IsInventoryOpen())
	{
		return;
	}
	using RiptideHud::ESlot;
	const int32 Have = Bait.IsNone() ? 0 : Crew->GetInventory()->CountOf(Bait);
	const FRiptideItemDef* BaitDef = Bait.IsNone() ? nullptr : RiptideItems::Find(Bait);
	const FString BaitName = BaitDef ? FString::Printf(TEXT("%s x%d"), *BaitDef->Name.ToString(), Have) : FString(TEXT("bare hook"));
	switch (State)
	{
	case ERiptideAnglerState::Idle:
		RiptideHud::Prompt(Crew, ESlot::Hands, Crew->CanUseHands()
			? FString::Printf(TEXT("LMB  Hold to cast    RMB  Bait: %s    Q  Put the rod away"), *BaitName)
			: FString(TEXT("Q  Put the rod away")));
		break;
	case ERiptideAnglerState::Charging:
		RiptideHud::Prompt(Crew, ESlot::Hands, TEXT("Casting: let go to throw"), FLinearColor::White, Charge);
		break;
	case ERiptideAnglerState::Flying:
	case ERiptideAnglerState::Waiting:
		RiptideHud::Prompt(Crew, ESlot::Hands, FString::Printf(TEXT("Waiting for a bite on the %s%s    LMB  Reel in"), *BaitName,
			Spot.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Spot.ToString())));
		break;
	case ERiptideAnglerState::Bite:
		RiptideHud::Prompt(Crew, ESlot::Hands, TEXT("It's biting!    LMB  Strike"), FLinearColor(1.f, 0.85f, 0.3f));
		break;
	case ERiptideAnglerState::Fight:
	{
		// The bar is the line's tension: red when it's about to go.
		const FLinearColor Colour = Fight.Tension > 0.8f ? FLinearColor(1.f, 0.35f, 0.25f) : Fight.Tension < 0.1f ? FLinearColor(0.7f, 0.85f, 1.f) : FLinearColor::White;
		FString Text = FString::Printf(TEXT("LMB  Hold to reel, ease off when it pulls    Line %.0f m"), Fight.Distance);
		if (Fight.bShark)
		{
			Text = FString::Printf(TEXT("SHARK ON THE LINE    F  Cut it loose    Line %.0f m"), Fight.Distance);
		}
		RiptideHud::Prompt(Crew, ESlot::Hands, Text, Colour, FMath::Clamp(Fight.Tension, 0.f, 1.f));
		break;
	}
	}
}
