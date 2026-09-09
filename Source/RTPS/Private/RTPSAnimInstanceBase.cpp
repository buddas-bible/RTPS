// Fill out your copyright notice in the Description page of Project Settings.


#include "RTPSAnimInstanceBase.h"
#include "GameFramework/Character.h"					// ACharacter
#include "GameFramework/CharacterMovementComponent.h"	// UCharacterMovementComponent

URTPSAnimInstanceBase::URTPSAnimInstanceBase()
{
	MovingThreshould = 3.f;
	JumpingThreshould = 100.f;
}

void URTPSAnimInstanceBase::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// 현재 AnimInstance를 사용 중인 액터 정보를 가져온다.
	// Actor 타입으로 가져오기 때문에 Character 타입 안정성 체크
	Owner = Cast<ACharacter>( GetOwningActor() );
	if( Owner )
	{
		Movement = Owner->GetCharacterMovement();
	}
}

void URTPSAnimInstanceBase::NativeUpdateAnimation( float DeltaSeconds )
{
	Super::NativeUpdateAnimation( DeltaSeconds );

	if( Movement )
	{
		Velocity = Movement->Velocity;
		GroundSpeed = Velocity.Size2D();
		bIsIdle = GroundSpeed < MovingThreshould;
		bIsFalling = Movement->IsFalling();
		bIsJumping = bIsFalling & ( Velocity.Z > JumpingThreshould );
	}
}