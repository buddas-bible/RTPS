#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "RTPSGameMode.generated.h"

class ARTPSCommonPlayerController;

UCLASS()
class RTPS_API ARTPSGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ARTPSGameMode();
	virtual void BeginPlay() override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	// 플레이어 컨트롤러에서 호출되는 서버 RPC로, 플레이어의 행동
	void HandleChatMessage(ARTPSCommonPlayerController* FromPlayer, const FString& MessageBody);
	// 플레이어가 채팅 메시지를 보낼 때 호출되는 서버 RPC입니다. 메시지를 검증하고, 게임 상태에 따라 적절히 처리합니다.
	void SendChatMessage(const FString& Message);
	// 플레이어가 잡몹을 처치할 때 호출되는 서버 RPC입니다. 현재 잡몹 처치 수를 증가시키고, 퀘스트 상태를 갱신합니다.
	void RegisterMinorMonsterKill();
	// 플레이어가 보스를 처치할 때 호출되는 서버 RPC입니다. 현재 보스 처치 수를 증가시키고, 퀘스트 상태를 갱신합니다.
	void RegisterTargetMonsterKill();
	// 퀘스트가 성공적으로 완료되었을 때 호출되는 서버 RPC입니다. 보상을 지급하고, 퀘스트 결과를 갱신합니다.
	void CompleteQuest();
	// 퀘스트 실패 처리와 관련된 서버 RPC로, 퀘스트 실패 시 필요한 상태 업데이트와 플레이어 통보를 수행합니다.
	void FailQuest();

protected:
	// 퀘스트 상태를 새로고침하는 함수입니다. 현재 연결된 플레이어 수와 퀘스트 진행 상황을 게임 상태에 업데이트합니다.
	void RefreshQuestState() const;

	// 퀘스트 결과 카운트다운 업데이트. 매 초마다 호출되어 남은 시간을 갱신하고, 시간이 다 되면 로비로 이동.
	void UpdateQuestResultCountdown();

	// 퀘스트 결과 화면에서 로비로 돌아가는 타이머가 만료되었을 때 호출되는 내부 함수로, 플레이어를 로비로 이동시키는 처리를 수행합니다.
	void ReturnPartyToLobby();

	// 퀘스트 성공 여부를 확인하는 내부 함수로, 현재 처치 수가 퀘스트 요구 사항을 충족하는지 검사하여 성공 여부를 반환합니다.
	bool HasQuestSucceeded() const;

	// 자동화 시나리오에서 퀘스트를 자동으로 완료하도록 트리거하는 내부 함수로, 테스트 목적으로 퀘스트 성공 조건을 충족시키고 퀘스트 완료 처리를 수행합니다.
	void TriggerAutoQuestCompletion();

protected:
	int32 CurrentMinorKills = 0;
	int32 CurrentBossKills = 0;
	bool bQuestEnding = false;

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	FString ActiveQuestName = TEXT("Jagged Claw Hunt");

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	int32 RequiredMinorKills = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	int32 RequiredBossKills = 1;

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	int32 QuestReward = 100;

	UPROPERTY(EditDefaultsOnly, Category = "Quest")
	float ReturnToLobbyDelay = 5.f;

	FTimerHandle ReturnToLobbyTimerHandle;
	FTimerHandle AutoQuestCompletionTimerHandle;
};
