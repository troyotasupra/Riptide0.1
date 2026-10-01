#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideAppearance.h"
#include "RiptideCrewFigure.generated.h"

class UPointLightComponent;
class USceneCaptureComponent2D;
class USpotLightComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * PLACEHOLDER until the crew mannequin (ARiptideCrewMannequin) lands: a crew member built from the engine's basic
 * shapes, posed standing with a rifle at the low ready. It shows the look's broad strokes (build, skin, hair and beard,
 * headgear, face cover, uniform colour, vest, gear colour, pack, gloves), so the crew screen's choices visibly change
 * something, and it reads as a soldier in silhouette in the main menu's shot.
 *
 * Nothing makes one directly: RiptideCrewFigure::Spawn and SetLook below are the menus' only way in, so swapping in
 * the mannequin is a change to those two functions.
 */
UCLASS(NotPlaceable)
class RIPTIDE_API ARiptideCrewFigurePlaceholder : public AActor
{
	GENERATED_BODY()

public:
	ARiptideCrewFigurePlaceholder();

	void SetAppearance(const FRiptideAppearance& Look);

private:
	UStaticMeshComponent* Part(const TCHAR* Name, int32 Shape, const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator);
	void Tint(UStaticMeshComponent* Part, const FLinearColor& Colour);

	UPROPERTY(VisibleAnywhere, Category = "Crew")
	TObjectPtr<USceneComponent> Root;

	/** Every part, by name ("Torso", "Helmet"...). */
	UPROPERTY(VisibleAnywhere, Category = "Crew")
	TMap<FName, TObjectPtr<UStaticMeshComponent>> Parts;
};

namespace RiptideCrewFigure
{
	/** Makes a crew member for the menus to show, standing at Where (its feet) and dressed in Look. */
	RIPTIDE_API AActor* Spawn(UWorld* World, const FTransform& Where, const FRiptideAppearance& Look);

	/** Dresses a figure Spawn made in a new look. */
	RIPTIDE_API void SetLook(AActor* Figure, const FRiptideAppearance& Look);
}

/**
 * The crew screen's live preview: a small lit photo booth far above the menu's sea, with a crew member in it, filmed
 * into a render target the screen shows. Turned by dragging on the preview; dressed as the player changes their look.
 */
UCLASS(NotPlaceable)
class RIPTIDE_API ARiptideCrewPreview : public AActor
{
	GENERATED_BODY()

public:
	ARiptideCrewPreview();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	void SetLook(const FRiptideAppearance& Look);

	/** Turns the crew member (degrees; it eases to the new angle). */
	void Turn(float DeltaYaw);

	/** Films only while the crew screen is showing it. */
	void SetFilming(bool bFilm);

	/** The render target's size, in pixels (the preview's shape on screen). */
	static constexpr int32 Width = 720;
	static constexpr int32 Height = 900;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USpotLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USpotLightComponent> RimLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> Backdrop;

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<UStaticMeshComponent> Floor;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<AActor> Figure;

private:
	float Yaw = 20.f;
	float TargetYaw = 20.f;
};
