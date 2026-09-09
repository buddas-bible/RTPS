#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RTPSLoginController.generated.h"

UCLASS()
class RTPS_API ARTPSLoginController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTPSLoginController();

protected:
	virtual void BeginPlay() override;

protected:
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class ULoginMenu> LoginMenuClass;

	UPROPERTY()
	TObjectPtr<class ULoginMenu> LoginMenu;
};
