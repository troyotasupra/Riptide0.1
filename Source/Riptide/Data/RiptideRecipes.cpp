#include "RiptideRecipes.h"

#define LOCTEXT_NAMESPACE "RiptideRecipes"

namespace
{
	struct FTable
	{
		TArray<FRiptideRecipe> Recipes;
		TArray<FName> Start;
		TArray<FName> Book;

		FRiptideRecipe& Add(const TCHAR* Id, const FText& Name, std::initializer_list<FRiptideNeed> Needs, const TCHAR* Makes, int32 Count,
			float Seconds, const TCHAR* Tool = nullptr)
		{
			FRiptideRecipe& R = Recipes.AddDefaulted_GetRef();
			R.Id = FName(Id);
			R.Name = Name;
			R.Needs = Needs;
			R.Makes = FName(Makes);
			R.Count = Count;
			R.Seconds = Seconds;
			R.Tool = Tool ? FName(Tool) : NAME_None;
			return R;
		}

		FTable()
		{
			Add(TEXT("rope"), LOCTEXT("rope", "Rope"), { { TEXT("fiber"), 5 } }, TEXT("rope"), 1, 2.f);
			Add(TEXT("stone_hatchet"), LOCTEXT("stone_hatchet", "Stone hatchet"), { { TEXT("wood"), 1 }, { TEXT("flint"), 2 }, { TEXT("rope"), 1 } }, TEXT("stone_hatchet"), 1, 4.f);
			Add(TEXT("oar"), LOCTEXT("oar", "Oar"), { { TEXT("wood"), 2 }, { TEXT("rope"), 1 } }, TEXT("oar"), 1, 4.f, TEXT("hatchet"));
			Add(TEXT("raft_kit"), LOCTEXT("raft_kit", "Raft kit"), { { TEXT("wood"), 3 }, { TEXT("rope"), 2 } }, TEXT("raft_kit"), 1, 5.f, TEXT("hatchet"));
			Add(TEXT("sandbag"), LOCTEXT("sandbag", "Sandbag"), { { TEXT("sand"), 3 }, { TEXT("fiber"), 3 } }, TEXT("sandbag"), 1, 3.f);
			Add(TEXT("campfire_kit"), LOCTEXT("campfire_kit", "Campfire kit"), { { TEXT("stone"), 2 } }, TEXT("campfire_kit"), 1, 2.f);
			Add(TEXT("lean_to_kit"), LOCTEXT("lean_to_kit", "Lean-to kit"), { { TEXT("tarp"), 1 }, { TEXT("paracord"), 1 }, { TEXT("wood"), 2 } }, TEXT("lean_to_kit"), 1, 4.f);
			Add(TEXT("spear"), LOCTEXT("spear", "Spear"), { { TEXT("wood"), 1 }, { TEXT("flint"), 1 }, { TEXT("fiber"), 2 } }, TEXT("spear"), 1, 3.f);
			Add(TEXT("bandage"), LOCTEXT("bandage", "Bandage"), { { TEXT("fiber"), 4 } }, TEXT("bandage"), 1, 2.f);
			Add(TEXT("torch"), LOCTEXT("torch", "Torch"), { { TEXT("wood"), 1 }, { TEXT("fiber"), 2 } }, TEXT("torch"), 1, 2.f);
			Add(TEXT("tent_kit"), LOCTEXT("tent_kit", "Tent kit"), { { TEXT("wood"), 4 }, { TEXT("rope"), 1 } }, TEXT("tent_kit"), 1, 5.f, TEXT("hatchet"));
			Add(TEXT("compost_bin_kit"), LOCTEXT("compost_bin_kit", "Compost bin kit"), { { TEXT("wood"), 4 }, { TEXT("rope"), 1 } }, TEXT("compost_bin_kit"), 1, 4.f);
			Add(TEXT("drying_rack_kit"), LOCTEXT("drying_rack_kit", "Drying rack kit"), { { TEXT("wood"), 6 }, { TEXT("rope"), 3 } }, TEXT("drying_rack_kit"), 1, 5.f);
			Add(TEXT("cut_bait"), LOCTEXT("cut_bait", "Cut bait"), { { TEXT("baitfish"), 1 } }, TEXT("cut_bait"), 4, 1.5f, TEXT("knife"));
			// A castaway's rod: a straight stick, a line twisted from fibre and a hook knapped from flint.
			Add(TEXT("fishing_rod"), LOCTEXT("fishing_rod", "Fishing rod"), { { TEXT("wood"), 1 }, { TEXT("fiber"), 4 }, { TEXT("flint"), 1 } }, TEXT("fishing_rod"), 1, 4.f);
			Add(TEXT("jig"), LOCTEXT("jig", "Jig"), { { TEXT("wood"), 1 }, { TEXT("fiber"), 2 }, { TEXT("flint"), 1 } }, TEXT("jig"), 1, 3.f, TEXT("knife"));
			Add(TEXT("peg_leg"), LOCTEXT("peg_leg", "Peg leg"), { { TEXT("log"), 1 }, { TEXT("rope"), 2 } }, TEXT("peg_leg"), 1, 5.f, TEXT("knife"));
			Add(TEXT("hook_hand"), LOCTEXT("hook_hand", "Hook hand"), { { TEXT("wood"), 1 }, { TEXT("rope"), 1 }, { TEXT("lure"), 1 } }, TEXT("hook_hand"), 1, 4.f, TEXT("knife"));
			Add(TEXT("storage_crate_kit"), LOCTEXT("storage_crate_kit", "Storage crate kit"), { { TEXT("log"), 3 }, { TEXT("rope"), 2 } }, TEXT("storage_crate_kit"), 1, 5.f, TEXT("hatchet"));

			Start = { TEXT("rope"), TEXT("stone_hatchet"), TEXT("oar"), TEXT("raft_kit"), TEXT("fishing_rod") };
			Book = { TEXT("campfire_kit"), TEXT("lean_to_kit"), TEXT("spear"), TEXT("bandage"), TEXT("torch"), TEXT("cut_bait"), TEXT("jig"),
				TEXT("compost_bin_kit"), TEXT("sandbag") };
		}
	};

	const FTable& Table()
	{
		static const FTable T;
		return T;
	}
}

const TArray<FRiptideRecipe>& RiptideRecipes::All()
{
	return Table().Recipes;
}

const FRiptideRecipe* RiptideRecipes::Find(FName Id)
{
	return Table().Recipes.FindByPredicate([Id](const FRiptideRecipe& R) { return R.Id == Id; });
}

const TArray<FName>& RiptideRecipes::KnownAtStart()
{
	return Table().Start;
}

const TArray<FName>& RiptideRecipes::TaughtByBook()
{
	return Table().Book;
}

#undef LOCTEXT_NAMESPACE
