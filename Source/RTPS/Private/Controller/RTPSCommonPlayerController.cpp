#include "Controller/RTPSCommonPlayerController.h"

#include "Engine/Engine.h"
#include "Game/RTPSGameInstance.h"
#include "Game/RTPSGameMode.h"
#include "Game/RTPSLobbyGameMode.h"
#include "Engine/LocalPlayer.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "State/RTPSPlayerState.h"
#include "UI/Chatting/Chatting.h"
#include "UI/RTPSHUD.h"

void ARTPSCommonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	ApplyGameplayCursorMode(false);

	if (!IsLocalController())
	{
		return;
	}

	RefreshChatHUD(TEXT("BeginPlay"));

	if (IsLobbyMap())
	{
		UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Local common controller reached lobby map '%s'."), *GetWorld()->GetMapName());

		FString ParsedAutoLobbyChat;
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyChat="), ParsedAutoLobbyChat))
		{
			AutoLobbyChatMessage = ParsedAutoLobbyChat;
		}

		float ParsedAutoLobbyChatDelay = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyChatDelay="), ParsedAutoLobbyChatDelay))
		{
			AutoLobbyChatDelaySeconds = FMath::Max(0.1f, ParsedAutoLobbyChatDelay);
		}

		int32 ParsedAutoLobbyChatBurstCount = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyChatBurstCount="), ParsedAutoLobbyChatBurstCount))
		{
			AutoLobbyChatBurstCount = FMath::Max(0, ParsedAutoLobbyChatBurstCount);
		}

		FString ParsedAutoLobbyChatBurstPrefix;
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyChatBurstPrefix="), ParsedAutoLobbyChatBurstPrefix) && !ParsedAutoLobbyChatBurstPrefix.IsEmpty())
		{
			AutoLobbyChatBurstPrefix = ParsedAutoLobbyChatBurstPrefix;
		}

		float ParsedAutoLobbyChatBurstDelay = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyChatBurstDelay="), ParsedAutoLobbyChatBurstDelay))
		{
			AutoLobbyChatBurstDelaySeconds = FMath::Max(0.1f, ParsedAutoLobbyChatBurstDelay);
		}

		float ParsedAutoLobbyChatBurstInterval = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyChatBurstInterval="), ParsedAutoLobbyChatBurstInterval))
		{
			AutoLobbyChatBurstIntervalSeconds = FMath::Max(0.01f, ParsedAutoLobbyChatBurstInterval);
		}

		if (AutoLobbyChatBurstCount > 0 && !bAutoLobbyChatBurstTriggered)
		{
			GetWorldTimerManager().SetTimer(AutoLobbyChatBurstStartTimerHandle, this, &ARTPSCommonPlayerController::StartAutoLobbyChatBurst, AutoLobbyChatBurstDelaySeconds, false);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Scheduled automatic lobby chat burst. DelaySeconds=%.2f Count=%d Prefix=%s Interval=%.2f"),
				AutoLobbyChatBurstDelaySeconds,
				AutoLobbyChatBurstCount,
				*AutoLobbyChatBurstPrefix,
				AutoLobbyChatBurstIntervalSeconds);
		}

		bAutoCleanChatOnReturn = FParse::Param(FCommandLine::Get(), TEXT("RTPSAutoCleanChatOnReturn"));

		if (!AutoLobbyChatMessage.IsEmpty() && !bAutoLobbyChatTriggered)
		{
			GetWorldTimerManager().SetTimer(AutoLobbyChatTimerHandle, this, &ARTPSCommonPlayerController::TriggerAutoLobbyChat, AutoLobbyChatDelaySeconds, false);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Scheduled automatic lobby chat. DelaySeconds=%.2f Message=%s"),
				AutoLobbyChatDelaySeconds,
				*AutoLobbyChatMessage);
		}
	}
}

void ARTPSCommonPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();

	RefreshChatHUD(TEXT("PostSeamlessTravel"));
	ScheduleChatHUDRefresh(TEXT("PostSeamlessTravel"));
}

void ARTPSCommonPlayerController::NotifyLoadedWorld(FName WorldPackageName, bool bFinalDest)
{
	Super::NotifyLoadedWorld(WorldPackageName, bFinalDest);

	if (!bFinalDest)
	{
		return;
	}

	if (IsQuestOrPlayMap())
	{
		bHasReachedQuestMap = true;
	}

	RefreshChatHUD(FString::Printf(TEXT("NotifyLoadedWorld:%s"), *WorldPackageName.ToString()));
	ScheduleChatHUDRefresh(FString::Printf(TEXT("NotifyLoadedWorld:%s"), *WorldPackageName.ToString()));

	if (bAutoCleanChatOnReturn && bHasReachedQuestMap && IsLobbyMap() && !bAutoLocalCleanOnReturnTriggered)
	{
		GetWorldTimerManager().SetTimer(AutoLocalCleanOnReturnTimerHandle, this, &ARTPSCommonPlayerController::TriggerAutoCleanChatOnReturn, 0.5f, false);
		UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Scheduled automatic local chat clean after return to lobby."));
	}
}

void ARTPSCommonPlayerController::PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel)
{
	ClearChatHUDRefreshTimer();

	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);
}

void ARTPSCommonPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearCommonTimers();

	Super::EndPlay(EndPlayReason);
}

void ARTPSCommonPlayerController::Destroyed()
{
	ClearCommonTimers();

	Super::Destroyed();
}

void ARTPSCommonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent == nullptr)
	{
		return;
	}

	BindCommonInput();
}

void ARTPSCommonPlayerController::BindCommonInput()
{
	if (InputComponent == nullptr)
	{
		return;
	}

	InputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &ARTPSCommonPlayerController::BeginTemporaryCursorMode);
	InputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &ARTPSCommonPlayerController::EndTemporaryCursorMode);
	InputComponent->BindKey(EKeys::RightControl, IE_Pressed, this, &ARTPSCommonPlayerController::BeginTemporaryCursorMode);
	InputComponent->BindKey(EKeys::RightControl, IE_Released, this, &ARTPSCommonPlayerController::EndTemporaryCursorMode);
}

void ARTPSCommonPlayerController::ActivateChatting()
{
	if (!IsLocalController())
	{
		return;
	}

	RefreshChatHUD(TEXT("ActivateChatting"));

	if (RTPSHUD && RTPSHUD->Chatting)
	{
		RTPSHUD->Chatting->ActivateChatText();
	}
}

void ARTPSCommonPlayerController::SubmitChatMessage(const FString& Message)
{
	if (!IsLocalController())
	{
		return;
	}

	const FString TrimmedMessage = Message.TrimStartAndEnd();
	if (TrimmedMessage.IsEmpty())
	{
		return;
	}

	if (HandleLocalChatCommand(TrimmedMessage))
	{
		return;
	}

	ServerSendChatMessage(TrimmedMessage);
}

ERTPSMatchPhase ARTPSCommonPlayerController::GetCurrentMatchPhase() const
{
	if (const ARTPSGameState* RTPSGameState = GetRTPSGameState())
	{
		return RTPSGameState->MatchPhase;
	}

	return ERTPSMatchPhase::Lobby;
}

TArray<FRTPSLobbyPlayerInfo> ARTPSCommonPlayerController::GetLobbyPlayerInfos() const
{
	if (const ARTPSGameState* RTPSGameState = GetRTPSGameState())
	{
		return RTPSGameState->GetLobbyPlayerInfos();
	}

	return {};
}

FRTPSQuestResultInfo ARTPSCommonPlayerController::GetQuestResultInfo() const
{
	if (const ARTPSGameState* RTPSGameState = GetRTPSGameState())
	{
		return RTPSGameState->GetQuestResultInfo();
	}

	return {};
}

bool ARTPSCommonPlayerController::IsLocalPlayerReady() const
{
	const ARTPSPlayerState* RTPSPlayerState = GetPlayerState<ARTPSPlayerState>();
	return RTPSPlayerState != nullptr && RTPSPlayerState->IsLobbyReady();
}

bool ARTPSCommonPlayerController::ShouldShowQuestResult() const
{
	return GetQuestResultInfo().bHasResult;
}

bool ARTPSCommonPlayerController::IsInLobbyState() const
{
	return GetCurrentMatchPhase() == ERTPSMatchPhase::Lobby;
}

TArray<FString> ARTPSCommonPlayerController::GetControlHintLines() const
{
	return {
		TEXT("Common Controls"),
		TEXT("Enter : Open / send chat"),
		TEXT("Hold Ctrl : Enable cursor")
	};
}

void ARTPSCommonPlayerController::ServerSetPlayerName_Implementation(const FString& NewName)
{
	if (ARTPSPlayerState* PS = Cast<ARTPSPlayerState>(PlayerState))
	{
		PS->SetPlayerName(NewName);
	}
}

void ARTPSCommonPlayerController::ServerSendChatMessage_Implementation(const FString& Message)
{
	const FString TrimmedMessage = Message.TrimStartAndEnd();
	if (TrimmedMessage.IsEmpty())
	{
		return;
	}

	if (TrimmedMessage.Equals(TEXT("/clean"), ESearchCase::IgnoreCase) || TrimmedMessage.Equals(TEXT("/clear"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Server rejected local-only chat command. Message=%s"), *TrimmedMessage);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] ServerSendChatMessage received. Map=%s Message=%s"), *GetWorld()->GetMapName(), *TrimmedMessage);

	if (ARTPSGameMode* RTPSGameMode = GetWorld()->GetAuthGameMode<ARTPSGameMode>())
	{
		RTPSGameMode->HandleChatMessage(this, TrimmedMessage);
		return;
	}

	if (ARTPSLobbyGameMode* LobbyGameMode = GetWorld()->GetAuthGameMode<ARTPSLobbyGameMode>())
	{
		LobbyGameMode->HandleChatMessage(this, TrimmedMessage);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] No chat handler GameMode was available for map '%s'."), *GetWorld()->GetMapName());
}

void ARTPSCommonPlayerController::ClientReceiveChatMessage_Implementation(const FString& Message)
{
	if (!IsLocalController())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] ClientReceiveChatMessage. Message=%s"), *Message);
	if (URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>())
	{
		RTPSGameInstance->AppendLocalChatMessage(Message);
	}

	RefreshChatHUD(TEXT("ClientReceiveChatMessage"));
}

void ARTPSCommonPlayerController::ClientNotifyLobbyMessage_Implementation(const FString& Message, bool bIsError)
{
	ShowLobbyMessage(Message, bIsError ? FColor::Red : FColor::Green);
}

void ARTPSCommonPlayerController::RefreshChatHUD(const FString& Reason)
{
	if (!CanUseChatHUD(Reason))
	{
		return;
	}

	RTPSHUD = Cast<ARTPSHUD>(GetHUD());
	if (RTPSHUD == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chat HUD refresh skipped because RTPSHUD is missing. Reason=%s Map=%s"),
			*Reason,
			GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"));
		return;
	}

	if (URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>())
	{
		RTPSHUD->RebuildChatMessages(RTPSGameInstance->GetLocalChatLog(), Reason);
	}
	else
	{
		RTPSHUD->AddChatting(Reason);
	}
}

void ARTPSCommonPlayerController::ScheduleChatHUDRefresh(const FString& Reason)
{
	if (!CanUseChatHUD(Reason))
	{
		return;
	}

	PendingChatHUDRefreshReason = FString::Printf(TEXT("%s/Delayed"), *Reason);
	GetWorldTimerManager().SetTimer(ChatHUDRefreshTimerHandle, this, &ARTPSCommonPlayerController::HandleDelayedChatHUDRefresh, 0.2f, false);
}

void ARTPSCommonPlayerController::HandleDelayedChatHUDRefresh()
{
	ClearChatHUDRefreshTimer();

	if (!CanUseChatHUD(PendingChatHUDRefreshReason.IsEmpty() ? TEXT("Delayed") : PendingChatHUDRefreshReason))
	{
		PendingChatHUDRefreshReason.Reset();
		return;
	}

	RefreshChatHUD(PendingChatHUDRefreshReason.IsEmpty() ? TEXT("Delayed") : PendingChatHUDRefreshReason);
	PendingChatHUDRefreshReason.Reset();
}

void ARTPSCommonPlayerController::ClearChatHUDRefreshTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChatHUDRefreshTimerHandle);
	}

	PendingChatHUDRefreshReason.Reset();
}

void ARTPSCommonPlayerController::ClearCommonTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(ChatHUDRefreshTimerHandle);
		TimerManager.ClearTimer(AutoLobbyChatTimerHandle);
		TimerManager.ClearTimer(AutoLobbyChatBurstStartTimerHandle);
		TimerManager.ClearTimer(AutoLobbyChatBurstMessageTimerHandle);
		TimerManager.ClearTimer(AutoLocalCleanOnReturnTimerHandle);
	}

	PendingChatHUDRefreshReason.Reset();
}

bool ARTPSCommonPlayerController::CanUseChatHUD(const FString& Reason)
{
	const UWorld* World = GetWorld();
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const bool bValidContext =
		IsLocalController()
		&& Player != nullptr
		&& LocalPlayer != nullptr
		&& LocalPlayer->PlayerController == this
		&& !IsPendingKillPending()
		&& World != nullptr;

	if (!bValidContext && !bLoggedInvalidChatHUDContext)
	{
		bLoggedInvalidChatHUDContext = true;
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chat HUD refresh skipped for stale or detached PlayerController. Controller=%s Reason=%s IsLocal=%d HasPlayer=%d HasLocalPlayer=%d IsCurrentLocalPC=%d PendingKill=%d HasWorld=%d"),
			*GetNameSafe(this),
			*Reason,
			IsLocalController() ? 1 : 0,
			Player != nullptr ? 1 : 0,
			LocalPlayer != nullptr ? 1 : 0,
			LocalPlayer != nullptr && LocalPlayer->PlayerController == this ? 1 : 0,
			IsPendingKillPending() ? 1 : 0,
			World != nullptr ? 1 : 0);
	}

	return bValidContext;
}

bool ARTPSCommonPlayerController::HandleLocalChatCommand(const FString& TrimmedMessage)
{
	if (!TrimmedMessage.Equals(TEXT("/clean"), ESearchCase::IgnoreCase) && !TrimmedMessage.Equals(TEXT("/clear"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>();
	const int32 PreviousCount = RTPSGameInstance != nullptr ? RTPSGameInstance->GetLocalChatLogCount() : 0;
	if (RTPSGameInstance != nullptr)
	{
		RTPSGameInstance->ClearLocalChatLog();
	}

	RefreshChatHUD(TEXT("LocalCleanCommand"));
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Local chat command handled without server send. Command=%s PreviousCount=%d CurrentCount=%d"),
		*TrimmedMessage,
		PreviousCount,
		RTPSGameInstance != nullptr ? RTPSGameInstance->GetLocalChatLogCount() : 0);
	return true;
}

bool ARTPSCommonPlayerController::IsLobbyMap() const
{
	return GetWorld() && GetWorld()->GetMapName().Contains(URTPSGameInstance::GetLobbyMapName());
}

bool ARTPSCommonPlayerController::IsQuestOrPlayMap() const
{
	return GetWorld() != nullptr && !IsLobbyMap() && !GetWorld()->GetMapName().Contains(TEXT("MainMenuMap"));
}

const ARTPSGameState* ARTPSCommonPlayerController::GetRTPSGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ARTPSGameState>() : nullptr;
}

void ARTPSCommonPlayerController::ShowLobbyMessage(const FString& Message, const FColor& MessageColor)
{
	if (!IsLocalController())
	{
		return;
	}

	ClientMessage(Message);

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, MessageColor, Message);
	}
}

void ARTPSCommonPlayerController::BeginTemporaryCursorMode()
{
	if (!IsLocalController() || bTemporaryCursorMode)
	{
		return;
	}

	ApplyGameplayCursorMode(true);
}

void ARTPSCommonPlayerController::EndTemporaryCursorMode()
{
	if (!IsLocalController() || !bTemporaryCursorMode)
	{
		return;
	}

	ApplyGameplayCursorMode(false);
}

void ARTPSCommonPlayerController::ApplyGameplayCursorMode(bool bEnableCursor)
{
	if (bEnableCursor)
	{
		FInputModeGameAndUI CursorInputMode;
		CursorInputMode.SetHideCursorDuringCapture(false);
		CursorInputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(CursorInputMode);
	}
	else
	{
		FInputModeGameOnly GameOnlyInputMode;
		SetInputMode(GameOnlyInputMode);
	}

	bShowMouseCursor = bEnableCursor;
	bEnableClickEvents = bEnableCursor;
	bEnableMouseOverEvents = bEnableCursor;
	SetIgnoreLookInput(bEnableCursor);
	bTemporaryCursorMode = bEnableCursor;
}

void ARTPSCommonPlayerController::TriggerAutoLobbyChat()
{
	if (bAutoLobbyChatTriggered || !IsLocalController() || !IsLobbyMap() || AutoLobbyChatMessage.IsEmpty())
	{
		return;
	}

	bAutoLobbyChatTriggered = true;
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Auto lobby chat send. Message=%s"), *AutoLobbyChatMessage);
	SubmitChatMessage(AutoLobbyChatMessage);
}

void ARTPSCommonPlayerController::StartAutoLobbyChatBurst()
{
	if (bAutoLobbyChatBurstTriggered || !IsLocalController() || !IsLobbyMap() || AutoLobbyChatBurstCount <= 0)
	{
		return;
	}

	bAutoLobbyChatBurstTriggered = true;
	AutoLobbyChatBurstIndex = 0;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Starting automatic lobby chat burst. Count=%d Prefix=%s Interval=%.2f"),
		AutoLobbyChatBurstCount,
		*AutoLobbyChatBurstPrefix,
		AutoLobbyChatBurstIntervalSeconds);
	GetWorldTimerManager().SetTimer(AutoLobbyChatBurstMessageTimerHandle, this, &ARTPSCommonPlayerController::TriggerAutoLobbyChatBurstMessage, AutoLobbyChatBurstIntervalSeconds, true, 0.0f);
}

void ARTPSCommonPlayerController::TriggerAutoLobbyChatBurstMessage()
{
	if (!IsLocalController() || !IsLobbyMap() || AutoLobbyChatBurstIndex >= AutoLobbyChatBurstCount)
	{
		GetWorldTimerManager().ClearTimer(AutoLobbyChatBurstMessageTimerHandle);
		UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Automatic lobby chat burst finished. Sent=%d"), AutoLobbyChatBurstIndex);
		return;
	}

	const FString BurstMessage = FString::Printf(TEXT("%s_%03d"), *AutoLobbyChatBurstPrefix, AutoLobbyChatBurstIndex + 1);
	++AutoLobbyChatBurstIndex;
	SubmitChatMessage(BurstMessage);
}

void ARTPSCommonPlayerController::TriggerAutoCleanChatOnReturn()
{
	if (bAutoLocalCleanOnReturnTriggered || !IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	bAutoLocalCleanOnReturnTriggered = true;
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Triggering automatic local chat clean after return."));
	SubmitChatMessage(TEXT("/clean"));
}
