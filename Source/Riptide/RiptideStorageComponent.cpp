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

int32 URiptideStorageComponent::AddStorage(const FText& Title, int32 Width, int32 Height, const FVector& Point, const FVector2D& LidHalfSize)
{
	FRiptideStorage& Storage = Storages.AddDefaulted_GetRef();
	Storage.Title = Title;
	Storage.Point = Point;
	Storage.LidHalfSize = LidHalfSize;
	Storage.Grid.Width = Width;
	Storage.Grid.Height = Height;
	return Storages.Num() - 1;
}

int32 URiptideStorageComponent::CountFreeCells(int32 Index) const
{
	if (!Storages.IsValidIndex(Index))
	{
		return 0;
	}
	const FRiptideItemGrid& Grid = Storages[Index].Grid;
	int32 Used = 0;
	for (const FRiptideItem& Item : Grid.Items)
	{
		Used += FRiptideItemGrid::RectOf(Item).Area();
	}
	return Grid.Width * Grid.Height - Used;
}

FVector URiptideStorageComponent::GetWorldPoint(int32 Index) const
{
	const AActor* Owner = GetOwner();
	return Storages.IsValidIndex(Index) && Owner ? Owner->GetActorTransform().TransformPosition(Storages[Index].Point) : FVector::ZeroVector;
}

int32 URiptideStorageComponent::FindLookedAt(const FVector& Eye, const FVector& Direction, float Reach) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return INDEX_NONE;
	}
	// In the owner's frame, where each lid is a level rectangle: where the look crosses the lid's plane, and whether
	// that's on the lid (a few cm of slack round its edge, for its frame and gasket).
	const FTransform& Xf = Owner->GetActorTransform();
	const FVector LocalEye = Xf.InverseTransformPosition(Eye);
	const FVector LocalDir = Xf.InverseTransformVectorNoScale(Direction.GetSafeNormal());
	constexpr float Slack = 4.f;
	int32 Best = INDEX_NONE;
	float BestDist = Reach;
	for (int32 i = 0; i < Storages.Num(); ++i)
	{
		const FRiptideStorage& S = Storages[i];
		if (S.LidHalfSize.IsZero() || FMath::Abs(LocalDir.Z) < 1e-3f)
		{
			continue;
		}
		const float Dist = (S.Point.Z - LocalEye.Z) / LocalDir.Z;
		const FVector At = LocalEye + LocalDir * Dist;
		if (Dist > 0.f && Dist <= BestDist && FMath::Abs(At.X - S.Point.X) <= S.LidHalfSize.X + Slack
			&& FMath::Abs(At.Y - S.Point.Y) <= S.LidHalfSize.Y + Slack)
		{
			Best = i;
			BestDist = Dist;
		}
	}
	return Best;
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
	// Moving the whole stack within its own grid, its old cells don't count as in the way; moving part of it, the
	// rest stays where it is and does.
	const int32 Ignore = bSameGrid && Moving == Item.Count ? Uid : 0;

	if (X < 0)
	{
		// Sent across: top up matching stacks there, then wherever the rest fits. Nothing is touched unless there's
		// room for at least some of it, and whatever doesn't fit stays where it was, as the same item.
		const bool bRoom = Target->Grid.Items.ContainsByPredicate([&](const FRiptideItem& Other)
			{ return Other.Id == Item.Id && Def && Other.Count < Def->Stack; });
		int32 SpaceX, SpaceY;
		bool bSpaceRot;
		if (!bSameGrid && (bRoom || Target->Grid.FindSpace(Item.Id, SpaceX, SpaceY, bSpaceRot)))
		{
			FRiptideItem Taken = Source->Grid.Take(Uid, Moving);
			const int32 Left = Target->Grid.Add(Taken.Id, Taken.Count);
			if (Left > 0)
			{
				if (FRiptideItem* Still = Source->Grid.Get(Uid))
				{
					Still->Count += Left;     // only part of the stack was taken: the rest never left
				}
				else
				{
					FRiptideItem Back = Item;
					Back.Count = Left;
					Source->Grid.Place(Back, Item.X, Item.Y, Item.bRotated);
				}
			}
			bMoved = Left < Taken.Count;
		}
	}
	else if (Target->Grid.Fits(Item.Id, X, Y, bRotated, Ignore))
	{
		if (Ignore)
		{
			FRiptideItem* Moved = Source->Grid.Get(Uid);
			Moved->X = X;
			Moved->Y = Y;
			Moved->bRotated = bRotated;
			bMoved = true;
		}
		else
		{
			FRiptideItem Taken = Source->Grid.Take(Uid, Moving);
			if (Target->Grid.Place(Taken, X, Y, bRotated))
			{
				bMoved = true;
			}
			else if (FRiptideItem* Still = Source->Grid.Get(Uid))
			{
				Still->Count += Taken.Count;       // didn't fit after all: it never left
			}
			else
			{
				Source->Grid.Place(Item, Item.X, Item.Y, Item.bRotated);
			}
		}
	}
	else if (const FRiptideItem* Onto = Target->Grid.SingleOverlap(Item.Id, X, Y, bRotated, Ignore))
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
