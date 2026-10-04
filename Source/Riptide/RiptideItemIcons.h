#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RiptideItemIcons.generated.h"

class ASceneCapture2D;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * Pictures of items for the inventory, taken in the running game from the items' own models: the first time an
 * item's icon is asked for, its mesh is stood in front of a hidden camera high above the world, photographed into
 * a small render target, and kept. No editor step, no files: whatever the model generator makes is what the card
 * shows. Items with no model get the crate's picture.
 */
UCLASS()
class RIPTIDE_API URiptideItemIconSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The brush for Id's picture, taking it now if it hasn't been taken yet. Null when nothing can be drawn. */
	const FSlateBrush* Icon(FName Id);

	/** Pixels on each side of an icon. */
	static constexpr int32 Size = 128;

	/** The item's mesh, or the crate's when it has none. */
	static UStaticMesh* MeshFor(FName Id);

private:
	struct FEntry
	{
		TObjectPtr<UTextureRenderTarget2D> Target;
		FSlateBrush Brush;
		/** When it was last photographed, and how many times (see Icon: retaken while shaders may be compiling). */
		double TakenAt = 0.0;
		int32 Takes = 0;
	};
	/** Photographs Mesh into Entry's target. */
	void Photograph(FEntry& Entry, UStaticMesh* Mesh);
	TMap<FName, FEntry> Icons;

	UPROPERTY()
	TObjectPtr<ASceneCapture2D> Camera;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Stand;

	UPROPERTY()
	TMap<FName, TObjectPtr<UStaticMesh>> Meshes;

	bool MakeStudio();
};
