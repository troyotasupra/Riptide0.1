#include "RiptideCraftingComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "RiptideCharacter.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.h"
#include "RiptideWorldItem.h"

URiptideCraftingComponent::URiptideCraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void URiptideCraftingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URiptideCraftingComponent, Known);
	DOREPLIFETIME(URiptideCraftingComponent, Crafting);
	DOREPLIFETIME(URiptideCraftingComponent, CraftEnd);
	DOREPLIFETIME(URiptideCraftingComponent, CraftSeconds);
}

void URiptideCraftingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority() && Known.Num() == 0)
	{
		Known = RiptideRecipes::KnownAtStart();
	}
}

ARiptideCharacter* URiptideCraftingComponent::Crew() const
{
	return Cast<ARiptideCharacter>(GetOwner());
}

double URiptideCraftingComponent::Now() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

void URiptideCraftingComponent::Learn(const TArray<FName>& Recipes)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	bool bNew = false;
	for (const FName Recipe : Recipes)
	{
		if (RiptideRecipes::Find(Recipe) && !Known.Contains(Recipe))
		{
			Known.Add(Recipe);
			bNew = true;
		}
	}
	if (bNew)
	{
		OnChanged.Broadcast();
	}
}

bool URiptideCraftingComponent::CanCraft(FName Recipe, TArray<FRiptideNeed>& OutShort, FName& OutMissingTool) const
{
	OutShort.Reset();
	OutMissingTool = NAME_None;
	const FRiptideRecipe* R = RiptideRecipes::Find(Recipe);
	const ARiptideCharacter* Who = Crew();
	const URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	if (!R || !Carrying || !Known.Contains(Recipe))
	{
		return false;
	}
	for (const FRiptideNeed& Need : R->Needs)
	{
		int32 Have = 0;
		for (int32 Grid = 0; Grid < Carrying->Num(); ++Grid)
		{
			if (const FRiptideStorage* Storage = Carrying->GetStorage(Grid))
			{
				Have += Storage->Grid.CountOf(Need.Item);
			}
		}
		if (Have < Need.Count)
		{
			OutShort.Add({ Need.Item, Need.Count - Have });
		}
	}
	if (!R->Tool.IsNone())
	{
		bool bHaveTool = false;
		for (int32 Grid = 0; Grid < Carrying->Num() && !bHaveTool; ++Grid)
		{
			const FRiptideStorage* Storage = Carrying->GetStorage(Grid);
			bHaveTool = Storage && Storage->Grid.HasTool(R->Tool);
		}
		if (!bHaveTool)
		{
			OutMissingTool = R->Tool;
		}
	}
	return OutShort.Num() == 0 && OutMissingTool.IsNone();
}

bool URiptideCraftingComponent::CanMake(FName Recipe) const
{
	TArray<FRiptideNeed> Short;
	FName Tool;
	return CanCraft(Recipe, Short, Tool);
}

void URiptideCraftingComponent::Craft(FName Recipe)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerCraft_Implementation(Recipe);
	}
	else
	{
		ServerCraft(Recipe);
	}
}

void URiptideCraftingComponent::ServerCraft_Implementation(FName Recipe)
{
	if (IsCrafting() || !CanMake(Recipe))
	{
		return;
	}
	const FRiptideRecipe* R = RiptideRecipes::Find(Recipe);
	Crafting = Recipe;
	CraftSeconds = FMath::Max(R->Seconds, 0.1f);
	CraftEnd = Now() + CraftSeconds;
	if (ARiptideCharacter* Who = Crew())
	{
		Who->StartAction(ERiptideCrewAction::Harvest);
	}
	OnChanged.Broadcast();
}

float URiptideCraftingComponent::GetProgress() const
{
	if (!IsCrafting() || CraftSeconds <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(1.f - float(CraftEnd - Now()) / CraftSeconds, 0.f, 1.f);
}

void URiptideCraftingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority() && IsCrafting() && Now() >= CraftEnd)
	{
		Finish();
	}
}

void URiptideCraftingComponent::Finish()
{
	const FRiptideRecipe* R = RiptideRecipes::Find(Crafting);
	ARiptideCharacter* Who = Crew();
	URiptideStorageComponent* Carrying = Who ? Who->GetInventory() : nullptr;
	const FName Was = Crafting;
	Crafting = NAME_None;
	// Checked again at the end: the makings could have been dropped meanwhile.
	if (!R || !Carrying || !CanMake(Was))
	{
		OnChanged.Broadcast();
		return;
	}
	for (const FRiptideNeed& Need : R->Needs)
	{
		int32 Left = Need.Count;
		for (int32 Grid = 0; Grid < Carrying->Num() && Left > 0; ++Grid)
		{
			if (FRiptideStorage* Storage = Carrying->GetStorage(Grid))
			{
				Left = Storage->Grid.Remove(Need.Item, Left);
			}
		}
	}
	Carrying->OnChanged.Broadcast();
	const int32 Over = Who->GiveItem(R->Makes, R->Count);
	(void)Over;      // GiveItem drops what doesn't fit at the feet
	++Made;
	OnChanged.Broadcast();
}
