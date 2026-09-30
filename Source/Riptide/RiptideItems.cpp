#include "RiptideItems.h"

#define LOCTEXT_NAMESPACE "RiptideItems"

namespace
{
	const FText AmmoHint = LOCTEXT("AmmoHint", "Rounds for one calibre of gun: they don't fit anything else.");
	const FText ComingSoon = LOCTEXT("ComingSoon", "Not usable yet: it arrives in a later update.");

	TArray<FRiptideItemDef> BuildTable()
	{
		auto Item = [](const TCHAR* Id, const FText& Name, const FText& Category, int32 W, int32 H, int32 Stack, float Kg, int32 Rarity,
			const FText& Hint)
		{
			FRiptideItemDef Def;
			Def.Id = FName(Id);
			Def.Name = Name;
			Def.Category = Category;
			Def.Size = FIntPoint(W, H);
			Def.Stack = Stack;
			Def.WeightKg = Kg;
			Def.Rarity = Rarity;
			Def.Hint = Hint;
			return Def;
		};
		const FText Ammo = LOCTEXT("Ammo", "Ammunition");
		const FText Weapon = LOCTEXT("Weapon", "Weapon");
		const FText Medical = LOCTEXT("Medical", "Medical");
		const FText Food = LOCTEXT("Food", "Food");
		const FText Drink = LOCTEXT("Drink", "Drink");
		const FText Tool = LOCTEXT("Tool", "Tool");
		const FText Material = LOCTEXT("Material", "Material");
		const FText Part = LOCTEXT("Part", "Part");
		const FText Chart = LOCTEXT("Chart", "Chart");
		// Ids match the Godot build's item table where the item existed there.
		return {
			Item(TEXT("ammo_556"), LOCTEXT("ammo_556", "5.56"), Ammo, 1, 1, 60, 0.013f, 2, AmmoHint),
			Item(TEXT("ammo_9mm"), LOCTEXT("ammo_9mm", "9mm"), Ammo, 1, 1, 60, 0.012f, 2, AmmoHint),
			Item(TEXT("ammo_12ga"), LOCTEXT("ammo_12ga", "12 gauge"), Ammo, 1, 1, 30, 0.045f, 2, AmmoHint),
			Item(TEXT("m4"), LOCTEXT("m4", "M4 carbine"), Weapon, 2, 4, 1, 3.1f, 3, ComingSoon),
			Item(TEXT("m1911"), LOCTEXT("m1911", "M1911"), Weapon, 2, 2, 1, 1.1f, 2, ComingSoon),
			Item(TEXT("flare_gun"), LOCTEXT("flare_gun", "Flare gun"), Weapon, 2, 1, 1, 0.6f, 2, ComingSoon),
			Item(TEXT("flare"), LOCTEXT("flare", "Flare"), Material, 1, 1, 6, 0.15f, 1, ComingSoon),
			Item(TEXT("bandage"), LOCTEXT("bandage", "Bandage"), Medical, 1, 1, 10, 0.05f, 1, LOCTEXT("bandage_hint", "Stops bleeding and heals a little.")),
			Item(TEXT("first_aid_kit"), LOCTEXT("first_aid_kit", "First aid kit"), Medical, 2, 2, 1, 1.2f, 2, LOCTEXT("fak_hint", "A boat's trauma kit: dressings, tourniquets, splints.")),
			Item(TEXT("ration_pack"), LOCTEXT("ration_pack", "Ration pack"), Food, 1, 2, 4, 0.6f, 1, LOCTEXT("ration_hint", "A sealed field meal. Keeps forever.")),
			Item(TEXT("canteen_clean"), LOCTEXT("canteen_clean", "Canteen (clean water)"), Drink, 1, 2, 1, 1.f, 0, LOCTEXT("canteen_hint", "A litre of clean water.")),
			Item(TEXT("rope"), LOCTEXT("rope", "Rope"), Material, 1, 1, 20, 0.1f, 1, LOCTEXT("rope_hint", "Mooring line, lashing, towing.")),
			Item(TEXT("binoculars"), LOCTEXT("binoculars", "Binoculars"), Tool, 2, 1, 1, 0.9f, 2, ComingSoon),
			Item(TEXT("handheld_radio"), LOCTEXT("handheld_radio", "Handheld radio"), Tool, 1, 2, 1, 0.4f, 2, ComingSoon),
			Item(TEXT("tool_kit"), LOCTEXT("tool_kit", "Tool kit"), Tool, 3, 2, 1, 4.5f, 1, LOCTEXT("toolkit_hint", "Spanners, sockets and spares for fixing motors and hulls.")),
			Item(TEXT("cleaning_kit"), LOCTEXT("cleaning_kit", "Cleaning kit"), Tool, 2, 1, 1, 0.3f, 1, ComingSoon),
			Item(TEXT("sea_chart"), LOCTEXT("sea_chart", "Sea chart"), Chart, 1, 2, 1, 0.1f, 2, ComingSoon),
			Item(TEXT("fuel_drum"), LOCTEXT("fuel_drum", "Fuel drum"), Part, 2, 3, 1, 18.f, 0, LOCTEXT("fuel_hint", "Twenty litres of outboard fuel in a steel drum.")),
		};
	}

	const TArray<FRiptideItemDef>& Table()
	{
		static const TArray<FRiptideItemDef> Items = BuildTable();
		return Items;
	}
}

const FRiptideItemDef* RiptideItems::Find(FName Id)
{
	return Table().FindByPredicate([Id](const FRiptideItemDef& Def) { return Def.Id == Id; });
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
	return (FMath::Rand() & 0x3fffffff) + 1;
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
	else if (!Fits(Stack.Id, X, Y, bRotated, Stack.Uid))
	{
		return false;
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
	const FRiptideItemDef* Def = RiptideItems::Find(Id);
	if (!Def || Count <= 0)
	{
		return Count;
	}
	for (FRiptideItem& Item : Items)
	{
		if (Count > 0 && Item.Id == Id && Item.Count < Def->Stack)
		{
			const int32 Moved = FMath::Min(Count, Def->Stack - Item.Count);
			Item.Count += Moved;
			Count -= Moved;
		}
	}
	while (Count > 0)
	{
		FRiptideItem Piece;
		Piece.Id = Id;
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
