// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Chatting/Chatting.h"
#include "Components/EditableText.h"
#include "GameFramework/PlayerState.h"
#include "Controller/RTPSCommonPlayerController.h"

void UChatting::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind Event
	if( ChatText )
	{
		ChatText->OnTextCommitted.RemoveDynamic( this, &UChatting::OnTextCommitted );
		ChatText->OnTextCommitted.AddDynamic( this, &UChatting::OnTextCommitted );
		ChatText->SetIsEnabled( false );
	}
}

void UChatting::ActivateChatText()
{
	if( ChatText )
	{
		ChatText->SetIsEnabled( true );
		ChatText->SetKeyboardFocus();
	}
}

void UChatting::OnTextCommitted( const FText& Text, ETextCommit::Type CommitMethod )
{
	if( CommitMethod == ETextCommit::OnEnter )
	{
		if( ChatText )
		{
			FText InputText = ChatText->GetText();
			FString InputString = InputText.ToString().TrimStartAndEnd();

			if( !InputString.IsEmpty() )
			{
				if ( ARTPSCommonPlayerController* PlayerController = GetOwningPlayer<ARTPSCommonPlayerController>() )
				{
					if( APlayerState* PlayerState = PlayerController->PlayerState )
					{
						PlayerController->SubmitChatMessage( InputString );

						FInputModeGameOnly InputMode;
						PlayerController->SetInputMode( InputMode );
					}
				}

				ChatText->SetText( FText::GetEmpty() );
				ChatText->SetIsEnabled( false );
			}
		}
	}
}
