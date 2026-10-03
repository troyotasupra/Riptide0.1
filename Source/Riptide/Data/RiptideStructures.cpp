#include "RiptideStructures.h"

#define LOCTEXT_NAMESPACE "RiptideStructures"

namespace
{
	struct FTable
	{
		TArray<FRiptideStructureDef> Defs;

		FRiptideStructureDef& Add(const TCHAR* Type, const FText& Name, std::initializer_list<TArray<FRiptideNeed>> Stages, float Hp)
		{
			FRiptideStructureDef& D = Defs.AddDefaulted_GetRef();
			D.Type = FName(Type);
			D.Name = Name;
			D.Stages = Stages;
			D.Hp = Hp;
			return D;
		}

		FTable()
		{
			FRiptideStructureDef& Fire = Add(TEXT("campfire"), LOCTEXT("campfire", "Campfire"), { { { TEXT("stone"), 4 } }, { { TEXT("wood"), 3 } } }, 60.f);
			Fire.Station = ERiptideStationKind::Cook;
			Fire.Warmth = 12.f;
			Fire.WarmRadius = 450.f;
			Fire.FootprintRadius = 90.f;
			FRiptideStructureDef& LeanTo = Add(TEXT("lean_to"), LOCTEXT("lean_to", "Lean-to"), {}, 80.f);
			LeanTo.Warmth = 5.f;
			LeanTo.WarmRadius = 200.f;
			LeanTo.FootprintRadius = 160.f;
			FRiptideStructureDef& Tent = Add(TEXT("tent"), LOCTEXT("tent", "Tent"), { { { TEXT("tarp"), 1 } }, { { TEXT("rope"), 3 } } }, 70.f);
			Tent.Warmth = 8.f;
			Tent.WarmRadius = 200.f;
			Tent.bBed = true;
			Tent.FootprintRadius = 180.f;
			FRiptideStructureDef& Compost = Add(TEXT("compost_bin"), LOCTEXT("compost_bin", "Compost bin"), {}, 60.f);
			Compost.Station = ERiptideStationKind::Compost;
			FRiptideStructureDef& Rack = Add(TEXT("drying_rack"), LOCTEXT("drying_rack", "Drying rack"), {}, 70.f);
			Rack.Station = ERiptideStationKind::Dry;
			Rack.FootprintRadius = 120.f;
			FRiptideStructureDef& Wall = Add(TEXT("sandbag_wall"), LOCTEXT("sandbag_wall", "Sandbag wall"), { { { TEXT("sandbag"), 2 } }, { { TEXT("sandbag"), 3 } } }, 240.f);
			Wall.FootprintRadius = 120.f;
			FRiptideStructureDef& Crate = Add(TEXT("storage_crate"), LOCTEXT("storage_crate", "Storage crate"), {}, 150.f);
			Crate.Container = FIntPoint(8, 5);
			Crate.FootprintRadius = 70.f;
			FRiptideStructureDef& Raft = Add(TEXT("raft_site"), LOCTEXT("raft_site", "Raft site"), { { { TEXT("log"), 6 } }, { { TEXT("rope"), 3 } } }, 120.f);
			Raft.bShore = true;
			Raft.FootprintRadius = 220.f;
			Raft.Launches = TEXT("raft");
		}
	};

	const FTable& Table()
	{
		static const FTable T;
		return T;
	}
}

const TArray<FRiptideStructureDef>& RiptideStructures::All()
{
	return Table().Defs;
}

const FRiptideStructureDef* RiptideStructures::Find(FName Type)
{
	return Table().Defs.FindByPredicate([Type](const FRiptideStructureDef& D) { return D.Type == Type; });
}

#undef LOCTEXT_NAMESPACE
