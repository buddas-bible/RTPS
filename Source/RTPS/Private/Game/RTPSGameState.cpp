#include "Game/RTPSGameState.h"

#include "Net/UnrealNetwork.h"
#include "State/RTPSPlayerState.h"

ARTPSGameState::ARTPSGameState()
	: MatchPhase(ERTPSMatchPhase::Lobby)
	, MinorKillTarget(0)
	, MinorKillCount(0)
	, BossKillTarget(0)
	, BossKillCount(0)
	, ConnectedPlayerCount(0)
	, ReadyPlayerCount(0)
	, bHasQuestResult(false)
	, bQuestSucceeded(false)
	, QuestRewardAmount(0)
	, ReturnCountdownSeconds(0)
{
}

void ARTPSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTPSGameState, MatchPhase);
	DOREPLIFETIME(ARTPSGameState, ActiveQuestId);
	DOREPLIFETIME(ARTPSGameState, ActiveQuestName);
	DOREPLIFETIME(ARTPSGameState, MinorKillTarget);
	DOREPLIFETIME(ARTPSGameState, MinorKillCount);
	DOREPLIFETIME(ARTPSGameState, BossKillTarget);
	DOREPLIFETIME(ARTPSGameState, BossKillCount);
	DOREPLIFETIME(ARTPSGameState, ConnectedPlayerCount);
	DOREPLIFETIME(ARTPSGameState, ReadyPlayerCount);
	DOREPLIFETIME(ARTPSGameState, bHasQuestResult);
	DOREPLIFETIME(ARTPSGameState, bQuestSucceeded);
	DOREPLIFETIME(ARTPSGameState, QuestRewardAmount);
	DOREPLIFETIME(ARTPSGameState, ReturnCountdownSeconds);
}

void ARTPSGameState::SetMatchPhase(ERTPSMatchPhase NewPhase)
{
	MatchPhase = NewPhase;
}

void ARTPSGameState::SetActiveQuestId(FName NewQuestId)
{
	ActiveQuestId = NewQuestId;
}

void ARTPSGameState::SetActiveQuestName(const FString& NewQuestName)
{
	ActiveQuestName = NewQuestName;
}

void ARTPSGameState::SetObjectives(int32 NewMinorTarget, int32 NewBossTarget)
{
	MinorKillTarget = FMath::Max(0, NewMinorTarget);
	BossKillTarget = FMath::Max(0, NewBossTarget);
}

void ARTPSGameState::SetProgress(int32 NewMinorKillCount, int32 NewBossKillCount)
{
	MinorKillCount = FMath::Clamp(NewMinorKillCount, 0, MinorKillTarget);
	BossKillCount = FMath::Clamp(NewBossKillCount, 0, BossKillTarget);
}

void ARTPSGameState::SetLobbyPopulation(int32 NewConnectedPlayers, int32 NewReadyPlayers)
{
	ConnectedPlayerCount = FMath::Max(0, NewConnectedPlayers);
	ReadyPlayerCount = FMath::Clamp(NewReadyPlayers, 0, ConnectedPlayerCount);
}

void ARTPSGameState::SetQuestResult(bool bNewHasResult, bool bNewSucceeded, int32 NewRewardAmount, int32 NewReturnCountdownSeconds)
{
	bHasQuestResult = bNewHasResult;
	bQuestSucceeded = bNewSucceeded;
	QuestRewardAmount = FMath::Max(0, NewRewardAmount);
	ReturnCountdownSeconds = FMath::Max(0, NewReturnCountdownSeconds);
}

void ARTPSGameState::ClearQuestResult()
{
	SetQuestResult(false, false, 0, 0);
}

TArray<FRTPSLobbyPlayerInfo> ARTPSGameState::GetLobbyPlayerInfos() const
{
	TArray<FRTPSLobbyPlayerInfo> PlayerInfos;
	PlayerInfos.Reserve(PlayerArray.Num());

	for (const APlayerState* PlayerState : PlayerArray)
	{
		const ARTPSPlayerState* RTPSPlayerState = Cast<ARTPSPlayerState>(PlayerState);
		if (RTPSPlayerState == nullptr)
		{
			continue;
		}

		FRTPSLobbyPlayerInfo PlayerInfo;
		PlayerInfo.PlayerName = RTPSPlayerState->GetPlayerName();
		PlayerInfo.bIsReady = RTPSPlayerState->IsLobbyReady();
		PlayerInfo.TotalReward = RTPSPlayerState->TotalReward;
		PlayerInfos.Add(PlayerInfo);
	}

	return PlayerInfos;
}

FRTPSQuestResultInfo ARTPSGameState::GetQuestResultInfo() const
{
	FRTPSQuestResultInfo ResultInfo;
	ResultInfo.bHasResult = bHasQuestResult;
	ResultInfo.bSucceeded = bQuestSucceeded;
	ResultInfo.QuestName = ActiveQuestName;
	ResultInfo.RewardAmount = QuestRewardAmount;
	ResultInfo.ReturnCountdownSeconds = ReturnCountdownSeconds;
	return ResultInfo;
}
