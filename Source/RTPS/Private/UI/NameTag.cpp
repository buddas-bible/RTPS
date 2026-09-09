// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/NameTag.h"
#include "Components/TextBlock.h" // Ensure UTextBlock is fully defined

void UNameTag::SetNameText(const FString& NewName)
{
	if (NameTextBlock)
	{
		NameTextBlock->SetText( FText::FromString( NewName ) );
	}
}
