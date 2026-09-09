#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RTPSPlayerState.generated.h"

UCLASS()
class RTPS_API ARTPSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRep_PlayerName() override;

	void SetLobbyReady(bool bNewLobbyReady);
	bool IsLobbyReady() const;
	void ResetLobbyState();
	void MarkQuestResult(bool bWasSuccessful, int32 RewardAmount);
	void SetTeamId(int32 NewTeamId);
	int32 GetTeamId() const;

public:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	bool bLobbyReady = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena")
	int32 TeamId = INDEX_NONE;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Quest")
	int32 TotalReward = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Quest")
	int32 LastQuestReward = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Quest")
	int32 CompletedQuestCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Quest")
	bool bLastQuestSucceeded = false;
};
