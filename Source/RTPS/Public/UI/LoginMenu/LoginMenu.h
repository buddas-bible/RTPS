#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/RTPSGameInstance.h"
#include "LoginMenu.generated.h"

class USessionSlot;
class UTextBlock;
class UScrollBox;

UCLASS()
class RTPS_API ULoginMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	class UWidgetSwitcher* MenuSwitcher;

	UPROPERTY(meta = (BindWidget))
	class UButton* CreateSessionButton;

	UPROPERTY(meta = (BindWidget))
	class UButton* JoinSessionButton;

	UPROPERTY(meta = (BindWidget))
	class UButton* QuitButton;

	UPROPERTY(meta = (BindWidget))
	class UButton* JoinButton;

	UPROPERTY(meta = (BindWidget))
	class UButton* BackButton;

	UPROPERTY(meta = (BindWidget))
	class UButton* RefreshButton;

	UPROPERTY( meta = ( BindWidget ) )
	class UScrollBox* SessionListScrollBox;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 SelectedSessionIndex = INDEX_NONE;

protected:
	UFUNCTION()
	void OnCreateSessionClicked();

	UFUNCTION()
	void OnJoinSessionClicked();

	UFUNCTION()
	void OnJoinClicked();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnRefeshClicked();

	UFUNCTION()
	void ExitGame();

	UFUNCTION()
	void HandleServerListUpdated();

	void RebuildSessionList();
	void ResolveSessionSlotClass();
	void EnsureControlHintText();
	void UpdateControlHintText();
	USessionSlot* CreateSessionSlotWidget(const FServerInfo& ServerInfo, int32 SessionIndex, bool bIsSelected);

public:
	void RefreshSessionListUI();

public:
	UFUNCTION(BlueprintCallable, Category = "Session")
	void SelectSessionByIndex(int32 SessionIndex);

	UFUNCTION(BlueprintPure, Category = "Session")
	TArray<FServerInfo> GetServerList() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Session")
	void BP_OnServerListUpdated();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Session")
	TSubclassOf<USessionSlot> SessionSlotClass;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ControlHintText;
};
