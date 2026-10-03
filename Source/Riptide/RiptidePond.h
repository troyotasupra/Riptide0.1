#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideInteractable.h"
#include "RiptidePond.generated.h"

class UStaticMeshComponent;

/**
 * An island's fresh-water pool. Look at it with an empty canteen and E fills it (stream water: boil it, or chance
 * being sick); with no canteen, hold E to drink straight from it. The island build spawns one over its basin
 * (riptide_islands.py _place_pond) and gives it the pool's sheet of water as its mesh.
 */
UCLASS()
class RIPTIDE_API ARiptidePond : public AActor, public IRiptideInteractable
{
	GENERATED_BODY()

public:
	ARiptidePond();

	UFUNCTION(BlueprintPure, Category = "Pond")
	UStaticMeshComponent* GetWater() const { return Water; }

	/** What drinking straight from it does: thirst back, and the chance of a bad stomach. */
	static constexpr float DrinkWater = 15.f;
	static constexpr float DrinkSickSeconds = 90.f;
	static constexpr float DrinkSickChance = 0.25f;

	// IRiptideInteractable
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const override;
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) override;

	enum EVerb : uint8 { VerbDrink, VerbFill };

protected:
	UPROPERTY(VisibleAnywhere, Category = "Pond")
	TObjectPtr<UStaticMeshComponent> Water;
};
