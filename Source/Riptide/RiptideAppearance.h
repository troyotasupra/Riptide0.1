#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RiptideAppearance.generated.h"

/** The parts of a crew member's look the player chooses, in the order the customisation screen lists them. */
UENUM(BlueprintType)
enum class ERiptideLook : uint8
{
	Body,           // build: male or female
	SkinTone,
	Hair,           // hairstyle, or none
	HairColour,
	Beard,
	Headgear,       // helmet, boonie hat, cap, beanie, or none
	FaceCover,      // sunglasses, ballistic glasses, balaclava, shemagh, or none
	Camo,           // the uniform's camouflage pattern
	Vest,           // plate carrier, chest rig, or none
	GearColour,     // the colour of the vest, helmet cover, pouches and pack
	Backpack,
	Gloves,
	// What castaways wash up in (the parts above from Headgear down are military gear: kept for later, when such
	// things are found, but not chosen in the menu and not worn from it).
	Shirt,          // a t-shirt in a colour, or none
	Shorts,
	Footwear,       // barefoot, sandals, slides or clogs
	Count UMETA(Hidden)
};

/**
 * How a crew member looks: an index into each ERiptideLook part's options (URiptideAppearanceLibrary lists them).
 * Chosen in the main menu, saved with the player's profile, sent to the host when joining (in the join URL), and
 * replicated to everyone on the player's ARiptidePlayerState; the crew member's body is built from it.
 */
USTRUCT(BlueprintType)
struct RIPTIDE_API FRiptideAppearance
{
	GENERATED_BODY()

	/** One index per ERiptideLook part. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<uint8> Choices;

	FRiptideAppearance();

	uint8 Get(ERiptideLook Part) const;
	void Set(ERiptideLook Part, uint8 Option);

	/** Every choice within its part's options (fills in missing parts with the defaults). */
	void Sanitise();

	/** A short text form for URLs and saves ("0.2.1.0..."), and back. FromString sanitises; a bad string gives the
	 * defaults. */
	FString ToString() const;
	static FRiptideAppearance FromString(const FString& Text);

	bool operator==(const FRiptideAppearance& Other) const { return Choices == Other.Choices; }
	bool operator!=(const FRiptideAppearance& Other) const { return Choices != Other.Choices; }
};

/** The options for each part of the look, for the customisation screen and the body builder. */
UCLASS()
class RIPTIDE_API URiptideAppearanceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many options a part has. */
	UFUNCTION(BlueprintPure, Category = "Appearance")
	static int32 GetOptionCount(ERiptideLook Part);

	/** An option's name to show the player. */
	UFUNCTION(BlueprintPure, Category = "Appearance")
	static FText GetOptionName(ERiptideLook Part, int32 Option);

	/** A part's name to show the player ("Headgear"). */
	UFUNCTION(BlueprintPure, Category = "Appearance")
	static FText GetPartName(ERiptideLook Part);

	/** The default for a part (what a new profile starts with). */
	static uint8 GetDefaultOption(ERiptideLook Part);

	/** Whether the customisation screen offers this part (the military gear isn't on offer to a castaway). */
	UFUNCTION(BlueprintPure, Category = "Appearance")
	static bool IsShownInMenu(ERiptideLook Part);

	/** The cloth colour of a shirt or shorts option. */
	static FLinearColor GetClothColour(ERiptideLook Part, int32 Option);

	/** A random look (for "randomise", and for AI crews). */
	UFUNCTION(BlueprintCallable, Category = "Appearance")
	static FRiptideAppearance RandomAppearance();

	/** Colours the body builder uses for the colour parts (sRGB, as a designer picks them). */
	static FLinearColor GetSkinTone(int32 Option);
	static FLinearColor GetHairColour(int32 Option);
	static FLinearColor GetGearColour(int32 Option);
};

/** The player's saved profile: their callsign and look. One per player, in the save slot "Profile". */
UCLASS()
class RIPTIDE_API URiptideProfileSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString Callsign;

	UPROPERTY()
	FRiptideAppearance Appearance;

	/** Loads the profile (or a fresh one, with a random callsign, if there's none yet). */
	static URiptideProfileSave* LoadOrCreate();

	/** Saves it. True if it was written. */
	bool Save();

	static const TCHAR* SlotName;
};
