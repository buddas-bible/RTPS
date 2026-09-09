#include "State/RTPSPlayerState.h"

#include "Net/UnrealNetwork.h"

void ARTPSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTPSPlayerState, bLobbyReady);
	DOREPLIFETIME(ARTPSPlayerState, TeamId);
	DOREPLIFETIME(ARTPSPlayerState, TotalReward);
	DOREPLIFETIME(ARTPSPlayerState, LastQuestReward);
	DOREPLIFETIME(ARTPSPlayerState, CompletedQuestCount);
	DOREPLIFETIME(ARTPSPlayerState, bLastQuestSucceeded);
}

void ARTPSPlayerState::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();

	UE_LOG(LogTemp, Log, TEXT("Player Name Updated: %s"), *GetPlayerName());
}

void ARTPSPlayerState::SetLobbyReady(bool bNewLobbyReady)
{
	bLobbyReady = bNewLobbyReady;
}

bool ARTPSPlayerState::IsLobbyReady() const
{
	return bLobbyReady;
}

void ARTPSPlayerState::ResetLobbyState()
{
	bLobbyReady = false;
}

void ARTPSPlayerState::MarkQuestResult(bool bWasSuccessful, int32 RewardAmount)
{
	bLastQuestSucceeded = bWasSuccessful;
	LastQuestReward = bWasSuccessful ? RewardAmount : 0;

	if (bWasSuccessful)
	{
		TotalReward += RewardAmount;
		++CompletedQuestCount;
	}
}

void ARTPSPlayerState::SetTeamId(int32 NewTeamId)
{
	TeamId = NewTeamId;
}

int32 ARTPSPlayerState::GetTeamId() const
{
	return TeamId;
}
