#pragma once

#include "CoreMinimal.h"
#include "Game/RTPSGameState.h"
#include "GameFramework/PlayerController.h"
#include "RTPSCommonPlayerController.generated.h"

class ARTPSHUD;

UCLASS()
class RTPS_API ARTPSCommonPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void PostSeamlessTravel() override;
	virtual void NotifyLoadedWorld(FName WorldPackageName, bool bFinalDest) override;
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void SetupInputComponent() override;

	void BindCommonInput();
	bool IsLobbyMap() const;
	bool IsQuestOrPlayMap() const;
	const ARTPSGameState* GetRTPSGameState() const;
	void ShowLobbyMessage(const FString& Message, const FColor& MessageColor);

public:
	UFUNCTION(BlueprintCallable)
	void ActivateChatting();

	UFUNCTION(BlueprintCallable)
	void SubmitChatMessage(const FString& Message);

	UFUNCTION(BlueprintPure)
	ERTPSMatchPhase GetCurrentMatchPhase() const;

	UFUNCTION(BlueprintPure)
	TArray<FRTPSLobbyPlayerInfo> GetLobbyPlayerInfos() const;

	UFUNCTION(BlueprintPure)
	FRTPSQuestResultInfo GetQuestResultInfo() const;

	UFUNCTION(BlueprintPure)
	bool IsLocalPlayerReady() const;

	UFUNCTION(BlueprintPure)
	bool ShouldShowQuestResult() const;

	UFUNCTION(BlueprintPure)
	bool IsInLobbyState() const;

	virtual TArray<FString> GetControlHintLines() const;

	UFUNCTION(Server, Reliable)
	void ServerSetPlayerName(const FString& NewName);

	UFUNCTION(Server, Reliable)
	void ServerSendChatMessage(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientReceiveChatMessage(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientNotifyLobbyMessage(const FString& Message, bool bIsError);

private:
	UPROPERTY()
	ARTPSHUD* RTPSHUD = nullptr;

	void RefreshChatHUD(const FString& Reason);
	void ScheduleChatHUDRefresh(const FString& Reason);
	void ClearChatHUDRefreshTimer();
	void ClearCommonTimers();
	bool CanUseChatHUD(const FString& Reason);
	void HandleDelayedChatHUDRefresh();
	bool HandleLocalChatCommand(const FString& TrimmedMessage);

	void BeginTemporaryCursorMode();
	void EndTemporaryCursorMode();
	void ApplyGameplayCursorMode(bool bEnableCursor);

	void TriggerAutoLobbyChat();
	void StartAutoLobbyChatBurst();
	void TriggerAutoLobbyChatBurstMessage();
	void TriggerAutoCleanChatOnReturn();

	bool bTemporaryCursorMode = false;
	bool bAutoLobbyChatTriggered = false;
	bool bAutoLobbyChatBurstTriggered = false;
	bool bAutoLocalCleanOnReturnTriggered = false;
	bool bHasReachedQuestMap = false;
	bool bAutoCleanChatOnReturn = false;
	int32 AutoLobbyChatBurstCount = 0;
	int32 AutoLobbyChatBurstIndex = 0;
	float AutoLobbyChatDelaySeconds = 4.0f;
	float AutoLobbyChatBurstDelaySeconds = 4.0f;
	float AutoLobbyChatBurstIntervalSeconds = 0.01f;
	FString AutoLobbyChatMessage;
	FString AutoLobbyChatBurstPrefix = TEXT("LobbyChatBurst");
	FTimerHandle AutoLobbyChatTimerHandle;
	FTimerHandle AutoLobbyChatBurstStartTimerHandle;
	FTimerHandle AutoLobbyChatBurstMessageTimerHandle;
	FTimerHandle AutoLocalCleanOnReturnTimerHandle;
	FTimerHandle ChatHUDRefreshTimerHandle;
	FString PendingChatHUDRefreshReason;
	bool bLoggedInvalidChatHUDContext = false;
};