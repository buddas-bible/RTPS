#include "Game/RTPSGameMode.h"

#include "Controller/RTPSCommonPlayerController.h"
#include "Controller/RTPSPlayerController.h"
#include "Game/RTPSGameInstance.h"
#include "Game/RTPSGameState.h"
#include "State/RTPSPlayerState.h"
#include "UI/RTPSHUD.h"
#include <Kismet/GameplayStatics.h>


ARTPSGameMode::ARTPSGameMode()
{
	const ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/BP_RTPSCharacterPlayer.BP_RTPSCharacterPlayer_C"));
	if (PlayerPawnClassFinder.Class)
	{
		DefaultPawnClass = PlayerPawnClassFinder.Class;
	}

	const ConstructorHelpers::FClassFinder<ARTPSPlayerController> PlayerControllerClassFinder( TEXT( "/Game/BP_RTPSPlayerController.BP_RTPSPlayerController_C" ) );
	if( PlayerControllerClassFinder.Class )
	{
		PlayerControllerClass = PlayerControllerClassFinder.Class;
	}

	const ConstructorHelpers::FClassFinder<ARTPSHUD> PlayerHUDClassRef(TEXT("/Game/BP_RTPSHUD.BP_RTPSHUD_C"));
	if (PlayerHUDClassRef.Class)
	{
		HUDClass = PlayerHUDClassRef.Class;
	}

	PlayerStateClass = ARTPSPlayerState::StaticClass();
	GameStateClass = ARTPSGameState::StaticClass();
	bUseSeamlessTravel = true;
}

void ARTPSGameMode::BeginPlay()
{
	Super::BeginPlay();

	CurrentMinorKills = 0;
	CurrentBossKills = 0;
	bQuestEnding = false;

	FRTPSDemoQuestDefinition SelectedQuest;
	FName ActiveQuestId = NAME_None;
	if (URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>())
	{
		if (RTPSGameInstance->GetPendingSelectedQuest(SelectedQuest))
		{
			ActiveQuestId = SelectedQuest.QuestId;
			ActiveQuestName = SelectedQuest.QuestName;
			RequiredMinorKills = SelectedQuest.RequiredMinorKills;
			RequiredBossKills = SelectedQuest.RequiredBossKills;
			QuestReward = SelectedQuest.RewardAmount;
		}
	}

	if (ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>())
	{
		RTPSGameState->SetMatchPhase(ERTPSMatchPhase::InQuest);
		RTPSGameState->SetActiveQuestId(ActiveQuestId);
		RTPSGameState->SetActiveQuestName(ActiveQuestName);
		RTPSGameState->SetObjectives(RequiredMinorKills, RequiredBossKills);
		RTPSGameState->SetProgress(CurrentMinorKills, CurrentBossKills);
		RTPSGameState->ClearQuestResult();
	}

	RefreshQuestState();

	const int32 ConnectedPlayers = GameState != nullptr ? GameState->PlayerArray.Num() : 0;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] InGame initialized. Map=%s ActiveQuestId=%s ActiveQuestName=%s MinorTarget=%d BossTarget=%d Reward=%d ConnectedPlayers=%d"),
		*GetWorld()->GetMapName(),
		*ActiveQuestId.ToString(),
		*ActiveQuestName,
		RequiredMinorKills,
		RequiredBossKills,
		QuestReward,
		ConnectedPlayers);

	if (URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>())
	{
		if (RTPSGameInstance->ShouldAutoCompleteQuest())
		{
			GetWorldTimerManager().SetTimer(AutoQuestCompletionTimerHandle, this, &ARTPSGameMode::TriggerAutoQuestCompletion, 3.0f, false);
			UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Scheduled automatic quest completion."));
		}
	}
}

APlayerController* ARTPSGameMode::Login( UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage )
{
	return Super::Login( NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage );
}

void ARTPSGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	RefreshQuestState();
}

void ARTPSGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	RefreshQuestState();
}


void ARTPSGameMode::HandleChatMessage(ARTPSCommonPlayerController* FromPlayer, const FString& MessageBody)
{
	if (!FromPlayer)
	{
		return;
	}

	FString SenderName = TEXT("Unknown");
	if (APlayerState* FromPlayerState = FromPlayer->PlayerState)
	{
		SenderName = FromPlayerState->GetPlayerName();
	}

	const FString Message = FString::Printf(TEXT("%s: %s"), *SenderName, *MessageBody);
	SendChatMessage(Message);
}

void ARTPSGameMode::SendChatMessage(const FString& Message)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARTPSCommonPlayerController* PlayerController = Cast<ARTPSCommonPlayerController>(It->Get());
		if (PlayerController)
		{
			PlayerController->ClientReceiveChatMessage(Message);
		}
	}
}

void ARTPSGameMode::RegisterMinorMonsterKill()
{
	if (!HasAuthority() || bQuestEnding)
	{
		return;
	}

	CurrentMinorKills = FMath::Clamp(CurrentMinorKills + 1, 0, RequiredMinorKills);
	RefreshQuestState();
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Minor kill registered. CurrentMinorKills=%d/%d CurrentBossKills=%d/%d"), CurrentMinorKills, RequiredMinorKills, CurrentBossKills, RequiredBossKills);

	if (HasQuestSucceeded())
	{
		CompleteQuest();
	}
}

void ARTPSGameMode::RegisterTargetMonsterKill()
{
	if (!HasAuthority() || bQuestEnding)
	{
		return;
	}

	CurrentBossKills = FMath::Clamp(CurrentBossKills + 1, 0, RequiredBossKills);
	RefreshQuestState();
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Boss kill registered. CurrentMinorKills=%d/%d CurrentBossKills=%d/%d"), CurrentMinorKills, RequiredMinorKills, CurrentBossKills, RequiredBossKills);

	if (HasQuestSucceeded())
	{
		CompleteQuest();
	}
}

void ARTPSGameMode::CompleteQuest()
{
	if (!HasAuthority() || bQuestEnding)
	{
		return;
	}

	bQuestEnding = true;

	if (ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>())
	{
		RTPSGameState->SetMatchPhase(ERTPSMatchPhase::QuestSucceeded);
		RTPSGameState->SetQuestResult(true, true, QuestReward, FMath::CeilToInt(ReturnToLobbyDelay));

		for (APlayerState* PlayerState : RTPSGameState->PlayerArray)
		{
			if (ARTPSPlayerState* RTPSPlayerState = Cast<ARTPSPlayerState>(PlayerState))
			{
				RTPSPlayerState->MarkQuestResult(true, QuestReward);
			}
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Quest completed successfully. ActiveQuestName=%s Reward=%d ReturnDelay=%.2f"),
		*ActiveQuestName,
		QuestReward,
		ReturnToLobbyDelay);

	GetWorldTimerManager().SetTimer(ReturnToLobbyTimerHandle, this, &ARTPSGameMode::UpdateQuestResultCountdown, 1.0f, true);
}

void ARTPSGameMode::FailQuest()
{
	if (!HasAuthority() || bQuestEnding)
	{
		return;
	}

	bQuestEnding = true;

	if (ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>())
	{
		RTPSGameState->SetMatchPhase(ERTPSMatchPhase::QuestFailed);
		RTPSGameState->SetQuestResult(true, false, 0, FMath::CeilToInt(ReturnToLobbyDelay));

		for (APlayerState* PlayerState : RTPSGameState->PlayerArray)
		{
			if (ARTPSPlayerState* RTPSPlayerState = Cast<ARTPSPlayerState>(PlayerState))
			{
				RTPSPlayerState->MarkQuestResult(false, 0);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Quest failed. ActiveQuestName=%s ReturnDelay=%.2f"), *ActiveQuestName, ReturnToLobbyDelay);

	GetWorldTimerManager().SetTimer(ReturnToLobbyTimerHandle, this, &ARTPSGameMode::UpdateQuestResultCountdown, 1.0f, true);
}


void ARTPSGameMode::RefreshQuestState() const
{
	ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>();
	if (RTPSGameState == nullptr)
	{
		return;
	}

	int32 ConnectedPlayers = 0;
	for (APlayerState* PlayerState : RTPSGameState->PlayerArray)
	{
		if (Cast<ARTPSPlayerState>(PlayerState))
		{
			++ConnectedPlayers;
		}
	}

	RTPSGameState->SetObjectives(RequiredMinorKills, RequiredBossKills);
	RTPSGameState->SetProgress(CurrentMinorKills, CurrentBossKills);
	RTPSGameState->SetLobbyPopulation(ConnectedPlayers, 0);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Quest state refreshed. ConnectedPlayers=%d ActiveQuestName=%s MinorProgress=%d/%d BossProgress=%d/%d"),
		ConnectedPlayers,
		*ActiveQuestName,
		CurrentMinorKills,
		RequiredMinorKills,
		CurrentBossKills,
		RequiredBossKills);
}

void ARTPSGameMode::UpdateQuestResultCountdown()
{
	ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>();
	if (RTPSGameState == nullptr)
	{
		GetWorldTimerManager().ClearTimer(ReturnToLobbyTimerHandle);
		return;
	}

	const int32 NextCountdown = RTPSGameState->ReturnCountdownSeconds - 1;
	RTPSGameState->SetQuestResult(true, RTPSGameState->bQuestSucceeded, RTPSGameState->QuestRewardAmount, NextCountdown);

	if (NextCountdown <= 0)
	{
		GetWorldTimerManager().ClearTimer(ReturnToLobbyTimerHandle);
		ReturnPartyToLobby();
	}
}

void ARTPSGameMode::ReturnPartyToLobby()
{
	if (ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>())
	{
		RTPSGameState->SetMatchPhase(ERTPSMatchPhase::ReturningToLobby);
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Returning party to lobby from map '%s'."), *GetWorld()->GetMapName());
	GetWorld()->ServerTravel(URTPSGameInstance::GetLobbyTravelPath());
}

bool ARTPSGameMode::HasQuestSucceeded() const
{
	return CurrentMinorKills >= RequiredMinorKills && CurrentBossKills >= RequiredBossKills;
}

void ARTPSGameMode::TriggerAutoQuestCompletion()
{
	if (!HasAuthority() || bQuestEnding)
	{
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Triggering automatic quest completion. ActiveQuestName=%s MinorTarget=%d BossTarget=%d"),
		*ActiveQuestName,
		RequiredMinorKills,
		RequiredBossKills);

	for (int32 MinorKillIndex = CurrentMinorKills; MinorKillIndex < RequiredMinorKills; ++MinorKillIndex)
	{
		RegisterMinorMonsterKill();
	}

	for (int32 BossKillIndex = CurrentBossKills; BossKillIndex < RequiredBossKills; ++BossKillIndex)
	{
		RegisterTargetMonsterKill();
	}

	if (!bQuestEnding && HasQuestSucceeded())
	{
		CompleteQuest();
	}
}
