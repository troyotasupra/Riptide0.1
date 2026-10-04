#include "RiptideResources.h"

#define LOCTEXT_NAMESPACE "RiptideResources"

namespace
{
	struct FTable
	{
		TArray<FRiptideResourceDef> Defs;
		TMap<FName, FName> PropKinds;

		FTable()
		{
			auto Def = [this](const TCHAR* Kind, const FText& Label, const FText& Depleted, float Respawn, float Hold, bool bHide)
				-> FRiptideResourceDef&
			{
				FRiptideResourceDef& D = Defs.AddDefaulted_GetRef();
				D.Kind = FName(Kind);
				D.Label = Label;
				D.DepletedLabel = Depleted;
				D.RespawnSeconds = Respawn;
				D.HoldSeconds = Hold;
				D.bHideWhenDepleted = bHide;
				return D;
			};
			FRiptideResourceDef& Palm = Def(TEXT("palm"), LOCTEXT("Palm", "Pick coconuts"), LOCTEXT("PalmBare", "No coconuts left"), 600.f, 1.f, false);
			Palm.Yields = { { TEXT("coconut"), 1, 2 } };
			FRiptideResourceDef& Tree = Def(TEXT("tree"), LOCTEXT("Tree", "Chop for logs"), LOCTEXT("TreeDone", "Chopped: nothing more to take"), 700.f, 4.f, false);
			Tree.RequiresTool = TEXT("hatchet");
			Tree.Yields = { { TEXT("log"), 2, 3 }, { TEXT("grub"), 0, 2 } };
			FRiptideResourceDef& Berries = Def(TEXT("berry_bush"), LOCTEXT("Berries", "Pick berries"), LOCTEXT("BerriesBare", "Picked clean"), 420.f, 1.5f, false);
			Berries.Yields = { { TEXT("berries"), 3, 6 } };
			Berries.ToolSpeed.Add(TEXT("knife"), 1.5f);
			Berries.ToolSpeed.Add(TEXT("machete"), 2.f);
			FRiptideResourceDef& Fiber = Def(TEXT("fiber"), LOCTEXT("Fiber", "Strip fiber"), LOCTEXT("FiberBare", "Stripped bare"), 240.f, 3.f, true);
			Fiber.Yields = { { TEXT("fiber"), 3, 4 } };
			Fiber.ToolSpeed.Add(TEXT("knife"), 2.f);
			Fiber.ToolSpeed.Add(TEXT("machete"), 6.f);
			FRiptideResourceDef& Stone = Def(TEXT("stone"), LOCTEXT("Stone", "Take stone"), LOCTEXT("StoneGone", "Taken"), 900.f, 0.f, true);
			Stone.Yields = { { TEXT("stone"), 1, 1 } };
			FRiptideResourceDef& Flint = Def(TEXT("flint"), LOCTEXT("Flint", "Take flint"), LOCTEXT("FlintGone", "Taken"), 900.f, 0.f, true);
			Flint.Yields = { { TEXT("flint"), 1, 1 } };
			FRiptideResourceDef& Driftwood = Def(TEXT("driftwood"), LOCTEXT("Driftwood", "Take driftwood"), LOCTEXT("DriftwoodGone", "Taken"), 900.f, 0.f, true);
			Driftwood.Yields = { { TEXT("driftwood"), 1, 2 }, { TEXT("grub"), 0, 1 } };

			// Which planted things are which resource (riptide_island_shape.py's prop kinds).
			for (const TCHAR* Palmy : { TEXT("palm_tall"), TEXT("palm_medium"), TEXT("palm_leaning"), TEXT("palm_sweeping"), TEXT("palm_young") })
			{
				PropKinds.Add(FName(Palmy), TEXT("palm"));
			}
			for (const TCHAR* Treey : { TEXT("tree"), TEXT("tree_small"), TEXT("tree_big") })
			{
				PropKinds.Add(FName(Treey), TEXT("tree"));
			}
			PropKinds.Add(TEXT("shrub"), TEXT("berry_bush"));
			for (const TCHAR* Fibrous : { TEXT("lowshrub"), TEXT("fern"), TEXT("sorrel") })
			{
				PropKinds.Add(FName(Fibrous), TEXT("fiber"));
			}
			PropKinds.Add(TEXT("branch"), TEXT("driftwood"));
			PropKinds.Add(TEXT("log"), TEXT("driftwood"));
			PropKinds.Add(TEXT("stone"), TEXT("stone"));
			PropKinds.Add(TEXT("flint"), TEXT("flint"));
		}
	};

	const FTable& Table()
	{
		static const FTable T;
		return T;
	}
}

FName RiptideResources::KindOfProp(FName PropKind)
{
	const FName* Kind = Table().PropKinds.Find(PropKind);
	return Kind ? *Kind : NAME_None;
}

const FRiptideResourceDef* RiptideResources::Find(FName Kind)
{
	return Table().Defs.FindByPredicate([Kind](const FRiptideResourceDef& D) { return D.Kind == Kind; });
}

float RiptideResources::HarvestSeconds(const FRiptideResourceDef& Def, const TArray<FName>& Tools)
{
	float Fastest = 1.f;
	for (const FName Tool : Tools)
	{
		if (const float* Speed = Def.ToolSpeed.Find(Tool))
		{
			Fastest = FMath::Max(Fastest, *Speed);
		}
	}
	return Def.HoldSeconds / Fastest;
}

#undef LOCTEXT_NAMESPACE
