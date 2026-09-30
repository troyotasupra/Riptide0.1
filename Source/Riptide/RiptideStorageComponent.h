#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.generated.h"

/** One named storage grid: a boat's locker, or a crew member's pockets or backpack. */
USTRUCT(BlueprintType)
struct RIPTIDE_API FRiptideStorage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Storage")
	FText Title;

	/** Where it's opened from, in the owner's frame (a locker's lid); unused for things you carry. */
	UPROPERTY(BlueprintReadOnly, Category = "Storage")
	FVector Point = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Storage")
	FRiptideItemGrid Grid;
};

/**
 * Storage grids on an actor, replicated to every machine. The server changes them (MoveItem); everyone sees the
 * result. A boat's lockers are one of these, and so is what a crew member carries.
 */
UCLASS(ClassGroup = (Riptide), meta = (BlueprintSpawnableComponent))
class RIPTIDE_API URiptideStorageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiptideStorageComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	int32 AddStorage(const FText& Title, int32 Width, int32 Height, const FVector& Point = FVector::ZeroVector);

	int32 Num() const { return Storages.Num(); }
	const FRiptideStorage* GetStorage(int32 Index) const { return Storages.IsValidIndex(Index) ? &Storages[Index] : nullptr; }
	FRiptideStorage* GetStorage(int32 Index) { return Storages.IsValidIndex(Index) ? &Storages[Index] : nullptr; }

	/** The storage whose opening point is nearest World (within Reach cm), or INDEX_NONE. */
	int32 FindNearest(const FVector& World, float Reach) const;

	/** A storage's opening point in the world. */
	FVector GetWorldPoint(int32 Index) const;

	/**
	 * Moves item Uid (Count of it, all when 0) from one grid to another, or within one: into a free spot at (X, Y),
	 * topping up a matching stack there, or wherever it fits in To when X < 0. Server only. True if anything moved.
	 */
	static bool MoveItem(URiptideStorageComponent* From, int32 FromIndex, int32 Uid, URiptideStorageComponent* To, int32 ToIndex,
		int32 X, int32 Y, bool bRotated, int32 Count = 0);

	/** Called on every machine when the contents change (to redraw an open menu). */
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
	FOnChanged OnChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Storages)
	TArray<FRiptideStorage> Storages;

	UFUNCTION()
	void OnRep_Storages() { OnChanged.Broadcast(); }
};
