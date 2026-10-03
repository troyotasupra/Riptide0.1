#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiptideAppearance.h"
#include "RiptideCrewAnimInstance.h"
#include "RiptideCrewMannequin.generated.h"

class URiptideCrewBodyComponent;

/**
 * A crew member standing on their own, for the menus: the main menu's silhouette and the customisation screen's
 * preview. It's the same body the game's crew wear (URiptideCrewBodyComponent), not networked, posed by code.
 *
 * Use it from C++ or Blueprint:
 *
 *     ARiptideCrewMannequin* Crew = World->SpawnActor<ARiptideCrewMannequin>(Location, Rotation);
 *     Crew->SetAppearance(Profile->Appearance);         // rebuild whenever the player changes an option
 *     Crew->SetPose(ERiptideCrewPose::RifleReady);      // or ERiptideCrewPose::Idle (the default)
 *
 * The actor's origin is at the crew member's feet and it faces the actor's forward (+X), as a character would.
 * The body is 1.77-1.81 m tall. GetBody() gives the body component (for its material slots, bounds or for tests).
 * Changing the look rebuilds only the parts that changed, so it's fine to call on every click of an option.
 */
UCLASS()
class RIPTIDE_API ARiptideCrewMannequin : public AActor
{
	GENERATED_BODY()

public:
	ARiptideCrewMannequin();

	/** Dresses the mannequin in this look (build, skin, hair, uniform, gear). */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetAppearance(const FRiptideAppearance& Look);

	UFUNCTION(BlueprintPure, Category = "Crew")
	const FRiptideAppearance& GetAppearance() const;

	/** Stands at ease (Idle) or holds the rifle shouldered (RifleReady); Gameplay is treated as Idle here. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetPose(ERiptideCrewPose Pose);

	UFUNCTION(BlueprintPure, Category = "Crew")
	URiptideCrewBodyComponent* GetBody() const { return Body; }

	/** The look and pose it starts with (set in the editor, or before it's spawned with deferred spawning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew")
	FRiptideAppearance InitialAppearance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew")
	ERiptideCrewPose InitialPose = ERiptideCrewPose::Idle;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Crew")
	TObjectPtr<URiptideCrewBodyComponent> Body;
};
