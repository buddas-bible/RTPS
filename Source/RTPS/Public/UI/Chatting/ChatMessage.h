// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ChatMessage.generated.h"

/**
 * 
 */
UCLASS()
class RTPS_API UChatMessage : public UUserWidget
{
	GENERATED_BODY()
	
public:

	UPROPERTY( meta = ( BindWidget ) )
	class UTextBlock* MessageText;
	
	void SetMessageText( const FString& InMessage );
};
