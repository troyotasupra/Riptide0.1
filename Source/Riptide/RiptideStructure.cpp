#include "RiptideStructure.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"
#include "RiptideStorageComponent.h"
#include "RiptideWorldItem.h"

#define LOCTEXT_NAMESPACE "RiptideStructure"

ARiptideStructure::ARiptideStructure()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetUpdateFrequency(5.f);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetMobility(EComponentMobility::Movable);

	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetupAttachment(Mesh);
	FireLight->SetRelativeLocation(FVector(0.f, 0.f, 45.f));
	FireLight->SetIntensity(0.f);
	FireLight->SetLightColor(FLinearColor(1.f, 0.55f, 0.22f));
	FireLight->SetAttenuationRadius(900.f);
	FireLight->SetCastShadows(false);

	Reach = CreateDefaultSubobject<UBoxComponent>(TEXT("Reach"));
	Reach->SetupAttachment(Mesh);
	Reach->SetCollisionProfileName(TEXT("RiptideHarvest"));
	Reach->SetBoxExtent(FVector(50.f, 50.f, 30.f));
	Reach->SetRelativeLocation(FVector(0.f, 0.f, 30.f));

	Storage = CreateDefaultSubobject<URiptideStorageComponent>(TEXT("Storage"));
	Storage->SetIsReplicated(true);
}

void ARiptideStructure::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptideStructure, Type);
	DOREPLIFETIME(ARiptideStructure, Stage);
	DOREPLIFETIME(ARiptideStructure, Have);
	DOREPLIFETIME(ARiptideStructure, Fuel);
	DOREPLIFETIME(ARiptideStructure, bLit);
	DOREPLIFETIME(ARiptideStructure, Slots);
	DOREPLIFETIME(ARiptideStructure, Health);
}

const FRiptideStructureDef* ARiptideStructure::GetDef() const
{
	return RiptideStructures::Find(Type);
}

double ARiptideStructure::Now() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

ARiptideStructure* ARiptideStructure::Place(UWorld* World, FName Type, const FTransform& Where)
{
	const FRiptideStructureDef* Def = RiptideStructures::Find(Type);
	if (!World || !Def)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideStructure* Built = World->SpawnActor<ARiptideStructure>(Where.GetLocation(), Where.Rotator(), Params);
	if (!Built)
	{
		return nullptr;
	}
	Built->Type = Type;
	Built->Health = Def->Hp;
	Built->Have.SetNumZeroed(Def->Stages.Num() ? Def->Stages[0].Num() : 0);
	if (Def->Container.X > 0)
	{
		Built->Storage->AddStorage(Def->Name, Def->Container.X, Def->Container.Y);
	}
	if (Def->Station != ERiptideStationKind::None)
	{
		Built->Slots.SetNum(3);
	}
	Built->Refresh();
	return Built;
}

void ARiptideStructure::BeginPlay()
{
	Super::BeginPlay();
	Refresh();
}

bool ARiptideStructure::IsFinished() const
{
	const FRiptideStructureDef* Def = GetDef();
	return Def && Stage >= Def->Stages.Num();
}

void ARiptideStructure::Refresh()
{
	// The model for this stage: SM_Structure_<type>_<stage> (riptide_item_models.py), the finished one last.
	const FRiptideStructureDef* Def = GetDef();
	if (!Def)
	{
		return;
	}
	const int32 Shown = FMath::Min(Stage, Def->Stages.Num());
	const FString Path = FString::Printf(TEXT("/Game/Riptide/Items/SM_Structure_%s_%d.SM_Structure_%s_%d"), *Type.ToString(), Shown, *Type.ToString(), Shown);
	UStaticMesh* Want = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Want)
	{
		Want = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Riptide/Items/SM_Item_crate.SM_Item_crate"));
	}
	if (Want && Mesh->GetStaticMesh() != Want)
	{
		Mesh->SetStaticMesh(Want);
	}
	// The box covers the footprint, knee high, however hollow the model.
	const float Half = FMath::Max(Def->FootprintRadius * 0.6f, 40.f);
	Reach->SetBoxExtent(FVector(Half, Half, 35.f));
	Reach->SetRelativeLocation(FVector(0.f, 0.f, 35.f));
	FireLight->SetIntensity(IsLit() ? 3000.f : 0.f);
}

void ARiptideStructure::OnRep_Build()
{
	Refresh();
}

void ARiptideStructure::OnRep_Lit()
{
	Refresh();
}

FText ARiptideStructure::NeedsText() const
{
	const FRiptideStructureDef* Def = GetDef();
	if (!Def || IsFinished())
	{
		return FText::GetEmpty();
	}
	FString Text;
	const TArray<FRiptideNeed>& Needs = Def->Stages[Stage];
	for (int32 i = 0; i < Needs.Num(); ++i)
	{
		const FRiptideItemDef* Item = RiptideItems::Find(Needs[i].Item);
		const FString Name = Item ? Item->Name.ToString() : Needs[i].Item.ToString();
		Text += FString::Printf(TEXT("%s%s %d/%d"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *Name, Have.IsValidIndex(i) ? Have[i] : 0, Needs[i].Count);
	}
	return FText::FromString(Text);
}

bool ARiptideStructure::AddMaterialsFrom(ARiptideCharacter* Who)
{
	const FRiptideStructureDef* Def = GetDef();
	URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	if (!HasAuthority() || !Def || !Carrying || IsFinished())
	{
		return false;
	}
	const TArray<FRiptideNeed>& Needs = Def->Stages[Stage];
	Have.SetNumZeroed(Needs.Num());
	bool bAny = false;
	for (int32 i = 0; i < Needs.Num(); ++i)
	{
		int32 Want = Needs[i].Count - Have[i];
		for (int32 Grid = 0; Grid < Carrying->Num() && Want > 0; ++Grid)
		{
			if (FRiptideStorage* Pocket = Carrying->GetStorage(Grid))
			{
				const int32 Short = Pocket->Grid.Remove(Needs[i].Item, Want);
				const int32 Took = Want - Short;
				Have[i] += Took;
				Want = Short;
				bAny |= Took > 0;
			}
		}
	}
	if (bAny)
	{
		Carrying->OnChanged.Broadcast();
	}
	bool bStageDone = true;
	for (int32 i = 0; i < Needs.Num(); ++i)
	{
		bStageDone &= Have[i] >= Needs[i].Count;
	}
	if (bStageDone)
	{
		++Stage;
		Have.Reset();     // SetNumZeroed alone keeps the old stage's counts
		Have.SetNumZeroed(Stage < Def->Stages.Num() ? Def->Stages[Stage].Num() : 0);
	}
	Refresh();
	return bAny;
}

bool ARiptideStructure::AddFuelFrom(ARiptideCharacter* Who)
{
	URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	const FRiptideStructureDef* Def = GetDef();
	if (!HasAuthority() || !Carrying || !Def || Def->Station != ERiptideStationKind::Cook || !IsFinished())
	{
		return false;
	}
	// The first wood carried: driftwood first.
	for (const TCHAR* Wood : { TEXT("driftwood"), TEXT("log") })
	{
		const FRiptideItemDef* Item = RiptideItems::Find(FName(Wood));
		for (int32 Grid = 0; Grid < Carrying->Num() && Item; ++Grid)
		{
			FRiptideStorage* Pocket = Carrying->GetStorage(Grid);
			if (Pocket && Pocket->Grid.CountOf(FName(Wood)) > 0 && Fuel + Item->FuelSeconds <= RiptideStructures::MaxFuelSeconds + 1.f)
			{
				Pocket->Grid.Remove(FName(Wood), 1);
				Fuel = FMath::Min(Fuel + Item->FuelSeconds, RiptideStructures::MaxFuelSeconds);
				Carrying->OnChanged.Broadcast();
				return true;
			}
		}
	}
	return false;
}

bool ARiptideStructure::LightWith(ARiptideCharacter* Who)
{
	URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	const FRiptideStructureDef* Def = GetDef();
	if (!HasAuthority() || !Carrying || !Def || Def->Station != ERiptideStationKind::Cook || !IsFinished() || bLit || Fuel <= 0.f)
	{
		return false;
	}
	for (int32 Grid = 0; Grid < Carrying->Num(); ++Grid)
	{
		FRiptideStorage* Pocket = Carrying->GetStorage(Grid);
		if (!Pocket)
		{
			continue;
		}
		for (FRiptideItem& Item : Pocket->Grid.Items)
		{
			const FRiptideItemDef* ItemDef = RiptideItems::Find(Item.Id);
			if (ItemDef && ItemDef->IsTool(TEXT("lighter")) && Item.Charges > 0)
			{
				--Item.Charges;
				bLit = true;
				Carrying->OnChanged.Broadcast();
				Refresh();
				return true;
			}
		}
	}
	return false;
}

bool ARiptideStructure::PutOn(ARiptideCharacter* Who, int32 StorageIndex, int32 Uid)
{
	URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	FRiptideStorage* From = Carrying ? Carrying->GetStorage(StorageIndex) : nullptr;
	FRiptideItem* Item = From ? From->Grid.Get(Uid) : nullptr;
	const FRiptideItemDef* ItemDef = Item ? RiptideItems::Find(Item->Id) : nullptr;
	const FRiptideStructureDef* Def = GetDef();
	if (!HasAuthority() || !ItemDef || !Def || Def->Station == ERiptideStationKind::None || !IsFinished())
	{
		return false;
	}
	const ERiptideStation Kind = Def->Station == ERiptideStationKind::Cook ? ERiptideStation::Cook
		: Def->Station == ERiptideStationKind::Dry ? ERiptideStation::Dry : ERiptideStation::Compost;
	const FName Result = ItemDef->TransformedBy(Kind);
	if (Result.IsNone())
	{
		return false;
	}
	for (FRiptideStationSlot& Slot : Slots)
	{
		if (Slot.Id.IsNone())
		{
			const float Seconds = Kind == ERiptideStation::Cook ? RiptideStructures::CookSeconds
				: Kind == ERiptideStation::Dry ? RiptideStructures::DrySeconds : RiptideStructures::CompostSeconds;
			Slot.Id = Item->Id;
			Slot.Result = Result;
			Slot.DoneAt = Now() + Seconds;
			From->Grid.Take(Uid, 1);
			Carrying->OnChanged.Broadcast();
			return true;
		}
	}
	return false;
}

bool ARiptideStructure::TakeOff(ARiptideCharacter* Who, int32 Index)
{
	if (!HasAuthority() || !Who || !Slots.IsValidIndex(Index) || Slots[Index].Id.IsNone())
	{
		return false;
	}
	const FName Id = Slots[Index].Id;
	Slots[Index] = FRiptideStationSlot();
	if (Who->GiveItem(Id, 1) > 0)
	{
		ARiptideWorldItem::Drop(GetWorld(), FRiptideItemGrid::NewStack(Id, 1), GetActorLocation() + FVector(0.f, 0.f, 80.f));
	}
	return true;
}

void ARiptideStructure::FinishSlot(int32 Index)
{
	// Cooked: it lies on the station as the cooked thing until taken.
	Slots[Index].Id = Slots[Index].Result;
	Slots[Index].Result = NAME_None;
	Slots[Index].DoneAt = 0.0;
}

void ARiptideStructure::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	const FRiptideStructureDef* Def = GetDef();
	if (!Def || Def->Station == ERiptideStationKind::None)
	{
		return;
	}
	const bool bFire = Def->Station == ERiptideStationKind::Cook;
	if (bFire && bLit)
	{
		Fuel -= DeltaSeconds;
		if (Fuel <= 0.f)
		{
			Fuel = 0.f;
			bLit = false;
			Refresh();
		}
	}
	const double T = Now();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FRiptideStationSlot& Slot = Slots[i];
		if (Slot.Id.IsNone() || Slot.Result.IsNone())
		{
			continue;
		}
		if (bFire && !IsLit())
		{
			Slot.DoneAt += DeltaSeconds;     // an unlit fire cooks nothing: the time waits
			continue;
		}
		if (T >= Slot.DoneAt)
		{
			FinishSlot(i);
		}
	}
}

bool ARiptideStructure::GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const
{
	const FRiptideStructureDef* Def = GetDef();
	if (!Def)
	{
		return false;
	}
	if (!IsFinished())
	{
		Out.Prompt = FText::Format(LOCTEXT("Build", "Build {0}: {1}"), Def->Name, NeedsText());
		Out.Verb = VerbBuild;
		return true;
	}
	const URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	auto Carries = [&](auto Test)
	{
		for (int32 Grid = 0; Carrying && Grid < Carrying->Num(); ++Grid)
		{
			const FRiptideStorage* Pocket = Carrying->GetStorage(Grid);
			for (const FRiptideItem& Item : Pocket ? Pocket->Grid.Items : TArray<FRiptideItem>())
			{
				if (const FRiptideItemDef* ItemDef = RiptideItems::Find(Item.Id); ItemDef && Test(*ItemDef, Item))
				{
					return true;
				}
			}
		}
		return false;
	};
	if (Def->Station == ERiptideStationKind::Cook)
	{
		// Wood in hand feeds it; a lighter lights it; raw food goes on; cooked food comes off; else it's just warm.
		const bool bWood = Carries([](const FRiptideItemDef& D, const FRiptideItem&) { return D.FuelSeconds > 0.f; });
		const bool bLighter = Carries([](const FRiptideItemDef& D, const FRiptideItem& I) { return D.IsTool(TEXT("lighter")) && I.Charges > 0; });
		const bool bRaw = Carries([](const FRiptideItemDef& D, const FRiptideItem&) { return !D.TransformedBy(ERiptideStation::Cook).IsNone(); });
		int32 Done = INDEX_NONE;
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (!Slots[i].Id.IsNone() && Slots[i].Result.IsNone())
			{
				Done = i;
			}
		}
		if (Done != INDEX_NONE)
		{
			const FRiptideItemDef* Cooked = RiptideItems::Find(Slots[Done].Id);
			Out.Prompt = FText::Format(LOCTEXT("TakeCooked", "Take {0}"), Cooked ? Cooked->Name : FText::FromName(Slots[Done].Id));
			Out.Verb = VerbTake;
			Out.Item = Done;
			return true;
		}
		if (!bLit && Fuel > 0.f && bLighter)
		{
			Out.Prompt = LOCTEXT("Light", "Light the fire");
			Out.Verb = VerbLight;
			return true;
		}
		if (bRaw && IsLit())
		{
			Out.Prompt = LOCTEXT("Cook", "Cook");
			Out.Verb = VerbPut;
			return true;
		}
		if (bWood && Fuel < RiptideStructures::MaxFuelSeconds - 60.f)
		{
			Out.Prompt = FText::Format(LOCTEXT("Fuel", "Add wood ({0} min of fire)"), FMath::RoundToInt(Fuel / 60.f));
			Out.Verb = VerbFuel;
			return true;
		}
		Out.Prompt = IsLit() ? FText::Format(LOCTEXT("Burning", "Campfire: {0} min left"), FMath::RoundToInt(Fuel / 60.f))
			: Fuel > 0.f ? LOCTEXT("Unlit", "Campfire: unlit (needs a lighter)") : LOCTEXT("NoWood", "Campfire: needs wood");
		Out.WhyNot = Out.Prompt;
		Out.bEnabled = false;
		Out.Verb = VerbDismantle;
		return true;
	}
	if (Def->Station != ERiptideStationKind::None)
	{
		const ERiptideStation Kind = Def->Station == ERiptideStationKind::Dry ? ERiptideStation::Dry : ERiptideStation::Compost;
		int32 Done = INDEX_NONE;
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (!Slots[i].Id.IsNone() && Slots[i].Result.IsNone())
			{
				Done = i;
			}
		}
		if (Done != INDEX_NONE)
		{
			const FRiptideItemDef* Made = RiptideItems::Find(Slots[Done].Id);
			Out.Prompt = FText::Format(LOCTEXT("TakeMade", "Take {0}"), Made ? Made->Name : FText::FromName(Slots[Done].Id));
			Out.Verb = VerbTake;
			Out.Item = Done;
			return true;
		}
		if (Carries([Kind](const FRiptideItemDef& D, const FRiptideItem&) { return !D.TransformedBy(Kind).IsNone(); }))
		{
			Out.Prompt = Kind == ERiptideStation::Dry ? LOCTEXT("Dry", "Hang to dry") : LOCTEXT("Compost", "Put in the compost");
			Out.Verb = VerbPut;
			return true;
		}
		Out.Prompt = FText::Format(LOCTEXT("Station", "{0}"), Def->Name);
		Out.WhyNot = Out.Prompt;
		Out.bEnabled = false;
		return true;
	}
	if (Def->Container.X > 0)
	{
		Out.Prompt = FText::Format(LOCTEXT("Open", "Open {0}"), Def->Name);
		Out.Verb = VerbOpen;
		return true;
	}
	if (!Def->Launches.IsNone())
	{
		Out.Prompt = LOCTEXT("Launch", "Launch the raft");
		Out.Verb = VerbLaunch;
		Out.HoldSeconds = 2.f;
		return true;
	}
	if (Def->bBed)
	{
		Out.Prompt = LOCTEXT("Bed", "Tent (sleeping comes later)");
		Out.WhyNot = Out.Prompt;
		Out.bEnabled = false;
		return true;
	}
	Out.Prompt = FText::Format(LOCTEXT("Dismantle", "Hold to dismantle {0}"), Def->Name);
	Out.Verb = VerbDismantle;
	Out.HoldSeconds = 3.f;
	return true;
}

void ARiptideStructure::Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb)
{
	if (!HasAuthority() || !Who)
	{
		return;
	}
	switch (Verb)
	{
	case VerbBuild: AddMaterialsFrom(Who); Who->StartAction(ERiptideCrewAction::Reach); break;
	case VerbLight: LightWith(Who); Who->StartAction(ERiptideCrewAction::Reach); break;
	case VerbFuel: AddFuelFrom(Who); Who->StartAction(ERiptideCrewAction::Reach); break;
	case VerbTake: TakeOff(Who, Hit.Item); Who->StartAction(ERiptideCrewAction::PickUp); break;
	case VerbPut:
	{
		// The first raw thing carried goes on.
		const FRiptideStructureDef* Def = GetDef();
		const ERiptideStation Kind = Def && Def->Station == ERiptideStationKind::Cook ? ERiptideStation::Cook
			: Def && Def->Station == ERiptideStationKind::Dry ? ERiptideStation::Dry : ERiptideStation::Compost;
		URiptideStorageComponent* Carrying = Who->GetInventory();
		for (int32 Grid = 0; Carrying && Grid < Carrying->Num(); ++Grid)
		{
			const FRiptideStorage* Pocket = Carrying->GetStorage(Grid);
			for (const FRiptideItem& Item : Pocket ? Pocket->Grid.Items : TArray<FRiptideItem>())
			{
				const FRiptideItemDef* ItemDef = RiptideItems::Find(Item.Id);
				if (ItemDef && !ItemDef->TransformedBy(Kind).IsNone() && PutOn(Who, Grid, Item.Uid))
				{
					Who->StartAction(ERiptideCrewAction::Reach);
					return;
				}
			}
		}
		break;
	}
	case VerbOpen: Who->ClientOpenContainer(Storage, 0); break;
	case VerbDismantle: Dismantle(Who); break;
	case VerbLaunch: Launch(Who); break;
	default: break;
	}
}

void ARiptideStructure::Dismantle(ARiptideCharacter* Who)
{
	// Most of what went in comes back out on the ground.
	const FRiptideStructureDef* Def = GetDef();
	if (!Def)
	{
		return;
	}
	TMap<FName, int32> Back;
	for (int32 S = 0; S < Def->Stages.Num(); ++S)
	{
		for (int32 i = 0; i < Def->Stages[S].Num(); ++i)
		{
			const int32 Put = S < Stage ? Def->Stages[S][i].Count : S == Stage && Have.IsValidIndex(i) ? Have[i] : 0;
			Back.FindOrAdd(Def->Stages[S][i].Item) += Put;
		}
	}
	for (const TPair<FName, int32>& Pair : Back)
	{
		const int32 Count = FMath::Max(1, FMath::FloorToInt(Pair.Value * RiptideStructures::DismantleShare));
		if (Pair.Value > 0)
		{
			ARiptideWorldItem::Drop(GetWorld(), FRiptideItemGrid::NewStack(Pair.Key, Count), GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}
	for (const FRiptideStationSlot& Slot : Slots)
	{
		if (!Slot.Id.IsNone())
		{
			ARiptideWorldItem::Drop(GetWorld(), FRiptideItemGrid::NewStack(Slot.Id, 1), GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}
	if (const FRiptideStorage* Kept = Storage->GetStorage(0))
	{
		for (const FRiptideItem& Item : Kept->Grid.Items)
		{
			ARiptideWorldItem::Drop(GetWorld(), Item, GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}
	Who->StartAction(ERiptideCrewAction::Harvest);
	Destroy();
}

void ARiptideStructure::Launch(ARiptideCharacter* Who)
{
	// The raft comes with its own milestone; for now the site stays until it does.
	Who->StartAction(ERiptideCrewAction::Reach);
}

#undef LOCTEXT_NAMESPACE
