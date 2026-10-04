#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideInteractable.h"
#include "RiptideLandedFish.generated.h"

class ARiptideCharacter;
class UBoxComponent;
class UStaticMeshComponent;

/**
 * A fish just dragged out of the water, flopping on the ground (or a deck) where it landed. Kill it first (E), then
 * take it. Left alone it tires and suffocates, and in a while it's gone. The server owns it; everyone sees the same
 * fish.
 */
UCLASS()
class RIPTIDE_API ARiptideLandedFish : public AActor, public IRiptideInteractable
{
	GENERATED_BODY()

public:
	ARiptideLandedFish();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Lands a fish of a kind (RiptideFish) at an angler's feet: in front where there's ground or deck to land on,
	 * beside them or at their feet otherwise, never back in the water. Server. */
	static ARiptideLandedFish* Land(ARiptideCharacter* Angler, FName Fish, float Kg);

	UFUNCTION(BlueprintPure, Category = "Fishing")
	FName GetFish() const { return Fish; }

	UFUNCTION(BlueprintPure, Category = "Fishing")
	float GetKg() const { return Kg; }

	UFUNCTION(BlueprintPure, Category = "Fishing")
	bool IsAlive() const { return bAlive; }

	// IRiptideInteractable
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const override;
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) override;

	/** How long a fish thrashes before it gives up, and how long it lies there after. */
	static constexpr float SuffocateSeconds = 35.f;
	static constexpr float RotSeconds = 300.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TObjectPtr<UBoxComponent> Reach;

	UPROPERTY(VisibleAnywhere, Category = "Fishing")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(ReplicatedUsing = OnRep_Fish)
	FName Fish;

	UPROPERTY(Replicated)
	float Kg = 1.f;

	UPROPERTY(ReplicatedUsing = OnRep_Alive)
	bool bAlive = true;

	UFUNCTION()
	void OnRep_Fish();

	UFUNCTION()
	void OnRep_Alive();

	void Kill();

	float Age = 0.f;
	float Seed = 0.f;
};
