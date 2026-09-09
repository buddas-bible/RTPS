#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include <Interfaces/OnlineSessionInterface.h>
#include "RTPSGameInstance.generated.h"

USTRUCT(BlueprintType)
struct FRTPSDemoQuestDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName QuestId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FString QuestName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FString MapPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	int32 RequiredMinorKills = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	int32 RequiredBossKills = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	int32 RewardAmount = 0;
};

USTRUCT(BlueprintType)
struct FServerInfo
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	FString ServerName;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FServerDel, FServerInfo, ServerListDel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FServerListUpdatedDel);

// DECLARE_DELEGATE_TwoParams( FOnCreateSessionCompleteDelegate, FName /*SessionName*/, bool /*bWasSuccessful*/ );
// DECLARE_DELEGATE_OneParam( FOnFindSessionsCompleteDelegate, bool /*bWasSuccessful*/ );
// DECLARE_DELEGATE_TwoParams( FOnJoinSessionCompleteDelegate, FName /*SessionName*/, EOnJoinSessionCompleteResult::Type /*Result*/ );

UCLASS()
class RTPS_API URTPSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
#pragma region Static runtime defaults
	static FString GetMainMenuMapPath();
	static FString GetLobbyMapPath();
	static FString GetQuestMapPath();
	static FString GetLobbyTravelPath();
	static FString GetQuestTravelPath();
	static FString BuildQuestTravelPath(const FString& QuestMapPath);
	static FString GetDefaultQuestName();
	static FString GetLobbyMapName();
	static int32 GetMaxLocalChatLogMessages();
#pragma endregion

#pragma region Session discovery / hosting
	UPROPERTY(BlueprintAssignable)
	FServerDel ServerListDel;

	UPROPERTY(BlueprintAssignable)
	FServerListUpdatedDel ServerListUpdatedDel;

	UFUNCTION(BlueprintCallable)
	void CreateGameSession();

	UFUNCTION(BlueprintCallable)
	void FindGameSessions();

	UFUNCTION(BlueprintCallable)
	bool JoinGameSessionByIndex(int32 SessionIndex);

	void StartHostedSession();
#pragma endregion

#pragma region Cached runtime state accessors
	UFUNCTION(BlueprintPure)
	TArray<FServerInfo> GetCachedServerList() const;

	const TArray<FRTPSDemoQuestDefinition>& GetDemoQuestDefinitions() const;
	const FRTPSDemoQuestDefinition* GetSelectedQuestDefinition() const;

	void AppendLocalChatMessage(const FString& Message);
	void ClearLocalChatLog();
	const TArray<FString>& GetLocalChatLog() const;
	int32 GetLocalChatLogCount() const;

	bool SetPendingSelectedQuestById(FName QuestId);
	bool CyclePendingSelectedQuest(int32 Direction, FRTPSDemoQuestDefinition* OutSelectedQuest = nullptr);
	bool GetPendingSelectedQuest(FRTPSDemoQuestDefinition& OutQuest) const;

	bool ShouldAutoReadyInLobby() const;
	bool ShouldAutoStartQuest() const;
	bool ShouldAutoCompleteQuest() const;
	int32 GetAutoExpectedPlayers() const;
	FName GetAutoSelectedQuestId() const;
#pragma endregion

protected:
#pragma region UGameInstance lifecycle
	virtual void Init() override;
	virtual void Shutdown() override;
#pragma endregion

#pragma region Session callbacks
	virtual void OnCreateSessionComplete(FName ServerName, bool Succeeded);
	virtual void OnFindSessionComplete(bool Succeeded);
	virtual void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	virtual void OnStartSessionComplete(FName SessionName, bool Succeeded);
	virtual void OnDestroySessionComplete(FName SessionName, bool Succeeded);
#pragma endregion

#pragma region Initialization / automation helpers
	void InitializeDemoQuestDefinitions();
	void ParseAutomationOptions();
	const FRTPSDemoQuestDefinition* FindQuestDefinitionById(FName QuestId) const;
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void BeginAutoJoinSearch(UWorld* LoadedWorld);
	void TickAutoJoinSearch();
#pragma endregion

#pragma region Runtime state
	IOnlineSessionPtr SessionInterface;
	TSharedPtr<FOnlineSessionSearch> SessionSearch;
	TArray<FServerInfo> CachedServerList;
	TArray<FRTPSDemoQuestDefinition> DemoQuestDefinitions;
	TArray<FString> LocalChatLog;
	FName PendingSelectedQuestId = NAME_None;
	bool bAutoCreateSession = false;
	bool bAutoJoinFirstSession = false;
	bool bAutoReadyInLobby = false;
	bool bAutoStartQuest = false;
	bool bAutoCompleteQuest = false;
	bool bAutoCreateTriggered = false;
	bool bAutoJoinResolved = false;
	bool bAutoJoinAttemptInProgress = false;
	int32 AutoExpectedPlayers = 1;
	FName AutoSelectedQuestId = NAME_None;
	FTimerHandle AutoJoinSearchTimerHandle;
#pragma endregion

private:
	bool CanStartHostedSession(FString& OutReason, EOnlineSessionState::Type& OutSessionState) const;

	bool bPendingJoinAfterDestroy = false;
	int32 PendingJoinSessionIndex = INDEX_NONE;

	const FName HostedSessionName = TEXT( "RTPSDemoSession" );

	// FOnCreateSessionCompleteDelegate CreateSessionComplete;
	// FOnFindSessionsCompleteDelegate FindSessionComplete;
	// FOnJoinSessionCompleteDelegate JoinSessionComplete;
};
