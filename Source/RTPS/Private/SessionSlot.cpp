// Fill out your copyright notice in the Description page of Project Settings.


#include "SessionSlot.h"

#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

void USessionSlot::InitializeSlot(const FServerInfo& ServerInfo, int32 InSessionIndex, bool bIsSelected)
{
	CachedServerName = ServerInfo.ServerName;
	CachedCurrentPlayers = ServerInfo.CurrentPlayers;
	CachedMaxPlayers = ServerInfo.MaxPlayers;
	SessionIndex = InSessionIndex;
	bSelected = bIsSelected;

	UpdateVisualState();
}

void USessionSlot::SetSelected(bool bIsSelected)
{
	bSelected = bIsSelected;
	UpdateVisualState();
}

FReply USessionSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSessionSlotClicked.Broadcast(SessionIndex);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void USessionSlot::UpdateVisualState()
{
	if (SessionName)
	{
		const FString DisplayName = FString::Printf(
			TEXT("%s%d. %s"),
			bSelected ? TEXT("> ") : TEXT(""),
			SessionIndex + 1,
			*CachedServerName);

		SessionName->SetText(FText::FromString(DisplayName));
	}

	if (Players)
	{
		Players->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), CachedCurrentPlayers, CachedMaxPlayers)));
	}

	const FSlateColor SlotColor = bSelected
		? FSlateColor(FLinearColor(1.0f, 0.85f, 0.15f, 1.0f))
		: FSlateColor(FLinearColor::White);

	if (SessionName)
	{
		SessionName->SetColorAndOpacity(SlotColor);
	}

	if (Players)
	{
		Players->SetColorAndOpacity(SlotColor);
	}
}


