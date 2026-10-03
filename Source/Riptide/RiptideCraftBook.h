#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ARiptideCharacter;
class SRiptideButton;
class SVerticalBox;

/**
 * The crafting book (B): every recipe the crew member knows down the left, as cards; the chosen one's makings on
 * the right with what's carried against what's needed, the tool it takes, and a Make button that greys out until
 * the makings are there. Redraws itself whenever the pockets or the recipes change. Drawn in C++ like the menus.
 */
class RIPTIDE_API SRiptideCraftBook : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRiptideCraftBook) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARiptideCharacter>, Crew)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Lays the book out again from the crew member's current recipes and pockets. */
	void Refresh();

private:
	TWeakObjectPtr<ARiptideCharacter> Crew;
	FSimpleDelegate OnClose;
	FName Chosen;
	TSharedPtr<SVerticalBox> RecipeList;
	TSharedPtr<SVerticalBox> Detail;
	FDelegateHandle InventoryWatch;
	FDelegateHandle CraftingWatch;

	void FillRecipes();
	void FillDetail();
	void Make();
};
