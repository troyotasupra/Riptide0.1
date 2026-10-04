#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RiptideInteractable.generated.h"

class ARiptideCharacter;

/** What a crew member can do with the thing under their crosshair: the prompt and whether it takes a hold. */
struct FRiptideInteraction
{
	FText Prompt;                 // "Take coconut", "Chop (hatchet)"
	FText WhyNot;                 // shown greyed when it can't be done: "Needs a hatchet"
	float HoldSeconds = 0.f;      // 0 happens on the press; otherwise E is held this long
	bool bEnabled = true;
	uint8 Verb = 0;               // which of the thing's actions this is, for things with more than one
	int32 Item = INDEX_NONE;      // for instanced things: which instance (FHitResult::Item)
};

UINTERFACE(MinimalAPI)
class URiptideInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Something the crew can use by looking at it and pressing E: a dropped item, a palm with coconuts, a campfire, a
 * locker lid. GetInteraction runs on every machine to show the prompt; Interact runs on the server once the press
 * or hold is done, after the server has checked the character is close enough.
 */
class RIPTIDE_API IRiptideInteractable
{
	GENERATED_BODY()

public:
	/** Fills Out for the part of this thing that was hit. False if nothing can be done with it. */
	virtual bool GetInteraction(const ARiptideCharacter* Who, const FHitResult& Hit, FRiptideInteraction& Out) const = 0;

	/** Does it. Server only. */
	virtual void Interact(ARiptideCharacter* Who, const FHitResult& Hit, uint8 Verb) = 0;
};
