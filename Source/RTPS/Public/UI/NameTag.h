// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NameTag.generated.h"

/**
 * 
 */
UCLASS()
class RTPS_API UNameTag : public UUserWidget
{
	GENERATED_BODY()
	
public:
	UFUNCTION( BlueprintCallable, Category = "NameTag" )
	void SetNameText( const FString& NewName );
	
protected:
	UPROPERTY( meta = ( BindWidget ) )
	class UTextBlock* NameTextBlock;
};
