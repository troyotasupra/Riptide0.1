#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"

class URiptideStorageComponent;

/**
 * The inventory screen, in the Godot build's Delta Force style (ui/inventory_screen.gd, grid_view.gd, item_tile.gd):
 * what you carry on the left (one grid per pocket or bag), the open container on the right, items as rarity-tinted
 * cards you drag between grids. Drawn and driven entirely here; moves are handed to OnMove (the server makes them).
 *
 * Mouse: drag to move · R rotates while dragging · Shift-drag moves half a stack · Ctrl-click sends an item across.
 * Tab, E or Esc closes.
 */
class RIPTIDE_API SRiptideInventory : public SLeafWidget
{
public:
	/** From grid (component, index, item uid) to grid (component, index, cell X, Y, rotated), Count (0 = all).
	 * X < 0 means "wherever it fits". */
	DECLARE_DELEGATE_NineParams(FOnMove, URiptideStorageComponent*, int32, int32, URiptideStorageComponent*, int32, int32, int32, bool, int32);
	/** An item (grid component, index, item uid) used (right-click: eaten, read, worn) or dropped (G over it). */
	DECLARE_DELEGATE_ThreeParams(FOnItem, URiptideStorageComponent*, int32, int32);

	SLATE_BEGIN_ARGS(SRiptideInventory) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URiptideStorageComponent>, Carrying)
		SLATE_ARGUMENT(TWeakObjectPtr<URiptideStorageComponent>, Container)
		SLATE_ARGUMENT(int32, ContainerIndex)
		SLATE_EVENT(FOnMove, OnMove)
		SLATE_EVENT(FOnItem, OnUse)
		SLATE_EVENT(FOnItem, OnDrop)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** One grid as laid out on screen. */
	struct FPanel
	{
		URiptideStorageComponent* Storage = nullptr;
		int32 Index = 0;
		FVector2f Origin;          // top-left of its cells
		FString Header;
		FVector2f HeaderPos;
		bool bContainer = false;
	};

	struct FDrag
	{
		bool bActive = false;
		URiptideStorageComponent* Storage = nullptr;
		int32 Index = 0;
		int32 Uid = 0;
		FName Id;
		bool bRotated = false;
		FIntPoint Grab;            // which of the item's cells is under the cursor
		int32 Count = 0;           // 0 = the whole stack
	};

	/** Where the drag would land: the panel, the item's top-left cell, and whether it fits, merges or is blocked. */
	struct FTarget
	{
		const FPanel* Panel = nullptr;
		FIntPoint Cell;
		enum { None, Ok, Merge, Blocked } State = None;
	};

	TArray<FPanel> Layout(const FVector2f& Screen) const;
	float CellSize(const FVector2f& Screen) const;
	const FPanel* PanelAt(const TArray<FPanel>& Panels, const FVector2f& Point, float Cell, FIntPoint& OutCell) const;
	FTarget FindTarget(const TArray<FPanel>& Panels, float Cell) const;
	void PaintItem(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FName Id, int32 Count, const FVector2f& Pos,
		const FVector2f& Size, float Alpha) const;

	TWeakObjectPtr<URiptideStorageComponent> Carrying;
	TWeakObjectPtr<URiptideStorageComponent> Container;
	int32 ContainerIndex = 0;
	FOnMove OnMove;
	FOnItem OnUse;
	FOnItem OnDrop;
	FSimpleDelegate OnClose;

	TArray<FSlateRoundedBoxBrush> CardBrushes;     // by rarity
	TUniquePtr<FSlateRoundedBoxBrush> TooltipBrush;

	FVector2f Mouse = FVector2f(-1.f, -1.f);
	FVector2f ScreenSize = FVector2f(1920.f, 1080.f);
	FDrag Drag;
};
