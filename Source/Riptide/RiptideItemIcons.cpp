#include "RiptideItemIcons.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "RiptideItems.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
	// Far above the sea, where nothing else is: the camera and its stand live here for the whole game.
	const FVector StudioAt(0.0, 0.0, 400000.0);
}

UStaticMesh* URiptideItemIconSubsystem::MeshFor(FName Id)
{
	const FString Path = FString::Printf(TEXT("/Game/Riptide/Items/SM_Item_%s.SM_Item_%s"), *Id.ToString(), *Id.ToString());
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
	{
		return Mesh;
	}
	// A canteen full of water is drawn as the canteen.
	if (const FRiptideItemDef* Def = RiptideItems::Find(Id))
	{
		if (Def->Food.IsSet() && !Def->Food->EmptiesTo.IsNone() && Def->Food->EmptiesTo != Id)
		{
			return MeshFor(Def->Food->EmptiesTo);
		}
	}
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Riptide/Items/SM_Item_crate.SM_Item_crate"));
}

bool URiptideItemIconSubsystem::MakeStudio()
{
	if (Camera && Stand)
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = World->SpawnActor<ASceneCapture2D>(StudioAt, FRotator::ZeroRotator, Params);
	if (!Camera)
	{
		return false;
	}
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->bAlwaysPersistRenderingState = true;
	Stand = NewObject<UStaticMeshComponent>(Camera, TEXT("ItemStand"));
	Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stand->SetCastShadow(false);
	Stand->RegisterComponent();
	Capture->ShowOnlyComponents.Add(Stand);
	return true;
}

const FSlateBrush* URiptideItemIconSubsystem::Icon(FName Id)
{
	if (FEntry* Known = Icons.Find(Id))
	{
		// Run from the editor, a material's shaders for a mesh compile the first time it's drawn: the first picture
		// can catch the stand-in grey. It's retaken a little later, twice, so it ends up in its real colours.
		const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
		const TObjectPtr<UStaticMesh>* Mesh = Meshes.Find(Id);
		if (Known->Takes < 3 && Now - Known->TakenAt > 1.5 * Known->Takes && Mesh && *Mesh && MakeStudio())
		{
			Photograph(*Known, *Mesh);
		}
		return &Known->Brush;
	}
	if (!MakeStudio())
	{
		return nullptr;
	}
	UStaticMesh* Mesh = MeshFor(Id);
	if (!Mesh)
	{
		return nullptr;
	}
	Meshes.Add(Id, Mesh);
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size, Size, ETextureRenderTargetFormat::RTF_RGBA8, FLinearColor::Transparent, false);
	if (!Target)
	{
		return nullptr;
	}

	FEntry& Entry = Icons.Add(Id);
	Entry.Target = Target;
	Photograph(Entry, Mesh);
	Entry.Brush.SetResourceObject(Target);
	Entry.Brush.ImageSize = FVector2D(Size, Size);
	Entry.Brush.DrawAs = ESlateBrushDrawType::Image;
	return &Entry.Brush;
}

void URiptideItemIconSubsystem::Photograph(FEntry& Entry, UStaticMesh* Mesh)
{
#if WITH_EDITOR
	// Let any shaders still compiling finish first (a packaged game has them all already).
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
#endif
	// The item stood on the stand, seen from the front and a little above, filling the frame.
	Stand->SetStaticMesh(Mesh);
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float Reach = FMath::Max(Bounds.BoxExtent.GetMax(), 1.f);
	const FVector Centre = StudioAt;
	Stand->SetWorldLocation(Centre - Bounds.Origin);
	Stand->SetWorldRotation(FRotator(0.f, 30.f, 0.f));
	USceneCaptureComponent2D* Capture = Camera->GetCaptureComponent2D();
	const FRotator Look(-32.f, 0.f, 0.f);
	Capture->SetWorldLocationAndRotation(Centre - Look.Vector() * (Reach * 6.f), Look);
	Capture->OrthoWidth = Reach * 2.3f;
	Capture->TextureTarget = Entry.Target;
	Capture->CaptureScene();
	Entry.TakenAt = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	++Entry.Takes;
}
