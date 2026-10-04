#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RiptideDataLibrary.generated.h"

/**
 * The game's tables, written out as JSON for the Python that builds item models and icons in the editor
 * (Content/Python/riptide_item_models.py). The tables themselves live in C++ (Data/); this is only a mirror.
 */
UCLASS()
class RIPTIDE_API URiptideDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Writes every table to Path (a full file path). True if the file was written. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Data")
	static bool ExportTables(const FString& Path);

	/** Every item id, in table order. */
	UFUNCTION(BlueprintPure, Category = "Riptide|Data")
	static TArray<FName> ItemIds();

	/** Whether Id names an item. */
	UFUNCTION(BlueprintPure, Category = "Riptide|Data")
	static bool IsItem(FName Id);

	/** Takes every item's inventory picture now (in a running game) and returns how many came out. For tests. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Data", meta = (WorldContext = "WorldContextObject"))
	static int32 TakeAllIcons(const UObject* WorldContextObject);

	/** How many of item Id a crew member carries across every grid, and the uid of the first stack of it (0 if
	 * none). For tests. */
	UFUNCTION(BlueprintPure, Category = "Riptide|Data")
	static int32 CountCarried(const class ARiptideCharacter* Crew, FName Id);

	UFUNCTION(BlueprintPure, Category = "Riptide|Data")
	static int32 FirstCarriedUid(const class ARiptideCharacter* Crew, FName Id);

	/** Seconds until the first carried stack of food Id goes off, or -1 if none is carried or it never will. For tests. */
	UFUNCTION(BlueprintPure, Category = "Riptide|Data")
	static float CarriedSpoilsIn(const class ARiptideCharacter* Crew, FName Id);

	/** Writes one item's inventory picture to a PNG at Path (a full file path). For looking at them. */
	UFUNCTION(BlueprintCallable, Category = "Riptide|Data", meta = (WorldContext = "WorldContextObject"))
	static bool SaveIcon(const UObject* WorldContextObject, FName Id, const FString& Path);
};
