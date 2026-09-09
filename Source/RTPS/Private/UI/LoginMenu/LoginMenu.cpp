#include "UI/LoginMenu/LoginMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "SessionSlot.h"
#include <Kismet/KismetSystemLibrary.h>

void ULoginMenu::NativeConstruct()
{
	Super::NativeConstruct();

	if (URTPSGameInstance* GI = Cast<URTPSGameInstance>(GetGameInstance()))
	{
		GI->ServerListUpdatedDel.AddDynamic(this, &ULoginMenu::HandleServerListUpdated);
	}

	if (CreateSessionButton)
	{
		CreateSessionButton->OnClicked.AddDynamic(this, &ULoginMenu::OnCreateSessionClicked);
	}

	if (JoinSessionButton)
	{
		JoinSessionButton->OnClicked.AddDynamic(this, &ULoginMenu::OnJoinSessionClicked);
	}

	if (JoinButton)
	{
		JoinButton->OnClicked.AddDynamic(this, &ULoginMenu::OnJoinClicked);
	}

	if (BackButton)
	{
		BackButton->OnClicked.AddDynamic(this, &ULoginMenu::OnBackClicked);
	}

	if (RefreshButton)
	{
		RefreshButton->OnClicked.AddDynamic(this, &ULoginMenu::OnRefeshClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked.AddDynamic(this, &ULoginMenu::ExitGame);
	}

	ResolveSessionSlotClass();
	EnsureControlHintText();
	RefreshSessionListUI();
	UpdateControlHintText();
}

void ULoginMenu::NativeDestruct()
{
	if (URTPSGameInstance* GI = Cast<URTPSGameInstance>(GetGameInstance()))
	{
		GI->ServerListUpdatedDel.RemoveDynamic(this, &ULoginMenu::HandleServerListUpdated);
	}

	Super::NativeDestruct();
}

void ULoginMenu::OnCreateSessionClicked()
{
	if (URTPSGameInstance* GI = Cast<URTPSGameInstance>(GetGameInstance()))
	{
		GI->CreateGameSession();
	}
}

void ULoginMenu::OnJoinSessionClicked()
{
	if (MenuSwitcher)
	{
		MenuSwitcher->SetActiveWidgetIndex(1);
	}

	OnRefeshClicked();
	UpdateControlHintText();
}

void ULoginMenu::OnJoinClicked()
{
	if (URTPSGameInstance* GI = Cast<URTPSGameInstance>(GetGameInstance()))
	{
		if (SelectedSessionIndex == INDEX_NONE)
		{
			GI->FindGameSessions();
			return;
		}

		GI->JoinGameSessionByIndex(SelectedSessionIndex);
	}
}

void ULoginMenu::OnBackClicked()
{
	if (MenuSwitcher)
	{
		MenuSwitcher->SetActiveWidgetIndex(0);
	}

	UpdateControlHintText();
}

void ULoginMenu::OnRefeshClicked()
{
	SelectedSessionIndex = INDEX_NONE;

	if (URTPSGameInstance* GI = Cast<URTPSGameInstance>(GetGameInstance()))
	{
		GI->FindGameSessions();
	}
}

void ULoginMenu::ExitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void ULoginMenu::HandleServerListUpdated()
{
	RebuildSessionList();
	BP_OnServerListUpdated();
}

void ULoginMenu::ResolveSessionSlotClass()
{
	if (SessionSlotClass != nullptr)
	{
		return;
	}

	SessionSlotClass = LoadClass<USessionSlot>(nullptr, TEXT("/Game/WBP_SessionSlot.WBP_SessionSlot_C"));
	if (SessionSlotClass == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginMenu: failed to load /Game/WBP_SessionSlot."));
	}
}

void ULoginMenu::EnsureControlHintText()
{
	if (ControlHintText != nullptr || WidgetTree == nullptr)
	{
		return;
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (RootCanvas == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginMenu: Root widget is not a CanvasPanel, control hint text was not created."));
		return;
	}

	ControlHintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ControlHintText"));
	if (ControlHintText == nullptr)
	{
		return;
	}

	ControlHintText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.95f, 0.65f, 1.0f)));
	ControlHintText->SetShadowOffset(FVector2D(1.5f, 1.5f));
	ControlHintText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));

	if (UCanvasPanelSlot* CanvasSlot = RootCanvas->AddChildToCanvas(ControlHintText))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		CanvasSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		CanvasSlot->SetPosition(FVector2D(24.0f, -24.0f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetZOrder(100);
	}
}

void ULoginMenu::UpdateControlHintText()
{
	if (ControlHintText == nullptr)
	{
		return;
	}

	// UI가 back 버튼을 가리는 문제로 인해 일단 비활성화. 추후 UI 개선 후 재활성화 예정.
	//const bool bJoinPageActive = MenuSwitcher != nullptr && MenuSwitcher->GetActiveWidgetIndex() == 1;

	//const FString HintText = bJoinPageActive
	//	? TEXT("Join Session\nMouse : Select a session\nJoin Button : Enter selected lobby\nRefresh Button : Search again\nBack Button : Return to main menu")
	//	: TEXT("Main Menu\nMouse : Click buttons\nCreate Session : Host a lobby\nJoin Session : Open session list\nQuit : Exit game");

	//ControlHintText->SetText(FText::FromString(HintText));
}

void ULoginMenu::RebuildSessionList()
{
	if (SessionListScrollBox == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("LoginMenu: SessionListScrollBox is not bound."));
		return;
	}

	ResolveSessionSlotClass();
	SessionListScrollBox->ClearChildren();

	const TArray<FServerInfo> ServerList = GetServerList();
	if (ServerList.Num() == 0)
	{
		SelectedSessionIndex = INDEX_NONE;

		UTextBlock* EmptyText = NewObject<UTextBlock>(SessionListScrollBox);
		if (EmptyText)
		{
			EmptyText->SetText(FText::FromString(TEXT("No sessions found. Press Refresh to search again.")));
			SessionListScrollBox->AddChild(EmptyText);
		}
		return;
	}

	if (!ServerList.IsValidIndex(SelectedSessionIndex))
	{
		SelectedSessionIndex = 0;
	}

	for (int32 SessionIndex = 0; SessionIndex < ServerList.Num(); ++SessionIndex)
	{
		const FServerInfo& ServerInfo = ServerList[SessionIndex];
		if (USessionSlot* SessionSlot = CreateSessionSlotWidget(ServerInfo, SessionIndex, SelectedSessionIndex == SessionIndex))
		{
			SessionListScrollBox->AddChild(SessionSlot);
		}
	}
}

USessionSlot* ULoginMenu::CreateSessionSlotWidget(const FServerInfo& ServerInfo, int32 SessionIndex, bool bIsSelected)
{
	if (SessionSlotClass == nullptr)
	{
		return nullptr;
	}

	USessionSlot* SessionSlot = CreateWidget<USessionSlot>(this, SessionSlotClass);
	if (SessionSlot == nullptr)
	{
		return nullptr;
	}

	SessionSlot->InitializeSlot(ServerInfo, SessionIndex, bIsSelected);
	SessionSlot->OnSessionSlotClicked.AddUObject(this, &ULoginMenu::SelectSessionByIndex);
	return SessionSlot;
}

void ULoginMenu::RefreshSessionListUI()
{
	RebuildSessionList();
}

void ULoginMenu::SelectSessionByIndex(int32 SessionIndex)
{
	SelectedSessionIndex = SessionIndex;
	RebuildSessionList();
}

TArray<FServerInfo> ULoginMenu::GetServerList() const
{
	if (const URTPSGameInstance* GI = Cast<URTPSGameInstance>(GetGameInstance()))
	{
		return GI->GetCachedServerList();
	}

	return {};
}
