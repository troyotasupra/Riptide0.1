#include "RiptideStorageComponent.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

URiptideStorageComponent::URiptideStorageComponent()
{
	SetIsReplicatedByDefault(true);
}

void URiptideStorageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URiptideStorageComponent, Storages);
}

int32 URiptideStorageComponent::AddStorage(const FText& Title, int32 Width, int32 Height, const FVector& Point)
{
	FRiptideStorage& Storage = Storages.AddDefaulted_GetRef();
	Storage.Title = Title;
	Storage.Point = Point;
	Storage.Grid.Width = Width;
	Storage.Grid.Height = Height;
	return Storages.Num() - 1;
}

FVector URiptideStorageComponent::GetWorldPoint(int32 Index) const
{
	const AActor* Owner = GetOwner();
	return Storages.IsValidIndex(Index) && Owner ? Owner->GetActorTransform().TransformPosition(Storages[Index].Point) : FVector::ZeroVector;
}

int32 URiptideStorageComponent::FindNearest(const FVector& World, float Reach) const
{
	int32 Best = INDEX_NONE;
	float BestDist = Reach;
	for (int32 i = 0; i < Storages.Num(); ++i)
	{
		const float Dist = FVector::Dist(GetWorldPoint(i), World);
		if (Dist <= BestDist)
		{
			Best = i;
			BestDist = Dist;
		}
	}
	return Best;
}

bool URiptideStorageComponent::MoveItem(URiptideStorageComponent* From, int32 FromIndex, int32 Uid, URiptideStorageComponent* To,
	int32 ToIndex, int32 X, int32 Y, bool bRotated, int32 Count)
{
	FRiptideStorage* Source = From ? From->GetStorage(FromIndex) : nullptr;
	FRiptideStorage* Target = To ? To->GetStorage(ToIndex) : nullptr;
	const FRiptideItem* Found = Source ? Source->Grid.Get(Uid) : nullptr;
	if (!Target || !Found)
	{
		return false;
	}
	const FRiptideItem Item = *Found;
	const FRiptideItemDef* Def = RiptideItems::Find(Item.Id);
	const int32 Moving = Count <= 0 ? Item.Count : FMath::Min(Count, Item.Count);
	const bool bSameGrid = Source == Target;
	bool bMoved = false;

	if (X < 0)
	{
		// Sent across: top up matching stacks there, then wherever the rest fits.
		if (!bSameGrid)
		{
			FRiptideItem Taken = Source->Grid.Take(Uid, Moving);
			const int32 Left = Target->Grid.Add(Taken.Id, Taken.Count);
			if (Left > 0)
			{
				Source->Grid.Add(Taken.Id, Left);
			}
			bMoved = Left < Taken.Count;
		}
	}
	else if (Target->Grid.Fits(Item.Id, X, Y, bRotated, bSameGrid ? Uid : 0))
	{
		if (bSameGrid && Moving == Item.Count)
		{
			FRiptideItem* Moved = Source->Grid.Get(Uid);
			Moved->X = X;
			Moved->Y = Y;
			Moved->bRotated = bRotated;
		}
		else
		{
			Target->Grid.Place(Source->Grid.Take(Uid, Moving), X, Y, bRotated);
		}
		bMoved = true;
	}
	else if (const FRiptideItem* Onto = Target->Grid.SingleOverlap(Item.Id, X, Y, bRotated, bSameGrid ? Uid : 0))
	{
		// Dropped on a stack of the same thing: top it up.
		if (Def && Onto->Id == Item.Id && Onto->Uid != Uid && Onto->Count < Def->Stack)
		{
			const int32 OntoUid = Onto->Uid;
			const int32 Room = FMath::Min(Moving, Def->Stack - Onto->Count);
			Source->Grid.Take(Uid, Room);
			Target->Grid.Get(OntoUid)->Count += Room;
			bMoved = true;
		}
	}

	if (bMoved)
	{
		From->OnChanged.Broadcast();
		if (To != From)
		{
			To->OnChanged.Broadcast();
		}
	}
	return bMoved;
}
