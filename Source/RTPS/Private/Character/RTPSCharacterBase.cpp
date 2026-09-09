// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RTPSCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
ARTPSCharacterBase::ARTPSCharacterBase()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize( 42.f, 96.0f );
	GetCapsuleComponent()->SetCollisionProfileName( TEXT( "Pawn" ) );

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator( 0.f, 500.f, 0.f );
	GetCharacterMovement()->GravityScale = 1.75f;

	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	GetCharacterMovement()->AirControl = 0.35f;

	// ?щ씪?곗튂 ?쒖꽦??
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->SetCrouchedHalfHeight( 60.f );

	GetMesh()->SetRelativeLocationAndRotation( FVector( 0.f, 0.f, -100.f ), FRotator( 0.f, -90.f, 0.f ) );
	GetMesh()->SetAnimationMode( EAnimationMode::AnimationBlueprint );
	GetMesh()->SetCollisionProfileName( TEXT( "CharacterMesh" ) );

	const ConstructorHelpers::FObjectFinder<USkeletalMesh> SkeletalMeshRef( TEXT( "/Script/Engine.SkeletalMesh'/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny'"));
	if( SkeletalMeshRef.Object )
	{
		GetMesh()->SetSkeletalMesh( SkeletalMeshRef.Object );
	}

	const ConstructorHelpers::FClassFinder<UAnimInstance> AnimBPClassRef( TEXT( "/Game/Characters/Mannequins/Animations/ABP_Manny.ABP_Manny_C" ) );
	if( AnimBPClassRef.Class )
	{
		GetMesh()->SetAnimInstanceClass( AnimBPClassRef.Class );
	}

}

float ARTPSCharacterBase::TakeDamage( float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser )
{
	Super::TakeDamage( Damage, DamageEvent, EventInstigator, DamageCauser );

	SetDead();

	return Damage;
}

void ARTPSCharacterBase::SetDead()
{
	GetCharacterMovement()->SetMovementMode( EMovementMode::MOVE_None );
	PlayDeadAnimation();
	SetActorEnableCollision( false );
}

void ARTPSCharacterBase::PlayDeadAnimation()
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();

	if( AnimInstance )
	{
		AnimInstance->StopAllMontages( 0.f );
		if( IsValid( DeadMontage ) )
		{
			AnimInstance->Montage_Play( DeadMontage, 1.f );
		}
	}
}
