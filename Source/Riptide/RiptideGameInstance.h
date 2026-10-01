#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "RiptideAppearance.h"
#include "RiptideGameInstance.generated.h"

class URiptideProfileSave;

/** A game the browser found, as the join screen lists it. */
USTRUCT(BlueprintType)
struct FRiptideGameListing
{
	GENERATED_BODY()

	/** The host's callsign. */
	UPROPERTY(BlueprintReadOnly, Category = "Online")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 Players = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 MaxPlayers = 0;

	/** Round trip to the host in milliseconds, or -1 when it isn't known (Steam doesn't measure it for lobbies). */
	UPROPERTY(BlueprintReadOnly, Category = "Online")
	int32 PingMs = -1;

	/** Hosted by one of the player's Steam friends. */
	UPROPERTY(BlueprintReadOnly, Category = "Online")
	bool bFriend = false;

	/** Only the host's friends can join it. */
	UPROPERTY(BlueprintReadOnly, Category = "Online")
	bool bFriendsOnly = false;
};

/** What the online side is busy with, for the menus to show. */
UENUM(BlueprintType)
enum class ERiptideOnlineActivity : uint8
{
	None,
	Hosting,        // creating the session, then loading into the game as its host
	Searching,      // looking for games
	Joining,        // joining a session, then connecting to its host
	Leaving         // ending the session on the way back to the menu (or out of the game)
};

/**
 * The game's online side, alive for the whole run: hosting, finding and joining games, accepting Steam invites,
 * leaving, and getting back to the main menu with a readable message when a connection or a map load fails.
 *
 * With Steam running it uses Steam lobbies (the public browser, friends' games, invites through the overlay) and
 * Steam's networking. Without it (Steam closed, -nosteam, or playing in the editor) the engine's Null subsystem
 * stands in: games on the local network show in the browser, and anyone can join by IP address.
 *
 * Every way into a game carries the player's callsign and look (?Name=...?Look=..., read by ARiptideGameMode), from
 * the profile kept here (URiptideProfileSave).
 */
UCLASS()
class RIPTIDE_API URiptideGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	/** The maps: the main menu, and the game itself. */
	static const TCHAR* MenuMap;
	static const TCHAR* GameMap;

	/** The most players in one game. */
	static constexpr int32 MaxCrew = 4;

	// --- Hosting and joining ---

	/** Creates a session (listed publicly, or for Steam friends only) and loads the game as its host. False if
	 * something else is already under way. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	bool HostGame(bool bFriendsOnly);

	/** Looks for games to join: public lobbies and friends' games on Steam, or games on the local network without
	 * it. The results replace GetFoundGames() when it's done. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	bool FindGames();

	/** Joins one of the found games (an index into GetFoundGames()). */
	UFUNCTION(BlueprintCallable, Category = "Online")
	bool JoinGame(int32 Index);

	/** Connects straight to a host's address ("192.168.1.20", "203.0.113.5:7777"), without the browser. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	bool JoinByAddress(const FString& Address);

	/** Gives up on a connection that hasn't gone through yet. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	void CancelJoin();

	/** Ends this player's part in the game (as host, the whole game) and goes back to the main menu. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	void LeaveGame();

	/** Leaves the session and quits to the desktop. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	void QuitToDesktop();

	/** Opens the Steam overlay's invite dialog for the current game. False without Steam (or a session). */
	UFUNCTION(BlueprintCallable, Category = "Online")
	bool ShowInviteOverlay();

	// --- State, for the menus ---

	UFUNCTION(BlueprintPure, Category = "Online")
	const TArray<FRiptideGameListing>& GetFoundGames() const { return Listings; }

	/** Counts up each time the found games change (so a screen knows to redraw its list). */
	int32 GetFoundGamesVersion() const { return ListingsVersion; }

	UFUNCTION(BlueprintPure, Category = "Online")
	ERiptideOnlineActivity GetActivity() const { return Activity; }

	/** What's happening right now, in a line ("Searching for games..."), or empty. */
	UFUNCTION(BlueprintPure, Category = "Online")
	FText GetStatusText() const { return StatusText; }

	/** The last search's outcome ("No games found on the local network."), or empty. */
	UFUNCTION(BlueprintPure, Category = "Online")
	FText GetSearchSummary() const { return SearchSummary; }

	/** True when Steam is up and this game is using it (otherwise it's LAN and join by IP). */
	UFUNCTION(BlueprintPure, Category = "Online")
	bool IsUsingSteam() const;

	/** One line about the connection to the online service, for the menu's footer. */
	UFUNCTION(BlueprintPure, Category = "Online")
	FText GetOnlineServiceText() const;

	/** This machine's address on its network, for friends joining by IP ("192.168.1.20"), or empty. */
	UFUNCTION(BlueprintPure, Category = "Online")
	FString GetLocalAddress() const;

	/** True while this player hosts a game (whether Steam lists it or not). */
	UFUNCTION(BlueprintPure, Category = "Online")
	bool IsHosting() const;

	/** Hosting for Steam friends only (the last game hosted). */
	bool IsFriendsOnly() const { return bHostedFriendsOnly; }

	/** Why we came back to the menu (a lost connection, a failed join), once; empty if nothing went wrong. */
	UFUNCTION(BlueprintCallable, Category = "Online")
	FText TakeMenuMessage();

	/** The map loaded now ("MainMenu", "Ocean_Test"), for scripts and tests. */
	UFUNCTION(BlueprintPure, Category = "Online")
	FString GetCurrentMapName() const;

	// --- The player's profile ---

	UFUNCTION(BlueprintPure, Category = "Crew")
	FString GetCallsign() const;

	UFUNCTION(BlueprintPure, Category = "Crew")
	FRiptideAppearance GetAppearance() const;

	/** Changes the callsign and look (the callsign is cleaned up: letters, digits, - _ and ., up to MaxCallsign), and
	 * writes them to the profile on disk if bSave. They go with the player into the next game they join or host. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetProfile(const FString& Callsign, const FRiptideAppearance& Look, bool bSave = true);

	/** The same, with the look in its text form (FRiptideAppearance::ToString), for scripts. */
	UFUNCTION(BlueprintCallable, Category = "Crew")
	void SetProfileFromText(const FString& Callsign, const FString& LookText, bool bSave = false);

	/** A callsign cleaned up for a URL and the crew list: letters, digits, - _ and ., at most MaxCallsign long. */
	static FString SanitiseCallsign(const FString& Callsign);
	static constexpr int32 MaxCallsign = 16;

	/** The options every way into a game carries: "Name=<callsign>?Look=<look>". */
	FString GetPlayerOptions() const;

private:
	IOnlineSessionPtr GetSessions();
	void WatchSessions(const IOnlineSessionPtr& Sessions);
	bool HasSession();

	void SetActivity(ERiptideOnlineActivity NewActivity, const FText& Status = FText::GetEmpty());

	// Hosting: create the session, then load the map as a listen server.
	void CreateSession();
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void TravelToHostedGame();

	// Finding: public lobbies (or the LAN), then each Steam friend who's playing.
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnReadFriendsComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr);
	void FindNextFriendSession();
	void OnFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& Found);
	void AddResult(const FOnlineSessionSearchResult& Result, bool bFriend);
	void FinishSearch();

	// Joining a session: join it, then connect to its host.
	void JoinSearchResult(const FOnlineSessionSearchResult& Result);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void TravelToAddress(const FString& Address);

	/** Steam: the player accepted an invite or clicked "Join game" on a friend. */
	void OnInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& Invite);

	/** Ends the session (if any), then runs Then. Then runs anyway if the online service doesn't answer in time. */
	void DestroySessionThen(TFunction<void()> Then);
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void RunAfterDestroy();

	void OpenMenu();
	void OnPostLoadMap(UWorld* World);
	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);

	UPROPERTY(Transient)
	TObjectPtr<URiptideProfileSave> Profile;

	/** The session interface the delegates below are bound to (it changes between play sessions in the editor). */
	TWeakPtr<IOnlineSession, ESPMode::ThreadSafe> WatchedSessions;
	FDelegateHandle CreateHandle, DestroyHandle, FindHandle, JoinHandle, InviteHandle, FriendSessionHandle;

	ERiptideOnlineActivity Activity = ERiptideOnlineActivity::None;
	FText StatusText;
	FText SearchSummary;
	FText MenuMessage;
	bool bHostedFriendsOnly = false;

	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FOnlineSessionSearchResult> Results;
	TArray<FRiptideGameListing> Listings;
	int32 ListingsVersion = 0;
	/** Steam friends still to look for games from. */
	TArray<FUniqueNetIdRef> FriendsToSearch;
	FTimerHandle SearchTimeout;

	/** What to do once the session has been destroyed, and the timer that does it anyway. */
	TFunction<void()> AfterDestroy;
	FTimerHandle DestroyTimeout;

	FDelegateHandle PostLoadMapHandle, NetworkFailureHandle, TravelFailureHandle;
};
