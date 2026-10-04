#include "RiptideItems.h"

#define LOCTEXT_NAMESPACE "RiptideItems"

TArray<FRiptideItemDef> RiptideItems_BuildTable();     // Data/RiptideItemTable.cpp

namespace
{
	struct FTables
	{
		TArray<FRiptideItemDef> Items;
		TMap<FName, int32> Index;
		TMap<FName, TArray<FName>> Groups;

		FTables()
		{
			Items = RiptideItems_BuildTable();
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				Index.Add(Items[i].Id, i);
			}
			// Recipes name these instead of one item; the first listed is spent first (data/item_table.gd GROUPS).
			Groups.Add(TEXT("wood"), { TEXT("driftwood"), TEXT("log") });
			Groups.Add(TEXT("baitfish"), { TEXT("raw_sardine"), TEXT("raw_mullet"), TEXT("raw_fish") });
		}
	};

	const FTables& Tables()
	{
		static const FTables T;
		return T;
	}
}

const FRiptideItemDef* RiptideItems::Find(FName Id)
{
	const int32* At = Tables().Index.Find(Id);
	return At ? &Tables().Items[*At] : nullptr;
}

const TArray<FRiptideItemDef>& RiptideItems::All()
{
	return Tables().Items;
}

const TArray<FName>* RiptideItems::Group(FName Group)
{
	return Tables().Groups.Find(Group);
}

bool RiptideItems::Matches(FName Need, FName Id)
{
	if (Need == Id)
	{
		return true;
	}
	const TArray<FName>* Members = Group(Need);
	return Members && Members->Contains(Id);
}

FText RiptideItems::KindName(ERiptideItemKind Kind)
{
	switch (Kind)
	{
	case ERiptideItemKind::Food: return LOCTEXT("Food", "Food");
	case ERiptideItemKind::Drink: return LOCTEXT("Drink", "Drink");
	case ERiptideItemKind::Material: return LOCTEXT("Material", "Material");
	case ERiptideItemKind::Tool: return LOCTEXT("Tool", "Tool");
	case ERiptideItemKind::Weapon: return LOCTEXT("Weapon", "Weapon");
	case ERiptideItemKind::Ammo: return LOCTEXT("Ammo", "Ammunition");
	case ERiptideItemKind::Attachment: return LOCTEXT("Attachment", "Attachment");
	case ERiptideItemKind::Medical: return LOCTEXT("Medical", "Medical");
	case ERiptideItemKind::Prosthetic: return LOCTEXT("Prosthetic", "Prosthetic");
	case ERiptideItemKind::Book: return LOCTEXT("Book", "Book");
	case ERiptideItemKind::Page: return LOCTEXT("Page", "Page");
	case ERiptideItemKind::Note: return LOCTEXT("Note", "Note");
	case ERiptideItemKind::Chart: return LOCTEXT("Chart", "Chart");
	case ERiptideItemKind::Key: return LOCTEXT("Key", "Key");
	case ERiptideItemKind::Kit: return LOCTEXT("Kit", "Kit");
	case ERiptideItemKind::Wearable: return LOCTEXT("Wearable", "Clothing");
	case ERiptideItemKind::Part: return LOCTEXT("Part", "Part");
	case ERiptideItemKind::Bag: return LOCTEXT("Bag", "Bag");
	}
	return FText::GetEmpty();
}

FLinearColor RiptideItems::RarityColour(int32 Rarity)
{
	static const FLinearColor Colours[] = {
		FLinearColor(0.45f, 0.47f, 0.50f), FLinearColor(0.30f, 0.62f, 0.34f), FLinearColor(0.25f, 0.48f, 0.85f),
		FLinearColor(0.58f, 0.34f, 0.82f), FLinearColor(0.90f, 0.68f, 0.20f), FLinearColor(0.86f, 0.26f, 0.24f) };
	return Colours[FMath::Clamp(Rarity, 0, 5)];
}

FText RiptideItems::RarityName(int32 Rarity)
{
	static const FText Names[] = { LOCTEXT("Common", "Common"), LOCTEXT("Uncommon", "Uncommon"), LOCTEXT("Rare", "Rare"),
		LOCTEXT("Epic", "Epic"), LOCTEXT("Legendary", "Legendary"), LOCTEXT("Exotic", "Exotic") };
	return Names[FMath::Clamp(Rarity, 0, 5)];
}

// --- Grid ---

int32 FRiptideItemGrid::NewUid()
{
	// Counted up (grids only change on the server), so no two stacks ever share one, which random numbers can't
	// promise (rand() is only 15 bits on Windows).
	static int32 Next = 1;
	const int32 Uid = Next;
	Next = Next >= 0x3fffffff ? 1 : Next + 1;
	return Uid;
}

FIntPoint FRiptideItemGrid::Footprint(FName Id, bool bRotated)
{
	const FRiptideItemDef* Def = RiptideItems::Find(Id);
	const FIntPoint Size = Def ? Def->Size : FIntPoint(1, 1);
	return bRotated ? FIntPoint(Size.Y, Size.X) : Size;
}

FIntRect FRiptideItemGrid::RectOf(const FRiptideItem& Item)
{
	const FIntPoint Size = Footprint(Item.Id, Item.bRotated);
	return FIntRect(Item.X, Item.Y, Item.X + Size.X, Item.Y + Size.Y);
}

FRiptideItem FRiptideItemGrid::NewStack(FName Id, int32 Count)
{
	FRiptideItem Stack;
	Stack.Id = Id;
	Stack.Count = Count;
	if (const FRiptideItemDef* Def = RiptideItems::Find(Id))
	{
		if (Def->Tool.IsSet() && Def->Tool->Uses > 0)
		{
			Stack.Charges = Def->Tool->Uses;
		}
		else if (Def->Tool.IsSet() && Def->Tool->BurnSeconds > 0.f)
		{
			Stack.Charges = FMath::RoundToInt(Def->Tool->BurnSeconds);
		}
		else if (Def->Food.IsSet() && Def->Food->Sips > 0)
		{
			Stack.Charges = Def->Food->Sips;
		}
	}
	return Stack;
}

const FRiptideItem* FRiptideItemGrid::Get(int32 Uid) const
{
	return Items.FindByPredicate([Uid](const FRiptideItem& Item) { return Item.Uid == Uid; });
}

FRiptideItem* FRiptideItemGrid::Get(int32 Uid)
{
	return Items.FindByPredicate([Uid](const FRiptideItem& Item) { return Item.Uid == Uid; });
}

const FRiptideItem* FRiptideItemGrid::ItemAt(FIntPoint Cell) const
{
	return Items.FindByPredicate([Cell](const FRiptideItem& Item) { return RectOf(Item).Contains(Cell); });
}

bool FRiptideItemGrid::InBounds(FName Id, int32 X, int32 Y, bool bRotated) const
{
	const FIntPoint Size = Footprint(Id, bRotated);
	return X >= 0 && Y >= 0 && X + Size.X <= Width && Y + Size.Y <= Height;
}

bool FRiptideItemGrid::Fits(FName Id, int32 X, int32 Y, bool bRotated, int32 IgnoreUid) const
{
	if (!InBounds(Id, X, Y, bRotated))
	{
		return false;
	}
	const FIntPoint Size = Footprint(Id, bRotated);
	const FIntRect Rect(X, Y, X + Size.X, Y + Size.Y);
	for (const FRiptideItem& Item : Items)
	{
		if (Item.Uid != IgnoreUid && Rect.Intersect(RectOf(Item)))
		{
			return false;
		}
	}
	return true;
}

const FRiptideItem* FRiptideItemGrid::SingleOverlap(FName Id, int32 X, int32 Y, bool bRotated, int32 IgnoreUid) const
{
	const FIntPoint Size = Footprint(Id, bRotated);
	const FIntRect Rect(X, Y, X + Size.X, Y + Size.Y);
	const FRiptideItem* Found = nullptr;
	for (const FRiptideItem& Item : Items)
	{
		if (Item.Uid != IgnoreUid && Rect.Intersect(RectOf(Item)))
		{
			if (Found)
			{
				return nullptr;
			}
			Found = &Item;
		}
	}
	return Found;
}

bool FRiptideItemGrid::FindSpace(FName Id, int32& OutX, int32& OutY, bool& bOutRotated) const
{
	const FIntPoint Size = Footprint(Id, false);
	for (const bool bRot : { false, true })
	{
		if (bRot && Size.X == Size.Y)
		{
			break;
		}
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (Fits(Id, X, Y, bRot))
				{
					OutX = X;
					OutY = Y;
					bOutRotated = bRot;
					return true;
				}
			}
		}
	}
	return false;
}

bool FRiptideItemGrid::Place(FRiptideItem Stack, int32 X, int32 Y, bool bRotated)
{
	if (X < 0)
	{
		if (!FindSpace(Stack.Id, X, Y, bRotated))
		{
			return false;
		}
	}
	else if (!Fits(Stack.Id, X, Y, bRotated, 0))
	{
		return false;     // a new stack in this grid: everything already here is in the way
	}
	Stack.X = X;
	Stack.Y = Y;
	Stack.bRotated = bRotated;
	if (Stack.Uid == 0 || Get(Stack.Uid))
	{
		Stack.Uid = NewUid();
	}
	Items.Add(Stack);
	return true;
}

int32 FRiptideItemGrid::Add(FName Id, int32 Count)
{
	return AddStack(NewStack(Id, Count));
}

int32 FRiptideItemGrid::AddStack(const FRiptideItem& Stack)
{
	const FRiptideItemDef* Def = RiptideItems::Find(Stack.Id);
	int32 Count = Stack.Count;
	if (!Def || Count <= 0)
	{
		return Count;
	}
	for (FRiptideItem& Item : Items)
	{
		if (Count > 0 && Item.CanMergeWith(Stack) && Item.Count < Def->Stack)
		{
			const int32 Moved = FMath::Min(Count, Def->Stack - Item.Count);
			Item.Count += Moved;
			Count -= Moved;
			// Merged food keeps the earlier spoil time.
			if (Stack.SpoilAt > 0.f && (Item.SpoilAt <= 0.f || Stack.SpoilAt < Item.SpoilAt))
			{
				Item.SpoilAt = Stack.SpoilAt;
			}
		}
	}
	while (Count > 0)
	{
		FRiptideItem Piece = Stack;
		Piece.Uid = 0;
		Piece.Count = FMath::Min(Count, Def->Stack);
		if (!Place(Piece))
		{
			break;
		}
		Count -= Piece.Count;
	}
	return Count;
}

FRiptideItem FRiptideItemGrid::Take(int32 Uid, int32 Count)
{
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		FRiptideItem& Item = Items[i];
		if (Item.Uid != Uid)
		{
			continue;
		}
		FRiptideItem Taken = Item;
		if (Count <= 0 || Count >= Item.Count)
		{
			Items.RemoveAt(i);
		}
		else
		{
			Taken.Count = Count;
			Taken.Uid = NewUid();
			Item.Count -= Count;
		}
		return Taken;
	}
	FRiptideItem Nothing;
	Nothing.Count = 0;
	return Nothing;
}

int32 FRiptideItemGrid::CountOf(FName Need) const
{
	int32 Total = 0;
	for (const FRiptideItem& Item : Items)
	{
		if (RiptideItems::Matches(Need, Item.Id))
		{
			Total += Item.Count;
		}
	}
	return Total;
}

int32 FRiptideItemGrid::Remove(FName Need, int32 Count)
{
	// A group is spent in its listed order: driftwood before logs.
	TArray<FName> Order;
	if (const TArray<FName>* Members = RiptideItems::Group(Need))
	{
		Order = *Members;
	}
	else
	{
		Order.Add(Need);
	}
	for (const FName Id : Order)
	{
		for (int32 i = 0; i < Items.Num() && Count > 0; )
		{
			if (Items[i].Id != Id)
			{
				++i;
				continue;
			}
			const int32 Taken = FMath::Min(Count, Items[i].Count);
			Items[i].Count -= Taken;
			Count -= Taken;
			if (Items[i].Count <= 0)
			{
				Items.RemoveAt(i);
			}
			else
			{
				++i;
			}
		}
	}
	return Count;
}

bool FRiptideItemGrid::HasTool(FName Type) const
{
	return FindTool(Type) != nullptr;
}

const FRiptideItem* FRiptideItemGrid::FindTool(FName Type) const
{
	return Items.FindByPredicate([Type](const FRiptideItem& Item)
	{
		const FRiptideItemDef* Def = RiptideItems::Find(Item.Id);
		return Def && Def->IsTool(Type) && (!Def->Tool->Uses || Item.Charges > 0);
	});
}

TArray<FName> FRiptideItemGrid::ToolTypes() const
{
	TArray<FName> Types;
	for (const FRiptideItem& Item : Items)
	{
		const FRiptideItemDef* Def = RiptideItems::Find(Item.Id);
		if (Def && Def->Tool.IsSet() && (!Def->Tool->Uses || Item.Charges > 0))
		{
			Types.AddUnique(Def->Tool->Type);
		}
	}
	return Types;
}

int32 FRiptideItemGrid::Spoil(float Now)
{
	int32 Changed = 0;
	for (FRiptideItem& Item : Items)
	{
		if (Item.SpoilAt > 0.f && Now >= Item.SpoilAt)
		{
			Item.Id = TEXT("spoiled_food");
			Item.SpoilAt = 0.f;
			Item.Charges = 0;
			++Changed;
		}
	}
	return Changed;
}

float FRiptideItemGrid::TotalWeight() const
{
	float Kg = 0.f;
	for (const FRiptideItem& Item : Items)
	{
		if (const FRiptideItemDef* Def = RiptideItems::Find(Item.Id))
		{
			Kg += Def->WeightKg * Item.Count;
		}
	}
	return Kg;
}

#undef LOCTEXT_NAMESPACE
