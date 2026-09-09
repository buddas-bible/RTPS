#include "Game/RTPSLoginGameMode.h"

#include "Controller/RTPSLoginController.h"

ARTPSLoginGameMode::ARTPSLoginGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ARTPSLoginController::StaticClass();
	bUseSeamlessTravel = false;
}
