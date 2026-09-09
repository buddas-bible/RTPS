#include "Character/RTPSVoiceComponent.h"

#include "Game/RTPSGameInstance.h"
#include "GameFramework/Character.h"
#include "Net/VoiceConfig.h"

URTPSVoiceComponent::URTPSVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ===================== UActorComponent 라이프사이클 =====================

void URTPSVoiceComponent::BeginPlay()
{
	Super::BeginPlay();
	ScheduleInitialization(TEXT("BeginPlay"));
}

void URTPSVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(InitTimerHandle);
	}

	Teardown(FString::Printf(TEXT("EndPlay:%d"), static_cast<int32>(EndPlayReason)));

	Super::EndPlay(EndPlayReason);
}

// ===================== Public 인터페이스 =====================

void URTPSVoiceComponent::PrepareForTravel(const FString& Reason)
{
	Teardown(Reason);
}

void URTPSVoiceComponent::ScheduleInitialization(const FString& Reason, float DelaySeconds)
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	PendingInitReason = Reason;

	const float ResolvedDelay = DelaySeconds > 0.0f ? DelaySeconds : InitDelaySeconds;
	GetWorld()->GetTimerManager().ClearTimer(InitTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		InitTimerHandle,
		this,
		&URTPSVoiceComponent::HandleDelayedInitialization,
		ResolvedDelay,
		false);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Scheduled C++ VOIPTalker initialization. Owner=%s Reason=%s DelaySeconds=%.2f"),
		GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"),
		*Reason,
		ResolvedDelay);
}

void URTPSVoiceComponent::HandleResumeAfterTravel(const FString& Reason)
{
	ScheduleInitialization(Reason, 0.05f);
}

void URTPSVoiceComponent::HandleCharacterRestart(const FString& Reason)
{
	ScheduleInitialization(Reason);

	ACharacter* OwnerCharacter = GetOwnerCharacter();
	if (OwnerCharacter == nullptr || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	if (!ShouldManageVoiceForCurrentMap())
	{
		return;
	}

}

bool URTPSVoiceComponent::IsTalkerReady() const
{
	const APlayerState* StateToValidate = nullptr;

	if (const ACharacter* OwnerCharacter = GetOwnerCharacter())
	{
		StateToValidate = OwnerCharacter->GetPlayerState();
	}

	if (StateToValidate == nullptr)
	{
		StateToValidate = CachedVoicePlayerState.Get();
	}

	return bRegistered
		&& ManagedVOIPTalker != nullptr
		&& IsValid(ManagedVOIPTalker)
		&& StateToValidate != nullptr
		&& IsValid(StateToValidate);
}

// ===================== Private 구현 =====================

void URTPSVoiceComponent::TryInitialize(const FString& Reason)
{
	UWorld* World = GetWorld();
	ACharacter* OwnerCharacter = GetOwnerCharacter();

	if (World == nullptr || OwnerCharacter == nullptr || OwnerCharacter->IsActorBeingDestroyed())
	{
		return;
	}

	if (!ShouldManageVoiceForCurrentMap())
	{
		return;
	}

	APlayerState* OwningPlayerState = OwnerCharacter->GetPlayerState<APlayerState>();
	if (OwningPlayerState == nullptr)
	{
		const bool bIsRetry = Reason.Contains(TEXT("/NoPlayerStateRetry"));
		if (!bIsRetry || RetryBaseReason.IsEmpty())
		{
			RetryBaseReason = Reason;
			RetryCount = 0;
		}

		++RetryCount;
		if (RetryCount > MaxRetryCount)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[RTPSValidation] C++ VOIPTalker initialization gave up after missing PlayerState retries. Owner=%s BaseReason=%s RetryCount=%d/%d"),
				*OwnerCharacter->GetName(),
				*RetryBaseReason,
				RetryCount - 1,
				MaxRetryCount);
			return;
		}

		const FString RetryReason = FString::Printf(
			TEXT("%s/NoPlayerStateRetry[%d/%d]"),
			*RetryBaseReason,
			RetryCount,
			MaxRetryCount);

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] C++ VOIPTalker initialization missing PlayerState. Owner=%s BaseReason=%s RetryCount=%d/%d"),
			*OwnerCharacter->GetName(),
			*RetryBaseReason,
			RetryCount,
			MaxRetryCount);

		ScheduleInitialization(RetryReason, 0.2f);
		return;
	}

	ResetRetryState();

	if (ManagedVOIPTalker == nullptr || !IsValid(ManagedVOIPTalker))
	{
		ManagedVOIPTalker = NewObject<UVOIPTalker>(OwnerCharacter, UVOIPTalker::StaticClass(), NAME_None, RF_Transient);
		if (ManagedVOIPTalker != nullptr)
		{
			OwnerCharacter->AddInstanceComponent(ManagedVOIPTalker);
			ManagedVOIPTalker->RegisterComponent();
			UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Created managed C++ VOIPTalker. Owner=%s"), *OwnerCharacter->GetName());
		}
	}

	if (ManagedVOIPTalker == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Failed to create managed C++ VOIPTalker. Owner=%s"), *OwnerCharacter->GetName());
		return;
	}

	FVoiceSettings VoiceSettings = ManagedVOIPTalker->Settings;
	VoiceSettings.ComponentToAttachTo = OwnerCharacter->GetMesh();
	ManagedVOIPTalker->Settings = VoiceSettings;
	ManagedVOIPTalker->RegisterWithPlayerState(OwningPlayerState);
	CachedVoicePlayerState = OwningPlayerState;
	bRegistered = true;

	if (OwnerCharacter->IsLocallyControlled())
	{
		UVOIPStatics::SetMicThreshold(LocalMicThreshold);
	}

	const int32 RemovedLegacyCount = DestroyLegacyTalkers(Reason, true);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Managed C++ VOIPTalker registered. Owner=%s PlayerState=%s Reason=%s RemovedLegacy=%d"),
		*OwnerCharacter->GetName(),
		*OwningPlayerState->GetPlayerName(),
		*Reason,
		RemovedLegacyCount);
}

void URTPSVoiceComponent::HandleDelayedInitialization()
{
	TryInitialize(PendingInitReason.IsEmpty() ? TEXT("DelayedInit") : PendingInitReason);
}

void URTPSVoiceComponent::Teardown(const FString& Reason)
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(InitTimerHandle);
	}

	ResetRetryState();

	if (ManagedVOIPTalker != nullptr)
	{
		APlayerState* StateToReset = nullptr;
		if (const ACharacter* OwnerCharacter = GetOwnerCharacter())
		{
			StateToReset = OwnerCharacter->GetPlayerState<APlayerState>();
		}

		if (StateToReset == nullptr)
		{
			StateToReset = CachedVoicePlayerState.Get();
		}

		if (StateToReset != nullptr && IsValid(StateToReset))
		{
			UVOIPStatics::ResetPlayerVoiceTalker(StateToReset);
		}

		CachedVoicePlayerState = nullptr;
		ManagedVOIPTalker->DestroyComponent();
		ManagedVOIPTalker = nullptr;
		bRegistered = false;

		UE_LOG(LogTemp, Log,
			TEXT("[RTPSValidation] Managed C++ VOIPTalker destroyed. Owner=%s Reason=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"),
			*Reason);
	}

	const int32 RemovedLegacyCount = DestroyLegacyTalkers(Reason, false);
	if (RemovedLegacyCount > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RTPSValidation] Legacy VOIPTalker components removed. Owner=%s Reason=%s Removed=%d"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"),
			*Reason,
			RemovedLegacyCount);
	}
}

int32 URTPSVoiceComponent::DestroyLegacyTalkers(const FString& Reason, bool bPreserveManagedTalker)
{
	ACharacter* OwnerCharacter = GetOwnerCharacter();
	if (OwnerCharacter == nullptr)
	{
		return 0;
	}

	TArray<UVOIPTalker*> VoiceTalkers;
	OwnerCharacter->GetComponents<UVOIPTalker>(VoiceTalkers);

	int32 RemovedCount = 0;
	for (UVOIPTalker* VoiceTalker : VoiceTalkers)
	{
		if (VoiceTalker == nullptr)
		{
			continue;
		}

		if (bPreserveManagedTalker && VoiceTalker == ManagedVOIPTalker)
		{
			continue;
		}

		VoiceTalker->DestroyComponent();
		++RemovedCount;
	}

	return RemovedCount;
}

bool URTPSVoiceComponent::ShouldManageVoiceForCurrentMap() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FString MapName = World->GetMapName();
	return MapName.Contains(URTPSGameInstance::GetLobbyMapName())
		|| MapName.Contains(TEXT("ThirdPersonMap"));
}

void URTPSVoiceComponent::ResetRetryState()
{
	RetryCount = 0;
	RetryBaseReason.Reset();
}

ACharacter* URTPSVoiceComponent::GetOwnerCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}
