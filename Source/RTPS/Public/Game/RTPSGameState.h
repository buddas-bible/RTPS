#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "RTPSGameState.generated.h"

UENUM(BlueprintType)
enum class ERTPSMatchPhase : uint8
{
	Lobby UMETA(DisplayName = "Lobby"),
	TravelingToQuest UMETA(DisplayName = "Traveling To Quest"),
	InQuest UMETA(DisplayName = "In Quest"),
	QuestSucceeded UMETA(DisplayName = "Quest Succeeded"),
	QuestFailed UMETA(DisplayName = "Quest Failed"),
	ReturningToLobby UMETA(DisplayName = "Returning To Lobby"),
};

USTRUCT(BlueprintType)
struct FRTPSLobbyPlayerInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	bool bIsReady = false;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	int32 TotalReward = 0;
};

USTRUCT(BlueprintType)
struct FRTPSQuestResultInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bHasResult = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	FString QuestName;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 RewardAmount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Result")
	int32 ReturnCountdownSeconds = 0;
};

UCLASS()
class RTPS_API ARTPSGameState : public AGameState
{
	GENERATED_BODY()

public:
	ARTPSGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetMatchPhase(ERTPSMatchPhase NewPhase);
	void SetActiveQuestId(FName NewQuestId);
	void SetActiveQuestName(const FString& NewQuestName);
	void SetObjectives(int32 NewMinorTarget, int32 NewBossTarget);
	void SetProgress(int32 NewMinorKillCount, int32 NewBossKillCount);
	void SetLobbyPopulation(int32 NewConnectedPlayers, int32 NewReadyPlayers);
	void SetQuestResult(bool bNewHasResult, bool bNewSucceeded, int32 NewRewardAmount, int32 NewReturnCountdownSeconds);
	void ClearQuestResult();

	UFUNCTION(BlueprintPure, Category = "Lobby")
	TArray<FRTPSLobbyPlayerInfo> GetLobbyPlayerInfos() const;

	UFUNCTION(BlueprintPure, Category = "Result")
	FRTPSQuestResultInfo GetQuestResultInfo() const;

public:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	ERTPSMatchPhase MatchPhase;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	FName ActiveQuestId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	FString ActiveQuestName;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	int32 MinorKillTarget;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	int32 MinorKillCount;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	int32 BossKillTarget;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	int32 BossKillCount;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	int32 ConnectedPlayerCount;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Cycle")
	int32 ReadyPlayerCount;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Result")
	bool bHasQuestResult;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Result")
	bool bQuestSucceeded;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Result")
	int32 QuestRewardAmount;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Result")
	int32 ReturnCountdownSeconds;
};
