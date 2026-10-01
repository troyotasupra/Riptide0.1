#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "RiptideLocalPlayer.generated.h"

/**
 * The player at this machine. Whenever it joins a game, however it got there (the browser, an invite, an IP
 * address, the console's "open"), the engine sends the host its nickname as ?Name= and its login options; here
 * those are the player's callsign and ?Look= (their saved look, see URiptideGameInstance), so their crew member is
 * named and dressed the way they chose. Without this the engine would send the Steam name (or the PC's name).
 */
UCLASS()
class RIPTIDE_API URiptideLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetNickname() const override;
	virtual FString GetGameLoginOptions() const override;
};
