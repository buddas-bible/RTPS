#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "RTPSLobbyGameMode.generated.h"

UCLASS()
class RTPS_API ARTPSLobbyGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ARTPSLobbyGameMode();

	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	void SetPlayerReadyState(APlayerController* PlayerController, bool bReady);
	bool SelectQuestByOffset(APlayerController* RequestingPlayer, int32 Direction, FString* OutFailureReason = nullptr);
	bool TryStartQuest(APlayerController* RequestingPlayer, FString* OutFailureReason = nullptr);
	void HandleChatMessage(class ARTPSCommonPlayerController* FromPlayer, const FString& MessageBody);

protected:
	bool CanStartQuest(FString* OutFailureReason = nullptr) const;
	void RefreshLobbyState() const;
	bool AreAllPlayersReady() const;
	bool IsHostPlayer(const APlayerController* PlayerController) const;
	void ApplySelectedQuestToLobbyState() const;
	void BroadcastQuestSelection(const FString& QuestName) const;
	void BroadcastChatMessage(const FString& Message) const;
	void MaybeRunAutoLobbyFlow();

	bool bAutoQuestSelectionApplied = false;
	bool bAutoQuestStartTriggered = false;
};
