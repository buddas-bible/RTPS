// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/RTPSGameInstance.h"
#include "SessionSlot.generated.h"

class UTextBlock;
struct FGeometry;
struct FPointerEvent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSessionSlotClicked, int32);

UCLASS()
class RTPS_API USessionSlot : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeSlot(const FServerInfo& ServerInfo, int32 InSessionIndex, bool bIsSelected);
	void SetSelected(bool bIsSelected);

	FOnSessionSlotClicked OnSessionSlotClicked;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SessionName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Players;

private:
	void UpdateVisualState();

	FString CachedServerName;
	int32 CachedCurrentPlayers = 0;
	int32 CachedMaxPlayers = 0;
	int32 SessionIndex = INDEX_NONE;
	bool bSelected = false;
};
