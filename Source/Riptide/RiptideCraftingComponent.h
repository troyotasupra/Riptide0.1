#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/RiptideRecipes.h"
#include "RiptideCraftingComponent.generated.h"

class ARiptideCharacter;

/**
 * A crew member's crafting: which recipes they know (the four every castaway knows, then what the survival book
 * and its pages teach), whether they have the makings of one, and the making itself, which takes the recipe's
 * seconds with the hands busy. The server does the making; everyone sees the progress.
 */
UCLASS(ClassGroup = (Riptide), meta = (BlueprintSpawnableComponent))
class RIPTIDE_API URiptideCraftingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiptideCraftingComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Recipes this crew member can make, in the table's order. */
	UFUNCTION(BlueprintPure, Category = "Crafting")
	TArray<FName> GetKnownRecipes() const { return Known; }

	UFUNCTION(BlueprintPure, Category = "Crafting")
	bool Knows(FName Recipe) const { return Known.Contains(Recipe); }

	/** Teaches recipes (reading a book or page). Server only. */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void Learn(const TArray<FName>& Recipes);

	/** Whether the makings of Recipe are carried: what's short, and the tool missing (none if not). */
	bool CanCraft(FName Recipe, TArray<FRiptideNeed>& OutShort, FName& OutMissingTool) const;

	UFUNCTION(BlueprintPure, Category = "Crafting")
	bool CanMake(FName Recipe) const;

	/** Starts making Recipe: the hands are busy for its seconds, then the ingredients become the product. */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	void Craft(FName Recipe);

	UFUNCTION(BlueprintPure, Category = "Crafting")
	FName GetCrafting() const { return Crafting; }

	/** 0..1 through the current making; 0 when idle. */
	UFUNCTION(BlueprintPure, Category = "Crafting")
	float GetProgress() const;

	UFUNCTION(BlueprintPure, Category = "Crafting")
	bool IsCrafting() const { return !Crafting.IsNone(); }

	/** How many times something has been made (for tests). */
	UFUNCTION(BlueprintPure, Category = "Crafting")
	int32 GetMadeCount() const { return Made; }

	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChanged;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Known)
	TArray<FName> Known;

	UPROPERTY(Replicated)
	FName Crafting;

	UPROPERTY(Replicated)
	double CraftEnd = 0.0;

	UPROPERTY(Replicated)
	float CraftSeconds = 0.f;

	int32 Made = 0;

	UFUNCTION()
	void OnRep_Known() { OnChanged.Broadcast(); }

	UFUNCTION(Server, Reliable)
	void ServerCraft(FName Recipe);

	ARiptideCharacter* Crew() const;
	double Now() const;
	void Finish();
};
