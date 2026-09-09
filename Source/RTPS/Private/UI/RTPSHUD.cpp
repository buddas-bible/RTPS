#include "UI/RTPSHUD.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include <Components/ScrollBox.h>
#include "Controller/RTPSCommonPlayerController.h"
#include "UI/Chatting/ChatMessage.h"
#include "UI/Chatting/Chatting.h"

namespace
{
	void GatherLocalChatWidgets(UWorld* World, APlayerController* OwningPlayer, TArray<UChatting*>& OutWidgets)
	{
		OutWidgets.Reset();
		if (World == nullptr || OwningPlayer == nullptr)
		{
			return;
		}

		TArray<UUserWidget*> FoundWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, FoundWidgets, UChatting::StaticClass(), false);
		for (UUserWidget* FoundWidget : FoundWidgets)
		{
			UChatting* ChattingWidget = Cast<UChatting>(FoundWidget);
			if (ChattingWidget != nullptr && ChattingWidget->GetOwningPlayer() == OwningPlayer)
			{
				OutWidgets.Add(ChattingWidget);
			}
		}
	}
}

ARTPSHUD::ARTPSHUD()
{
	const ConstructorHelpers::FClassFinder<UUserWidget> ChattingClassFinder(TEXT("/Game/WBP_Chatting.WBP_Chatting_C"));
	if (ChattingClassFinder.Class)
	{
		ChattingClass = ChattingClassFinder.Class;
	}

	const ConstructorHelpers::FClassFinder<UUserWidget> ChatMessageClassFinder(TEXT("/Game/WBP_ChatMessage.WBP_ChatMessage_C"));
	if (ChatMessageClassFinder.Class)
	{
		ChatMessageClass = ChatMessageClassFinder.Class;
	}
}

void ARTPSHUD::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void ARTPSHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawControlHints();
}

ERTPSMatchPhase ARTPSHUD::GetCurrentMatchPhase() const
{
	if (const ARTPSCommonPlayerController* RTPSPlayerController = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController()))
	{
		return RTPSPlayerController->GetCurrentMatchPhase();
	}

	return ERTPSMatchPhase::Lobby;
}

TArray<FRTPSLobbyPlayerInfo> ARTPSHUD::GetLobbyPlayerInfos() const
{
	if (const ARTPSCommonPlayerController* RTPSPlayerController = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController()))
	{
		return RTPSPlayerController->GetLobbyPlayerInfos();
	}

	return {};
}

FRTPSQuestResultInfo ARTPSHUD::GetQuestResultInfo() const
{
	if (const ARTPSCommonPlayerController* RTPSPlayerController = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController()))
	{
		return RTPSPlayerController->GetQuestResultInfo();
	}

	return {};
}

bool ARTPSHUD::IsLocalPlayerReady() const
{
	if (const ARTPSCommonPlayerController* RTPSPlayerController = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController()))
	{
		return RTPSPlayerController->IsLocalPlayerReady();
	}

	return false;
}

bool ARTPSHUD::ShouldShowQuestResult() const
{
	if (const ARTPSCommonPlayerController* RTPSPlayerController = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController()))
	{
		return RTPSPlayerController->ShouldShowQuestResult();
	}

	return false;
}

void ARTPSHUD::AddChatting(const FString& Reason)
{
	ARTPSCommonPlayerController* OwningPlayer = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController());
	if (OwningPlayer == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chatting widget ensure failed because owning player is missing. Reason=%s Map=%s"),
			*Reason,
			GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"));
		return;
	}

	bool bReusedExistingWidget = false;
	if (!IsValid(Chatting))
	{
		Chatting = nullptr;
	}

	if (Chatting == nullptr)
	{
		Chatting = FindReusableChatWidget(OwningPlayer);
		bReusedExistingWidget = Chatting != nullptr;
	}

	bool bCreatedNewWidget = false;
	if (Chatting == nullptr)
	{
		if (ChattingClass == nullptr)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[RTPSValidation] Chatting widget ensure failed because ChattingClass is missing. Reason=%s Map=%s"),
				*Reason,
				GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"));
			return;
		}

		Chatting = CreateWidget<UChatting>(OwningPlayer, ChattingClass);
		bCreatedNewWidget = Chatting != nullptr;
	}

	if (Chatting == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chatting widget ensure failed because widget creation returned null. Reason=%s Map=%s"),
			*Reason,
			GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"));
		return;
	}

	const int32 RemovedStaleCount = RemoveStaleChatWidgets(OwningPlayer);
	const bool bWasInViewport = Chatting->IsInViewport();
	if (!bWasInViewport)
	{
		Chatting->AddToViewport();
	}

	const int32 ActiveWidgetCount = CountActiveChatWidgets(OwningPlayer);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chatting widget ensured in viewport. Reason=%s Map=%s WasInViewport=%d ReusedExisting=%d CreatedNew=%d RemovedStale=%d ActiveWidgetCount=%d"),
		*Reason,
		GetWorld() ? *GetWorld()->GetMapName() : TEXT("None"),
		bWasInViewport ? 1 : 0,
		bReusedExistingWidget ? 1 : 0,
		bCreatedNewWidget ? 1 : 0,
		RemovedStaleCount,
		ActiveWidgetCount);
}

void ARTPSHUD::AddChatMessage(const FString& Message)
{
	ARTPSCommonPlayerController* OwningPlayer = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController());
	if (OwningPlayer == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Chat message render failed because owning player is missing. Message=%s"), *Message);
		return;
	}

	if (ChattingClass == nullptr || ChatMessageClass == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Chat message render failed because chat widget classes are missing. Message=%s"), *Message);
		return;
	}

	if (Chatting == nullptr)
	{
		Chatting = CreateWidget<UChatting>(OwningPlayer, ChattingClass);
	}

	if (Chatting == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Chat message render failed because Chatting widget could not be created. Message=%s"), *Message);
		return;
	}

	if (!Chatting->IsInViewport())
	{
		Chatting->AddToViewport();
	}

	if (Chatting->ChatScrollBox == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Chat message render failed because ChatScrollBox is not bound. Message=%s"), *Message);
		return;
	}

	UChatMessage* ChatMessageWidget = CreateWidget<UChatMessage>(OwningPlayer, ChatMessageClass);
	if (ChatMessageWidget == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Chat message render failed because ChatMessage widget could not be created. Message=%s"), *Message);
		return;
	}

	ChatMessageWidget->SetMessageText(Message);
	Chatting->ChatScrollBox->AddChild(ChatMessageWidget);
	Chatting->ChatScrollBox->ScrollToEnd();
	Chatting->ChatScrollBox->SetAnimateWheelScrolling(true);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chat message widget added. ChildCount=%d Message=%s"),
		Chatting->ChatScrollBox->GetChildrenCount(),
		*Message);
}

void ARTPSHUD::ClearChatMessages(const FString& Reason)
{
	AddChatting(Reason);

	if (Chatting == nullptr || Chatting->ChatScrollBox == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chat message clear failed because chat widget or scroll box is missing. Reason=%s"),
			*Reason);
		return;
	}

	const int32 PreviousCount = Chatting->ChatScrollBox->GetChildrenCount();
	Chatting->ChatScrollBox->ClearChildren();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chat message view cleared. Reason=%s PreviousChildCount=%d CurrentChildCount=0"),
		*Reason,
		PreviousCount);
}

void ARTPSHUD::RebuildChatMessages(const TArray<FString>& Messages, const FString& Reason)
{
	AddChatting(Reason);

	if (Chatting == nullptr || Chatting->ChatScrollBox == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chat message rebuild failed because chat widget or scroll box is missing. Reason=%s LogCount=%d"),
			*Reason,
			Messages.Num());
		return;
	}

	ARTPSCommonPlayerController* OwningPlayer = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController());
	if (OwningPlayer == nullptr || ChatMessageClass == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chat message rebuild failed because owning player or ChatMessageClass is missing. Reason=%s LogCount=%d"),
			*Reason,
			Messages.Num());
		return;
	}

	Chatting->ChatScrollBox->ClearChildren();
	for (const FString& Message : Messages)
	{
		UChatMessage* ChatMessageWidget = CreateWidget<UChatMessage>(OwningPlayer, ChatMessageClass);
		if (ChatMessageWidget == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Chat message rebuild skipped an entry because ChatMessage widget could not be created. Reason=%s Message=%s"), *Reason, *Message);
			continue;
		}

		ChatMessageWidget->SetMessageText(Message);
		Chatting->ChatScrollBox->AddChild(ChatMessageWidget);
	}

	Chatting->ChatScrollBox->ScrollToEnd();
	Chatting->ChatScrollBox->SetAnimateWheelScrolling(true);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chat message view rebuilt from persistent log. Reason=%s LogCount=%d ChildCount=%d"),
		*Reason,
		Messages.Num(),
		Chatting->ChatScrollBox->GetChildrenCount());
}

void ARTPSHUD::DrawControlHints()
{
	if (Canvas == nullptr)
	{
		return;
	}

	const ARTPSCommonPlayerController* RTPSPlayerController = Cast<ARTPSCommonPlayerController>(GetOwningPlayerController());
	if (RTPSPlayerController == nullptr || !RTPSPlayerController->IsLocalController())
	{
		return;
	}

	const TArray<FString> HintLines = RTPSPlayerController->GetControlHintLines();
	if (HintLines.IsEmpty())
	{
		return;
	}

	UFont* HintFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (HintFont == nullptr)
	{
		return;
	}

	float MaxLineWidth = 0.0f;
	float TotalHeight = 0.0f;

	for (const FString& HintLine : HintLines)
	{
		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		GetTextSize(HintLine, LineWidth, LineHeight, HintFont);
		MaxLineWidth = FMath::Max(MaxLineWidth, LineWidth);
		TotalHeight += LineHeight + 4.0f;
	}

	const float BoxX = 24.0f;
	const float BoxY = 24.0f;
	const float Padding = 12.0f;
	const float BoxWidth = MaxLineWidth + Padding * 2.0f;
	const float BoxHeight = TotalHeight + Padding * 2.0f;

	DrawRect(FLinearColor(0.02f, 0.02f, 0.02f, 0.65f), BoxX, BoxY, BoxWidth, BoxHeight);

	float TextY = BoxY + Padding;
	for (int32 LineIndex = 0; LineIndex < HintLines.Num(); ++LineIndex)
	{
		const FString& HintLine = HintLines[LineIndex];
		const FLinearColor TextColor = LineIndex == 0
			? FLinearColor(1.0f, 0.85f, 0.25f, 1.0f)
			: FLinearColor::White;

		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		GetTextSize(HintLine, LineWidth, LineHeight, HintFont);
		DrawText(HintLine, TextColor, BoxX + Padding, TextY, HintFont, 1.0f, false);
		TextY += LineHeight + 4.0f;
	}
}

UChatting* ARTPSHUD::FindReusableChatWidget(ARTPSCommonPlayerController* OwningPlayer) const
{
	TArray<UChatting*> LocalChatWidgets;
	GatherLocalChatWidgets(GetWorld(), OwningPlayer, LocalChatWidgets);

	for (UChatting* ChattingWidget : LocalChatWidgets)
	{
		if (ChattingWidget == Chatting)
		{
			return ChattingWidget;
		}
	}

	for (UChatting* ChattingWidget : LocalChatWidgets)
	{
		if (IsValid(ChattingWidget))
		{
			return ChattingWidget;
		}
	}

	return nullptr;
}

int32 ARTPSHUD::RemoveStaleChatWidgets(ARTPSCommonPlayerController* OwningPlayer)
{
	TArray<UChatting*> LocalChatWidgets;
	GatherLocalChatWidgets(GetWorld(), OwningPlayer, LocalChatWidgets);

	int32 RemovedCount = 0;
	for (UChatting* ChattingWidget : LocalChatWidgets)
	{
		if (!IsValid(ChattingWidget) || ChattingWidget == Chatting)
		{
			continue;
		}

		ChattingWidget->RemoveFromParent();
		++RemovedCount;
	}

	return RemovedCount;
}

int32 ARTPSHUD::CountActiveChatWidgets(ARTPSCommonPlayerController* OwningPlayer) const
{
	TArray<UChatting*> LocalChatWidgets;
	GatherLocalChatWidgets(GetWorld(), OwningPlayer, LocalChatWidgets);

	int32 ActiveCount = 0;
	for (UChatting* ChattingWidget : LocalChatWidgets)
	{
		if (IsValid(ChattingWidget) && ChattingWidget->IsInViewport())
		{
			++ActiveCount;
		}
	}

	return ActiveCount;
}
