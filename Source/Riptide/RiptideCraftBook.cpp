#include "RiptideCraftBook.h"

#include "Data/RiptideRecipes.h"
#include "RiptideCharacter.h"
#include "RiptideCraftingComponent.h"
#include "RiptideItems.h"
#include "RiptideMenuWidgets.h"
#include "RiptideStorageComponent.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RiptideCraftBook"

void SRiptideCraftBook::Construct(const FArguments& InArgs)
{
	Crew = InArgs._Crew;
	OnClose = InArgs._OnClose;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SRiptideBackdrop).Full(true).Strength(0.75f)
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(980.f).HeightOverride(640.f)
			[
				SNew(SImage).Image(RiptideMenuStyle::PanelBrush())
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(940.f).HeightOverride(600.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
				[
					SNew(STextBlock).Text(LOCTEXT("Title", "SURVIVAL BOOK")).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Condensed, 28, 2))
						.ColorAndOpacity(RiptideMenuStyle::Heading())
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 20.f, 0.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(RecipeList, SVerticalBox)
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SAssignNew(Detail, SVerticalBox)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("Hint", "Click a recipe · Make · B or Esc closes")).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Regular, 12))
						.ColorAndOpacity(RiptideMenuStyle::Dim())
				]
			]
		]
	];

	if (ARiptideCharacter* Who = Crew.Get())
	{
		if (URiptideStorageComponent* Inventory = Who->GetInventory())
		{
			InventoryWatch = Inventory->OnChanged.AddSP(this, &SRiptideCraftBook::Refresh);
		}
		if (URiptideCraftingComponent* Crafting = Who->GetCrafting())
		{
			CraftingWatch = Crafting->OnChanged.AddSP(this, &SRiptideCraftBook::Refresh);
			const TArray<FName> Known = Crafting->GetKnownRecipes();
			Chosen = Known.Num() ? Known[0] : NAME_None;
		}
	}
	Refresh();
}

void SRiptideCraftBook::Refresh()
{
	FillRecipes();
	FillDetail();
}

void SRiptideCraftBook::FillRecipes()
{
	RecipeList->ClearChildren();
	const ARiptideCharacter* Who = Crew.Get();
	const URiptideCraftingComponent* Crafting = Who ? Who->GetCrafting() : nullptr;
	if (!Crafting)
	{
		return;
	}
	for (const FRiptideRecipe& R : RiptideRecipes::All())
	{
		if (!Crafting->Knows(R.Id))
		{
			continue;
		}
		const FName Id = R.Id;
		const bool bCan = Crafting->CanMake(Id);
		RecipeList->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SRiptideButton).Width(340.f).Height(50.f).FontSize(18)
				.Text(R.Name)
				.Detail(bCan ? LOCTEXT("Ready", "the makings are in your pockets") : LOCTEXT("Short", "short of something"))
				.Selected_Lambda([this, Id]() { return Chosen == Id; })
				.OnClicked(FSimpleDelegate::CreateLambda([this, Id]() { Chosen = Id; FillDetail(); }))
		];
	}
}

void SRiptideCraftBook::FillDetail()
{
	Detail->ClearChildren();
	const ARiptideCharacter* Who = Crew.Get();
	URiptideCraftingComponent* Crafting = Who ? Who->GetCrafting() : nullptr;
	const FRiptideRecipe* R = RiptideRecipes::Find(Chosen);
	if (!Crafting || !R)
	{
		Detail->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(LOCTEXT("Nothing", "Nothing to make yet. Read what you find.")).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Regular, 16))
				.ColorAndOpacity(RiptideMenuStyle::Dim())
		];
		return;
	}
	const FRiptideItemDef* Product = RiptideItems::Find(R->Makes);
	Detail->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(STextBlock).Text(R->Name).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Condensed, 26, 1)).ColorAndOpacity(RiptideMenuStyle::Ink())
	];
	if (Product && !Product->Hint.IsEmpty())
	{
		Detail->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(STextBlock).Text(Product->Hint).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Regular, 14)).ColorAndOpacity(RiptideMenuStyle::Dim()).AutoWrapText(true)
		];
	}
	Detail->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock).Text(LOCTEXT("Needs", "NEEDS")).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Condensed, 14, 2)).ColorAndOpacity(RiptideMenuStyle::Heading())
	];
	TArray<FRiptideNeed> Short;
	FName MissingTool;
	const bool bCan = Crafting->CanCraft(Chosen, Short, MissingTool);
	const URiptideStorageComponent* Carrying = Who->GetInventory();
	for (const FRiptideNeed& Need : R->Needs)
	{
		int32 Have = 0;
		for (int32 Grid = 0; Carrying && Grid < Carrying->Num(); ++Grid)
		{
			if (const FRiptideStorage* Storage = Carrying->GetStorage(Grid))
			{
				Have += Storage->Grid.CountOf(Need.Item);
			}
		}
		const FRiptideItemDef* Def = RiptideItems::Find(Need.Item);
		const FText Name = Def ? Def->Name : FText::FromName(Need.Item);
		const bool bEnough = Have >= Need.Count;
		Detail->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(STextBlock).Text(FText::Format(LOCTEXT("NeedRow", "{0}   {1} / {2}"), Name, Have, Need.Count))
				.Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Regular, 16))
				.ColorAndOpacity(bEnough ? RiptideMenuStyle::Ink() : RiptideMenuStyle::Warning())
		];
	}
	if (!R->Tool.IsNone())
	{
		Detail->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
		[
			SNew(STextBlock).Text(FText::Format(LOCTEXT("ToolRow", "With a {0} in hand"), FText::FromName(R->Tool)))
				.Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Regular, 14))
				.ColorAndOpacity(MissingTool.IsNone() ? RiptideMenuStyle::Dim() : RiptideMenuStyle::Warning())
		];
	}
	const bool bBusy = Crafting->IsCrafting();
	Detail->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
	[
		SNew(SRiptideButton).Width(260.f).Height(54.f).FontSize(20).Centred(true).Primary(bCan && !bBusy)
			.Text(bBusy ? LOCTEXT("Making", "MAKING...") : bCan ? LOCTEXT("Make", "MAKE") : LOCTEXT("CantMake", "SHORT OF THE MAKINGS"))
			.OnClicked(FSimpleDelegate::CreateSP(this, &SRiptideCraftBook::Make))
	];
	if (bBusy)
	{
		Detail->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text_Lambda([this]()
			{
				const ARiptideCharacter* C = Crew.Get();
				const URiptideCraftingComponent* K = C ? C->GetCrafting() : nullptr;
				return K && K->IsCrafting() ? FText::Format(LOCTEXT("Progress", "{0}%"), FMath::RoundToInt(K->GetProgress() * 100.f)) : LOCTEXT("Done", "Done");
			}).Font(RiptideMenuStyle::Font(RiptideMenuStyle::EFont::Bold, 16)).ColorAndOpacity(RiptideMenuStyle::Accent())
		];
	}
}

void SRiptideCraftBook::Make()
{
	ARiptideCharacter* Who = Crew.Get();
	URiptideCraftingComponent* Crafting = Who ? Who->GetCrafting() : nullptr;
	if (Crafting && !Chosen.IsNone() && Crafting->CanMake(Chosen) && !Crafting->IsCrafting())
	{
		Crafting->Craft(Chosen);
	}
}

FReply SRiptideCraftBook::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::B || InKeyEvent.GetKey() == EKeys::Tab || RiptideMenuStyle::IsBackKey(InKeyEvent))
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
