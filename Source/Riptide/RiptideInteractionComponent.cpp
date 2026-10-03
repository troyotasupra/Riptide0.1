#include "RiptideInteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "RiptideCharacter.h"

URiptideInteractionComponent::URiptideInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

ARiptideCharacter* URiptideInteractionComponent::Crew() const
{
	return Cast<ARiptideCharacter>(GetOwner());
}

void URiptideInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ARiptideCharacter* Who = Crew();
	if (!Who || !Who->IsLocallyControlled())
	{
		return;
	}
	Look();
	if (HoldElapsed >= 0.f)
	{
		// Still on the same thing? A hold follows the thing, not the crosshair, as long as it's still in focus.
		if (!Focus.IsSet() || Focus.Actor != Holding.Actor || Focus.Hit.Item != Holding.Hit.Item || !Focus.Interaction.bEnabled)
		{
			HoldElapsed = -1.f;
			return;
		}
		HoldElapsed += DeltaTime;
		if (HoldElapsed >= Holding.Interaction.HoldSeconds)
		{
			HoldElapsed = -1.f;
			Fire(Focus);
		}
	}
}

void URiptideInteractionComponent::Look()
{
	Focus = FFocus();
	ARiptideCharacter* Who = Crew();
	UCameraComponent* Camera = Who ? Who->GetFirstPersonCamera() : nullptr;
	if (!Camera || !Who->GetController())
	{
		return;
	}
	const FVector From = Camera->GetComponentLocation();
	const FVector Dir = Who->GetControlRotation().Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RiptideInteract), false, Who);
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByChannel(Hits, From, From + Dir * Reach, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(10.f), Params);
	for (const FHitResult& Hit : Hits)
	{
		AActor* Actor = Hit.GetActor();
		if (!Actor)
		{
			continue;
		}
		if (IRiptideInteractable* Thing = Cast<IRiptideInteractable>(Actor))
		{
			FRiptideInteraction Interaction;
			if (Thing->GetInteraction(Who, Hit, Interaction))
			{
				Focus.Actor = Actor;
				Focus.Hit = Hit;
				Focus.Interaction = Interaction;
				return;
			}
		}
		if (Hit.bBlockingHit)
		{
			return;      // something solid in the way that can't be used
		}
	}
}

bool URiptideInteractionComponent::BeginUse()
{
	if (!Focus.IsSet())
	{
		return false;
	}
	if (!Focus.Interaction.bEnabled)
	{
		return true;     // the prompt says why not; the press still counts as meant for it
	}
	if (Focus.Interaction.HoldSeconds <= 0.f)
	{
		Fire(Focus);
	}
	else
	{
		Holding = Focus;
		HoldElapsed = 0.f;
	}
	return true;
}

void URiptideInteractionComponent::EndUse()
{
	HoldElapsed = -1.f;
}

float URiptideInteractionComponent::GetHoldFraction() const
{
	return HoldElapsed >= 0.f && Holding.Interaction.HoldSeconds > 0.f ? FMath::Clamp(HoldElapsed / Holding.Interaction.HoldSeconds, 0.f, 1.f) : 0.f;
}

bool URiptideInteractionComponent::UseFocused()
{
	Look();
	if (!Focus.IsSet() || !Focus.Interaction.bEnabled)
	{
		return false;
	}
	Fire(Focus);
	return true;
}

void URiptideInteractionComponent::Fire(const FFocus& On)
{
	// The instance or slot the thing named, else what the trace hit (a planted thing's instance).
	const int32 Item = On.Interaction.Item != INDEX_NONE ? On.Interaction.Item : On.Hit.Item;
	ServerInteract(On.Actor.Get(), On.Hit.GetComponent(), On.Interaction.Verb, On.Hit.ImpactPoint, Item);
}

void URiptideInteractionComponent::ServerInteract_Implementation(AActor* Target, UPrimitiveComponent* Component, uint8 Verb, FVector_NetQuantize HitPoint, int32 Item)
{
	ARiptideCharacter* Who = Crew();
	IRiptideInteractable* Thing = Cast<IRiptideInteractable>(Target);
	UCameraComponent* Camera = Who ? Who->GetFirstPersonCamera() : nullptr;
	if (!Thing || !Camera)
	{
		return;
	}
	// Within reach of the eyes, with some slack for a lagging client's view of a moving world.
	if (FVector::Dist(Camera->GetComponentLocation(), HitPoint) > Reach * 1.4f)
	{
		return;
	}
	FHitResult Hit;
	Hit.ImpactPoint = HitPoint;
	Hit.Location = HitPoint;
	Hit.Item = Item;
	Hit.HitObjectHandle = FActorInstanceHandle(Target);
	Hit.Component = Component;      // which batch of planted things was hit, for the island's props
	FRiptideInteraction Interaction;
	if (!Thing->GetInteraction(Who, Hit, Interaction) || !Interaction.bEnabled)
	{
		return;
	}
	Thing->Interact(Who, Hit, Verb);
}
