#include "RiptideAppearance.h"

#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "RiptideAppearance"

namespace
{
	// Each part's options, in order. The body builder (ARiptideCharacter) shows option N of a part; changing an
	// option's place here changes what saved profiles show, so add new options at the end.
	struct FPartOptions
	{
		FText Name;
		TArray<FText> Options;
		uint8 Default;
	};

	const TArray<FPartOptions>& Parts()
	{
		static const TArray<FPartOptions> Table = {
			{ LOCTEXT("Body", "Build"), { LOCTEXT("Male", "Male"), LOCTEXT("Female", "Female") }, 0 },
			{ LOCTEXT("Skin", "Skin tone"), { LOCTEXT("Skin0", "Fair"), LOCTEXT("Skin1", "Light"), LOCTEXT("Skin2", "Tan"),
				LOCTEXT("Skin3", "Olive"), LOCTEXT("Skin4", "Brown"), LOCTEXT("Skin5", "Dark brown"), LOCTEXT("Skin6", "Deep") }, 2 },
			{ LOCTEXT("Hair", "Hair"), { LOCTEXT("HairNone", "Shaved"), LOCTEXT("HairBuzzed", "Buzz cut"), LOCTEXT("HairParted", "Short, parted"),
				LOCTEXT("HairLong", "Long"), LOCTEXT("HairBuns", "Buns") }, 1 },
			{ LOCTEXT("HairColour", "Hair colour"), { LOCTEXT("HairBlack", "Black"), LOCTEXT("HairDarkBrown", "Dark brown"),
				LOCTEXT("HairBrown", "Brown"), LOCTEXT("HairBlond", "Blond"), LOCTEXT("HairRed", "Red"), LOCTEXT("HairGrey", "Grey") }, 1 },
			{ LOCTEXT("Beard", "Beard"), { LOCTEXT("BeardNone", "Clean-shaven"), LOCTEXT("BeardFull", "Full beard") }, 0 },
			{ LOCTEXT("Headgear", "Headgear"), { LOCTEXT("HeadNone", "None"), LOCTEXT("Helmet", "Combat helmet"),
				LOCTEXT("Boonie", "Boonie hat"), LOCTEXT("Cap", "Cap"), LOCTEXT("Beanie", "Beanie") }, 1 },
			{ LOCTEXT("Face", "Face"), { LOCTEXT("FaceNone", "Bare"), LOCTEXT("Sunglasses", "Sunglasses"),
				LOCTEXT("Ballistic", "Ballistic glasses"), LOCTEXT("Balaclava", "Balaclava"), LOCTEXT("Shemagh", "Shemagh") }, 0 },
			{ LOCTEXT("Camo", "Uniform"), { LOCTEXT("CamoMulti", "Multi-terrain"), LOCTEXT("CamoWoodland", "Woodland"),
				LOCTEXT("CamoDesert", "Desert"), LOCTEXT("CamoUrban", "Urban grey"), LOCTEXT("CamoOlive", "Plain olive"),
				LOCTEXT("CamoBlack", "Black") }, 0 },
			{ LOCTEXT("Vest", "Vest"), { LOCTEXT("VestNone", "None"), LOCTEXT("PlateCarrier", "Plate carrier"),
				LOCTEXT("ChestRig", "Chest rig") }, 1 },
			{ LOCTEXT("GearColour", "Gear colour"), { LOCTEXT("Coyote", "Coyote brown"), LOCTEXT("RangerGreen", "Ranger green"),
				LOCTEXT("GearBlack", "Black"), LOCTEXT("Wolf", "Wolf grey"), LOCTEXT("Tan", "Tan") }, 0 },
			{ LOCTEXT("Backpack", "Pack"), { LOCTEXT("PackNone", "None"), LOCTEXT("Assault", "Assault pack"),
				LOCTEXT("Hydration", "Hydration pack") }, 0 },
			{ LOCTEXT("Gloves", "Gloves"), { LOCTEXT("GlovesNone", "None"), LOCTEXT("GlovesTactical", "Tactical gloves") }, 1 },
		};
		check(Table.Num() == int32(ERiptideLook::Count));
		return Table;
	}

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}
}

FRiptideAppearance::FRiptideAppearance()
{
	Sanitise();
}

uint8 FRiptideAppearance::Get(ERiptideLook Part) const
{
	const int32 Index = int32(Part);
	return Choices.IsValidIndex(Index) ? Choices[Index] : URiptideAppearanceLibrary::GetDefaultOption(Part);
}

void FRiptideAppearance::Set(ERiptideLook Part, uint8 Option)
{
	Sanitise();
	Choices[int32(Part)] = uint8(FMath::Clamp<int32>(Option, 0, URiptideAppearanceLibrary::GetOptionCount(Part) - 1));
}

void FRiptideAppearance::Sanitise()
{
	const int32 Count = int32(ERiptideLook::Count);
	for (int32 i = Choices.Num(); i < Count; ++i)
	{
		Choices.Add(URiptideAppearanceLibrary::GetDefaultOption(ERiptideLook(i)));
	}
	Choices.SetNum(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		if (Choices[i] >= URiptideAppearanceLibrary::GetOptionCount(ERiptideLook(i)))
		{
			Choices[i] = URiptideAppearanceLibrary::GetDefaultOption(ERiptideLook(i));
		}
	}
}

FString FRiptideAppearance::ToString() const
{
	TArray<FString> Parts;
	for (const uint8 Choice : Choices)
	{
		Parts.Add(FString::FromInt(Choice));
	}
	return FString::Join(Parts, TEXT("."));
}

FRiptideAppearance FRiptideAppearance::FromString(const FString& Text)
{
	FRiptideAppearance Look;
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("."));
	for (int32 i = 0; i < Parts.Num() && i < int32(ERiptideLook::Count); ++i)
	{
		if (Parts[i].IsNumeric())
		{
			Look.Choices[i] = uint8(FMath::Clamp(FCString::Atoi(*Parts[i]), 0, 255));
		}
	}
	Look.Sanitise();
	return Look;
}

int32 URiptideAppearanceLibrary::GetOptionCount(ERiptideLook Part)
{
	return Parts().IsValidIndex(int32(Part)) ? Parts()[int32(Part)].Options.Num() : 0;
}

FText URiptideAppearanceLibrary::GetOptionName(ERiptideLook Part, int32 Option)
{
	return Parts().IsValidIndex(int32(Part)) && Parts()[int32(Part)].Options.IsValidIndex(Option) ? Parts()[int32(Part)].Options[Option] : FText::GetEmpty();
}

FText URiptideAppearanceLibrary::GetPartName(ERiptideLook Part)
{
	return Parts().IsValidIndex(int32(Part)) ? Parts()[int32(Part)].Name : FText::GetEmpty();
}

uint8 URiptideAppearanceLibrary::GetDefaultOption(ERiptideLook Part)
{
	return Parts().IsValidIndex(int32(Part)) ? Parts()[int32(Part)].Default : 0;
}

FRiptideAppearance URiptideAppearanceLibrary::RandomAppearance()
{
	FRiptideAppearance Look;
	for (int32 i = 0; i < int32(ERiptideLook::Count); ++i)
	{
		Look.Set(ERiptideLook(i), uint8(FMath::RandRange(0, GetOptionCount(ERiptideLook(i)) - 1)));
	}
	return Look;
}

FLinearColor URiptideAppearanceLibrary::GetSkinTone(int32 Option)
{
	static const FLinearColor Tones[] = { Srgb(241, 204, 180), Srgb(224, 180, 145), Srgb(198, 146, 106), Srgb(168, 120, 82),
		Srgb(130, 88, 58), Srgb(98, 64, 42), Srgb(66, 44, 30) };
	return Tones[FMath::Clamp(Option, 0, int32(UE_ARRAY_COUNT(Tones)) - 1)];
}

FLinearColor URiptideAppearanceLibrary::GetHairColour(int32 Option)
{
	static const FLinearColor Colours[] = { Srgb(20, 18, 17), Srgb(48, 32, 22), Srgb(92, 62, 38), Srgb(196, 160, 104),
		Srgb(132, 56, 28), Srgb(150, 148, 144) };
	return Colours[FMath::Clamp(Option, 0, int32(UE_ARRAY_COUNT(Colours)) - 1)];
}

FLinearColor URiptideAppearanceLibrary::GetGearColour(int32 Option)
{
	// Real gear colours: coyote brown, ranger green, black, wolf grey, tan 499.
	static const FLinearColor Colours[] = { Srgb(129, 97, 62), Srgb(73, 76, 56), Srgb(28, 28, 30), Srgb(96, 98, 98), Srgb(170, 146, 108) };
	return Colours[FMath::Clamp(Option, 0, int32(UE_ARRAY_COUNT(Colours)) - 1)];
}

const TCHAR* URiptideProfileSave::SlotName = TEXT("Profile");

URiptideProfileSave* URiptideProfileSave::LoadOrCreate()
{
	if (URiptideProfileSave* Saved = Cast<URiptideProfileSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
	{
		Saved->Appearance.Sanitise();
		return Saved;
	}
	URiptideProfileSave* Fresh = Cast<URiptideProfileSave>(UGameplayStatics::CreateSaveGameObject(URiptideProfileSave::StaticClass()));
	static const TCHAR* Names[] = { TEXT("Gunner"), TEXT("Raven"), TEXT("Drift"), TEXT("Anchor"), TEXT("Tide"), TEXT("Hawk"), TEXT("Brine"), TEXT("Squall") };
	Fresh->Callsign = FString::Printf(TEXT("%s-%02d"), Names[FMath::RandRange(0, int32(UE_ARRAY_COUNT(Names)) - 1)], FMath::RandRange(1, 99));
	return Fresh;
}

bool URiptideProfileSave::Save()
{
	Appearance.Sanitise();
	return UGameplayStatics::SaveGameToSlot(this, SlotName, 0);
}

#undef LOCTEXT_NAMESPACE
