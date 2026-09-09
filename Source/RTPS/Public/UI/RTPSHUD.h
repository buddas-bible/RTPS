#pragma once

#include "CoreMinimal.h"
#include "Game/RTPSGameState.h"
#include "GameFramework/HUD.h"
#include "RTPSHUD.generated.h"

UCLASS()
class RTPS_API ARTPSHUD : public AHUD
{
	GENERATED_BODY()

public:
	ARTPSHUD();

	virtual void PostInitializeComponents() override;
	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UUserWidget> ChattingClass;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UUserWidget> ChatMessageClass;

	UPROPERTY(BlueprintReadWrite)
	class UChatting* Chatting;

	UFUNCTION(BlueprintPure)
	ERTPSMatchPhase GetCurrentMatchPhase() const;

	UFUNCTION(BlueprintPure)
	TArray<FRTPSLobbyPlayerInfo> GetLobbyPlayerInfos() const;

	UFUNCTION(BlueprintPure)
	FRTPSQuestResultInfo GetQuestResultInfo() const;

	UFUNCTION(BlueprintPure)
	bool IsLocalPlayerReady() const;

	UFUNCTION(BlueprintPure)
	bool ShouldShowQuestResult() const;

	void AddChatting(const FString& Reason = TEXT("Unspecified"));
	void AddChatMessage(const FString& Message);
	void ClearChatMessages(const FString& Reason = TEXT("Unspecified"));
	void RebuildChatMessages(const TArray<FString>& Messages, const FString& Reason = TEXT("Unspecified"));

private:
	void DrawControlHints();
	class UChatting* FindReusableChatWidget(class ARTPSCommonPlayerController* OwningPlayer) const;
	int32 RemoveStaleChatWidgets(class ARTPSCommonPlayerController* OwningPlayer);
	int32 CountActiveChatWidgets(class ARTPSCommonPlayerController* OwningPlayer) const;
};
