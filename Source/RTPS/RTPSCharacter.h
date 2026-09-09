// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RTPSCharacter.generated.h"

UCLASS()
class RTPS_API ARTPSCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ARTPSCharacter();

protected:
	virtual void SetupPlayerInputComponent( class UInputComponent* PlayerInputComponent ) override;

	UPROPERTY(ReplicatedUsing = OnRep_Sprinting)
	bool bIsSprinting = false;

	UFUNCTION()
	void OnRep_Sprinting();

	UFUNCTION( Server, Reliable )
	void ServerSetSprinting( bool bNewSprinting );

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	void StartSprint();
	void StopSprint();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
