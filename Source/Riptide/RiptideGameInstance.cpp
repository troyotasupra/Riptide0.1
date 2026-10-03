#include "RiptideGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "IPAddress.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "RiptideSettings.h"
#include "SocketSubsystem.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "RiptideOnline"

DEFINE_LOG_CATEGORY_STATIC(LogRiptideOnline, Log, All);

const TCHAR* URiptideGameInstance::MenuMap = TEXT("/Game/Riptide/Maps/MainMenu");
const TCHAR* URiptideGameInstance::GameMap = TEXT("/Game/Riptide/Maps/Ocean_Test");

namespace
{
	// Custom values every Riptide session carries. The game tag keeps other games out of the browser: Valve's test
	// app 480 is shared by every game still in development, so its lobby list is full of strangers' games. Bump the
	// tag when a new version can't play with older ones.
	const FName KeyGame(TEXT("RIPTIDE_GAME"));
	const TCHAR* GameTag = TEXT("riptide-1");
	const FName KeyCallsign(TEXT("RIPTIDE_CALLSIGN"));
	const FName KeyFriendsOnly(TEXT("RIPTIDE_FRIENDSONLY"));

	const FName SteamName(TEXT("STEAM"));

	// How long to wait on the online service before giving up on it: a search, and ending a session.
	constexpr float SearchTimeoutSeconds = 20.f;
	constexpr float DestroyTimeoutSeconds = 4.f;
}

void URiptideGameInstance::Init()
{
	Super::Init();

	Profile = URiptideProfileSave::LoadOrCreate();
	if (!UGameplayStatics::DoesSaveGameExist(URiptideProfileSave::SlotName, 0))
	{
		// A first run's random callsign is kept, so it doesn't change every time the game starts.
		Profile->Save();
	}
	URiptideSettingsSave::Get();

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &URiptideGameInstance::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &URiptideGameInstance::OnTravelFailure);
	}
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &URiptideGameInstance::OnPostLoadMap);

	// Listen for Steam invites from the start: a game launched from an invite gets it right away.
	GetSessions();
	UE_LOG(LogRiptideOnline, Log, TEXT("Online: %s"), *GetOnlineServiceText().ToString());
}

void URiptideGameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	WatchSessions(nullptr);
	Super::Shutdown();
}

// --- The session interface ---

IOnlineSessionPtr URiptideGameInstance::GetSessions()
{
	// Through the world, so each play-in-editor window gets its own (the editor runs one online subsystem per window).
	IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (Sessions != WatchedSessions.Pin())
	{
		WatchSessions(Sessions);
	}
	return Sessions;
}

void URiptideGameInstance::WatchSessions(const IOnlineSessionPtr& Sessions)
{
	if (IOnlineSessionPtr Old = WatchedSessions.Pin())
	{
		Old->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Old->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		Old->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Old->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Old->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
		Old->ClearOnFindFriendSessionCompleteDelegate_Handle(0, FriendSessionHandle);
	}
	WatchedSessions = Sessions;
	if (Sessions)
	{
		CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(this, &URiptideGameInstance::OnCreateSessionComplete));
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &URiptideGameInstance::OnDestroySessionComplete));
		FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(this, &URiptideGameInstance::OnFindSessionsComplete));
		JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(this, &URiptideGameInstance::OnJoinSessionComplete));
		InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &URiptideGameInstance::OnInviteAccepted));
		FriendSessionHandle = Sessions->AddOnFindFriendSessionCompleteDelegate_Handle(0,
			FOnFindFriendSessionCompleteDelegate::CreateUObject(this, &URiptideGameInstance::OnFindFriendSessionComplete));
	}
}

bool URiptideGameInstance::HasSession()
{
	const IOnlineSessionPtr Sessions = GetSessions();
	return Sessions && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
}

bool URiptideGameInstance::IsUsingSteam() const
{
	const IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	return Subsystem && Subsystem->GetSubsystemName() == SteamName;
}

FText URiptideGameInstance::GetOnlineServiceText() const
{
	if (IsUsingSteam())
	{
		const IOnlineIdentityPtr Identity = Online::GetIdentityInterface(GetWorld());
		const FString Name = Identity ? Identity->GetPlayerNickname(0) : FString();
		return Name.IsEmpty() ? LOCTEXT("SteamOnline", "Steam: online")
			: FText::Format(LOCTEXT("SteamOnlineAs", "Steam: online as {0}"), FText::FromString(Name));
	}
	return LOCTEXT("NoSteam", "Steam isn't running: games on your local network, or join by IP");
}

FString URiptideGameInstance::GetLocalAddress() const
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets)
	{
		return FString();
	}
	bool bCanBindAll = false;
	const TSharedRef<FInternetAddr> Address = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	return Address->IsValid() ? Address->ToString(false) : FString();
}

bool URiptideGameInstance::IsHosting() const
{
	return GetWorld() && GetWorld()->GetNetMode() == NM_ListenServer;
}

FString URiptideGameInstance::GetCurrentMapName() const
{
	return GetWorld() ? UGameplayStatics::GetCurrentLevelName(GetWorld(), true) : FString();
}

FText URiptideGameInstance::TakeMenuMessage()
{
	FText Message = MenuMessage;
	MenuMessage = FText::GetEmpty();
	return Message;
}

void URiptideGameInstance::SetActivity(ERiptideOnlineActivity NewActivity, const FText& Status)
{
	Activity = NewActivity;
	StatusText = Status;
	if (!Status.IsEmpty())
	{
		UE_LOG(LogRiptideOnline, Log, TEXT("%s"), *Status.ToString());
	}
}

// --- The profile ---

FString URiptideGameInstance::GetCallsign() const
{
	return Profile ? Profile->Callsign : FString();
}

FRiptideAppearance URiptideGameInstance::GetAppearance() const
{
	return Profile ? Profile->Appearance : FRiptideAppearance();
}

FString URiptideGameInstance::SanitiseCallsign(const FString& Callsign)
{
	// Only what's safe in a URL option (?Name=...) and readable in the crew list.
	FString Clean;
	for (const TCHAR C : Callsign.TrimStartAndEnd())
	{
		if ((FChar::IsAlnum(C) && C < 128) || C == TEXT('-') || C == TEXT('_') || C == TEXT('.'))
		{
			Clean.AppendChar(C);
		}
		else if (C == TEXT(' ') && !Clean.EndsWith(TEXT("-")))
		{
			Clean.AppendChar(TEXT('-'));
		}
	}
	return Clean.Left(MaxCallsign);
}

void URiptideGameInstance::SetProfile(const FString& Callsign, const FRiptideAppearance& Look, bool bSave)
{
	if (!Profile)
	{
		Profile = URiptideProfileSave::LoadOrCreate();
	}
	const FString Clean = SanitiseCallsign(Callsign);
	if (!Clean.IsEmpty())
	{
		Profile->Callsign = Clean;
	}
	Profile->Appearance = Look;
	Profile->Appearance.Sanitise();
	if (bSave)
	{
		Profile->Save();
	}
}

void URiptideGameInstance::SetProfileFromText(const FString& Callsign, const FString& LookText, bool bSave)
{
	SetProfile(Callsign, FRiptideAppearance::FromString(LookText), bSave);
}

FString URiptideGameInstance::GetPlayerOptions() const
{
	return FString::Printf(TEXT("Name=%s?Look=%s"), *GetCallsign(), *GetAppearance().ToString());
}

// --- Hosting ---

bool URiptideGameInstance::HostGame(bool bFriendsOnly)
{
	if (Activity != ERiptideOnlineActivity::None)
	{
		return false;
	}
	// Friends only is a Steam lobby setting; without Steam the game is open to the local network and by IP.
	bHostedFriendsOnly = bFriendsOnly && IsUsingSteam();
	SetActivity(ERiptideOnlineActivity::Hosting, LOCTEXT("SettingUp", "Setting up the game..."));
	DestroySessionThen([this]() { CreateSession(); });
	return true;
}

void URiptideGameInstance::CreateSession()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		TravelToHostedGame();
		return;
	}
	const bool bSteam = IsUsingSteam();
	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = MaxCrew;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = !bSteam;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bAllowInvites = true;
	// A Steam lobby, tied to the host's Steam presence: it shows in the browser (or only to friends), friends can
	// join from their friends list, and the overlay can send invites to it.
	Settings.bUsesPresence = bSteam;
	Settings.bUseLobbiesIfAvailable = bSteam;
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = bHostedFriendsOnly;
	Settings.Set(SETTING_MAPNAME, FString(TEXT("Ocean_Test")), EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(KeyGame, FString(GameTag), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyCallsign, GetCallsign(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(KeyFriendsOnly, bHostedFriendsOnly, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SetActivity(ERiptideOnlineActivity::Hosting, LOCTEXT("CreatingSession", "Creating the session..."));
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings) && Activity == ERiptideOnlineActivity::Hosting
		&& !HasSession())
	{
		OnCreateSessionComplete(NAME_GameSession, false);
	}
}

void URiptideGameInstance::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName != NAME_GameSession || Activity != ERiptideOnlineActivity::Hosting)
	{
		return;
	}
	if (!bWasSuccessful)
	{
		// The game still goes ahead: the crew can join by IP address, it just isn't listed.
		UE_LOG(LogRiptideOnline, Warning, TEXT("Couldn't create the online session; hosting unlisted (join by IP)"));
	}
	TravelToHostedGame();
}

void URiptideGameInstance::TravelToHostedGame()
{
	SetActivity(ERiptideOnlineActivity::Hosting, LOCTEXT("LoadingGame", "Loading the game..."));
	UGameplayStatics::OpenLevel(this, FName(GameMap), true, FString(TEXT("listen?")) + GetPlayerOptions());
}

// --- Finding games ---

bool URiptideGameInstance::FindGames()
{
	if (Activity != ERiptideOnlineActivity::None)
	{
		return false;
	}
	IOnlineSessionPtr Sessions = GetSessions();
	Results.Reset();
	Listings.Reset();
	++ListingsVersion;
	SearchSummary = FText::GetEmpty();
	if (!Sessions)
	{
		SearchSummary = LOCTEXT("NoOnline", "Online play isn't available. Join by IP address.");
		return false;
	}
	const bool bSteam = IsUsingSteam();
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = !bSteam;
	Search->MaxSearchResults = 100;
	Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(KeyGame, FString(GameTag), EOnlineComparisonOp::Equals);
	SetActivity(ERiptideOnlineActivity::Searching, bSteam ? LOCTEXT("SearchingSteam", "Searching Steam for games...")
		: LOCTEXT("SearchingLan", "Searching the local network for games..."));
	GetTimerManager().SetTimer(SearchTimeout, this, &URiptideGameInstance::FinishSearch, SearchTimeoutSeconds, false);
	if (!Sessions->FindSessions(0, Search.ToSharedRef()) && Activity == ERiptideOnlineActivity::Searching)
	{
		FinishSearch();
	}
	return true;
}

void URiptideGameInstance::OnFindSessionsComplete(bool bWasSuccessful)
{
	if (Activity != ERiptideOnlineActivity::Searching || !Search)
	{
		return;
	}
	for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
	{
		AddResult(Result, false);
	}
	// Then each Steam friend who's playing Riptide right now: friends-only games never show in the public list.
	IOnlineFriendsPtr Friends = IsUsingSteam() ? Online::GetFriendsInterface(GetWorld()) : nullptr;
	if (!Friends || !Friends->ReadFriendsList(0, EFriendsLists::ToString(EFriendsLists::Default),
		FOnReadFriendsListComplete::CreateUObject(this, &URiptideGameInstance::OnReadFriendsComplete)))
	{
		FinishSearch();
	}
}

void URiptideGameInstance::OnReadFriendsComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr)
{
	if (Activity != ERiptideOnlineActivity::Searching)
	{
		return;
	}
	FriendsToSearch.Reset();
	TArray<TSharedRef<FOnlineFriend>> Friends;
	if (const IOnlineFriendsPtr FriendsInterface = Online::GetFriendsInterface(GetWorld()); bWasSuccessful && FriendsInterface)
	{
		FriendsInterface->GetFriendsList(LocalUserNum, ListName, Friends);
	}
	for (const TSharedRef<FOnlineFriend>& Friend : Friends)
	{
		if (Friend->GetPresence().bIsPlayingThisGame)
		{
			FriendsToSearch.Add(Friend->GetUserId());
		}
	}
	FindNextFriendSession();
}

void URiptideGameInstance::FindNextFriendSession()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (FriendsToSearch.IsEmpty() || !Sessions)
	{
		FinishSearch();
		return;
	}
	// One friend at a time (Steam runs one friend search at once). It always answers through the delegate, even
	// when it can't search.
	const FUniqueNetIdRef Friend = FriendsToSearch.Pop();
	Sessions->FindFriendSession(0, *Friend);
}

void URiptideGameInstance::OnFindFriendSessionComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FOnlineSessionSearchResult>& Found)
{
	if (Activity != ERiptideOnlineActivity::Searching)
	{
		return;
	}
	for (const FOnlineSessionSearchResult& Result : Found)
	{
		AddResult(Result, true);
	}
	FindNextFriendSession();
}

void URiptideGameInstance::AddResult(const FOnlineSessionSearchResult& Result, bool bFriend)
{
	FString Tag;
	if (!Result.IsValid() || !Result.Session.SessionSettings.Get(KeyGame, Tag) || Tag != GameTag)
	{
		return;
	}
	for (int32 i = 0; i < Results.Num(); ++i)
	{
		if (Results[i].GetSessionIdStr() == Result.GetSessionIdStr())
		{
			Listings[i].bFriend |= bFriend;
			return;
		}
	}
	FRiptideGameListing Listing;
	if (!Result.Session.SessionSettings.Get(KeyCallsign, Listing.HostName) || Listing.HostName.IsEmpty())
	{
		Listing.HostName = Result.Session.OwningUserName;
	}
	Listing.MaxPlayers = FMath::Max(1, Result.Session.SessionSettings.NumPublicConnections);
	Listing.Players = FMath::Clamp(Listing.MaxPlayers - Result.Session.NumOpenPublicConnections, 1, Listing.MaxPlayers);
	// Steam leaves lobbies' ping at its "unknown" maximum (9999).
	Listing.PingMs = Result.PingInMs >= 0 && Result.PingInMs < 9999 ? Result.PingInMs : -1;
	Listing.bFriend = bFriend;
	Result.Session.SessionSettings.Get(KeyFriendsOnly, Listing.bFriendsOnly);
	Results.Add(Result);
	Listings.Add(Listing);
}

void URiptideGameInstance::FinishSearch()
{
	if (Activity != ERiptideOnlineActivity::Searching)
	{
		return;
	}
	GetTimerManager().ClearTimer(SearchTimeout);
	FriendsToSearch.Reset();
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Search && Search->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		Sessions->CancelFindSessions();
	}
	const bool bSteam = IsUsingSteam();
	if (Listings.IsEmpty())
	{
		SearchSummary = bSteam ? LOCTEXT("NoneSteam", "No games found. Ask a friend to invite you, or join by IP.")
			: LOCTEXT("NoneLan", "No games found on the local network. Join by IP to play over the internet.");
	}
	else
	{
		SearchSummary = FText::Format(LOCTEXT("Found", "{0} {0}|plural(one=game,other=games) found."), Listings.Num());
	}
	++ListingsVersion;
	SetActivity(ERiptideOnlineActivity::None);
	UE_LOG(LogRiptideOnline, Log, TEXT("Search done: %s"), *SearchSummary.ToString());
}

// --- Joining ---

bool URiptideGameInstance::JoinGame(int32 Index)
{
	if (Activity != ERiptideOnlineActivity::None || !Results.IsValidIndex(Index))
	{
		return false;
	}
	const FOnlineSessionSearchResult Result = Results[Index];
	SetActivity(ERiptideOnlineActivity::Joining,
		FText::Format(LOCTEXT("JoiningGame", "Joining {0}'s game..."), FText::FromString(Listings[Index].HostName)));
	DestroySessionThen([this, Result]() { JoinSearchResult(Result); });
	return true;
}

void URiptideGameInstance::JoinSearchResult(const FOnlineSessionSearchResult& Result)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions)
	{
		MenuMessage = LOCTEXT("NoSessions", "Online play isn't available.");
		SetActivity(ERiptideOnlineActivity::None);
		return;
	}
	if (!Sessions->JoinSession(0, NAME_GameSession, Result) && Activity == ERiptideOnlineActivity::Joining && !HasSession())
	{
		OnJoinSessionComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
	}
}

void URiptideGameInstance::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (SessionName != NAME_GameSession || Activity != ERiptideOnlineActivity::Joining)
	{
		return;
	}
	FString Address;
	IOnlineSessionPtr Sessions = GetSessions();
	if ((Result == EOnJoinSessionCompleteResult::Success || Result == EOnJoinSessionCompleteResult::AlreadyInSession)
		&& Sessions && Sessions->GetResolvedConnectString(NAME_GameSession, Address) && !Address.IsEmpty())
	{
		TravelToAddress(Address);
		return;
	}
	switch (Result)
	{
	case EOnJoinSessionCompleteResult::SessionIsFull:
		MenuMessage = LOCTEXT("Full", "That game is full.");
		break;
	case EOnJoinSessionCompleteResult::SessionDoesNotExist:
		MenuMessage = LOCTEXT("Gone", "That game has ended.");
		break;
	case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
		MenuMessage = LOCTEXT("NoAddress", "Couldn't reach the host of that game.");
		break;
	default:
		MenuMessage = LOCTEXT("JoinFailed", "Couldn't join that game.");
		break;
	}
	SetActivity(ERiptideOnlineActivity::None);
	DestroySessionThen([]() {});
}

bool URiptideGameInstance::JoinByAddress(const FString& Address)
{
	if (Activity != ERiptideOnlineActivity::None)
	{
		return false;
	}
	// An address is a host name or IP, maybe with a port: anything else (spaces, a stray ?option) is cut off.
	FString Clean;
	for (const TCHAR C : Address.TrimStartAndEnd())
	{
		if ((FChar::IsAlnum(C) && C < 128) || C == TEXT('.') || C == TEXT(':') || C == TEXT('-') || C == TEXT('[') || C == TEXT(']'))
		{
			Clean.AppendChar(C);
		}
	}
	if (Clean.IsEmpty())
	{
		MenuMessage = LOCTEXT("NoAddressGiven", "Type the host's IP address first.");
		return false;
	}
	SetActivity(ERiptideOnlineActivity::Joining, FText::Format(LOCTEXT("Connecting", "Connecting to {0}..."), FText::FromString(Clean)));
	DestroySessionThen([this, Clean]() { TravelToAddress(Clean); });
	return true;
}

void URiptideGameInstance::TravelToAddress(const FString& Address)
{
	APlayerController* Player = GetFirstLocalPlayerController();
	if (!Player)
	{
		MenuMessage = LOCTEXT("NoPlayer", "Couldn't connect: no local player.");
		SetActivity(ERiptideOnlineActivity::None);
		return;
	}
	SetActivity(ERiptideOnlineActivity::Joining, FText::Format(LOCTEXT("ConnectingTo", "Connecting to {0}..."), FText::FromString(Address)));
	Player->ClientTravel(Address + TEXT("?") + GetPlayerOptions(), TRAVEL_Absolute);
}

void URiptideGameInstance::CancelJoin()
{
	if (Activity != ERiptideOnlineActivity::Joining)
	{
		return;
	}
	if (GEngine && GetWorld())
	{
		GEngine->CancelPending(GetWorld());
	}
	SetActivity(ERiptideOnlineActivity::None);
	DestroySessionThen([]() {});
}

void URiptideGameInstance::OnInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId,
	const FOnlineSessionSearchResult& Invite)
{
	if (!bWasSuccessful || !Invite.IsValid())
	{
		MenuMessage = LOCTEXT("InviteFailed", "Couldn't join your friend's game.");
		return;
	}
	// Whatever we were doing, the invite wins: out of this game (or search) and into theirs.
	GetTimerManager().ClearTimer(SearchTimeout);
	SetActivity(ERiptideOnlineActivity::Joining, LOCTEXT("JoiningFriend", "Joining your friend's game..."));
	DestroySessionThen([this, Invite]() { JoinSearchResult(Invite); });
}

// --- Leaving ---

void URiptideGameInstance::LeaveGame()
{
	SetActivity(ERiptideOnlineActivity::Leaving, LOCTEXT("Leaving", "Leaving the game..."));
	DestroySessionThen([this]() { OpenMenu(); });
}

void URiptideGameInstance::QuitToDesktop()
{
	SetActivity(ERiptideOnlineActivity::Leaving, LOCTEXT("Quitting", "Quitting..."));
	DestroySessionThen([this]()
	{
		UKismetSystemLibrary::QuitGame(GetWorld(), GetFirstLocalPlayerController(), EQuitPreference::Quit, false);
	});
}

bool URiptideGameInstance::ShowInviteOverlay()
{
	const IOnlineExternalUIPtr Overlay = Online::GetExternalUIInterface(GetWorld());
	return IsUsingSteam() && Overlay && HasSession() && Overlay->ShowInviteUI(0, NAME_GameSession);
}

void URiptideGameInstance::OpenMenu()
{
	UGameplayStatics::OpenLevel(this, FName(MenuMap));
}

void URiptideGameInstance::DestroySessionThen(TFunction<void()> Then)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions || !Sessions->GetNamedSession(NAME_GameSession))
	{
		Then();
		return;
	}
	AfterDestroy = MoveTemp(Then);
	GetTimerManager().SetTimer(DestroyTimeout, this, &URiptideGameInstance::RunAfterDestroy, DestroyTimeoutSeconds, false);
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		RunAfterDestroy();
	}
}

void URiptideGameInstance::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (SessionName == NAME_GameSession)
	{
		RunAfterDestroy();
	}
}

void URiptideGameInstance::RunAfterDestroy()
{
	GetTimerManager().ClearTimer(DestroyTimeout);
	TFunction<void()> Then = MoveTemp(AfterDestroy);
	AfterDestroy = nullptr;
	if (Then)
	{
		Then();
	}
}

// --- Maps loading, and things going wrong ---

void URiptideGameInstance::OnPostLoadMap(UWorld* World)
{
	if (!World || World->GetGameInstance() != this)
	{
		return;
	}
	// Whatever brought us here (hosting, joining, leaving) is done.
	if (Activity != ERiptideOnlineActivity::Searching)
	{
		SetActivity(ERiptideOnlineActivity::None);
	}
	URiptideSettingsSave::Get()->Apply(World);
	// Back at the menu, still in a session (the connection dropped, or the host left): out of it, so the lobby
	// doesn't keep a ghost member and the next game starts clean.
	if (GetCurrentMapName() == FPaths::GetBaseFilename(MenuMap) && HasSession())
	{
		DestroySessionThen([]() {});
	}
}

void URiptideGameInstance::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error)
{
	if (World && World->GetGameInstance() != this)
	{
		return;     // another play-in-editor window's
	}
	if (NetDriver && NetDriver->NetDriverName != NAME_GameNetDriver && NetDriver->NetDriverName != NAME_PendingNetDriver)
	{
		return;     // not the game's own connection
	}
	if (Activity == ERiptideOnlineActivity::Leaving)
	{
		return;     // we hung up ourselves
	}
	UE_LOG(LogRiptideOnline, Warning, TEXT("Network failure %s: %s"), ENetworkFailure::ToString(FailureType), *Error);
	switch (FailureType)
	{
	case ENetworkFailure::PendingConnectionFailure:
		MenuMessage = Error.IsEmpty() ? LOCTEXT("ConnectFailed", "Couldn't connect to that game.")
			: FText::Format(LOCTEXT("ConnectFailedWhy", "Couldn't connect to that game: {0}"), FText::FromString(Error));
		break;
	case ENetworkFailure::ConnectionTimeout:
		MenuMessage = World && World->GetNetMode() == NM_Client ? LOCTEXT("TimedOut", "The connection to the host timed out.")
			: LOCTEXT("ConnectTimedOut", "Nobody answered at that address. Check it, and that the host's game is running.");
		break;
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::FailureReceived:
		MenuMessage = LOCTEXT("HostLeft", "The connection to the host was lost: they may have ended the game.");
		break;
	case ENetworkFailure::NetDriverListenFailure:
	case ENetworkFailure::NetDriverCreateFailure:
		MenuMessage = FText::Format(LOCTEXT("ListenFailed", "Couldn't host the game: {0}"), FText::FromString(Error));
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		MenuMessage = LOCTEXT("Version", "That game is running a different version of Riptide.");
		break;
	default:
		MenuMessage = FText::Format(LOCTEXT("NetError", "Network error: {0}"), FText::FromString(Error));
		break;
	}
	// The engine takes the player back to the main menu itself (the default map); the session goes with it.
	SetActivity(ERiptideOnlineActivity::None);
}

void URiptideGameInstance::OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if (World && World->GetGameInstance() != this)
	{
		return;
	}
	UE_LOG(LogRiptideOnline, Warning, TEXT("Travel failure %s: %s"), ETravelFailure::ToString(FailureType), *Error);
	switch (FailureType)
	{
	case ETravelFailure::NoLevel:
	case ETravelFailure::LoadMapFailure:
		MenuMessage = FText::Format(LOCTEXT("MapFailed", "Couldn't load the map: {0}"), FText::FromString(Error));
		break;
	default:
		MenuMessage = Error.IsEmpty() ? LOCTEXT("TravelFailed", "Couldn't get into that game.")
			: FText::Format(LOCTEXT("TravelFailedWhy", "Couldn't get into that game: {0}"), FText::FromString(Error));
		break;
	}
	SetActivity(ERiptideOnlineActivity::None);
}

#undef LOCTEXT_NAMESPACE
