#include "RiptideInventoryWidget.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "RiptideInventory"

namespace
{
	/** The Godot build's colours are sRGB (as a colour picker shows them); Slate draws in linear colour. */
	FLinearColor Srgb(float R, float G, float B, float A = 1.f)
	{
		FLinearColor C(FColor(uint8(R * 255.f + 0.5f), uint8(G * 255.f + 0.5f), uint8(B * 255.f + 0.5f)));
		C.A = A;
		return C;
	}
	FLinearColor Srgb(const FLinearColor& C) { return Srgb(C.R, C.G, C.B, C.A); }

	// The Godot build's colours (inventory_screen.gd, grid_view.gd).
	const FLinearColor Shade = Srgb(0.01f, 0.03f, 0.05f, 0.78f);
	const FLinearColor Heading = Srgb(0.75f, 0.85f, 0.95f);
	const FLinearColor GridBack = Srgb(0.03f, 0.05f, 0.06f, 0.9f);
	const FLinearColor CellColour = Srgb(0.13f, 0.16f, 0.19f, 0.95f);
	const FLinearColor GridEdge = Srgb(0.35f, 0.42f, 0.48f, 0.8f);
	const FLinearColor DropOk = Srgb(0.3f, 0.9f, 0.4f, 0.28f);
	const FLinearColor DropMerge = Srgb(0.35f, 0.65f, 1.f, 0.3f);
	const FLinearColor DropBlocked = Srgb(1.f, 0.25f, 0.2f, 0.3f);

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	FVector2f Measure(const FString& Text, const FSlateFontInfo& FontInfo)
	{
		if (FSlateApplication::IsInitialized())
		{
			return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, FontInfo));
		}
		return FVector2f(FontInfo.Size * 0.6f * Text.Len(), FontInfo.Size * 1.3f);
	}

	void FillRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& Colour)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), FCoreStyle::Get().GetBrush("WhiteBrush"),
			ESlateDrawEffect::None, Colour);
	}

	void Outline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
		const FLinearColor& Colour, float Thickness)
	{
		TArray<FVector2f> Points = { Pos, Pos + FVector2f(Size.X, 0.f), Pos + Size, Pos + FVector2f(0.f, Size.Y), Pos };
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Colour, true, Thickness);
	}

	void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& String, const FSlateFontInfo& FontInfo,
		const FVector2f& Pos, const FLinearColor& Colour, bool bCentre = false)
	{
		const FVector2f Size = Measure(String, FontInfo);
		const FVector2f At = bCentre ? Pos - FVector2f(Size.X * 0.5f, 0.f) : Pos;
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(At)), String, FontInfo, ESlateDrawEffect::None, Colour);
	}

	/** Breaks text into lines no wider than Width. */
	TArray<FString> Wrap(const FString& String, const FSlateFontInfo& FontInfo, float Width)
	{
		TArray<FString> Words, Lines;
		String.ParseIntoArray(Words, TEXT(" "));
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Try = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			if (!Line.IsEmpty() && Measure(Try, FontInfo).X > Width)
			{
				Lines.Add(Line);
				Line = Word;
			}
			else
			{
				Line = Try;
			}
		}
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
		return Lines;
	}
}

void SRiptideInventory::Construct(const FArguments& InArgs)
{
	Carrying = InArgs._Carrying;
	Container = InArgs._Container;
	ContainerIndex = InArgs._ContainerIndex;
	OnMove = InArgs._OnMove;
	OnClose = InArgs._OnClose;
	ForceVolatile(true);

	// Item cards: dark fill in the rarity's colour, edged with the colour itself (item_tile.gd).
	for (int32 Rarity = 0; Rarity < 6; ++Rarity)
	{
		const FLinearColor Colour = RiptideItems::RarityColour(Rarity);
		CardBrushes.Emplace(Srgb(Colour.R * 0.3f, Colour.G * 0.3f, Colour.B * 0.3f, 0.95f), 4.f, Srgb(Colour), 2.f);
	}
	TooltipBrush = MakeUnique<FSlateRoundedBoxBrush>(Srgb(0.03f, 0.05f, 0.07f, 0.96f), 4.f, Srgb(0.4f, 0.46f, 0.52f), 1.f);
}

float SRiptideInventory::CellSize(const FVector2f& Screen) const
{
	return FMath::Clamp(FMath::FloorToFloat(Screen.Y / 15.5f), 40.f, 58.f);
}

TArray<SRiptideInventory::FPanel> SRiptideInventory::Layout(const FVector2f& Screen) const
{
	TArray<FPanel> Panels;
	const float Cell = CellSize(Screen);
	const URiptideStorageComponent* Carry = Carrying.Get();
	URiptideStorageComponent* Box = Container.Get();

	// Two columns side by side, centred: what you carry, then the container.
	float CarryW = 0.f, CarryH = 0.f;
	if (Carry)
	{
		for (int32 i = 0; i < Carry->Num(); ++i)
		{
			const FRiptideItemGrid& Grid = Carry->GetStorage(i)->Grid;
			CarryW = FMath::Max(CarryW, Grid.Width * Cell);
			CarryH += 24.f + Grid.Height * Cell + 12.f;
		}
	}
	const FRiptideStorage* Open = Box ? Box->GetStorage(ContainerIndex) : nullptr;
	const float BoxW = Open ? Open->Grid.Width * Cell : 260.f;
	const float Gap = 48.f;
	const float Left = (Screen.X - (CarryW + Gap + BoxW)) * 0.5f;
	const float Top = FMath::Max(90.f, (Screen.Y - FMath::Max(CarryH, Open ? Open->Grid.Height * Cell + 24.f : 0.f)) * 0.5f);

	float Y = Top + 30.f;
	if (Carry)
	{
		for (int32 i = 0; i < Carry->Num(); ++i)
		{
			const FRiptideStorage* Storage = Carry->GetStorage(i);
			FPanel& P = Panels.AddDefaulted_GetRef();
			P.Storage = const_cast<URiptideStorageComponent*>(Carry);
			P.Index = i;
			P.Header = FString::Printf(TEXT("%s  %d×%d"), *Storage->Title.ToString(), Storage->Grid.Width, Storage->Grid.Height);
			P.HeaderPos = FVector2f(Left, Y);
			P.Origin = FVector2f(Left, Y + 22.f);
			Y = P.Origin.Y + Storage->Grid.Height * Cell + 14.f;
		}
	}
	if (Open)
	{
		FPanel& P = Panels.AddDefaulted_GetRef();
		P.Storage = Box;
		P.Index = ContainerIndex;
		P.bContainer = true;
		P.Header = FString::Printf(TEXT("%d×%d"), Open->Grid.Width, Open->Grid.Height);
		P.HeaderPos = FVector2f(Left + CarryW + Gap, Top + 30.f);
		P.Origin = FVector2f(Left + CarryW + Gap, Top + 52.f);
	}
	return Panels;
}

const SRiptideInventory::FPanel* SRiptideInventory::PanelAt(const TArray<FPanel>& Panels, const FVector2f& Point, float Cell, FIntPoint& OutCell) const
{
	for (const FPanel& P : Panels)
	{
		const FRiptideItemGrid& Grid = P.Storage->GetStorage(P.Index)->Grid;
		const FVector2f Local = (Point - P.Origin) / Cell;
		if (Local.X >= 0.f && Local.Y >= 0.f && Local.X < Grid.Width && Local.Y < Grid.Height)
		{
			OutCell = FIntPoint(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y));
			return &P;
		}
	}
	return nullptr;
}

SRiptideInventory::FTarget SRiptideInventory::FindTarget(const TArray<FPanel>& Panels, float Cell) const
{
	FTarget Target;
	if (!Drag.bActive)
	{
		return Target;
	}
	FIntPoint Under;
	Target.Panel = PanelAt(Panels, Mouse, Cell, Under);
	if (!Target.Panel)
	{
		return Target;
	}
	Target.Cell = Under - Drag.Grab;
	const FRiptideItemGrid& Grid = Target.Panel->Storage->GetStorage(Target.Panel->Index)->Grid;
	const bool bSame = Target.Panel->Storage == Drag.Storage && Target.Panel->Index == Drag.Index;
	// Its own cells are free to drop back on only when the whole stack is moving (Count 0); half a stack leaves the
	// rest behind in them.
	const int32 Ignore = bSame && Drag.Count == 0 ? Drag.Uid : 0;
	if (Grid.Fits(Drag.Id, Target.Cell.X, Target.Cell.Y, Drag.bRotated, Ignore))
	{
		Target.State = FTarget::Ok;
	}
	else if (const FRiptideItem* Onto = Grid.InBounds(Drag.Id, Target.Cell.X, Target.Cell.Y, Drag.bRotated)
		? Grid.SingleOverlap(Drag.Id, Target.Cell.X, Target.Cell.Y, Drag.bRotated, Ignore) : nullptr)
	{
		const FRiptideItemDef* Def = RiptideItems::Find(Drag.Id);
		Target.State = Def && Onto->Id == Drag.Id && Onto->Count < Def->Stack ? FTarget::Merge : FTarget::Blocked;
	}
	else
	{
		Target.State = FTarget::Blocked;
	}
	return Target;
}

void SRiptideInventory::PaintItem(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FName Id, int32 Count,
	const FVector2f& Pos, const FVector2f& Size, float Alpha) const
{
	const FRiptideItemDef* Def = RiptideItems::Find(Id);
	const int32 Rarity = Def ? Def->Rarity : 0;
	const FLinearColor Colour = RiptideItems::RarityColour(Rarity);
	// Card, a glow along its lower half, the item's name, and the stack count bottom right (item_tile.gd).
	// (A box is filled with the tint it's drawn with, not its brush's colour.)
	FLinearColor Fill = CardBrushes[Rarity].TintColor.GetSpecifiedColor();
	Fill.A *= Alpha;
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size - FVector2f(4.f), FSlateLayoutTransform(Pos + FVector2f(2.f))),
		&CardBrushes[Rarity], ESlateDrawEffect::None, Fill);
	FillRect(Out, Layer + 1, G, Pos + FVector2f(4.f, Size.Y * 0.55f), FVector2f(Size.X - 8.f, Size.Y * 0.45f - 4.f), FLinearColor(Colour.R, Colour.G, Colour.B, 0.18f * Alpha));
	const FSlateFontInfo NameFont = Font(10, true);
	const TArray<FString> Lines = Wrap(Def ? Def->Name.ToString() : Id.ToString(), NameFont, Size.X - 10.f);
	const float LineH = NameFont.Size * 1.35f;
	float Y = Pos.Y + (Size.Y - LineH * Lines.Num()) * 0.5f;
	for (const FString& Line : Lines)
	{
		Text(Out, Layer + 2, G, Line, NameFont, FVector2f(Pos.X + Size.X * 0.5f, Y), FLinearColor(0.92f, 0.94f, 0.96f, Alpha), true);
		Y += LineH;
	}
	if (Count > 1)
	{
		const FString CountText = FString::Printf(TEXT("×%d"), Count);
		FSlateFontInfo CountFont = Font(11, true);
		CountFont.OutlineSettings.OutlineSize = 2;
		CountFont.OutlineSettings.OutlineColor = FLinearColor::Black;
		const FVector2f CountSize = Measure(CountText, CountFont);
		Text(Out, Layer + 3, G, CountText, CountFont, Pos + Size - CountSize - FVector2f(6.f, 3.f), FLinearColor(1.f, 1.f, 1.f, Alpha));
	}
}

int32 SRiptideInventory::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FGeometry& G = AllottedGeometry;
	const FVector2f Screen = FVector2f(G.GetLocalSize());
	const_cast<SRiptideInventory*>(this)->ScreenSize = Screen;
	const float Cell = CellSize(Screen);
	const TArray<FPanel> Panels = Layout(Screen);
	int32 L = LayerId;

	FillRect(Out, L, G, FVector2f(0.f), Screen, Shade);
	Text(Out, L + 1, G, TEXT("INVENTORY"), Font(24, true), FVector2f(Screen.X * 0.5f, 22.f), Srgb(0.92f, 0.94f, 0.96f), true);

	// Column headings.
	for (const FPanel& P : Panels)
	{
		if (P.bContainer || P.Index == 0)
		{
			const FString Column = P.bContainer ? P.Storage->GetStorage(P.Index)->Title.ToString().ToUpper() : TEXT("CARRYING");
			Text(Out, L + 1, G, Column, Font(14, true), P.HeaderPos - FVector2f(0.f, 28.f), Heading);
		}
		Text(Out, L + 1, G, P.Header, Font(11), P.HeaderPos, Srgb(0.8f, 0.85f, 0.9f, 0.85f));
	}
	if (!Container.IsValid())
	{
		Text(Out, L + 1, G, LOCTEXT("Nearby", "Open a locker to move things in and out.").ToString(), Font(12),
			FVector2f(Screen.X * 0.5f + 60.f, Screen.Y * 0.4f), Srgb(1.f, 1.f, 1.f, 0.55f));
	}

	// Grids and their items.
	const FTarget Target = FindTarget(Panels, Cell);
	FString Tip;
	const FRiptideItem* Hovered = nullptr;
	for (const FPanel& P : Panels)
	{
		const FRiptideItemGrid& Grid = P.Storage->GetStorage(P.Index)->Grid;
		const FVector2f Size(Grid.Width * Cell, Grid.Height * Cell);
		FillRect(Out, L + 1, G, P.Origin, Size, GridBack);
		for (int32 Y = 0; Y < Grid.Height; ++Y)
		{
			for (int32 X = 0; X < Grid.Width; ++X)
			{
				FillRect(Out, L + 2, G, P.Origin + FVector2f(X * Cell + 1.f, Y * Cell + 1.f), FVector2f(Cell - 2.f), CellColour);
			}
		}
		Outline(Out, L + 3, G, P.Origin, Size, GridEdge, 1.5f);
		for (const FRiptideItem& Item : Grid.Items)
		{
			const FIntRect R = FRiptideItemGrid::RectOf(Item);
			const bool bDragged = Drag.bActive && Drag.Storage == P.Storage && Drag.Index == P.Index && Drag.Uid == Item.Uid;
			const FVector2f Pos = P.Origin + FVector2f(R.Min.X, R.Min.Y) * Cell;
			const FVector2f ItemSize = FVector2f(R.Width(), R.Height()) * Cell;
			PaintItem(Out, L + 4, G, Item.Id, bDragged && Drag.Count > 0 ? Item.Count - Drag.Count : Item.Count, Pos, ItemSize,
				bDragged && Drag.Count == 0 ? 0.3f : 1.f);
			if (!Drag.bActive && Mouse.X >= Pos.X && Mouse.Y >= Pos.Y && Mouse.X < Pos.X + ItemSize.X && Mouse.Y < Pos.Y + ItemSize.Y)
			{
				Hovered = &Item;
			}
		}
		if (Target.Panel == &P && Target.State != FTarget::None)
		{
			const FIntPoint Foot = FRiptideItemGrid::Footprint(Drag.Id, Drag.bRotated);
			const FLinearColor Colour = Target.State == FTarget::Ok ? DropOk : Target.State == FTarget::Merge ? DropMerge : DropBlocked;
			const FVector2f Pos = P.Origin + FVector2f(Target.Cell.X, Target.Cell.Y) * Cell;
			FillRect(Out, L + 8, G, Pos, FVector2f(Foot.X, Foot.Y) * Cell, Colour);
			Outline(Out, L + 8, G, Pos, FVector2f(Foot.X, Foot.Y) * Cell, FLinearColor(Colour.R, Colour.G, Colour.B, 1.f), 2.f);
		}
	}

	// The dragged item follows the cursor.
	if (Drag.bActive)
	{
		const FIntPoint Foot = FRiptideItemGrid::Footprint(Drag.Id, Drag.bRotated);
		const FVector2f Pos = Mouse - (FVector2f(Drag.Grab.X, Drag.Grab.Y) + FVector2f(0.5f)) * Cell;
		const FRiptideItem* Source = Drag.Storage ? Drag.Storage->GetStorage(Drag.Index)->Grid.Get(Drag.Uid) : nullptr;
		const int32 Count = Drag.Count > 0 ? Drag.Count : Source ? Source->Count : 1;
		PaintItem(Out, L + 10, G, Drag.Id, Count, Pos, FVector2f(Foot.X, Foot.Y) * Cell, 0.85f);
	}

	// Tooltip for the item under the cursor (inventory_screen.gd's tooltip).
	if (Hovered)
	{
		const FRiptideItemDef* Def = RiptideItems::Find(Hovered->Id);
		if (Def)
		{
			const FSlateFontInfo NameFont = Font(15, true), Body = Font(11);
			const FIntPoint Size = Def->Size;
			const FString Info = FString::Printf(TEXT("%s · %s · %d×%d · %.2f kg"), *RiptideItems::RarityName(Def->Rarity).ToString(),
				*Def->Category.ToString(), Size.X, Size.Y, Def->WeightKg * Hovered->Count);
			const TArray<FString> HintLines = Wrap(Def->Hint.ToString(), Body, 260.f);
			const float Width = FMath::Max(280.f, Measure(Info, Body).X + 20.f);
			const float Height = 20.f + NameFont.Size * 1.5f + Body.Size * 1.5f * (1 + HintLines.Num()) + 8.f;
			FVector2f Pos = Mouse + FVector2f(18.f, 18.f);
			Pos.X = FMath::Min(Pos.X, Screen.X - Width - 8.f);
			Pos.Y = FMath::Min(Pos.Y, Screen.Y - Height - 8.f);
			FSlateDrawElement::MakeBox(Out, L + 20, G.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(Pos)), TooltipBrush.Get(),
				ESlateDrawEffect::None, TooltipBrush->TintColor.GetSpecifiedColor());
			const FLinearColor Rarity = RiptideItems::RarityColour(Def->Rarity);
			const FLinearColor NameColour = Srgb(FMath::Lerp(Rarity.R, 1.f, 0.25f), FMath::Lerp(Rarity.G, 1.f, 0.25f), FMath::Lerp(Rarity.B, 1.f, 0.25f));
			float Y = Pos.Y + 10.f;
			Text(Out, L + 21, G, Def->Name.ToString(), NameFont, FVector2f(Pos.X + 10.f, Y), NameColour);
			Y += NameFont.Size * 1.5f;
			Text(Out, L + 21, G, Info, Body, FVector2f(Pos.X + 10.f, Y), Srgb(0.54f, 0.6f, 0.66f));
			Y += Body.Size * 1.5f + 4.f;
			for (const FString& Line : HintLines)
			{
				Text(Out, L + 21, G, Line, Body, FVector2f(Pos.X + 10.f, Y), Srgb(0.72f, 0.77f, 0.81f));
				Y += Body.Size * 1.5f;
			}
		}
	}

	Text(Out, L + 1, G, LOCTEXT("Hint", "Drag to move · R rotate · Shift-drag half a stack · Ctrl-click send across · Tab or E close").ToString(),
		Font(11), FVector2f(Screen.X * 0.5f, Screen.Y - 34.f), Srgb(1.f, 1.f, 1.f, 0.6f), true);
	return L + 22;
}

FReply SRiptideInventory::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Mouse = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	return FReply::Handled();
}

FReply SRiptideInventory::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Mouse = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}
	const float Cell = CellSize(ScreenSize);
	const TArray<FPanel> Panels = Layout(ScreenSize);
	FIntPoint Under;
	const FPanel* Panel = PanelAt(Panels, Mouse, Cell, Under);
	const FRiptideItem* Item = Panel ? Panel->Storage->GetStorage(Panel->Index)->Grid.ItemAt(Under) : nullptr;
	if (!Item)
	{
		return FReply::Handled();
	}
	if (MouseEvent.IsControlDown())
	{
		// Send across: from what you carry to the container, or from the container into your pockets, then bag.
		if (Panel->bContainer)
		{
			if (URiptideStorageComponent* Carry = Carrying.Get())
			{
				for (int32 i = 0; i < Carry->Num(); ++i)
				{
					const FRiptideItemGrid& Grid = Carry->GetStorage(i)->Grid;
					int32 X, Y;
					bool bRot;
					if (Grid.FindSpace(Item->Id, X, Y, bRot))
					{
						OnMove.ExecuteIfBound(Panel->Storage, Panel->Index, Item->Uid, Carry, i, -1, -1, false, 0);
						break;
					}
				}
			}
		}
		else if (Container.IsValid())
		{
			OnMove.ExecuteIfBound(Panel->Storage, Panel->Index, Item->Uid, Container.Get(), ContainerIndex, -1, -1, false, 0);
		}
		return FReply::Handled();
	}
	Drag.bActive = true;
	Drag.Storage = Panel->Storage;
	Drag.Index = Panel->Index;
	Drag.Uid = Item->Uid;
	Drag.Id = Item->Id;
	Drag.bRotated = Item->bRotated;
	Drag.Grab = Under - FIntPoint(Item->X, Item->Y);
	Drag.Count = MouseEvent.IsShiftDown() && Item->Count > 1 ? Item->Count / 2 : 0;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SRiptideInventory::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Mouse = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (Drag.bActive && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const TArray<FPanel> Panels = Layout(ScreenSize);
		const FTarget Target = FindTarget(Panels, CellSize(ScreenSize));
		if (Target.State == FTarget::Ok || Target.State == FTarget::Merge)
		{
			OnMove.ExecuteIfBound(Drag.Storage, Drag.Index, Drag.Uid, Target.Panel->Storage, Target.Panel->Index, Target.Cell.X, Target.Cell.Y,
				Drag.bRotated, Drag.Count);
		}
		Drag = FDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}

FReply SRiptideInventory::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::R && Drag.bActive)
	{
		// Turn it sideways, keeping the grabbed cell under the cursor.
		Drag.bRotated = !Drag.bRotated;
		Drag.Grab = FIntPoint(Drag.Grab.Y, Drag.Grab.X);
		return FReply::Handled();
	}
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Handled();       // holding E after opening a locker mustn't close it again
	}
	if (Key == EKeys::Tab || Key == EKeys::E || Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right
		|| Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_FaceButton_Left)
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
