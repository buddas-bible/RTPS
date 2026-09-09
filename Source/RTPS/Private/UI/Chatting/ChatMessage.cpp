// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Chatting/ChatMessage.h"
#include "Components/TextBlock.h"

void UChatMessage::SetMessageText( const FString& InMessage )
{
	if( MessageText )
	{
		MessageText->SetText( FText::FromString( InMessage ) );
		MessageText->Font.Size = 24;
	}
}
