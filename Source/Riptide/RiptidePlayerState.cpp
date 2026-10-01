#include "RiptidePlayerState.h"

#include "Net/UnrealNetwork.h"

void ARiptidePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARiptidePlayerState, Appearance);
}

void ARiptidePlayerState::CopyProperties(APlayerState* PlayerState)
{
	// Carried over when the game travels to a new map, and when a player who dropped out rejoins.
	Super::CopyProperties(PlayerState);
	if (ARiptidePlayerState* Riptide = Cast<ARiptidePlayerState>(PlayerState))
	{
		Riptide->SetAppearance(Appearance);
	}
}

void ARiptidePlayerState::SetAppearance(const FRiptideAppearance& NewAppearance)
{
	FRiptideAppearance Sane = NewAppearance;
	Sane.Sanitise();
	if (Sane == Appearance)
	{
		return;
	}
	Appearance = Sane;
	OnAppearanceChanged.Broadcast();
}

void ARiptidePlayerState::OnRep_Appearance()
{
	OnAppearanceChanged.Broadcast();
}
