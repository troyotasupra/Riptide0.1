#include "RiptideLandedFish.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RiptideFish.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "RiptideBoat.h"
#include "RiptideCharacter.h"
#include "RiptideItemIcons.h"
#include "RiptideItems.h"
#include "RiptideSea.h"

#define LOCTEXT_NAMESPACE "RiptideLandedFish"

ARiptideLandedFish::ARiptideLandedFish()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(5.f);

	Reach = CreateDefaultSubobject<UBoxComponent>(TEXT("Reach"));
	SetRootComponent(Reach);
	Reach->SetCollisionProfileName(TEXT("RiptideHarvest"));
	Reach->SetBoxExtent(FVector(30.f, 30.f, 15.f));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Reach);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -12.f));
}

void ARiptideLandedFish::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideLandedFish, Fish);
	DOREPLIFETIME(ARiptideLandedFish, Kg);
	DOREPLIFETIME(ARiptideLandedFish, bAlive);
}

ARiptideLandedFish* ARiptideLandedFish::Land(ARiptideCharacter* Angler, FName InFish, float InKg)
{
	UWorld* World = Angler ? Angler->GetWorld() : nullptr;
	if (!World || !RiptideFish::Find(InFish))
	{
		return nullptr;
	}
	// Wherever there's ground or deck to land on: out in front if there is, otherwise beside or at the feet.
	const FVector At = Angler->GetActorLocation();
	const FVector Forward = FRotator(0.f, Angler->GetControlRotation().Yaw, 0.f).Vector();
	const FVector Side(-Forward.Y, Forward.X, 0.f);
	const float Feet = At.Z - Angler->GetSimpleCollisionHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LandFish), false, Angler);
	FHitResult Spot;
	bool bFound = false;
	for (const FVector& Offset : { Forward * 110.f, Forward * 75.f, Side * 65.f, -Side * 65.f, -Forward * 60.f })
	{
		const FVector Try = At + Offset;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, FVector(Try.X, Try.Y, Feet + 120.f), FVector(Try.X, Try.Y, Feet - 150.f), ECC_Visibility, Params)
			&& Hit.ImpactPoint.Z > URiptideSeaSubsystem::SeaSurfaceAt(Angler, Hit.ImpactPoint) - 10.f && Hit.ImpactNormal.Z > 0.6f)
		{
			Spot = Hit;
			bFound = true;
			break;
		}
	}
	const FVector Where = bFound ? Spot.ImpactPoint + FVector(0.f, 0.f, 12.f) : FVector(At.X, At.Y, Feet + 12.f);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideLandedFish* Landed = World->SpawnActor<ARiptideLandedFish>(Where, FRotator(0.f, Angler->GetControlRotation().Yaw + 90.f, 0.f), Spawn);
	if (!Landed)
	{
		return nullptr;
	}
	Landed->Fish = InFish;
	Landed->Kg = InKg;
	Landed->OnRep_Fish();
	// Landed in a boat: it flops on the deck and goes where she goes.
	if (bFound && Spot.GetActor() && Spot.GetActor()->IsA<ARiptideBoat>())
	{
		Landed->AttachToActor(Spot.GetActor(), FAttachmentTransformRules::KeepWorldTransform);
	}
	return Landed;
}

void ARiptideLandedFish::OnRep_Fish()
{
	const FRiptideFishDef* Def = RiptideFish::Find(Fish);
	if (!Def)
	{
		return;
	}
	Body->SetStaticMesh(URiptideItemIconSubsystem::MeshFor(Def->Item));
	// The item models lie along x on their side; a big one a little bigger.
	Body->SetRelativeScale3D(FVector(FMath::Clamp(0.9f + Kg * 0.04f, 0.9f, 1.7f)));
	Seed = FMath::FRandRange(0.f, 10.f);
}

void ARiptideLandedFish::OnRep_Alive()
{
	if (!bAlive)
	{
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -12.f), FRotator::ZeroRotator);
	}
}

void ARiptideLandedFish::Kill()
{
	if (bAlive)
	{
		bAlive = false;
		OnRep_Alive();
		ForceNetUpdate();
	}
}

void ARiptideLandedFish::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (HasAuthority())
	{
		if (bAlive && Age > SuffocateSeconds)
		{
			Kill();
		}
		else if (Age > RotSeconds)
		{
			Destroy();
			return;
		}
	}
	if (bAlive)
	{
		// It thrashes hard at first and weakens as it tires.
		const float Strength = FMath::Clamp(1.f - Age / SuffocateSeconds, 0.15f, 1.f);
		const float T = GetWorld()->GetTimeSeconds() + Seed;
		const float Beat = FMath::Sin(T * 7.f) * FMath::Sin(T * 2.3f);
		Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -12.f + FMath::Abs(Beat) * 12.f * Strength),
			FRotator(0.f, Beat * 40.f * Strength, FMath::Sin(T * 9.f) * 14.f * Strength));
	}
}

bool ARiptideLandedFish::GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const
{
	const FRiptideFishDef* Def = RiptideFish::Find(Fish);
	if (!Def)
	{
		return false;
	}
	const FText Name = FText::FromString(Def->Name.ToString().ToLower());
	Out.Prompt = bAlive ? FText::Format(LOCTEXT("Kill", "Kill the {0} kg {1}"), FText::AsNumber(FMath::RoundToFloat(Kg * 10.f) / 10.f), Name)
		: FText::Format(LOCTEXT("Take", "Take the {0} kg {1}"), FText::AsNumber(FMath::RoundToFloat(Kg * 10.f) / 10.f), Name);
	Out.Verb = bAlive ? 0 : 1;
	return true;
}

void ARiptideLandedFish::Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb)
{
	const FRiptideFishDef* Def = RiptideFish::Find(Fish);
	if (!Who || !HasAuthority() || !Def)
	{
		return;
	}
	if (bAlive)
	{
		Kill();
		Who->StartAction(ERiptideCrewAction::Reach);
		return;
	}
	Who->GiveItem(Def->Item, 1);       // into the pockets or pack (on the ground beside them if there's no room)
	Who->StartAction(ERiptideCrewAction::PickUp);
	Destroy();
}

#undef LOCTEXT_NAMESPACE
