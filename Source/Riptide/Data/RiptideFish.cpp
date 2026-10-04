#include "RiptideFish.h"

#define LOCTEXT_NAMESPACE "RiptideFish"

namespace
{
	FRiptideFishDef Fish(const TCHAR* Id, const FText& Name, const TCHAR* Item, float MinKg, float MaxKg, float Fight, float Speed,
		TMap<FName, float> Habitats, TMap<FName, float> Baits, TMap<FName, float> Times)
	{
		FRiptideFishDef Def;
		Def.Id = Id;
		Def.Name = Name;
		Def.Item = Item;
		Def.MinKg = MinKg;
		Def.MaxKg = MaxKg;
		Def.Fight = Fight;
		Def.Speed = Speed;
		Def.Habitats = MoveTemp(Habitats);
		Def.Baits = MoveTemp(Baits);
		Def.Times = MoveTemp(Times);
		return Def;
	}

	struct FBait
	{
		FName Item;
		FName Kind;
		bool bReusable;
	};

	const TArray<FBait>& Baits()
	{
		static const TArray<FBait> Table = {
			{ TEXT("grub"), TEXT("grub"), false },
			{ TEXT("berries"), TEXT("berries"), false },
			{ TEXT("cut_bait"), TEXT("cut_bait"), false },
			{ TEXT("lure"), TEXT("lure"), true },
			{ TEXT("jig"), TEXT("jig"), true },
		};
		return Table;
	}
}

const TArray<FRiptideFishDef>& RiptideFish::All()
{
	static const TArray<FRiptideFishDef> Table = {
		Fish(TEXT("sardine"), LOCTEXT("Sardine", "Sardine"), TEXT("raw_sardine"), 0.06f, 0.22f, 0.12f, 1.4f,
			{ { TEXT("shore"), 3.f }, { TEXT("reef"), 1.2f }, { TEXT("deep"), 0.4f } },
			{ { TEXT("bare"), 0.5f }, { TEXT("grub"), 1.6f }, { TEXT("berries"), 0.8f }, { TEXT("cut_bait"), 0.4f } },
			{ { TEXT("dawn"), 1.3f }, { TEXT("dusk"), 1.3f }, { TEXT("night"), 0.5f } }),
		Fish(TEXT("mullet"), LOCTEXT("Mullet", "Mullet"), TEXT("raw_mullet"), 0.4f, 2.2f, 0.3f, 1.f,
			{ { TEXT("shore"), 2.4f }, { TEXT("reef"), 0.6f } },
			{ { TEXT("bare"), 0.2f }, { TEXT("grub"), 1.2f }, { TEXT("berries"), 1.8f } },
			{ { TEXT("dawn"), 1.2f }, { TEXT("night"), 0.6f } }),
		Fish(TEXT("pufferfish"), LOCTEXT("Pufferfish", "Pufferfish"), TEXT("raw_pufferfish"), 0.3f, 1.3f, 0.18f, 0.6f,
			{ { TEXT("shore"), 0.7f }, { TEXT("reef"), 1.1f } },
			{ { TEXT("bare"), 0.3f }, { TEXT("grub"), 0.9f }, { TEXT("cut_bait"), 0.6f } },
			{}),
		Fish(TEXT("snapper"), LOCTEXT("Snapper", "Red snapper"), TEXT("raw_snapper"), 1.f, 7.f, 0.5f, 1.f,
			{ { TEXT("shore"), 0.6f }, { TEXT("reef"), 2.8f }, { TEXT("deep"), 0.6f } },
			{ { TEXT("grub"), 0.8f }, { TEXT("cut_bait"), 1.6f }, { TEXT("lure"), 0.7f } },
			{ { TEXT("dawn"), 1.3f }, { TEXT("dusk"), 1.4f }, { TEXT("night"), 0.8f } }),
		Fish(TEXT("grouper"), LOCTEXT("Grouper", "Grouper"), TEXT("raw_grouper"), 3.f, 22.f, 0.78f, 0.7f,
			{ { TEXT("reef"), 1.8f }, { TEXT("deep"), 0.9f } },
			{ { TEXT("cut_bait"), 1.8f }, { TEXT("lure"), 0.5f } },
			{ { TEXT("day"), 0.6f }, { TEXT("dusk"), 1.6f }, { TEXT("night"), 1.8f } }),
		Fish(TEXT("barracuda"), LOCTEXT("Barracuda", "Barracuda"), TEXT("raw_barracuda"), 2.f, 14.f, 0.82f, 1.8f,
			{ { TEXT("reef"), 1.2f }, { TEXT("deep"), 1.4f } },
			{ { TEXT("cut_bait"), 0.5f }, { TEXT("lure"), 2.f }, { TEXT("jig"), 1.7f } },
			{ { TEXT("dawn"), 1.4f }, { TEXT("day"), 1.1f }, { TEXT("dusk"), 1.2f }, { TEXT("night"), 0.5f } }),
		Fish(TEXT("mahi_mahi"), LOCTEXT("MahiMahi", "Mahi-mahi"), TEXT("raw_mahi_mahi"), 4.f, 16.f, 0.86f, 1.6f,
			{ { TEXT("deep"), 2.4f }, { TEXT("reef"), 0.3f } },
			{ { TEXT("lure"), 1.4f }, { TEXT("jig"), 2.f }, { TEXT("cut_bait"), 0.6f } },
			{ { TEXT("dawn"), 1.2f }, { TEXT("day"), 1.3f }, { TEXT("dusk"), 0.9f }, { TEXT("night"), 0.3f } }),
		Fish(TEXT("tuna"), LOCTEXT("Tuna", "Yellowfin tuna"), TEXT("raw_tuna"), 10.f, 48.f, 1.f, 1.3f,
			{ { TEXT("deep"), 1.1f } },
			{ { TEXT("jig"), 1.4f }, { TEXT("cut_bait"), 1.f }, { TEXT("lure"), 0.8f } },
			{ { TEXT("dawn"), 1.5f }, { TEXT("day"), 0.9f }, { TEXT("dusk"), 1.3f }, { TEXT("night"), 0.4f } }),
	};
	return Table;
}

const FRiptideFishDef* RiptideFish::Find(FName Id)
{
	return All().FindByPredicate([Id](const FRiptideFishDef& D) { return D.Id == Id; });
}

FName RiptideFish::BaitKind(FName Item)
{
	const FBait* Bait = Baits().FindByPredicate([Item](const FBait& B) { return B.Item == Item; });
	return Bait ? Bait->Kind : FName(TEXT("bare"));
}

bool RiptideFish::IsBait(FName Item)
{
	return Baits().ContainsByPredicate([Item](const FBait& B) { return B.Item == Item; });
}

bool RiptideFish::IsBaitReusable(FName Item)
{
	const FBait* Bait = Baits().FindByPredicate([Item](const FBait& B) { return B.Item == Item; });
	return Bait && Bait->bReusable;
}

const TArray<FName>& RiptideFish::BaitItems()
{
	static const TArray<FName> Items = []()
	{
		TArray<FName> Out;
		for (const FBait& B : Baits())
		{
			Out.Add(B.Item);
		}
		return Out;
	}();
	return Items;
}

FName RiptideFish::SpotOf(float WaterDepthMetres)
{
	return WaterDepthMetres < ShoreDepth ? FName(TEXT("shore")) : WaterDepthMetres < ReefDepth ? FName(TEXT("reef")) : FName(TEXT("deep"));
}

FName RiptideFish::TimeBand(float Hours)
{
	if (Hours >= 5.f && Hours < 8.f)
	{
		return TEXT("dawn");
	}
	if (Hours >= 8.f && Hours < 17.f)
	{
		return TEXT("day");
	}
	if (Hours >= 17.f && Hours < 20.f)
	{
		return TEXT("dusk");
	}
	return TEXT("night");
}

TMap<FName, float> RiptideFish::Weights(FName Spot, FName BaitKind, FName Band)
{
	TMap<FName, float> Out;
	for (const FRiptideFishDef& Def : All())
	{
		const float* Habitat = Def.Habitats.Find(Spot);
		const float* Appetite = Def.Baits.Find(BaitKind);
		const float* Time = Def.Times.Find(Band);
		const float W = (Habitat ? *Habitat : 0.f) * (Appetite ? *Appetite : 0.f) * (Time ? *Time : 1.f);
		if (W > 0.f)
		{
			Out.Add(Def.Id, W);
		}
	}
	return Out;
}

FName RiptideFish::Pick(const TMap<FName, float>& Odds, float Roll)
{
	float Total = 0.f;
	for (const auto& Pair : Odds)
	{
		Total += Pair.Value;
	}
	if (Total <= 0.f)
	{
		return NAME_None;
	}
	float Target = FMath::Clamp(Roll, 0.f, 0.9999f) * Total;
	FName Last;
	for (const auto& Pair : Odds)
	{
		Target -= Pair.Value;
		Last = Pair.Key;
		if (Target < 0.f)
		{
			return Pair.Key;
		}
	}
	return Last;
}

float RiptideFish::BiteSeconds(const TMap<FName, float>& Odds, float Roll)
{
	float Total = 0.f;
	for (const auto& Pair : Odds)
	{
		Total += Pair.Value;
	}
	if (Total <= 0.f)
	{
		return 1.0e6f;
	}
	return FMath::Lerp(4.f, 14.f, FMath::Clamp(Roll, 0.f, 1.f)) / FMath::Clamp(0.4f + Total * 0.25f, 0.5f, 2.5f);
}

float RiptideFish::RollKg(const FRiptideFishDef& Fish, float Roll)
{
	return FMath::Lerp(Fish.MinKg, Fish.MaxKg, Roll * Roll);
}

float RiptideFish::Strength(const FRiptideFishDef& Fish, float Kg)
{
	const float Size = FMath::GetRangePct(Fish.MinKg, Fish.MaxKg, Kg);
	return Fish.Fight * FMath::Lerp(0.75f, 1.15f, FMath::Clamp(Size, 0.f, 1.f));
}

FRiptideFishFight RiptideFish::NewFight(float Distance)
{
	FRiptideFishFight Fight;
	Fight.Distance = Distance;
	Fight.Start = Distance;
	return Fight;
}

ERiptideFightResult RiptideFish::Step(FRiptideFishFight& Fight, bool bReeling, float Dt, float Surge, float PullStrength)
{
	const float Pull = PullStrength * FMath::Max(0.f, Surge) * (Fight.bShark ? SharkPull : 1.f);
	float Tension = Fight.Tension;
	if (bReeling)
	{
		Tension += (TensionReel + Pull * 0.9f) * Dt;
		Fight.Distance += (-ReelSpeed * (1.f - 0.5f * Tension) + Pull * 1.2f) * Dt;
	}
	else
	{
		Tension += (-TensionEase + Pull * 0.25f) * Dt;
		Fight.Distance += Pull * RunSpeed * Dt;
	}
	Fight.Tension = FMath::Clamp(Tension, 0.f, 1.2f);
	Fight.Slack = Fight.Tension < 0.04f ? Fight.Slack + Dt : 0.f;
	if (Fight.Tension >= 1.f)
	{
		return ERiptideFightResult::Snapped;
	}
	if (Fight.Distance >= Fight.Start + EscapeExtra || Fight.Slack >= SlackEscape)
	{
		return ERiptideFightResult::Escaped;
	}
	if (Fight.Distance <= LandDistance)
	{
		return ERiptideFightResult::Landed;
	}
	return ERiptideFightResult::None;
}

float RiptideFish::SurgeAt(float Time, float Speed)
{
	return FMath::Clamp(0.55f + 0.45f * FMath::Sin(Time * Speed * 3.1f) + 0.3f * FMath::Sin(Time * Speed * 7.3f + 1.7f), 0.f, 1.3f);
}

#undef LOCTEXT_NAMESPACE
