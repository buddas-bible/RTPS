#include "Game/RTPSGameInstance.h"

#include "Engine/World.h"
#include <Kismet/GameplayStatics.h>
#include <Misc/CommandLine.h>
#include <Misc/Parse.h>
#include <Online/OnlineSessionNames.h>
#include <OnlineSessionSettings.h>
#include <OnlineSubsystem.h>


#pragma region Static runtime defaults
FString URTPSGameInstance::GetMainMenuMapPath()
{
	return TEXT("/Game/ThirdPerson/Maps/MainMenuMap");
}

FString URTPSGameInstance::GetLobbyMapPath()
{
	return TEXT("/Game/TestLevel/CharacterTestLevel");
}

FString URTPSGameInstance::GetQuestMapPath()
{
	return TEXT("/Game/ThirdPerson/Maps/VoxelTestMap");
}

FString URTPSGameInstance::GetLobbyTravelPath()
{
	return GetLobbyMapPath() + TEXT("?listen?game=/Script/RTPS.RTPSLobbyGameMode");
}

FString URTPSGameInstance::GetQuestTravelPath()
{
	return BuildQuestTravelPath(GetQuestMapPath());
}

FString URTPSGameInstance::BuildQuestTravelPath(const FString& QuestMapPath)
{
	const FString ResolvedMapPath = QuestMapPath.IsEmpty() ? GetQuestMapPath() : QuestMapPath;
	return ResolvedMapPath + TEXT("?listen?game=/Script/RTPS.RTPSGameMode");
}

FString URTPSGameInstance::GetDefaultQuestName()
{
	return TEXT("Village Outpost Hunt");
}

FString URTPSGameInstance::GetLobbyMapName()
{
	return TEXT("CharacterTestLevel");
}

int32 URTPSGameInstance::GetMaxLocalChatLogMessages()
{
	return 200;
}
#pragma endregion

#pragma region Session discovery / hosting
void URTPSGameInstance::CreateGameSession()
{
	UE_LOG(LogTemp, Warning, TEXT("CreateSession"));
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] CreateGameSession invoked."));

	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] CreateGameSession aborted because SessionInterface is invalid."));
		return;
	}

	if (SessionInterface->GetNamedSession( HostedSessionName ) )
		SessionInterface->DestroySession( HostedSessionName );

	FOnlineSessionSettings SessionSettings;
	const bool bIsLanMatch = IOnlineSubsystem::Get()->GetSubsystemName() == "NULL";
	SessionSettings.NumPublicConnections = 3;
	// SessionSettings.NumPrivateConnections = 3;
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bIsLANMatch = bIsLanMatch;
	SessionSettings.bIsDedicated = false;
	// SessionSettings.bUsesStats = false;
	// SessionSettings.bAllowInvites = true;
	SessionSettings.bUsesPresence = !bIsLanMatch;
	SessionSettings.bAllowJoinViaPresence = !bIsLanMatch;
	// SessionSettings.bAllowJoinViaPresenceFriendsOnly = false;
	// SessionSettings.bAntiCheatProtected = false;
	SessionSettings.bUseLobbiesIfAvailable = !bIsLanMatch;
	// SessionSettings.bUseLobbiesVoiceChatIfAvailable = false;

	// SessionSettings.Set(FName("SERVER_NAME"), FString("RTPS Lobby"), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set( FName( "MatchType" ), FString( "FreeForAll" ), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing );

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Create session settings. IsLanMatch=%d UsesPresence=%d UsesLobbies=%d"),
		bIsLanMatch,
		SessionSettings.bUsesPresence,
		SessionSettings.bUseLobbiesIfAvailable);

	// SessionInterface->CreateSession( 0, HostedSessionName, SessionSettings );
	if ( const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController() )
		SessionInterface->CreateSession( *LocalPlayer->GetPreferredUniqueNetId(), HostedSessionName, SessionSettings);
}

void URTPSGameInstance::FindGameSessions()
{
	if( !SessionInterface.IsValid() )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] FindGameSessions aborted because SessionInterface is invalid." ) );
		return;
	}

	if( SessionSearch.IsValid() && SessionSearch->SearchState == EOnlineAsyncTaskState::InProgress )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] FindGameSessions skipped because a search is already in progress." ) );
		return;
	}

	CachedServerList.Reset();
	SessionSearch = MakeShareable( new FOnlineSessionSearch() );

	const bool bIsLanQuery = IOnlineSubsystem::Get()->GetSubsystemName() == "NULL";
	SessionSearch->bIsLanQuery = bIsLanQuery;
	SessionSearch->MaxSearchResults = 100;

	if( !bIsLanQuery )
		SessionSearch->QuerySettings.Set( SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals );

	UE_LOG( LogTemp, Log, TEXT( "[RTPSValidation] FindGameSessions invoked. IsLanQuery=%d" ), bIsLanQuery );

	// SessionInterface->FindSessions( 0, SessionSearch.ToSharedRef() );
	if( const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController() )
		SessionInterface->FindSessions( *LocalPlayer->GetPreferredUniqueNetId(), SessionSearch.ToSharedRef() );
}

bool URTPSGameInstance::JoinGameSessionByIndex( int32 SessionIndex )
{
	if( !SessionInterface.IsValid() )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] JoinGameSessionByIndex aborted because SessionInterface is invalid." ) );
		return false;
	}

	if( !SessionSearch.IsValid() || !SessionSearch->SearchResults.IsValidIndex( SessionIndex ) )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] JoinGameSessionByIndex received an invalid session index: %d" ), SessionIndex );
		return false;
	}

	const FOnlineSessionSearchResult SearchResult = SessionSearch->SearchResults[SessionIndex];

	//if( SessionInterface->GetNamedSession( HostedSessionName ) != nullptr )
	//{
	//	UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] Existing local session found. Destroying before join retry." ) );

	//	bPendingJoinAfterDestroy = true;
	//	PendingJoinSessionIndex = SessionIndex;
	//	SessionInterface->DestroySession( HostedSessionName );
	//	return true;
	//}

	UE_LOG( LogTemp, Warning, TEXT( "Joining selected server index : %d" ), SessionIndex );
	// return SessionInterface->JoinSession( 0, HostedSessionName, SessionSearch->SearchResults[SessionIndex] );

	FString MatchType;
	SearchResult.Session.SessionSettings.Get( FName( "MatchType" ), MatchType );
	if ( MatchType != FString("FreeForAll") )
	{
		bPendingJoinAfterDestroy = true;
		PendingJoinSessionIndex = SessionIndex;
		SessionInterface->DestroySession( HostedSessionName );
		return true;
	}

	bool bResult = false;
	if( const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController() )
		bResult = SessionInterface->JoinSession( *LocalPlayer->GetPreferredUniqueNetId(), HostedSessionName, SearchResult );
	
	return bResult;
}

void URTPSGameInstance::StartHostedSession()
{
	if( !SessionInterface.IsValid() )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] StartHostedSession aborted because SessionInterface is invalid." ) );
		return;
	}

	if( SessionInterface->GetNamedSession( HostedSessionName ) == nullptr )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] StartHostedSession skipped because no hosted session exists." ) );
		return;
	}

	FString StartFailureReason;
	EOnlineSessionState::Type SessionState = EOnlineSessionState::NoSession;
	if (!CanStartHostedSession(StartFailureReason, SessionState))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] StartHostedSession skipped. SessionName=%s State=%s Reason=%s"),
			*HostedSessionName.ToString(),
			EOnlineSessionState::ToString(SessionState),
			*StartFailureReason);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] StartHostedSession calling StartSession. SessionName=%s State=%s"),
		*HostedSessionName.ToString(),
		EOnlineSessionState::ToString(SessionState));
	SessionInterface->StartSession(HostedSessionName);
}

bool URTPSGameInstance::CanStartHostedSession(FString& OutReason, EOnlineSessionState::Type& OutSessionState) const
{
	OutReason.Reset();
	OutSessionState = EOnlineSessionState::NoSession;

	if (!SessionInterface.IsValid())
	{
		OutReason = TEXT("SessionInterface is invalid.");
		return false;
	}

	if (SessionInterface->GetNamedSession(HostedSessionName) == nullptr)
	{
		OutReason = TEXT("No hosted session exists.");
		return false;
	}

	OutSessionState = SessionInterface->GetSessionState(HostedSessionName);

	switch( OutSessionState )
	{
	case EOnlineSessionState::Pending:
		OutReason = TEXT( "Session is pending and can be started." );
		return true;

	case EOnlineSessionState::Starting:
	case EOnlineSessionState::InProgress:
		OutReason = TEXT( "Session is already active or starting." );
		return false;

	case EOnlineSessionState::Creating:
		OutReason = TEXT( "Session is still being created and cannot be started yet." );
		return false;

	case EOnlineSessionState::Ending:
		OutReason = TEXT( "Session is ending and cannot be started." );
		return false;

	case EOnlineSessionState::Ended:
		OutReason = TEXT( "Session has ended and cannot be started without a new session lifecycle." );
		return false;

	case EOnlineSessionState::Destroying:
		OutReason = TEXT( "Session is being destroyed and cannot be started." );
		return false;

	case EOnlineSessionState::NoSession:
		OutReason = TEXT( "No active hosted session state is available." );
		return false;

	default:
		OutReason = TEXT( "Session is in an unexpected non-startable state." );
		return false;
	}
}
#pragma endregion

#pragma region Cached runtime state accessors
TArray<FServerInfo> URTPSGameInstance::GetCachedServerList() const
{
	return CachedServerList;
}

const TArray<FRTPSDemoQuestDefinition>& URTPSGameInstance::GetDemoQuestDefinitions() const
{
	return DemoQuestDefinitions;
}

const FRTPSDemoQuestDefinition* URTPSGameInstance::GetSelectedQuestDefinition() const
{
	if( const FRTPSDemoQuestDefinition* SelectedQuest = FindQuestDefinitionById( PendingSelectedQuestId ) )
	{
		return SelectedQuest;
	}

	return DemoQuestDefinitions.Num() > 0 ? &DemoQuestDefinitions[0] : nullptr;
}


void URTPSGameInstance::AppendLocalChatMessage( const FString& Message )
{
	const FString TrimmedMessage = Message.TrimStartAndEnd();
	if( TrimmedMessage.IsEmpty() )
	{
		return;
	}

	LocalChatLog.Add( TrimmedMessage );

	int32 RemovedOldestCount = 0;
	const int32 MaxMessages = GetMaxLocalChatLogMessages();
	if( LocalChatLog.Num() > MaxMessages )
	{
		RemovedOldestCount = LocalChatLog.Num() - MaxMessages;
		LocalChatLog.RemoveAt( 0, RemovedOldestCount, EAllowShrinking::No );
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT( "[RTPSValidation] Local chat log appended. Count=%d Max=%d RemovedOldest=%d Message=%s" ),
		LocalChatLog.Num(),
		MaxMessages,
		RemovedOldestCount,
		*TrimmedMessage );
}

void URTPSGameInstance::ClearLocalChatLog()
{
	const int32 PreviousCount = LocalChatLog.Num();
	LocalChatLog.Reset();
	UE_LOG( LogTemp, Log, TEXT( "[RTPSValidation] Local chat log cleared. PreviousCount=%d Count=0" ), PreviousCount );
}

const TArray<FString>& URTPSGameInstance::GetLocalChatLog() const
{
	return LocalChatLog;
}

int32 URTPSGameInstance::GetLocalChatLogCount() const
{
	return LocalChatLog.Num();
}


bool URTPSGameInstance::SetPendingSelectedQuestById( FName QuestId )
{
	if( const FRTPSDemoQuestDefinition* QuestDefinition = FindQuestDefinitionById( QuestId ) )
	{
		PendingSelectedQuestId = QuestDefinition->QuestId;
		return true;
	}

	return false;
}

bool URTPSGameInstance::CyclePendingSelectedQuest( int32 Direction, FRTPSDemoQuestDefinition* OutSelectedQuest )
{
	if( DemoQuestDefinitions.Num() == 0 )
	{
		return false;
	}

	int32 CurrentIndex = 0;
	for( int32 QuestIndex = 0; QuestIndex < DemoQuestDefinitions.Num(); ++QuestIndex )
	{
		if( DemoQuestDefinitions[QuestIndex].QuestId == PendingSelectedQuestId )
		{
			CurrentIndex = QuestIndex;
			break;
		}
	}

	const int32 NextIndex = ( CurrentIndex + Direction + DemoQuestDefinitions.Num() ) % DemoQuestDefinitions.Num();
	PendingSelectedQuestId = DemoQuestDefinitions[NextIndex].QuestId;

	if( OutSelectedQuest != nullptr )
	{
		*OutSelectedQuest = DemoQuestDefinitions[NextIndex];
	}

	return true;
}

bool URTPSGameInstance::GetPendingSelectedQuest( FRTPSDemoQuestDefinition& OutQuest ) const
{
	if( const FRTPSDemoQuestDefinition* SelectedQuest = GetSelectedQuestDefinition() )
	{
		OutQuest = *SelectedQuest;
		return true;
	}

	return false;
}


bool URTPSGameInstance::ShouldAutoReadyInLobby() const
{
	return bAutoReadyInLobby;
}

bool URTPSGameInstance::ShouldAutoStartQuest() const
{
	return bAutoStartQuest;
}

bool URTPSGameInstance::ShouldAutoCompleteQuest() const
{
	return bAutoCompleteQuest;
}

int32 URTPSGameInstance::GetAutoExpectedPlayers() const
{
	return AutoExpectedPlayers;
}

FName URTPSGameInstance::GetAutoSelectedQuestId() const
{
	return AutoSelectedQuestId;
}
#pragma endregion

#pragma region UGameInstance lifecycle
void URTPSGameInstance::Init()
{
	Super::Init();
	InitializeDemoQuestDefinitions();
	ParseAutomationOptions();
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);

	if (IOnlineSubsystem* SubSystem = IOnlineSubsystem::Get())
	{
		UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Online subsystem in use: %s"), *SubSystem->GetSubsystemName().ToString());
		SessionInterface = SubSystem->GetSessionInterface();

		if (SessionInterface.IsValid())
		{
			SessionInterface->OnCreateSessionCompleteDelegates.AddUObject(this, &ThisClass::OnCreateSessionComplete);
			SessionInterface->OnFindSessionsCompleteDelegates.AddUObject(this, &ThisClass::OnFindSessionComplete);
			SessionInterface->OnJoinSessionCompleteDelegates.AddUObject(this, &ThisClass::OnJoinSessionComplete);
			SessionInterface->OnStartSessionCompleteDelegates.AddUObject(this, &ThisClass::OnStartSessionComplete);
			SessionInterface->OnDestroySessionCompleteDelegates.AddUObject(this, &ThisClass::OnDestroySessionComplete);
		}
	}
}

void URTPSGameInstance::Shutdown()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	Super::Shutdown();
}
#pragma endregion

#pragma region Session callbacks
void URTPSGameInstance::OnCreateSessionComplete(FName ServerName, bool Succeeded)
{
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Create session result. SessionName=%s Succeeded=%d"), *ServerName.ToString(), Succeeded);

	if (!Succeeded)
	{
		return;
	}

	if ( UWorld* World = GetWorld() )
	{
		const FString CurrentMapName = World->GetMapName();
		if ( !CurrentMapName.Contains( GetLobbyMapName() ) )
		{
			World->ServerTravel( GetLobbyTravelPath() );
		}
	}
}

void URTPSGameInstance::OnFindSessionComplete( bool Succeeded )
{
	UE_LOG(LogTemp, Warning, TEXT("OnFindSessionComplete, Succeeded : %d"), Succeeded);

	CachedServerList.Reset();

	if (!Succeeded || !SessionSearch.IsValid())
	{
		ServerListUpdatedDel.Broadcast();
		return;
	}

	const TArray<FOnlineSessionSearchResult>& SearchResults = SessionSearch->SearchResults;
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Session search completed. Succeeded=%d Results=%d"), Succeeded, SearchResults.Num());

	for ( const FOnlineSessionSearchResult& SearchResult : SearchResults )
	{
		FServerInfo ServerInfo;
		FString AdvertisedServerName;
		// if (!SearchResult.Session.SessionSettings.Get(FName("SERVER_NAME"), AdvertisedServerName) || AdvertisedServerName.IsEmpty())
		// {
		// 	AdvertisedServerName = SearchResult.Session.OwningUserName;
		// }
		// 
		// ServerInfo.ServerName = AdvertisedServerName;
		// ServerInfo.CurrentPlayers = SearchResult.Session.SessionSettings.NumPublicConnections - SearchResult.Session.NumOpenPublicConnections;
		// ServerInfo.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;
		// 
		// CachedServerList.Add(ServerInfo);
		// ServerListDel.Broadcast(ServerInfo);
		// UE_LOG(
		// 	LogTemp,
		// 	Log,
		// 	TEXT("[RTPSValidation] Search result %d: ServerName=%s CurrentPlayers=%d MaxPlayers=%d"),
		// 	SearchResultIndex,
		// 	*ServerInfo.ServerName,
		// 	ServerInfo.CurrentPlayers,
		// 	ServerInfo.MaxPlayers);

		const FString Id = SearchResult.GetSessionIdStr();
		const FString UserName = SearchResult.Session.OwningUserName;

		FString MatchType;
		if( !SearchResult.Session.SessionSettings.Get( FName( "MatchType" ), MatchType ) || AdvertisedServerName.IsEmpty() )
		{
			AdvertisedServerName = UserName;
		}

		ServerInfo.ServerName = AdvertisedServerName;
		ServerInfo.CurrentPlayers = SearchResult.Session.SessionSettings.NumPublicConnections - SearchResult.Session.NumOpenPublicConnections;
		ServerInfo.MaxPlayers = SearchResult.Session.SessionSettings.NumPublicConnections;

		CachedServerList.Add( ServerInfo );
		ServerListDel.Broadcast( ServerInfo );
		UE_LOG(
			LogTemp,
			Log,
			TEXT( "[RTPSValidation] Search result: Id=%s UserName=%s MatchType=%s" ),
			*Id, *UserName, *MatchType );
	}

	ServerListUpdatedDel.Broadcast();

	if (CachedServerList.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No lobby session found."));
	}

	if (bAutoJoinFirstSession && !bAutoJoinResolved && !bAutoJoinAttemptInProgress && SessionSearch.IsValid() && SessionSearch->SearchResults.Num() > 0)
	{
		bAutoJoinAttemptInProgress = true;
		UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Auto-joining the first discovered session."));
		if (!JoinGameSessionByIndex(0))
		{
			bAutoJoinAttemptInProgress = false;
			UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Auto-join request for session index 0 failed immediately."));
		}
	}
}

void URTPSGameInstance::OnJoinSessionComplete( FName SessionName, EOnJoinSessionCompleteResult::Type Result )
{
	UE_LOG( LogTemp, Log, TEXT( "[RTPSValidation] Join session result. SessionName=%s Result=%d" ), *SessionName.ToString(), static_cast< int32 >( Result ) );

	bAutoJoinAttemptInProgress = false;
	bAutoJoinResolved = ( Result == EOnJoinSessionCompleteResult::Success );

	if( UWorld* World = GetWorld() )
	{
		World->GetTimerManager().ClearTimer( AutoJoinSearchTimerHandle );

		if( !bAutoJoinResolved && bAutoJoinFirstSession )
		{
			BeginAutoJoinSearch( World );
		}
	}

	// 실패면 여기서 끝
	if( Result != EOnJoinSessionCompleteResult::Success )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] Join failed. Travel will not be attempted." ) );
		return;
	}

	if( !SessionInterface.IsValid() )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] Join succeeded but SessionInterface is invalid." ) );
		return;
	}

	FString JoinAddress;
	const bool bResolved = SessionInterface->GetResolvedConnectString( SessionName, JoinAddress );

	UE_LOG(
		LogTemp,
		Log,
		TEXT( "[RTPSValidation] Resolved connect string. Success=%d Address=%s" ),
		bResolved ? 1 : 0,
		JoinAddress.IsEmpty() ? TEXT( "<empty>" ) : *JoinAddress );

	if( !bResolved || JoinAddress.IsEmpty() || JoinAddress.Contains( TEXT( ":0" ) ) )
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] Invalid join address. Travel aborted." ) );
		return;
	}

	if( APlayerController* PC = GetFirstLocalPlayerController() )
	{
		PC->ClientTravel( JoinAddress, ETravelType::TRAVEL_Absolute );
	}
	else
	{
		UE_LOG( LogTemp, Warning, TEXT( "[RTPSValidation] No local player controller found for ClientTravel." ) );
	}
}

void URTPSGameInstance::OnStartSessionComplete(FName SessionName, bool Succeeded)
{
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Start session result. SessionName=%s Succeeded=%d"), *SessionName.ToString(), Succeeded);
}

void URTPSGameInstance::OnDestroySessionComplete( FName SessionName, bool bWasSuccessful )
{
	UE_LOG(
		LogTemp,
		Log,
		TEXT( "[RTPSValidation] Destroy session complete. SessionName=%s Success=%d PendingJoin=%d PendingIndex=%d" ),
		*SessionName.ToString(),
		bWasSuccessful ? 1 : 0,
		bPendingJoinAfterDestroy ? 1 : 0,
		PendingJoinSessionIndex );

	if( bPendingJoinAfterDestroy )
	{
		const int32 JoinIndex = PendingJoinSessionIndex;
		bPendingJoinAfterDestroy = false;
		PendingJoinSessionIndex = INDEX_NONE;

		if( bWasSuccessful )
		{
			JoinGameSessionByIndex( JoinIndex );
		}
	}
}
#pragma endregion

#pragma region Initialization / automation helpers
void URTPSGameInstance::InitializeDemoQuestDefinitions()
{
	if (DemoQuestDefinitions.Num() > 0)
	{
		return;
	}

	FRTPSDemoQuestDefinition VillageOutpostHunt;
	VillageOutpostHunt.QuestId = TEXT("village_outpost_hunt");
	VillageOutpostHunt.QuestName = TEXT("Village Outpost Hunt");
	VillageOutpostHunt.MapPath = GetQuestMapPath();
	VillageOutpostHunt.RequiredMinorKills = 3;
	VillageOutpostHunt.RequiredBossKills = 1;
	VillageOutpostHunt.RewardAmount = 100;
	DemoQuestDefinitions.Add(VillageOutpostHunt);

	FRTPSDemoQuestDefinition VillageOutpostSweep;
	VillageOutpostSweep.QuestId = TEXT("village_outpost_sweep");
	VillageOutpostSweep.QuestName = TEXT("Village Outpost Sweep");
	VillageOutpostSweep.MapPath = GetQuestMapPath();
	VillageOutpostSweep.RequiredMinorKills = 5;
	VillageOutpostSweep.RequiredBossKills = 0;
	VillageOutpostSweep.RewardAmount = 75;
	DemoQuestDefinitions.Add(VillageOutpostSweep);

	if (PendingSelectedQuestId.IsNone() && DemoQuestDefinitions.Num() > 0)
	{
		PendingSelectedQuestId = DemoQuestDefinitions[0].QuestId;
	}
}

void URTPSGameInstance::ParseAutomationOptions()
{
	const TCHAR* CommandLine = FCommandLine::Get();

	bAutoCreateSession = FParse::Param(CommandLine, TEXT("RTPSAutoCreateSession"));
	bAutoJoinFirstSession = FParse::Param(CommandLine, TEXT("RTPSAutoJoinFirstSession"));
	bAutoReadyInLobby = FParse::Param(CommandLine, TEXT("RTPSAutoReadyInLobby"));
	bAutoStartQuest = FParse::Param(CommandLine, TEXT("RTPSAutoStartQuest"));
	bAutoCompleteQuest = FParse::Param(CommandLine, TEXT("RTPSAutoCompleteQuest"));

	FString AutoQuestIdString;
	if (FParse::Value(CommandLine, TEXT("RTPSAutoQuestId="), AutoQuestIdString) && !AutoQuestIdString.IsEmpty())
	{
		AutoSelectedQuestId = FName(*AutoQuestIdString);
	}

	int32 ParsedExpectedPlayers = 0;
	if (FParse::Value(CommandLine, TEXT("RTPSAutoExpectedPlayers="), ParsedExpectedPlayers))
	{
		AutoExpectedPlayers = FMath::Max(1, ParsedExpectedPlayers);
	}

	if (AutoSelectedQuestId != NAME_None && FindQuestDefinitionById(AutoSelectedQuestId) == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Unknown RTPSAutoQuestId '%s'. The current pending quest will be used instead."), *AutoSelectedQuestId.ToString());
		AutoSelectedQuestId = NAME_None;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Automation options parsed. AutoCreate=%d AutoJoin=%d AutoReady=%d AutoStart=%d AutoComplete=%d ExpectedPlayers=%d AutoQuestId=%s"),
		bAutoCreateSession,
		bAutoJoinFirstSession,
		bAutoReadyInLobby,
		bAutoStartQuest,
		bAutoCompleteQuest,
		AutoExpectedPlayers,
		AutoSelectedQuestId == NAME_None ? TEXT("<none>") : *AutoSelectedQuestId.ToString());
}

const FRTPSDemoQuestDefinition* URTPSGameInstance::FindQuestDefinitionById( FName QuestId ) const
{
	for (const FRTPSDemoQuestDefinition& QuestDefinition : DemoQuestDefinitions)
	{
		if (QuestDefinition.QuestId == QuestId)
		{
			return &QuestDefinition;
		}
	}

	return nullptr;
}

void URTPSGameInstance::HandlePostLoadMap( UWorld* LoadedWorld )
{
	if (LoadedWorld == nullptr)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Post-load map hook reached map '%s'."), *LoadedWorld->GetMapName());

	if (LoadedWorld->GetMapName().Contains(TEXT("MainMenuMap")))
	{
		if (bAutoCreateSession && !bAutoCreateTriggered)
		{
			bAutoCreateTriggered = true;
			LoadedWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &URTPSGameInstance::CreateGameSession));
			UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Scheduled automatic host session creation from MainMenuMap."));
		}

		if (bAutoJoinFirstSession && !bAutoJoinResolved)
		{
			BeginAutoJoinSearch(LoadedWorld);
		}
	}
}

void URTPSGameInstance::BeginAutoJoinSearch( UWorld* LoadedWorld )
{
	if (LoadedWorld == nullptr)
	{
		return;
	}

	LoadedWorld->GetTimerManager().ClearTimer(AutoJoinSearchTimerHandle);
	LoadedWorld->GetTimerManager().SetTimer(AutoJoinSearchTimerHandle, this, &URTPSGameInstance::TickAutoJoinSearch, 2.0f, true, 0.25f);
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Started automatic join search timer."));
}

void URTPSGameInstance::TickAutoJoinSearch()
{
	if (bAutoJoinResolved || bAutoJoinAttemptInProgress)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Triggering automatic session search."));
	FindGameSessions();
}
#pragma endregion
