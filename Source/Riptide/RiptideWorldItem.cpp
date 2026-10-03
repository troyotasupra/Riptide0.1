#include "RiptideWorldItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"
#include "RiptideItemIcons.h"
#include "RiptideSea.h"
#include "RiptideStorageComponent.h"

#define LOCTEXT_NAMESPACE "RiptideWorldItem"

namespace
{
	const float MergeReach = 80.f;       // cm: things dropped this close to each other become one bag
	const int32 BagWidth = 6, BagHeight = 4;
}

ARiptideWorldItem::ARiptideWorldItem()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	NetUpdateFrequency = 10.f;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);     // the crew walk through small things on the ground
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->CastShadow = true;

	Contents = CreateDefaultSubobject<URiptideStorageComponent>(TEXT("Contents"));
	Contents->SetIsReplicated(true);
}

void ARiptideWorldItem::BeginPlay()
{
	Super::BeginPlay();
	Contents->OnChanged.AddUObject(this, &ARiptideWorldItem::Refresh);
	Refresh();
}

const FRiptideItem* ARiptideWorldItem::GetSingle() const
{
	const FRiptideStorage* Storage = Contents->GetStorage(0);
	return Storage && Storage->Grid.Items.Num() == 1 ? &Storage->Grid.Items[0] : nullptr;
}

bool ARiptideWorldItem::IsBag() const
{
	const FRiptideStorage* Storage = Contents->GetStorage(0);
	return Storage && Storage->Grid.Items.Num() > 1;
}

FName ARiptideWorldItem::GetItemId() const
{
	const FRiptideItem* Single = GetSingle();
	return Single ? Single->Id : IsBag() ? FName(TEXT("loot_bag")) : NAME_None;
}

int32 ARiptideWorldItem::GetCount() const
{
	const FRiptideItem* Single = GetSingle();
	return Single ? Single->Count : 0;
}

void ARiptideWorldItem::Refresh()
{
	const FName Id = GetItemId();
	if (Id.IsNone())
	{
		// Emptied: nothing to see. (Not before Drop has filled it: BeginPlay runs inside SpawnActor, ahead of that.)
		if (HasAuthority() && Contents->Num() > 0)
		{
			Destroy();
		}
		return;
	}
	UStaticMesh* Want = URiptideItemIconSubsystem::MeshFor(Id);
	if (Want && Mesh->GetStaticMesh() != Want)
	{
		Mesh->SetStaticMesh(Want);
	}
	const FRiptideItemDef* Def = RiptideItems::Find(Id);
	bFloating = Def && Def->bFloats;
}

ARiptideWorldItem* ARiptideWorldItem::Drop(UWorld* World, const FRiptideItem& Stack, const FVector& At, const FVector& Throw)
{
	if (!World || Stack.Count <= 0 || !RiptideItems::Find(Stack.Id))
	{
		return nullptr;
	}
	// Into a bag already lying here, if there is one.
	for (TActorIterator<ARiptideWorldItem> It(World); It; ++It)
	{
		if (FVector::Dist(It->GetActorLocation(), At) <= MergeReach && It->AddStack(Stack) == 0)
		{
			return *It;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideWorldItem* Item = World->SpawnActor<ARiptideWorldItem>(At, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
	if (!Item)
	{
		return nullptr;
	}
	const FIntPoint Size = FRiptideItemGrid::Footprint(Stack.Id, false);
	Item->Contents->AddStorage(LOCTEXT("OnTheGround", "On the ground"), Size.X, Size.Y);
	Item->Contents->GetStorage(0)->Grid.Place(Stack);
	Item->Refresh();
	Item->Mesh->SetSimulatePhysics(true);
	Item->Mesh->SetPhysicsLinearVelocity(Throw);
	Item->Settling = Item->SettleSeconds;
	Item->Contents->OnChanged.Broadcast();
	return Item;
}

int32 ARiptideWorldItem::AddStack(const FRiptideItem& Stack)
{
	FRiptideStorage* Storage = Contents->GetStorage(0);
	if (!Storage)
	{
		return Stack.Count;
	}
	if (Storage->Grid.Width < BagWidth || Storage->Grid.Height < BagHeight)
	{
		// Growing into a bag: the old stack is laid out again in the bigger grid.
		TArray<FRiptideItem> Had = Storage->Grid.Items;
		Storage->Title = LOCTEXT("Bag", "Bag of gear");
		Storage->Grid.Width = BagWidth;
		Storage->Grid.Height = BagHeight;
		Storage->Grid.Items.Empty();
		for (FRiptideItem& Old : Had)
		{
			Storage->Grid.Place(Old);
		}
	}
	const int32 Left = Storage->Grid.AddStack(Stack);
	Contents->OnChanged.Broadcast();
	return Left;
}

void ARiptideWorldItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	if (Settling > 0.f)
	{
		Settling -= DeltaSeconds;
		if (Settling <= 0.f)
		{
			Settle();
		}
	}
	else if (bFloating)
	{
		// Rides the sea's surface where there's water over the ground.
		const FVector Here = GetActorLocation();
		const float Ground = URiptideSeaSubsystem::GroundHeightAt(this, Here);
		if (Ground < 0.f)
		{
			SetActorLocation(FVector(Here.X, Here.Y, FMath::FInterpTo(Here.Z, 0.f, DeltaSeconds, 2.f)));
		}
	}
}

void ARiptideWorldItem::Settle()
{
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}

bool ARiptideWorldItem::GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const
{
	const FName Id = GetItemId();
	if (Id.IsNone())
	{
		return false;
	}
	if (IsBag())
	{
		Out.Prompt = LOCTEXT("Search", "Search the bag");
		Out.Verb = 1;
		return true;
	}
	const FRiptideItemDef* Def = RiptideItems::Find(Id);
	const int32 Count = GetCount();
	Out.Prompt = Count > 1
		? FText::Format(LOCTEXT("TakeMany", "Take {0} ×{1}"), Def ? Def->Name : FText::FromName(Id), Count)
		: FText::Format(LOCTEXT("TakeOne", "Take {0}"), Def ? Def->Name : FText::FromName(Id));
	Out.Verb = 0;
	return true;
}

void ARiptideWorldItem::Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb)
{
	if (!Who || !HasAuthority())
	{
		return;
	}
	if (Verb == 1)
	{
		Who->ClientOpenContainer(Contents, 0);
		return;
	}
	TakeInto(Who);
}

bool ARiptideWorldItem::TakeInto(ARiptideCharacter* Who)
{
	URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	FRiptideStorage* Storage = Contents->GetStorage(0);
	if (!Carrying || !Storage)
	{
		return false;
	}
	bool bAny = false;
	// Pockets first, then whatever else is carried.
	for (int32 Grid = 0; Grid < Carrying->Num(); ++Grid)
	{
		for (int32 i = Storage->Grid.Items.Num() - 1; i >= 0; --i)
		{
			const int32 Uid = Storage->Grid.Items[i].Uid;
			if (URiptideStorageComponent::MoveItem(Contents, 0, Uid, Carrying, Grid, -1, -1, false))
			{
				bAny = true;
			}
		}
	}
	if (bAny)
	{
		Refresh();
	}
	return bAny;
}

#undef LOCTEXT_NAMESPACE
