#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideInteractable.h"
#include "RiptideItems.h"
#include "RiptideWorldItem.generated.h"

class UStaticMeshComponent;
class URiptideStorageComponent;

/**
 * Something lying on the ground (or floating): a dropped item, drawn as itself. Several things dropped together
 * become a bag that opens like a locker. The server owns it: it falls and settles under physics for a moment,
 * then lies still; clients only see where it ended up.
 */
UCLASS()
class RIPTIDE_API ARiptideWorldItem : public AActor, public IRiptideInteractable
{
	GENERATED_BODY()

public:
	ARiptideWorldItem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Puts Stack into the world at At, thrown along Throw (cm/s), merging into another world item within reach of
	 * the spot. Server only. Returns the item it ended up in. */
	static ARiptideWorldItem* Drop(UWorld* World, const FRiptideItem& Stack, const FVector& At, const FVector& Throw = FVector::ZeroVector);

	/** Its contents: one grid. A single stack is drawn as that item; more than one as a bag. */
	UFUNCTION(BlueprintPure, Category = "Item")
	URiptideStorageComponent* GetContents() const { return Contents; }

	/** The one stack in it, when it's a single thing (null for a bag or when empty). */
	const FRiptideItem* GetSingle() const;

	UFUNCTION(BlueprintPure, Category = "Item")
	bool IsBag() const;

	UFUNCTION(BlueprintPure, Category = "Item")
	FName GetItemId() const;

	UFUNCTION(BlueprintPure, Category = "Item")
	int32 GetCount() const;

	// IRiptideInteractable
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const override;
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) override;

	/** Takes whatever fits into Who's carrying grids; the rest stays. Server only. True if anything was taken. */
	bool TakeInto(ARiptideCharacter* Who);

	/** Adds a stack to this (growing a single thing into a bag). Server only. Returns how many didn't fit. */
	int32 AddStack(const FRiptideItem& Stack);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Item")
	TObjectPtr<URiptideStorageComponent> Contents;

	/** Seconds the server lets it tumble before it lies still. */
	UPROPERTY(EditDefaultsOnly, Category = "Item")
	float SettleSeconds = 3.f;

private:
	float Settling = 0.f;
	bool bFloating = false;
	void Refresh();
	void Settle();
};
