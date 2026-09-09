#pragma once

#include "CoreMinimal.h"
#include "Controller/RTPSCommonPlayerController.h"
#include "RTPSLobbyPlayerController.generated.h"

UCLASS()
class RTPS_API ARTPSLobbyPlayerController : public ARTPSCommonPlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void ToggleLobbyReady();

	UFUNCTION(BlueprintCallable)
	void RequestStartQuest();

	UFUNCTION(BlueprintCallable)
	void RequestSelectPreviousQuest();

	UFUNCTION(BlueprintCallable)
	void RequestSelectNextQuest();

	virtual TArray<FString> GetControlHintLines() const override;

	UFUNCTION(Server, Reliable)
	void ServerSetLobbyReady(bool bReady);

	UFUNCTION(Server, Reliable)
	void ServerRequestStartQuest();

	UFUNCTION(Server, Reliable)
	void ServerSelectQuestOffset(int32 Direction);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void TriggerAutoLobbyReady();

	bool bAutoLobbyReadyTriggered = false;
	float AutoLobbyReadyDelaySeconds = 1.0f;
	FTimerHandle AutoLobbyReadyTimerHandle;
};