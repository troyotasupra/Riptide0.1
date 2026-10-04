#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/RiptideStructures.h"
#include "RiptideInteractable.h"
#include "RiptideItems.h"
#include "RiptideStructure.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class URiptideStorageComponent;

/** One thing on a station: what was put on, what it becomes, and when (server time; pushed back while a fire is out). */
USTRUCT()
struct FRiptideStationSlot
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	UPROPERTY()
	FName Result;

	UPROPERTY()
	double DoneAt = 0.0;
};

/**
 * Something the crew built: a campfire, a tent, a drying rack, a crate, the raft site. Placed from its kit, then
 * built up stage by stage with materials (E with them in your pockets); finished, it does its job: a fire is lit
 * with a lighter and burns wood, cooking and boiling what's laid on it; a rack dries; a crate opens like a locker;
 * a tent is a bed; the raft site launches a raft. Hold E to dismantle it for most of its materials back. Server
 * owned; everyone sees its stage, fuel and what's on it.
 */
UCLASS()
class RIPTIDE_API ARiptideStructure : public AActor, public IRiptideInteractable
{
	GENERATED_BODY()

public:
	ARiptideStructure();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Places a structure of Type at a transform, as its kit's first stage. Server only. */
	static ARiptideStructure* Place(UWorld* World, FName Type, const FTransform& Where);

	UFUNCTION(BlueprintPure, Category = "Structure")
	FName GetType() const { return Type; }

	const FRiptideStructureDef* GetDef() const;

	/** Which stage is being built (0 = the kit just placed), and whether it's finished. */
	UFUNCTION(BlueprintPure, Category = "Structure")
	int32 GetStage() const { return Stage; }

	UFUNCTION(BlueprintPure, Category = "Structure")
	bool IsFinished() const;

	/** What the current stage still needs: a text like "stone 2/4". */
	FText NeedsText() const;

	// A fire.
	UFUNCTION(BlueprintPure, Category = "Structure")
	bool IsLit() const { return bLit && Fuel > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Structure")
	float GetFuelSeconds() const { return Fuel; }

	// A station.
	int32 GetSlotCount() const { return Slots.Num(); }
	const TArray<FRiptideStationSlot>& GetSlots() const { return Slots; }

	/** Puts one of a carried item on the station (server), if the station can do something with it. */
	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool PutOn(class ARiptideCharacter* Who, int32 StorageIndex, int32 Uid);

	/** Takes what's on slot N into Who's pockets (server). */
	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool TakeOff(class ARiptideCharacter* Who, int32 Slot);

	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool AddFuelFrom(class ARiptideCharacter* Who);

	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool LightWith(class ARiptideCharacter* Who);

	/** Adds the next stage's materials from Who's pockets (server). True if any went in. */
	UFUNCTION(BlueprintCallable, Category = "Structure")
	bool AddMaterialsFrom(class ARiptideCharacter* Who);

	UFUNCTION(BlueprintPure, Category = "Structure")
	URiptideStorageComponent* GetStorage() const { return Storage; }

	// IRiptideInteractable
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const override;
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) override;

	enum EVerb : uint8 { VerbBuild, VerbLight, VerbFuel, VerbPut, VerbTake, VerbOpen, VerbDismantle, VerbLaunch, VerbSleep };

protected:
	friend class URiptideWorldSave;     // saves and rebuilds what the crew built

	UPROPERTY(VisibleAnywhere, Category = "Structure")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Structure")
	TObjectPtr<UPointLightComponent> FireLight;

	/** An unseen box over the footprint so a look at any part of it (the hollow of a stone ring) finds it. */
	UPROPERTY(VisibleAnywhere, Category = "Structure")
	TObjectPtr<class UBoxComponent> Reach;

	UPROPERTY(VisibleAnywhere, Category = "Structure")
	TObjectPtr<URiptideStorageComponent> Storage;

	UPROPERTY(Replicated)
	FName Type;

	/** How much of the current stage has gone in, per need. */
	UPROPERTY(ReplicatedUsing = OnRep_Build)
	int32 Stage = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Build)
	TArray<int32> Have;

	UPROPERTY(Replicated)
	float Fuel = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Lit)
	bool bLit = false;

	UPROPERTY(Replicated)
	TArray<FRiptideStationSlot> Slots;

	UPROPERTY(Replicated)
	float Health = 100.f;

	UFUNCTION()
	void OnRep_Build();

	UFUNCTION()
	void OnRep_Lit();

	void Refresh();
	double Now() const;
	void FinishSlot(int32 Index);
	void Dismantle(ARiptideCharacter* Who);
	void Launch(ARiptideCharacter* Who);
};
