#include "Controller/RTPSLoginController.h"

#include "Blueprint/UserWidget.h"
#include "UI/LoginMenu/LoginMenu.h"

ARTPSLoginController::ARTPSLoginController()
{
	const ConstructorHelpers::FClassFinder<ULoginMenu> LoginMenuClassRef(TEXT("/Game/WBP_LoginMenu.WBP_LoginMenu_C"));
	if (LoginMenuClassRef.Class)
	{
		LoginMenuClass = LoginMenuClassRef.Class;
	}
}

void ARTPSLoginController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeUIOnly MenuInputMode;
	SetInputMode(MenuInputMode);
	bShowMouseCursor = true;

	if (!IsLocalController() || LoginMenuClass == nullptr || LoginMenu != nullptr)
	{
		return;
	}

	LoginMenu = CreateWidget<ULoginMenu>(this, LoginMenuClass);
	if (LoginMenu)
	{
		LoginMenu->AddToViewport();
	}
}
