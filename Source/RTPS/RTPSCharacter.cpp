// Fill out your copyright notice in the Description page of Project Settings.


#include "RTPSCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

// Sets default values
ARTPSCharacter::ARTPSCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	SetNetUpdateFrequency( 60.f );
	SetMinNetUpdateFrequency( 30.f );
}

// Called to bind functionality to input
void ARTPSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ARTPSCharacter::OnRep_Sprinting()
{
	GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? 800.f : 500.f;
}

void ARTPSCharacter::ServerSetSprinting_Implementation( bool bNewSprinting )
{
	bIsSprinting = bNewSprinting;
	GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? 800.f : 500.f;
}

void ARTPSCharacter::StartSprint()
{
	if( !bIsSprinting )
	{
		ServerSetSprinting( true );
	}
}

void ARTPSCharacter::StopSprint()
{
	if( !bIsSprinting )
	{
		ServerSetSprinting( false );
	}
}

// Called when the game starts or when spawned
void ARTPSCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

void ARTPSCharacter::GetLifetimeReplicatedProps( TArray<FLifetimeProperty>& OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );
	DOREPLIFETIME( ARTPSCharacter, bIsSprinting );
}

// Called every frame
void ARTPSCharacter::Tick( float DeltaTime )
{
	Super::Tick( DeltaTime );
}
