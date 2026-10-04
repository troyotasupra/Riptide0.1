#include "RiptideWorldSave.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RiptideCharacter.h"
#include "RiptideCraftingComponent.h"
#include "RiptideRaft.h"
#include "RiptideChart.h"
#include "RiptideSea.h"
#include "RiptideSkyClock.h"
#include "RiptideWorldItem.h"

FString URiptideWorldSave::SlotFor(const UObject* WorldContext)
{
	// Tests running the game itself name their own (-RiptideSaveSlot=), so they never touch the player's.
	FString Slot;
	if (FParse::Value(FCommandLine::Get(), TEXT("RiptideSaveSlot="), Slot) && !Slot.IsEmpty())
	{
		return Slot;
	}
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World && World->IsPlayInEditor() ? TEXT("WorldEditor") : TEXT("World");
}

URiptideWorldSave* URiptideWorldSave::Load(const UObject* WorldContext)
{
	const FString Slot = SlotFor(WorldContext);
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		return nullptr;
	}
	URiptideWorldSave* Save = Cast<URiptideWorldSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (Save && Save->Version != CurrentVersion)
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: the saved world is from an older version (%d, now %d); it can't be continued"), Save->Version, CurrentVersion);
		return nullptr;
	}
	return Save;
}

bool URiptideWorldSave::Write(const UObject* WorldContext)
{
	SavedAt = FDateTime::UtcNow();
	Version = CurrentVersion;
	return UGameplayStatics::SaveGameToSlot(this, SlotFor(WorldContext), 0);
}

double URiptideWorldSave::ServerNow(const UWorld* World)
{
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
}

FString URiptideWorldSave::PlayerKey(const AController* Controller)
{
	if (!Controller)
	{
		return FString();
	}
	if (Controller->IsLocalController())
	{
		return TEXT("host");
	}
	const APlayerState* State = Controller->PlayerState;
	if (State && State->GetUniqueId().IsValid())
	{
		return State->GetUniqueId().ToString();
	}
	return State ? TEXT("name:") + State->GetPlayerName() : FString();
}

FRiptideItemGrid URiptideWorldSave::ToSaved(const FRiptideItemGrid& Grid, double Now)
{
	FRiptideItemGrid Saved = Grid;
	for (FRiptideItem& Item : Saved.Items)
	{
		if (Item.SpoilAt > 0.f)
		{
			Item.SpoilAt = FMath::Max(float(Item.SpoilAt - Now), 0.01f);
		}
	}
	return Saved;
}

TArray<FRiptideItem> URiptideWorldSave::FromSaved(FRiptideItemGrid& Into, const FRiptideItemGrid& Saved, double Now)
{
	TArray<FRiptideItem> Left;
	Into.Items.Empty();
	for (FRiptideItem Item : Saved.Items)
	{
		if (!RiptideItems::Find(Item.Id) || Item.Count <= 0)
		{
			continue;       // an item the game no longer has
		}
		Item.Uid = 0;       // fresh ids: these numbers start again each run
		if (Item.SpoilAt > 0.f)
		{
			Item.SpoilAt = float(Now) + Item.SpoilAt;
		}
		if (!Into.Place(Item, Item.X, Item.Y, Item.bRotated) && !Into.Place(Item))
		{
			Left.Add(Item);
		}
	}
	return Left;
}

// --- Saving ---

void URiptideWorldSave::Capture(UWorld* World)
{
	if (!World)
	{
		return;
	}
	const double Now = ServerNow(World);
	Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	if (const ARiptideSkyClock* Clock = ARiptideSkyClock::Get(World))
	{
		Day = Clock->GetDay();
		Hours = Clock->GetHours();
	}

	Structures.Reset();
	for (TActorIterator<ARiptideStructure> It(World); It; ++It)
	{
		const ARiptideStructure* Built = *It;
		if (!IsValid(Built) || Built->Type.IsNone())
		{
			continue;
		}
		FRiptideSavedStructure& Out = Structures.AddDefaulted_GetRef();
		Out.Type = Built->Type;
		Out.Transform = Built->GetActorTransform();
		Out.Stage = Built->Stage;
		Out.Have = Built->Have;
		Out.Fuel = Built->Fuel;
		Out.bLit = Built->bLit;
		Out.Health = Built->Health;
		Out.Slots = Built->Slots;
		for (FRiptideStationSlot& Slot : Out.Slots)
		{
			Slot.DoneAt = Slot.Id.IsNone() ? 0.0 : FMath::Max(Slot.DoneAt - Now, 0.01);
		}
		for (int32 i = 0; i < Built->Storage->Num(); ++i)
		{
			Out.Stored.Add(ToSaved(Built->Storage->GetStorage(i)->Grid, Now));
		}
	}

	WorldItems.Reset();
	for (TActorIterator<ARiptideWorldItem> It(World); It; ++It)
	{
		const FRiptideStorage* Contents = IsValid(*It) ? It->Contents->GetStorage(0) : nullptr;
		if (Contents && Contents->Grid.Items.Num() > 0)
		{
			FRiptideSavedWorldItem& Out = WorldItems.AddDefaulted_GetRef();
			Out.Transform = It->GetActorTransform();
			Out.Contents = *Contents;
			Out.Contents.Grid = ToSaved(Contents->Grid, Now);
		}
	}

	if (const ARiptideChart* Chart = ARiptideChart::Get(World))
	{
		ChartSeen = Chart->GetSeenCells();
		bChartRead = Chart->IsRead();
	}

	Rafts.Reset();
	for (TActorIterator<ARiptideRaft> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			FRiptideSavedRaft& Out = Rafts.AddDefaulted_GetRef();
			Out.Transform = It->GetActorTransform();
			Out.bOars = It->HasOars();
		}
	}

	Harvested.Reset();
	for (TActorIterator<ARiptideIslandProps> It(World); It; ++It)
	{
		for (const FRiptideDepletedProp& Taken : It->Depleted)
		{
			FRiptideSavedProp& Out = Harvested.AddDefaulted_GetRef();
			Out.Props = It->GetName();
			Out.PropCount = It->GetPropCount();
			Out.Batch = Taken.Batch;
			Out.Instance = Taken.Instance;
			Out.RespawnIn = FMath::Max(float(Taken.RespawnAt - Now), 1.f);
		}
	}

	// Everyone playing now; anyone who's left keeps the record from when they went.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ARiptideCharacter* Character = It->IsValid() ? Cast<ARiptideCharacter>((*It)->GetPawn()) : nullptr)
		{
			CapturePlayer(Character);
		}
	}
}

void URiptideWorldSave::CapturePlayer(const ARiptideCharacter* Character)
{
	const AController* Controller = Character ? Character->GetController() : nullptr;
	const FString Key = PlayerKey(Controller);
	if (Key.IsEmpty())
	{
		return;
	}
	FRiptideSavedPlayer* Record = Players.FindByPredicate([&Key](const FRiptideSavedPlayer& P) { return P.Key == Key; });
	if (!Record)
	{
		Record = &Players.AddDefaulted_GetRef();
		Record->Key = Key;
	}
	const double Now = ServerNow(Character->GetWorld());
	Record->Callsign = Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString();
	Record->Location = Character->GetActorLocation();
	Record->Yaw = Controller->GetControlRotation().Yaw;
	Record->Vitals = Character->GetSurvival()->GetVitals();
	Record->Carried.Reset();
	const URiptideStorageComponent* Inventory = Character->GetInventory();
	for (int32 i = 0; i < Inventory->Num(); ++i)
	{
		Record->Carried.Add(ToSaved(Inventory->GetStorage(i)->Grid, Now));
	}
	Record->Recipes = Character->GetCrafting()->GetKnownRecipes();
}

const FRiptideSavedPlayer* URiptideWorldSave::FindPlayer(const AController* Controller) const
{
	const FString Key = PlayerKey(Controller);
	if (const FRiptideSavedPlayer* Record = Players.FindByPredicate([&Key](const FRiptideSavedPlayer& P) { return P.Key == Key; }))
	{
		return Record;
	}
	// Without an online id that lasts from one game to the next (the local network), a player is known by name.
	const FString Name = Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString();
	if (Name.IsEmpty() || Key == TEXT("host"))
	{
		return nullptr;
	}
	return Players.FindByPredicate([&Name](const FRiptideSavedPlayer& P) { return P.Key != TEXT("host") && P.Callsign == Name; });
}

// --- Loading ---

void URiptideWorldSave::RestoreWorld(UWorld* World) const
{
	if (!World)
	{
		return;
	}
	const double Now = ServerNow(World);
	if (ARiptideSkyClock* Clock = ARiptideSkyClock::Get(World))
	{
		Clock->SetDay(Day);
		Clock->SetHours(Hours);
	}

	for (const FRiptideSavedStructure& Saved : Structures)
	{
		ARiptideStructure* Built = ARiptideStructure::Place(World, Saved.Type, Saved.Transform);
		if (!Built)
		{
			continue;       // a structure the game no longer has
		}
		Built->Stage = Saved.Stage;
		Built->Have = Saved.Have;
		Built->Fuel = Saved.Fuel;
		Built->bLit = Saved.bLit;
		Built->Health = Saved.Health;
		for (int32 i = 0; i < FMath::Min(Saved.Slots.Num(), Built->Slots.Num()); ++i)
		{
			Built->Slots[i] = Saved.Slots[i];
			Built->Slots[i].DoneAt = Saved.Slots[i].Id.IsNone() ? 0.0 : Now + Saved.Slots[i].DoneAt;
		}
		for (int32 i = 0; i < FMath::Min(Saved.Stored.Num(), Built->Storage->Num()); ++i)
		{
			for (const FRiptideItem& Spare : FromSaved(Built->Storage->GetStorage(i)->Grid, Saved.Stored[i], Now))
			{
				ARiptideWorldItem::Drop(World, Spare, Saved.Transform.GetLocation() + FVector(0.f, 0.f, 80.f));
			}
		}
		Built->Storage->OnChanged.Broadcast();
		Built->Refresh();
	}

	for (const FRiptideSavedWorldItem& Saved : WorldItems)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARiptideWorldItem* Item = World->SpawnActor<ARiptideWorldItem>(Saved.Transform.GetLocation(), Saved.Transform.Rotator(), Params);
		if (!Item)
		{
			continue;
		}
		Item->Contents->AddStorage(Saved.Contents.Title, Saved.Contents.Grid.Width, Saved.Contents.Grid.Height);
		FromSaved(Item->Contents->GetStorage(0)->Grid, Saved.Contents.Grid, Now);
		if (Item->Contents->GetStorage(0)->Grid.Items.Num() == 0)
		{
			Item->Destroy();
			continue;
		}
		Item->Refresh();
		Item->Settle();         // it had already come to rest where it lies
		Item->Contents->OnChanged.Broadcast();
	}

	for (const FRiptideSavedRaft& Saved : Rafts)
	{
		ARiptideRaft::Restore(World, Saved.Transform, Saved.bOars);
	}
	if (ARiptideChart* Chart = ARiptideChart::Get(World))
	{
		Chart->Restore(ChartSeen, bChartRead);
	}

	for (TActorIterator<ARiptideIslandProps> It(World); It; ++It)
	{
		const FString Name = It->GetName();
		const int32 Count = It->GetPropCount();
		for (const FRiptideSavedProp& Saved : Harvested)
		{
			// Only on the same island as it was: one rebuilt since has its props in a different order.
			if (Saved.Props == Name && Saved.PropCount == Count && It->Batches.IsValidIndex(Saved.Batch)
				&& Saved.Instance < It->Batches[Saved.Batch]->GetInstanceCount() && !It->IsDepleted(Saved.Batch, Saved.Instance))
			{
				FRiptideDepletedProp Taken;
				Taken.Batch = Saved.Batch;
				Taken.Instance = Saved.Instance;
				Taken.RespawnAt = float(Now) + Saved.RespawnIn;
				It->Depleted.Add(Taken);
			}
		}
		It->OnRep_Depleted();
	}
	UE_LOG(LogTemp, Log, TEXT("Riptide: continued the saved world: day %d, %.2f h, %d structures, %d things lying about, %d harvested, %d rafts"),
		Day, Hours, Structures.Num(), WorldItems.Num(), Harvested.Num(), Rafts.Num());
}

void URiptideWorldSave::RestorePlayer(ARiptideCharacter* Character, const FRiptideSavedPlayer& Saved)
{
	if (!Character || !Character->HasAuthority())
	{
		return;
	}
	const double Now = ServerNow(Character->GetWorld());
	URiptideSurvivalComponent* Survival = Character->GetSurvival();
	Survival->Vitals = Saved.Vitals;
	Survival->Vitals.bWarm = false;
	URiptideStorageComponent* Inventory = Character->GetInventory();
	TArray<FRiptideItem> Spare;
	for (int32 i = 0; i < Saved.Carried.Num(); ++i)
	{
		if (FRiptideStorage* Storage = Inventory->GetStorage(i))
		{
			Spare.Append(FromSaved(Storage->Grid, Saved.Carried[i], Now));
		}
		else
		{
			FRiptideItemGrid None;
			Spare.Append(FromSaved(None, Saved.Carried[i], Now));
		}
	}
	for (const FRiptideItem& Item : Spare)
	{
		ARiptideWorldItem::Drop(Character->GetWorld(), Item, Character->GetActorLocation() + Character->GetActorForwardVector() * 60.f);
	}
	Inventory->OnChanged.Broadcast();
	Character->GetCrafting()->Learn(Saved.Recipes);
	int32 Stacks = 0;
	for (const FRiptideItemGrid& Grid : Saved.Carried)
	{
		Stacks += Grid.Items.Num();
	}
	UE_LOG(LogTemp, Log, TEXT("Riptide: %s is back as they were (health %.0f, %d stacks carried)"), *Saved.Callsign, Saved.Vitals.Health, Stacks);
}
