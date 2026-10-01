#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RiptideAppearance.h"
#include "RiptidePlayerState.generated.h"

/**
 * A player in the game, as everyone sees them: their callsign (the player name) and how their crew member looks.
 * The host sets both when the player joins, from what their client sent in the join URL (?Name=...?Look=..., see
 * ARiptideGameMode::InitNewPlayer); they replicate to every machine, and the crew member's body is built from them.
 */
UCLASS()
class RIPTIDE_API ARiptidePlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "Crew")
	const FRiptideAppearance& GetAppearance() const { return Appearance; }

	/** Changes the look. Server only; everyone's copy of the crew member rebuilds its body. */
	void SetAppearance(const FRiptideAppearance& NewAppearance);

	/** Called on every machine when the look changes (and when it first arrives). */
	DECLARE_MULTICAST_DELEGATE(FOnAppearanceChanged);
	FOnAppearanceChanged OnAppearanceChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Appearance)
	FRiptideAppearance Appearance;

	UFUNCTION()
	void OnRep_Appearance();
};
