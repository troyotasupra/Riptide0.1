#include "RiptideSurvivalComponent.h"
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
	if (IsSick())
	{
		Vitals.Sickness = FMath::Max(0.f, Vitals.Sickness - Seconds);
		Vitals.Health -= SickDamage * Seconds;
	}
	else if (Vitals.Hunger > 50.f && Vitals.Thirst > 50.f)
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
