// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RTPSCharacterBase.generated.h"

UCLASS()
class RTPS_API ARTPSCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ARTPSCharacterBase();

	/* Attack Hit */
protected:
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/* Dead */
protected:
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Stat, Meta = ( AllowPrivateAccess = "true" ) )
	TObjectPtr<class UAnimMontage> DeadMontage;
	virtual void SetDead();
	void PlayDeadAnimation();

	float DeadEventDelayTime = 5.f;
};
