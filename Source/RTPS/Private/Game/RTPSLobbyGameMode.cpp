#include "Game/RTPSLobbyGameMode.h"

#include "Controller/RTPSCommonPlayerController.h"
#include "Controller/RTPSLobbyPlayerController.h"
#include "Game/RTPSGameInstance.h"
#include "Game/RTPSGameState.h"
#include "State/RTPSPlayerState.h"
#include "UI/RTPSHUD.h"

ARTPSLobbyGameMode::ARTPSLobbyGameMode()
{
	const ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/BP_RTPSCharacterPlayer.BP_RTPSCharacterPlayer_C"));
	if (PlayerPawnClassFinder.Class)
	{
		DefaultPawnClass = PlayerPawnClassFinder.Class;
	}

	const ConstructorHelpers::FClassFinder<ARTPSHUD> PlayerHUDClassRef(TEXT("/Game/BP_RTPSHUD.BP_RTPSHUD_C"));
	if (PlayerHUDClassRef.Class)
	{
		HUDClass = PlayerHUDClassRef.Class;
	}

	PlayerControllerClass = ARTPSLobbyPlayerController::StaticClass();
	PlayerStateClass = ARTPSPlayerState::StaticClass();
	GameStateClass = ARTPSGameState::StaticClass();
	bUseSeamlessTravel = true;
}

void ARTPSLobbyGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>())
	{
		for (APlayerState* PlayerState : RTPSGameState->PlayerArray)
		{
			if (ARTPSPlayerState* RTPSPlayerState = Cast<ARTPSPlayerState>(PlayerState))
			{
				RTPSPlayerState->ResetLobbyState();
			}
		}

		RTPSGameState->SetMatchPhase(ERTPSMatchPhase::Lobby);
		RTPSGameState->ClearQuestResult();
	}

	if (URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>())
	{
		RTPSGameInstance->StartHostedSession();
	}

	ApplySelectedQuestToLobbyState();
	RefreshLobbyState();
	MaybeRunAutoLobbyFlow();
}

void ARTPSLobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (ARTPSPlayerState* RTPSPlayerState = NewPlayer ? NewPlayer->GetPlayerState<ARTPSPlayerState>() : nullptr)
	{
		RTPSPlayerState->ResetLobbyState();
	}

	ApplySelectedQuestToLobbyState();
	RefreshLobbyState();
	MaybeRunAutoLobbyFlow();
}

void ARTPSLobbyGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	RefreshLobbyState();
	MaybeRunAutoLobbyFlow();
}

void ARTPSLobbyGameMode::SetPlayerReadyState(APlayerController* PlayerController, bool bReady)
{
	if (!HasAuthority() || PlayerController == nullptr)
	{
		return;
	}

	if (ARTPSPlayerState* RTPSPlayerState = PlayerController->GetPlayerState<ARTPSPlayerState>())
	{
		RTPSPlayerState->SetLobbyReady(bReady);
	}

	RefreshLobbyState();
	MaybeRunAutoLobbyFlow();
}

bool ARTPSLobbyGameMode::SelectQuestByOffset(APlayerController* RequestingPlayer, int32 Direction, FString* OutFailureReason)
{
	if (!HasAuthority())
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Only the server can change the selected quest.");
		}

		return false;
	}

	if (RequestingPlayer == nullptr || !IsHostPlayer(RequestingPlayer))
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Only the host can change the selected quest.");
		}

		return false;
	}

	URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>();
	if (RTPSGameInstance == nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Quest selection data is unavailable.");
		}

		return false;
	}

	FRTPSDemoQuestDefinition SelectedQuest;
	if (!RTPSGameInstance->CyclePendingSelectedQuest(Direction, &SelectedQuest))
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("No selectable demo quests are available.");
		}

		return false;
	}

	ApplySelectedQuestToLobbyState();
	BroadcastQuestSelection(SelectedQuest.QuestName);
	return true;
}

bool ARTPSLobbyGameMode::TryStartQuest(APlayerController* RequestingPlayer, FString* OutFailureReason)
{
	if (!HasAuthority())
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Only the server can start the quest.");
		}

		return false;
	}

	if (RequestingPlayer == nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("The requesting player could not be resolved.");
		}

		return false;
	}

	if (!IsHostPlayer(RequestingPlayer))
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Only the host can start the quest.");
		}

		return false;
	}

	if (!CanStartQuest(OutFailureReason))
	{
		return false;
	}

	FRTPSDemoQuestDefinition SelectedQuest;
	URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>();
	if (RTPSGameInstance == nullptr || !RTPSGameInstance->GetPendingSelectedQuest(SelectedQuest))
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Selected quest data could not be resolved before travel.");
		}

		return false;
	}

	if (ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>())
	{
		RTPSGameState->SetMatchPhase(ERTPSMatchPhase::TravelingToQuest);
	}

	const bool bTravelStarted = GetWorld()->ServerTravel(URTPSGameInstance::BuildQuestTravelPath(SelectedQuest.MapPath));
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] TryStartQuest invoked. ActiveQuestId=%s ActiveQuestName=%s TravelMap=%s TravelStarted=%d"),
		*SelectedQuest.QuestId.ToString(),
		*SelectedQuest.QuestName,
		*SelectedQuest.MapPath,
		bTravelStarted);
	if (!bTravelStarted && OutFailureReason != nullptr)
	{
		*OutFailureReason = TEXT("The server failed to travel to the selected quest map.");
	}

	return bTravelStarted;
}

bool ARTPSLobbyGameMode::CanStartQuest(FString* OutFailureReason) const
{
	constexpr int32 MaxPartyMembers = 4;
	int32 ConnectedPlayers = 0;
	int32 ReadyPlayers = 0;

	ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>();
	if (RTPSGameState == nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("No players are connected to the lobby.");
		}

		return false;
	}

	for (APlayerState* PlayerState : RTPSGameState->PlayerArray)
	{
		const ARTPSPlayerState* RTPSPlayerState = Cast<ARTPSPlayerState>(PlayerState);
		if (RTPSPlayerState == nullptr)
		{
			continue;
		}

		++ConnectedPlayers;
		ReadyPlayers += RTPSPlayerState->IsLobbyReady() ? 1 : 0;
	}

	if (ConnectedPlayers <= 0)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("No players are connected to the lobby.");
		}

		return false;
	}

	if (ConnectedPlayers > MaxPartyMembers)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("The demo lobby supports up to 4 players.");
		}

		return false;
	}

	if (ConnectedPlayers == 1)
	{
		return true;
	}

	if (ReadyPlayers < ConnectedPlayers)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = FString::Printf(
				TEXT("Only %d/%d players are ready. Every party member must be ready before the host starts the quest."),
				ReadyPlayers,
				ConnectedPlayers);
		}

		return false;
	}

	return true;
}

void ARTPSLobbyGameMode::HandleChatMessage(ARTPSCommonPlayerController* FromPlayer, const FString& MessageBody)
{
	if (!HasAuthority() || FromPlayer == nullptr)
	{
		return;
	}

	const FString TrimmedMessage = MessageBody.TrimStartAndEnd();
	if (TrimmedMessage.IsEmpty())
	{
		return;
	}

	const APlayerState* SenderState = FromPlayer->PlayerState;
	const FString SenderName = SenderState != nullptr ? SenderState->GetPlayerName() : TEXT("Unknown");
	const FString FormattedMessage = FString::Printf(TEXT("%s : %s"), *SenderName, *TrimmedMessage);

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Lobby chat received. Sender=%s Message=%s"), *SenderName, *TrimmedMessage);
	BroadcastChatMessage(FormattedMessage);
}

void ARTPSLobbyGameMode::RefreshLobbyState() const
{
	ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>();
	if (RTPSGameState == nullptr)
	{
		return;
	}

	int32 ConnectedPlayers = 0;
	int32 ReadyPlayers = 0;

	for (APlayerState* PlayerState : RTPSGameState->PlayerArray)
	{
		if (ARTPSPlayerState* RTPSPlayerState = Cast<ARTPSPlayerState>(PlayerState))
		{
			++ConnectedPlayers;
			ReadyPlayers += RTPSPlayerState->IsLobbyReady() ? 1 : 0;
		}
	}

	RTPSGameState->SetLobbyPopulation(ConnectedPlayers, ReadyPlayers);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Lobby state refreshed. ConnectedPlayers=%d ReadyPlayers=%d ActiveQuestId=%s ActiveQuestName=%s"),
		ConnectedPlayers,
		ReadyPlayers,
		*RTPSGameState->ActiveQuestId.ToString(),
		*RTPSGameState->ActiveQuestName);
}

bool ARTPSLobbyGameMode::AreAllPlayersReady() const
{
	return CanStartQuest();
}

bool ARTPSLobbyGameMode::IsHostPlayer(const APlayerController* PlayerController) const
{
	return PlayerController != nullptr && PlayerController->IsLocalController();
}

void ARTPSLobbyGameMode::ApplySelectedQuestToLobbyState() const
{
	const URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>();
	ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>();
	if (RTPSGameState == nullptr || RTPSGameInstance == nullptr)
	{
		return;
	}

	FRTPSDemoQuestDefinition SelectedQuest;
	if (!RTPSGameInstance->GetPendingSelectedQuest(SelectedQuest))
	{
		return;
	}

	RTPSGameState->SetActiveQuestId(SelectedQuest.QuestId);
	RTPSGameState->SetActiveQuestName(SelectedQuest.QuestName);
	RTPSGameState->SetObjectives(SelectedQuest.RequiredMinorKills, SelectedQuest.RequiredBossKills);
	RTPSGameState->SetProgress(0, 0);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Lobby selected quest applied. QuestId=%s QuestName=%s MinorTarget=%d BossTarget=%d"),
		*SelectedQuest.QuestId.ToString(),
		*SelectedQuest.QuestName,
		SelectedQuest.RequiredMinorKills,
		SelectedQuest.RequiredBossKills);
}

void ARTPSLobbyGameMode::BroadcastQuestSelection(const FString& QuestName) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FString Notification = FString::Printf(TEXT("Selected quest: %s"), *QuestName);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARTPSCommonPlayerController* PlayerController = Cast<ARTPSCommonPlayerController>(It->Get()))
		{
			PlayerController->ClientNotifyLobbyMessage(Notification, false);
		}
	}
}

void ARTPSLobbyGameMode::BroadcastChatMessage(const FString& Message) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	int32 RecipientCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARTPSCommonPlayerController* PlayerController = Cast<ARTPSCommonPlayerController>(It->Get()))
		{
			PlayerController->ClientReceiveChatMessage(Message);
			++RecipientCount;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Lobby chat broadcast. Recipients=%d Message=%s"), RecipientCount, *Message);
}

void ARTPSLobbyGameMode::MaybeRunAutoLobbyFlow()
{
	if (!HasAuthority())
	{
		return;
	}

	URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>();
	ARTPSGameState* RTPSGameState = GetGameState<ARTPSGameState>();
	if (RTPSGameInstance == nullptr || RTPSGameState == nullptr)
	{
		return;
	}

	if (!bAutoQuestSelectionApplied)
	{
		const FName DesiredQuestId = RTPSGameInstance->GetAutoSelectedQuestId();
		if (DesiredQuestId != NAME_None && RTPSGameInstance->SetPendingSelectedQuestById(DesiredQuestId))
		{
			ApplySelectedQuestToLobbyState();
			BroadcastQuestSelection(RTPSGameState->ActiveQuestName);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Automatic lobby quest selection applied. QuestId=%s QuestName=%s"),
				*RTPSGameState->ActiveQuestId.ToString(),
				*RTPSGameState->ActiveQuestName);
		}

		bAutoQuestSelectionApplied = true;
	}

	if (!RTPSGameInstance->ShouldAutoStartQuest() || bAutoQuestStartTriggered)
	{
		return;
	}

	const int32 ExpectedPlayers = RTPSGameInstance->GetAutoExpectedPlayers();
	if (RTPSGameState->ConnectedPlayerCount < ExpectedPlayers)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Automatic quest start waiting for players. ConnectedPlayers=%d ExpectedPlayers=%d"),
			RTPSGameState->ConnectedPlayerCount,
			ExpectedPlayers);
		return;
	}

	FString FailureReason;
	if (!CanStartQuest(&FailureReason))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Automatic quest start waiting for ready state. Reason=%s"),
			*FailureReason);
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PlayerController = It->Get())
		{
			if (IsHostPlayer(PlayerController))
			{
				bAutoQuestStartTriggered = TryStartQuest(PlayerController, &FailureReason);
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[RTPSValidation] Automatic quest start attempted. Success=%d Reason=%s"),
					bAutoQuestStartTriggered,
					FailureReason.IsEmpty() ? TEXT("<none>") : *FailureReason);
				return;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Automatic quest start could not resolve the host player controller."));
}
