// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/RTPSCharacterBase.h"
#include "RTPSEnemy.generated.h"

class UBossComponent;

/**
 * 
 */
UCLASS( Blueprintable )
class RTPS_API ARTPSEnemy : public ARTPSCharacterBase
{
	GENERATED_BODY()
	
public:
	ARTPSEnemy();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const override;

protected:
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
};
