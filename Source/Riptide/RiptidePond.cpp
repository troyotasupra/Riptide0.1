#include "RiptidePond.h"
#include "Components/StaticMeshComponent.h"
#include "RiptideCharacter.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.h"
#include "RiptideSurvivalComponent.h"

#define LOCTEXT_NAMESPACE "RiptidePond"

ARiptidePond::ARiptidePond()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	SetRootComponent(Water);
	// Looked at, never walked on: the crew wade into the hollow.
	Water->SetCollisionProfileName(TEXT("RiptideHarvest"));
	Water->SetMobility(EComponentMobility::Static);
	Water->SetCastShadow(false);
}

namespace
{
	/** The first empty canteen carried: which pocket and which stack. */
	bool FindEmptyCanteen(const ARiptideCharacter* Who, int32& OutGrid, int32& OutUid)
	{
		const URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
		for (int32 Grid = 0; Carrying && Grid < Carrying->Num(); ++Grid)
		{
			const FRiptideStorage* Pocket = Carrying->GetStorage(Grid);
			if (!Pocket)
			{
				continue;
			}
			for (const FRiptideItem& Item : Pocket->Grid.Items)
			{
				const FRiptideItemDef* Def = RiptideItems::Find(Item.Id);
				if (Def && Def->IsTool(TEXT("canteen")))
				{
					OutGrid = Grid;
					OutUid = Item.Uid;
					return true;
				}
			}
		}
		return false;
	}
}

bool ARiptidePond::GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const
{
	int32 Grid, Uid;
	if (FindEmptyCanteen(Who, Grid, Uid))
	{
		Out.Prompt = LOCTEXT("Fill", "Fill the canteen");
		Out.Verb = VerbFill;
		Out.HoldSeconds = 1.5f;
		return true;
	}
	Out.Prompt = LOCTEXT("Drink", "Drink (unboiled)");
	Out.Verb = VerbDrink;
	Out.HoldSeconds = 1.5f;
	return true;
}

void ARiptidePond::Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb)
{
	if (!HasAuthority() || !Who)
	{
		return;
	}
	if (Verb == VerbFill)
	{
		int32 Grid, Uid;
		if (!FindEmptyCanteen(Who, Grid, Uid))
		{
			return;
		}
		FRiptideStorage* Pocket = Who->GetInventory()->GetStorage(Grid);
		FRiptideItem* Canteen = Pocket ? Pocket->Grid.Get(Uid) : nullptr;
		const FRiptideItemDef* Full = RiptideItems::Find(TEXT("canteen_dirty"));
		if (!Canteen || !Full || !Full->Food.IsSet())
		{
			return;
		}
		Canteen->Id = Full->Id;
		Canteen->Charges = Full->Food->Sips;
		Who->GetInventory()->OnChanged.Broadcast();
		Who->StartAction(ERiptideCrewAction::Reach);
		return;
	}
	if (URiptideSurvivalComponent* Survival = Who->GetSurvival())
	{
		Survival->Consume(0.f, DrinkWater, DrinkSickSeconds, DrinkSickChance);
	}
	Who->StartAction(ERiptideCrewAction::Consume);
}

#undef LOCTEXT_NAMESPACE
