#include "RiptideSkyClock.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"

namespace
{
	/** The sun's highest at noon, and how the moon hangs at night. */
	constexpr float NoonElevationDeg = 62.f;
	constexpr float MoonElevationDeg = 40.f;
	constexpr float MoonBrightness = 0.06f;
	const FLinearColor MoonColour(0.55f, 0.65f, 1.f);
	const FLinearColor LowSunColour(1.f, 0.55f, 0.32f);
}

ARiptideSkyClock::ARiptideSkyClock()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(0.5f);
}

void ARiptideSkyClock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideSkyClock, Hours);
}

ARiptideSkyClock* ARiptideSkyClock::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ARiptideSkyClock> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ARiptideSkyClock::SetHours(float InHours)
{
	if (HasAuthority())
	{
		Hours = FMath::Fmod(FMath::Fmod(InHours, 24.f) + 24.f, 24.f);
		ForceNetUpdate();
		ApplySky();
	}
}

float ARiptideSkyClock::SkipTo(float Hour)
{
	if (!HasAuthority())
	{
		return 0.f;
	}
	const float Ahead = FMath::Fmod(Hour - Hours + 24.f, 24.f);
	SetHours(Hour);
	return Ahead / 24.f * DaySeconds;
}

void ARiptideSkyClock::OnRep_Hours()
{
	ApplySky();
}

void ARiptideSkyClock::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Every machine runs the clock on (a client is corrected each time the server's hour arrives).
	Hours = FMath::Fmod(Hours + DeltaSeconds * 24.f / FMath::Max(DaySeconds, 1.f), 24.f);
	ApplySky();
	// When everyone's asleep at night, the night passes: morning, everyone up, a night's hunger and thirst later.
	if (HasAuthority() && IsNight())
	{
		TArray<ARiptideCharacter*> Crew;
		bool bAllAsleep = true;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARiptideCharacter* Character = It->IsValid() ? Cast<ARiptideCharacter>((*It)->GetPawn()) : nullptr;
			if (!Character || !Character->IsSleeping())
			{
				bAllAsleep = false;
				break;
			}
			Crew.Add(Character);
		}
		if (bAllAsleep && Crew.Num() > 0)
		{
			const float Skipped = SkipTo(DawnHour + 0.5f);
			for (ARiptideCharacter* Character : Crew)
			{
				Character->WakeAfterNight(Skipped);
			}
			UE_LOG(LogTemp, Log, TEXT("Riptide: everyone slept; the night passed (%.0f s)"), Skipped);
		}
	}
}

ADirectionalLight* ARiptideSkyClock::FindSun()
{
	if (!Sun.IsValid())
	{
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			if (*It != Moon)        // the level's sun, not the moon this clock makes
			{
				Sun = *It;
				break;
			}
		}
	}
	return Sun.Get();
}

void ARiptideSkyClock::ApplySky()
{
	ADirectionalLight* Light = FindSun();
	ULightComponent* Comp = Light ? Light->GetLightComponent() : nullptr;
	if (!Comp)
	{
		return;
	}
	if (!bHaveSunDefaults)
	{
		SunYaw = Light->GetActorRotation().Yaw;
		SunIntensity = Comp->Intensity;
		SunColour = Comp->GetLightColor();
		bHaveSunDefaults = true;
		// The level's sun may be Stationary (partly baked); it has to be Movable to cross the sky.
		Comp->SetMobility(EComponentMobility::Movable);
	}
	// The sun's way across the sky: up at dawn, highest at noon, down at dusk, and under the horizon all night.
	const float DayLength = DuskHour - DawnHour;
	const float T = (Hours - DawnHour) / DayLength;                 // 0 at dawn, 1 at dusk; outside that, night
	const float Elevation = T >= 0.f && T <= 1.f ? NoonElevationDeg * FMath::Sin(UE_PI * T)
		: -NoonElevationDeg * 0.5f * FMath::Sin(UE_PI * FMath::Fmod(Hours - DuskHour + 24.f, 24.f) / (24.f - DayLength));
	const float Night = FMath::Clamp((-Elevation - 3.f) / 6.f, 0.f, 1.f);       // 0 by day, 1 in full night
	// The sun follows its real path all night too (under the horizon), so the sky darkens: it's the light the sky
	// draws its sun and its colour from. It crosses from the east through the south to the west by day, and on round
	// under the world by night, reddening near the horizon.
	const float NightT = FMath::Fmod(Hours - DuskHour + 24.f, 24.f) / (24.f - DayLength);
	const float Yaw = T >= 0.f && T <= 1.f ? SunYaw + (T - 0.5f) * 160.f : SunYaw + 80.f + 200.f * NightT;
	Light->SetActorRotation(FRotator(-Elevation, Yaw, 0.f));
	Comp->SetIntensity(SunIntensity * FMath::Clamp((Elevation + 3.f) / 9.f, 0.f, 1.f));
	Comp->SetLightColor(FMath::Lerp(LowSunColour, SunColour, FMath::SmoothStep(0.f, 20.f, Elevation)));
	// The moon: a light of its own, cold and dim, high in the sky opposite, that the sky doesn't take for a sun.
	if (!Moon && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Moon = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, Params);
		if (UDirectionalLightComponent* MoonLight = Moon ? Cast<UDirectionalLightComponent>(Moon->GetLightComponent()) : nullptr)
		{
			MoonLight->SetMobility(EComponentMobility::Movable);
			MoonLight->SetAtmosphereSunLight(false);
			MoonLight->SetCastShadows(false);
			MoonLight->SetLightColor(MoonColour);
		}
	}
	if (Moon && Moon->GetLightComponent())
	{
		Moon->SetActorRotation(FRotator(-MoonElevationDeg, SunYaw + 180.f, 0.f));
		Moon->GetLightComponent()->SetIntensity(SunIntensity * MoonBrightness * Night);
		Moon->GetLightComponent()->SetVisibility(Night > 0.f);
	}
	// Night exposure: left to itself the camera's auto exposure would brighten a moonlit beach to midday. Through the
	// night a fixed, darker exposure takes over (a volume blended in by how deep the night is), so moonlight reads as
	// dim and blue.
	if (!NightExposure && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		NightExposure = GetWorld()->SpawnActor<APostProcessVolume>(Params);
		if (NightExposure)
		{
			NightExposure->bUnbound = true;
			NightExposure->Priority = 900.f;
			NightExposure->Settings.bOverride_AutoExposureMinBrightness = true;
			NightExposure->Settings.bOverride_AutoExposureMaxBrightness = true;
		}
	}
	if (NightExposure)
	{
		NightExposure->Settings.AutoExposureMinBrightness = NightExposureEV;
		NightExposure->Settings.AutoExposureMaxBrightness = NightExposureEV;
		NightExposure->BlendWeight = Night;
		NightExposure->bEnabled = Night > 0.f;
	}
}
