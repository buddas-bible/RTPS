// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/NameTag.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Game/RTPSRuntimeTest.h"
#include "State/RTPSPlayerState.h"

void UNameTag::SetNameText(const FString& NewName)
{
	if (NameTextBlock)
	{
		const ARTPSPlayerState* PlayerState = NamePlayerState.Get();
		NameTextBlock->SetText(FText::FromString(PlayerState ? PlayerState->GetPlayerName() : NewName));
	}
}

void UNameTag::BindPlayerState(ARTPSPlayerState* NewPlayerState)
{
	if (NamePlayerState.Get() != NewPlayerState)
	{
		if (ARTPSPlayerState* OldPlayerState = NamePlayerState.Get())
		{
			OldPlayerState->OnPlayerNameChanged.RemoveAll(this);
		}
		NamePlayerState = NewPlayerState;
		if (NewPlayerState)
		{
			NewPlayerState->OnPlayerNameChanged.AddUObject(this, &UNameTag::RefreshPlayerName);
		}
	}
	RefreshPlayerName();
}

void UNameTag::RefreshPlayerName()
{
	const ARTPSPlayerState* PlayerState = NamePlayerState.Get();
	SetNameText(FString());
	if (PlayerState && NameTextBlock && !RTPSRuntimeTest::Directory().IsEmpty())
	{
		RTPSRuntimeTest::Emit(TEXT("name_tag_updated"),
			{{TEXT("map"), GetWorld()->GetMapName()},
			 {TEXT("player_id"), FString::FromInt(PlayerState->GetPlayerId())},
			 {TEXT("player_name"), PlayerState->GetPlayerName()},
			 {TEXT("text"), NameTextBlock->GetText().ToString()}});
	}
}
