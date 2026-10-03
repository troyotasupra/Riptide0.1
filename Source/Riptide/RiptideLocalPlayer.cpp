#include "RiptideLocalPlayer.h"

#include "RiptideGameInstance.h"

FString URiptideLocalPlayer::GetNickname() const
{
	if (const URiptideGameInstance* Game = Cast<URiptideGameInstance>(GetGameInstance()))
	{
		return Game->GetCallsign();
	}
	return Super::GetNickname();
}

FString URiptideLocalPlayer::GetGameLoginOptions() const
{
	if (const URiptideGameInstance* Game = Cast<URiptideGameInstance>(GetGameInstance()))
	{
		return FString::Printf(TEXT("Look=%s"), *Game->GetAppearance().ToString());
	}
	return Super::GetGameLoginOptions();
}
