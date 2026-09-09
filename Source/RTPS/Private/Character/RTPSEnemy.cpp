// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RTPSEnemy.h"
#include "Net/UnrealNetwork.h"

ARTPSEnemy::ARTPSEnemy()
{
	bReplicates = true;
	// AI는 보통 서버에서만 의미가 크지만, 캐릭터 자체는 복제되도록 두는 게 일반적
	SetReplicateMovement( true );

	PrimaryActorTick.bCanEverTick = false;
}

void ARTPSEnemy::BeginPlay()
{
	Super::BeginPlay();
}

void ARTPSEnemy::GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );
}

float ARTPSEnemy::TakeDamage( float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser )
{
	Super::TakeDamage( Damage, DamageEvent,EventInstigator, DamageCauser );

	return Damage;
}
