// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "RTPSAnimInstanceBase.generated.h"

/**
 * 
 */
UCLASS()
class RTPS_API URTPSAnimInstanceBase : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	URTPSAnimInstanceBase();

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation( float DeltaSeconds ) override;

	UPROPERTY( VisibleAnywhere, BlueprintReadOnly, Category = Character )
	TObjectPtr<class ACharacter> Owner;

	UPROPERTY( VisibleAnywhere, BlueprintReadOnly, Category = Character )
	TObjectPtr<class UCharacterMovementComponent> Movement;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	FVector Velocity;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	float GroundSpeed;

	// idle 상태인지를 나타내는 bool 값
	// bool 값은 크기가 불분명하기 때문에 언리얼에서는 int 타입을 활용한다.
	// 다른 int와 구분하기 위해서 b 접두사를 붙이고 비트 플래그를 달아주면 사이즈가 명확한 bool 값이 된다.
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	uint8 bIsIdle : 1;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	float MovingThreshould;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	uint8 bIsFalling : 1;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	uint8 bIsJumping : 1;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Character )
	float JumpingThreshould;
};
