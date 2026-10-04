#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RiptideInteractable.h"
#include "RiptideInteractionComponent.generated.h"

class ARiptideCharacter;

/**
 * Finds what a crew member is looking at and lets them use it: a sphere trace from the camera every frame on the
 * owning machine picks the nearest IRiptideInteractable under the crosshair; E does it at once, or starts a hold
 * that fills a ring and fires when done. The server is asked to do the deed and checks the reach itself.
 */
UCLASS(ClassGroup = (Riptide), meta = (BlueprintSpawnableComponent))
class RIPTIDE_API URiptideInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiptideInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** How far from the eyes something can be used, cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float Reach = 260.f;

	/** What's under the crosshair now, if anything. */
	struct FFocus
	{
		TWeakObjectPtr<AActor> Actor;
		FHitResult Hit;
		FRiptideInteraction Interaction;
		bool IsSet() const { return Actor.IsValid(); }
	};
	const FFocus& GetFocus() const { return Focus; }

	/** The prompt to show, or empty. For the HUD and tests. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	FText GetFocusedPrompt() const { return Focus.IsSet() ? Focus.Interaction.Prompt : FText::GetEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool HasFocus() const { return Focus.IsSet(); }

	/** Looks again right now and returns the prompt (tests that just changed what's carried or where they stand). */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	FText PeekPrompt() { Look(); return GetFocusedPrompt(); }

	/** E pressed: uses the thing at once, or begins its hold. True if there was something to use. */
	bool BeginUse();

	/** E released: a hold not yet finished is dropped. */
	void EndUse();

	/** 0..1 through the current hold; 0 when not holding. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	float GetHoldFraction() const;

	/** Uses what's in focus as if E were pressed and held long enough. For tests. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	bool UseFocused();

	/** Asks the server to use Target. Clients call this through BeginUse; the server checks the reach. */
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target, UPrimitiveComponent* Component, uint8 Verb, FVector_NetQuantize HitPoint, int32 Item);

private:
	FFocus Focus;
	float HoldElapsed = -1.f;      // < 0 when not holding
	FFocus Holding;                // what the hold is on; dropped if the focus moves off it

	void Look();
	void Fire(const FFocus& On);
	ARiptideCharacter* Crew() const;
};
