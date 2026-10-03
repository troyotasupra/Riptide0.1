#include "RiptideSurvivalComponent.h"
#include "Data/RiptideStructures.h"
#include "RiptideStructure.h"
#include "RiptideSkyClock.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

URiptideSurvivalComponent::URiptideSurvivalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void URiptideSurvivalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(URiptideSurvivalComponent, Vitals, COND_OwnerOnly);
}

void URiptideSurvivalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// Four steps a second is plenty for numbers that take minutes to move.
	Accumulated += DeltaTime;
	if (Accumulated >= 0.25f)
	{
		Simulate(Accumulated);
		Accumulated = 0.f;
	}
}

void URiptideSurvivalComponent::Simulate(float Seconds)
{
	Simulated += Seconds;
	// Effort: running doubles the drain.
	float Effort = 1.f;
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const float Speed = Character->GetVelocity().Size2D();
		Effort = 1.f + FMath::Clamp((Speed - 200.f) / 400.f, 0.f, 1.f);
	}
	Vitals.Hunger = FMath::Max(0.f, Vitals.Hunger - 100.f / HungerSeconds * Effort * Seconds * (IsSick() ? 2.f : 1.f));
	Vitals.Thirst = FMath::Max(0.f, Vitals.Thirst - 100.f / ThirstSeconds * Effort * Seconds * (IsSick() ? 2.f : 1.f));
	if (Vitals.Hunger <= 0.f)
	{
		Vitals.Health -= StarvingDamage * Seconds;
	}
	if (Vitals.Thirst <= 0.f)
	{
		Vitals.Health -= StarvingDamage * Seconds;
	}
	// The cold: out at night with nothing warm the body chills; by a fire, in a shelter, or by day it warms again.
	Vitals.bWarm = IsNearWarmth();
	const ARiptideSkyClock* Clock = ARiptideSkyClock::Get(this);
	const bool bChilling = Clock && Clock->IsNight() && !Vitals.bWarm;
	Vitals.Cold = FMath::Clamp(Vitals.Cold + (bChilling ? 100.f / ColdSeconds : -100.f / WarmUpSeconds) * Seconds, 0.f, 100.f);
	if (Vitals.Cold >= FreezingAt)
	{
		Vitals.Health -= FreezingDamage * Seconds;
	}
	if (IsSick())
	{
		Vitals.Sickness = FMath::Max(0.f, Vitals.Sickness - Seconds);
		Vitals.Health -= SickDamage * Seconds;
	}
	else if (Vitals.Hunger > 50.f && Vitals.Thirst > 50.f && Vitals.Cold < 50.f)
	{
		Vitals.Health += RegenPerSecond * Seconds;
	}
	Vitals.Health = FMath::Clamp(Vitals.Health, 0.f, 100.f);
}

void URiptideSurvivalComponent::Consume(float Food, float Water, float SicknessSeconds, float SickChance)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Vitals.Hunger = FMath::Clamp(Vitals.Hunger + Food, 0.f, 100.f);
	Vitals.Thirst = FMath::Clamp(Vitals.Thirst + Water, 0.f, 100.f);
	if (SicknessSeconds > 0.f && FMath::FRand() < SickChance)
	{
		Vitals.Sickness = FMath::Max(Vitals.Sickness, SicknessSeconds);
	}
}

bool URiptideSurvivalComponent::IsNearWarmth() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !GetWorld())
	{
		return false;
	}
	const FVector Here = Owner->GetActorLocation();
	for (TActorIterator<ARiptideStructure> It(GetWorld()); It; ++It)
	{
		const FRiptideStructureDef* Def = It->GetDef();
		if (!Def || Def->Warmth <= 0.f || FVector::Dist2D(Here, It->GetActorLocation()) > Def->WarmRadius)
		{
			continue;
		}
		// A fire warms while it burns; a shelter once it's built.
		if (Def->Station == ERiptideStationKind::Cook ? It->IsLit() : It->IsFinished())
		{
			return true;
		}
	}
	return false;
}

void URiptideSurvivalComponent::PassTimeAsleep(float Seconds, float Metabolism)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Vitals.Hunger = FMath::Max(0.f, Vitals.Hunger - 100.f / HungerSeconds * Metabolism * Seconds);
	Vitals.Thirst = FMath::Max(0.f, Vitals.Thirst - 100.f / ThirstSeconds * Metabolism * Seconds);
	Vitals.Cold = 0.f;
	Simulated += Seconds;
}

void URiptideSurvivalComponent::Heal(float Amount)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Vitals.Health = FMath::Clamp(Vitals.Health + Amount, 0.f, 100.f);
	}
}

void URiptideSurvivalComponent::SetVitals(float Health, float Hunger, float Thirst)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Vitals.Health = FMath::Clamp(Health, 0.f, 100.f);
		Vitals.Hunger = FMath::Clamp(Hunger, 0.f, 100.f);
		Vitals.Thirst = FMath::Clamp(Thirst, 0.f, 100.f);
	}
}
