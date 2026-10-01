#include "RiptideSettings.h"

#include "AudioDevice.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

const TCHAR* URiptideSettingsSave::SlotName = TEXT("Settings");

namespace
{
	// The sound classes init_unreal.py makes and puts each imported sound in.
	const TCHAR* EffectsClassPath = TEXT("/Game/Riptide/Audio/SC_Effects.SC_Effects");
	const TCHAR* AmbientClassPath = TEXT("/Game/Riptide/Audio/SC_Ambient.SC_Ambient");

	URiptideSettingsSave* GSettings = nullptr;
}

URiptideSettingsSave* URiptideSettingsSave::Get()
{
	if (!GSettings)
	{
		GSettings = Cast<URiptideSettingsSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
		if (!GSettings)
		{
			GSettings = Cast<URiptideSettingsSave>(UGameplayStatics::CreateSaveGameObject(URiptideSettingsSave::StaticClass()));
		}
		// Kept for the whole run: every world reads it (in the editor, play sessions come and go around it).
		GSettings->AddToRoot();
		GSettings->Sanitise();
	}
	return GSettings;
}

void URiptideSettingsSave::Sanitise()
{
	MouseSensitivity = FMath::Clamp(FMath::IsFinite(MouseSensitivity) ? MouseSensitivity : 1.f, MinSensitivity, MaxSensitivity);
	FieldOfView = FMath::Clamp(FMath::IsFinite(FieldOfView) ? FieldOfView : 90.f, MinFieldOfView, MaxFieldOfView);
	for (float* Volume : { &MasterVolume, &EffectsVolume, &AmbientVolume })
	{
		*Volume = FMath::Clamp(FMath::IsFinite(*Volume) ? *Volume : 1.f, 0.f, 1.f);
	}
}

bool URiptideSettingsSave::Save()
{
	Sanitise();
	return UGameplayStatics::SaveGameToSlot(this, SlotName, 0);
}

void URiptideSettingsSave::Apply(const UObject* WorldContext)
{
	Sanitise();
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// Without an audio device (-nosound, a dedicated server) there's nothing to turn up or down.
	FAudioDeviceHandle Device = World->GetAudioDevice();
	if (!Device.IsValid())
	{
		return;
	}
	Device->SetTransientPrimaryVolume(MasterVolume);

	// The other two scale their sound classes through a mix that stays pushed on each audio device: changing an
	// override in it takes effect at once (no fade).
	if (!VolumeMix)
	{
		VolumeMix = NewObject<USoundMix>(this, TEXT("RiptideVolumeMix"));
	}
	USoundClass* Effects = LoadObject<USoundClass>(nullptr, EffectsClassPath);
	USoundClass* Ambient = LoadObject<USoundClass>(nullptr, AmbientClassPath);
	if (Effects)
	{
		UGameplayStatics::SetSoundMixClassOverride(World, VolumeMix, Effects, EffectsVolume, 1.f, 0.f, true);
	}
	if (Ambient)
	{
		UGameplayStatics::SetSoundMixClassOverride(World, VolumeMix, Ambient, AmbientVolume, 1.f, 0.f, true);
	}
	if (!PushedToDevices.Contains(Device.GetDeviceID()))
	{
		PushedToDevices.Add(Device.GetDeviceID());
		UGameplayStatics::PushSoundMixModifier(World, VolumeMix);
	}
}

FVector2D RiptideSettings::LookScale()
{
	const URiptideSettingsSave* Settings = URiptideSettingsSave::Get();
	return FVector2D(Settings->MouseSensitivity, Settings->MouseSensitivity * (Settings->bInvertY ? -1.f : 1.f));
}
