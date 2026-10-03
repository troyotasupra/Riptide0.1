#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RiptideSurvivalComponent.generated.h"

/** A crew member's condition, each 0..100. */
USTRUCT(BlueprintType)
struct FRiptideVitals
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float Health = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float Hunger = 100.f;      // 100 full, 0 starving

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float Thirst = 100.f;      // 100 slaked, 0 parched

	UPROPERTY(BlueprintReadOnly, Category = "Survival")
	float Sickness = 0.f;      // seconds of sickness left
};

/**
 * Hunger, thirst, health and sickness, run on the server with the Godot build's rates (player/survival.gd): hunger
 * empties in forty minutes and thirst in twenty-five, each draining faster with effort; starving or parched costs
 * health, and health comes back while both are above half. Eating and drinking go through Consume. The owner sees
 * their own numbers; others only whether they're down.
 */
UCLASS(ClassGroup = (Riptide), meta = (BlueprintSpawnableComponent))
class RIPTIDE_API URiptideSurvivalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiptideSurvivalComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Survival")
	const FRiptideVitals& GetVitals() const { return Vitals; }

	UFUNCTION(BlueprintPure, Category = "Survival")
	float GetHealth() const { return Vitals.Health; }

	UFUNCTION(BlueprintPure, Category = "Survival")
	float GetHunger() const { return Vitals.Hunger; }

	UFUNCTION(BlueprintPure, Category = "Survival")
	float GetThirst() const { return Vitals.Thirst; }

	UFUNCTION(BlueprintPure, Category = "Survival")
	bool IsSick() const { return Vitals.Sickness > 0.f; }

	/** Eats or drinks: Food and Water out of 100, and a chance of Sickness seconds of being sick. Server only. */
	UFUNCTION(BlueprintCallable, Category = "Survival")
	void Consume(float Food, float Water, float SicknessSeconds = 0.f, float SickChance = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Survival")
	void Heal(float Amount);

	/** How many seconds of the day have gone by in the simulation (for tests: SetTimeScale runs it faster). */
	UFUNCTION(BlueprintPure, Category = "Survival")
	float GetSimulatedSeconds() const { return Simulated; }

	/** Sets the numbers outright (dev mode and tests). Server only. */
	UFUNCTION(BlueprintCallable, Category = "Survival")
	void SetVitals(float Health, float Hunger, float Thirst);

	// The rates (seconds for the bar to empty, damage per second), from the old build.
	static constexpr float HungerSeconds = 2400.f;
	static constexpr float ThirstSeconds = 1500.f;
	static constexpr float StarvingDamage = 1.f;
	static constexpr float RegenPerSecond = 0.5f;
	static constexpr float SickDamage = 0.15f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Vitals)
	FRiptideVitals Vitals;

	float Simulated = 0.f;
	float Accumulated = 0.f;

	UFUNCTION()
	void OnRep_Vitals() {}

	void Simulate(float Seconds);
};
