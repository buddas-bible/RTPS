#include "Controller/RTPSLobbyPlayerController.h"

#include "Game/RTPSGameInstance.h"
#include "Game/RTPSLobbyGameMode.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "State/RTPSPlayerState.h"

void ARTPSLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Lobby controls: Q/E to select quest, R to toggle ready, G to start quest, hold Ctrl for cursor."));
	ShowLobbyMessage(TEXT("Lobby: host uses Q/E to select a quest, party members use R to ready, and host uses G to start. Hold Ctrl for cursor."), FColor::Yellow);

	const URTPSGameInstance* RTPSGameInstance = GetGameInstance<URTPSGameInstance>();
	if (RTPSGameInstance == nullptr || !RTPSGameInstance->ShouldAutoReadyInLobby() || bAutoLobbyReadyTriggered)
	{
		return;
	}

	FParse::Value(FCommandLine::Get(), TEXT("RTPSAutoLobbyReadyDelay="), AutoLobbyReadyDelaySeconds);
	AutoLobbyReadyDelaySeconds = FMath::Max(0.1f, AutoLobbyReadyDelaySeconds);
	GetWorldTimerManager().SetTimer(AutoLobbyReadyTimerHandle, this, &ARTPSLobbyPlayerController::TriggerAutoLobbyReady, AutoLobbyReadyDelaySeconds, false);
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Scheduled automatic lobby ready toggle. DelaySeconds=%.2f"), AutoLobbyReadyDelaySeconds);
}

void ARTPSLobbyPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent == nullptr)
	{
		return;
	}

	// Lobby-only bindings live here so Q/E/R/G invoke lobby handlers exactly once.
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ARTPSLobbyPlayerController::RequestSelectPreviousQuest);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ARTPSLobbyPlayerController::RequestSelectNextQuest);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ARTPSLobbyPlayerController::ToggleLobbyReady);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ARTPSLobbyPlayerController::RequestStartQuest);
}

void ARTPSLobbyPlayerController::ToggleLobbyReady()
{
	if (!IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	const ARTPSPlayerState* RTPSPlayerState = GetPlayerState<ARTPSPlayerState>();
	const bool bShouldReady = RTPSPlayerState == nullptr || !RTPSPlayerState->IsLobbyReady();
	ShowLobbyMessage(bShouldReady ? TEXT("Ready enabled.") : TEXT("Ready disabled."), FColor::Green);
	ServerSetLobbyReady(bShouldReady);
}

void ARTPSLobbyPlayerController::RequestStartQuest()
{
	if (!IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	ServerRequestStartQuest();
}

void ARTPSLobbyPlayerController::RequestSelectPreviousQuest()
{
	if (!IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	ServerSelectQuestOffset(-1);
}

void ARTPSLobbyPlayerController::RequestSelectNextQuest()
{
	if (!IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	ServerSelectQuestOffset(1);
}

TArray<FString> ARTPSLobbyPlayerController::GetControlHintLines() const
{
	if (IsLobbyMap())
	{
		return {
			TEXT("Lobby Controls"),
			TEXT("Q / E : Select quest (host)"),
			TEXT("R : Toggle ready"),
			TEXT("G : Start quest"),
			TEXT("Enter : Open / send chat"),
			TEXT("Hold Ctrl : Enable cursor")
		};
	}

	return Super::GetControlHintLines();
}

void ARTPSLobbyPlayerController::ServerSetLobbyReady_Implementation(bool bReady)
{
	if (ARTPSLobbyGameMode* LobbyGameMode = GetWorld()->GetAuthGameMode<ARTPSLobbyGameMode>())
	{
		LobbyGameMode->SetPlayerReadyState(this, bReady);
	}
}

void ARTPSLobbyPlayerController::ServerRequestStartQuest_Implementation()
{
	if (ARTPSLobbyGameMode* LobbyGameMode = GetWorld()->GetAuthGameMode<ARTPSLobbyGameMode>())
	{
		FString FailureReason;
		if (!LobbyGameMode->TryStartQuest(this, &FailureReason) && !FailureReason.IsEmpty())
		{
			ClientNotifyLobbyMessage(FailureReason, true);
		}
	}
}

void ARTPSLobbyPlayerController::ServerSelectQuestOffset_Implementation(int32 Direction)
{
	if (ARTPSLobbyGameMode* LobbyGameMode = GetWorld()->GetAuthGameMode<ARTPSLobbyGameMode>())
	{
		FString FailureReason;
		if (!LobbyGameMode->SelectQuestByOffset(this, Direction, &FailureReason) && !FailureReason.IsEmpty())
		{
			ClientNotifyLobbyMessage(FailureReason, true);
		}
	}
}

void ARTPSLobbyPlayerController::TriggerAutoLobbyReady()
{
	if (bAutoLobbyReadyTriggered || !IsLocalController() || !IsLobbyMap())
	{
		return;
	}

	bAutoLobbyReadyTriggered = true;
	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Triggering automatic lobby ready toggle."));
	ToggleLobbyReady();
}