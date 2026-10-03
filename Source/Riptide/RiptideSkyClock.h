#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideSkyClock.generated.h"

class ADirectionalLight;
class APostProcessVolume;

/**
 * The time of day, the same for everyone: the server keeps the clock and every machine turns its own sun (and moon) by
 * it. A day lasts DaySeconds of real time; the sun rises at 6 and sets at 18. At night a dim, cold moonlight replaces
 * the sun and the exposure opens up, so it's dark but playable.
 *
 * When every player is asleep in a shelter at night, the night is skipped to morning (ARiptideGameMode asks via
 * SkipTo). The dev mode's time-of-day key sets the clock too.
 */
UCLASS()
class RIPTIDE_API ARiptideSkyClock : public AActor
{
	GENERATED_BODY()

public:
	ARiptideSkyClock();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** The world's clock, if it has one (ARiptideGameMode places one in every game). */
	static ARiptideSkyClock* Get(const UObject* WorldContext);

	/** The hour, 0-24. */
	UFUNCTION(BlueprintPure, Category = "Time")
	float GetHours() const { return Hours; }

	/** Night: between dusk and dawn (the sun below the horizon). */
	UFUNCTION(BlueprintPure, Category = "Time")
	bool IsNight() const { return Hours >= DuskHour || Hours < DawnHour; }

	/** Sets the hour (server). */
	UFUNCTION(BlueprintCallable, Category = "Time")
	void SetHours(float InHours);

	/** Moves the clock forward to Hour (the next one, past midnight if need be). Server. Returns the game seconds skipped. */
	float SkipTo(float Hour);

	/** The exposure held through the night (EV100, higher is darker): dim, but enough to find your way by the moon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	float NightExposureEV = 0.f;

	/** Real seconds in a game day. */
	UPROPERTY(EditAnywhere, Category = "Time")
	float DaySeconds = 2400.f;

	static constexpr float DawnHour = 6.f;
	static constexpr float DuskHour = 18.5f;

private:
	UFUNCTION()
	void OnRep_Hours();

	void ApplySky();
	ADirectionalLight* FindSun();

	/** The hour, replicated every few seconds; each machine runs it on in between. */
	UPROPERTY(ReplicatedUsing = OnRep_Hours)
	float Hours = 8.f;

	TWeakObjectPtr<ADirectionalLight> Sun;
	bool bHaveSunDefaults = false;
	float SunYaw = 0.f;
	float SunIntensity = 10.f;
	FLinearColor SunColour = FLinearColor::White;

	UPROPERTY(Transient)
	TObjectPtr<APostProcessVolume> NightExposure;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> Moon;
};
