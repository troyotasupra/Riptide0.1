#include "RiptideDataLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "RiptideCharacter.h"
#include "RiptideItemIcons.h"
#include "RiptideItems.h"
#include "RiptideStorageComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* SlotName(ERiptideWearSlot Slot)
	{
		switch (Slot)
		{
		case ERiptideWearSlot::Head: return TEXT("head");
		case ERiptideWearSlot::Torso: return TEXT("torso");
		case ERiptideWearSlot::Vest: return TEXT("vest");
		case ERiptideWearSlot::Legs: return TEXT("legs");
		case ERiptideWearSlot::Feet: return TEXT("feet");
		case ERiptideWearSlot::Back: return TEXT("back");
		case ERiptideWearSlot::Arm: return TEXT("arm");
		case ERiptideWearSlot::Leg: return TEXT("leg");
		default: return TEXT("");
		}
	}

	TSharedPtr<FJsonObject> ItemJson(const FRiptideItemDef& Def)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("id"), Def.Id.ToString());
		J->SetStringField(TEXT("name"), Def.Name.ToString());
		J->SetStringField(TEXT("kind"), Def.Category.ToString().ToLower());
		J->SetNumberField(TEXT("width"), Def.Size.X);
		J->SetNumberField(TEXT("height"), Def.Size.Y);
		J->SetNumberField(TEXT("stack"), Def.Stack);
		J->SetNumberField(TEXT("weight_kg"), Def.WeightKg);
		J->SetNumberField(TEXT("rarity"), Def.Rarity);
		J->SetBoolField(TEXT("floats"), Def.bFloats);
		if (!Def.Places.IsNone())
		{
			J->SetStringField(TEXT("places"), Def.Places.ToString());
		}
		if (Def.Tool.IsSet())
		{
			J->SetStringField(TEXT("tool"), Def.Tool->Type.ToString());
		}
		if (Def.Wear.IsSet())
		{
			J->SetStringField(TEXT("slot"), SlotName(Def.Wear->Slot));
			if (Def.Wear->Storage.X > 0)
			{
				J->SetNumberField(TEXT("storage_width"), Def.Wear->Storage.X);
				J->SetNumberField(TEXT("storage_height"), Def.Wear->Storage.Y);
			}
		}
		if (Def.Food.IsSet())
		{
			J->SetNumberField(TEXT("food"), Def.Food->Food);
			J->SetNumberField(TEXT("water"), Def.Food->Water);
		}
		return J;
	}
}

bool URiptideDataLibrary::ExportTables(const FString& Path)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FRiptideItemDef& Def : RiptideItems::All())
	{
		Items.Add(MakeShared<FJsonValueObject>(ItemJson(Def)));
	}
	Root->SetArrayField(TEXT("items"), Items);
	TSharedPtr<FJsonObject> Groups = MakeShared<FJsonObject>();
	for (const TCHAR* Name : { TEXT("wood"), TEXT("baitfish") })
	{
		TArray<TSharedPtr<FJsonValue>> Members;
		if (const TArray<FName>* Ids = RiptideItems::Group(FName(Name)))
		{
			for (const FName Id : *Ids)
			{
				Members.Add(MakeShared<FJsonValueString>(Id.ToString()));
			}
		}
		Groups->SetArrayField(Name, Members);
	}
	Root->SetObjectField(TEXT("groups"), Groups);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TArray<FName> URiptideDataLibrary::ItemIds()
{
	TArray<FName> Ids;
	for (const FRiptideItemDef& Def : RiptideItems::All())
	{
		Ids.Add(Def.Id);
	}
	return Ids;
}

bool URiptideDataLibrary::IsItem(FName Id)
{
	return RiptideItems::Find(Id) != nullptr;
}

int32 URiptideDataLibrary::TakeAllIcons(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	URiptideItemIconSubsystem* Icons = Game ? Game->GetSubsystem<URiptideItemIconSubsystem>() : nullptr;
	if (!Icons)
	{
		return 0;
	}
	int32 Taken = 0;
	for (const FRiptideItemDef& Def : RiptideItems::All())
	{
		if (Icons->Icon(Def.Id))
		{
			++Taken;
		}
	}
	return Taken;
}

int32 URiptideDataLibrary::CountCarried(const ARiptideCharacter* Crew, FName Id)
{
	const URiptideStorageComponent* Inventory = Crew ? Crew->GetInventory() : nullptr;
	int32 Total = 0;
	for (int32 Grid = 0; Inventory && Grid < Inventory->Num(); ++Grid)
	{
		if (const FRiptideStorage* Storage = Inventory->GetStorage(Grid))
		{
			Total += Storage->Grid.CountOf(Id);
		}
	}
	return Total;
}

int32 URiptideDataLibrary::FirstCarriedUid(const ARiptideCharacter* Crew, FName Id)
{
	const URiptideStorageComponent* Inventory = Crew ? Crew->GetInventory() : nullptr;
	for (int32 Grid = 0; Inventory && Grid < Inventory->Num(); ++Grid)
	{
		if (const FRiptideStorage* Storage = Inventory->GetStorage(Grid))
		{
			for (const FRiptideItem& Item : Storage->Grid.Items)
			{
				if (Item.Id == Id)
				{
					return Item.Uid;
				}
			}
		}
	}
	return 0;
}

float URiptideDataLibrary::CarriedSpoilsIn(const ARiptideCharacter* Crew, FName Id)
{
	const URiptideStorageComponent* Inventory = Crew ? Crew->GetInventory() : nullptr;
	const AGameStateBase* State = Crew && Crew->GetWorld() ? Crew->GetWorld()->GetGameState() : nullptr;
	const double Now = State ? State->GetServerWorldTimeSeconds() : 0.0;
	for (int32 Grid = 0; Inventory && Grid < Inventory->Num(); ++Grid)
	{
		for (const FRiptideItem& Item : Inventory->GetStorage(Grid)->Grid.Items)
		{
			if (Item.Id == Id)
			{
				return Item.SpoilAt > 0.f ? float(Item.SpoilAt - Now) : -1.f;
			}
		}
	}
	return -1.f;
}

bool URiptideDataLibrary::SaveIcon(const UObject* WorldContextObject, FName Id, const FString& Path)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	URiptideItemIconSubsystem* Icons = Game ? Game->GetSubsystem<URiptideItemIconSubsystem>() : nullptr;
	const FSlateBrush* Brush = Icons ? Icons->Icon(Id) : nullptr;
	UTextureRenderTarget2D* Target = Brush ? Cast<UTextureRenderTarget2D>(Brush->GetResourceObject()) : nullptr;
	if (!Target)
	{
		return false;
	}
	UKismetRenderingLibrary::ExportRenderTarget(const_cast<UObject*>(WorldContextObject), Target, FPaths::GetPath(Path), FPaths::GetCleanFilename(Path));
	return true;
}
