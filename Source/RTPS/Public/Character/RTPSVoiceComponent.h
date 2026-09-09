#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTPSVoiceComponent.generated.h"

/**
 * VOIP(음성 채팅) 관리 전담 컴포넌트
 * RTPSCharacterPlayer에 자동 부착되며, 음성 초기화/해제/여행 복구 로직을 담당합니다.
 */
UCLASS(ClassGroup=(RTPS), meta=(BlueprintSpawnableComponent))
class RTPS_API URTPSVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTPSVoiceComponent();

	// 레벨 이동 전 음성 정리
	void PrepareForTravel(const FString& Reason);

	// 음성 초기화 예약 (딜레이 0이면 기본값 사용)
	void ScheduleInitialization(const FString& Reason, float DelaySeconds = 0.0f);

	// 레벨 이동 후 음성 복구
	void HandleResumeAfterTravel(const FString& Reason);

	// Restart 시 음성 복구 처리 (캐릭터 Restart에서 호출)
	void HandleCharacterRestart(const FString& Reason);

	// 음성 탈커가 준비됐는지 확인
	bool IsTalkerReady() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TryInitialize(const FString& Reason);
	void HandleDelayedInitialization();
	void Teardown(const FString& Reason);
	int32 DestroyLegacyTalkers(const FString& Reason, bool bPreserveManagedTalker);
	bool ShouldManageVoiceForCurrentMap() const;
	void ResetRetryState();

	// 오너 캐릭터 반환 (ACharacter로 캐스트)
	class ACharacter* GetOwnerCharacter() const;

	UPROPERTY(Transient)
	TObjectPtr<class UVOIPTalker> ManagedVOIPTalker;

	UPROPERTY()
	TObjectPtr<APlayerState> CachedVoicePlayerState;

	FTimerHandle InitTimerHandle;
	FString PendingInitReason;
	bool bRegistered = false;

	UPROPERTY(EditDefaultsOnly, Category = "Voice")
	float InitDelaySeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Voice")
	float LocalMicThreshold = -1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Voice")
	int32 MaxRetryCount = 6;

	int32 RetryCount = 0;
	FString RetryBaseReason;
};
