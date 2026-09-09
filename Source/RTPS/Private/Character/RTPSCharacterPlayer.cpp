/// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/RTPSCharacterPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Game/RTPSGameInstance.h"
#include "Net/VoiceConfig.h"
#include "Controller/RTPSCommonPlayerController.h"
#include "Controller/RTPSPlayerController.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelAuthoring/VoxelEditComponent.h"

// --- Construction ---

ARTPSCharacterPlayer::ARTPSCharacterPlayer()
{
	// Camera
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>( TEXT( "CameraBoom" ) );
	CameraBoom->SetupAttachment( RootComponent );
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>( TEXT( "FollowCamera" ) );
	FollowCamera->SetupAttachment( CameraBoom, USpringArmComponent::SocketName );
	FollowCamera->bUsePawnControlRotation = false;

	const ConstructorHelpers::FObjectFinder<UInputMappingContext> InputMappingContextRef( TEXT( "/Script/EnhancedInput.InputMappingContext'/Game/RTPS/Input/IMC_Shoulder.IMC_Shoulder'" ) );
	if( nullptr != InputMappingContextRef.Object )
	{
		DefaultMappingContext = InputMappingContextRef.Object;
	}

	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionRespawnRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/RTPS/Input/Action/IA_Respawn.IA_Respawn'" ) );
	if( nullptr != InputActionRespawnRef.Object )
	{
		RespawnAction = InputActionRespawnRef.Object;
	}

	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionJumpRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/RTPS/Input/Action/IA_Jump.IA_Jump'" ) );
	if( nullptr != InputActionJumpRef.Object )
	{
		JumpAction = InputActionJumpRef.Object;
	}

	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionShoulderMoveRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/RTPS/Input/Action/IA_Move.IA_Move'" ) );
	if( nullptr != InputActionShoulderMoveRef.Object )
	{
		ShoulderMoveAction = InputActionShoulderMoveRef.Object;
	}

	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionShoulderLookRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/RTPS/Input/Action/IA_Look.IA_Look'" ) );
	if( nullptr != InputActionShoulderLookRef.Object )
	{
		ShoulderLookAction = InputActionShoulderLookRef.Object;
	}

	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionChatRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/RTPS/Input/Action/IA_Chat.IA_Chat'" ) );
	if( nullptr != InputActionChatRef.Object )
	{
		ChatAction = InputActionChatRef.Object;
	}

	// Î®?üæ?åÎåÑÎ£∑Îö∞??(FollowCamera?∫¬ÄÔß°Áßª?ÄÏ∞ìÎ∫£?ÉÎ∫•?óÈçÆÍæ©ÎïÑ)
	FlashlightComponent = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	FlashlightComponent->SetupAttachment(FollowCamera);
	FlashlightComponent->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
	FlashlightComponent->SetRelativeRotation(FRotator(0.f, 0.f, 0.f));
	FlashlightComponent->SetIntensity(8000.f);
	FlashlightComponent->SetOuterConeAngle(30.f);
	FlashlightComponent->SetInnerConeAngle(15.f);
	FlashlightComponent->SetAttenuationRadius(3000.f);
	FlashlightComponent->SetVisibility(false); // Êπ≤Í≥ï???∞Ïá±Ï≠?Í≥πÍπ≠

	// G Í≥πÏÉáÎ¨íÏäú ?™ÎÄ?Êø°ÏíïÎ±?
	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionInteractRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/Input/IA_Interact.IA_Interact'" ) );
	if( nullptr != InputActionInteractRef.Object )
	{
		InteractAction = InputActionInteractRef.Object;
	}

	// R Œº???™ÎÄ?Êø°ÏíïÎ±?
	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionReloadRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/Input/IA_Reload.IA_Reload'" ) );
	if( nullptr != InputActionReloadRef.Object )
	{
		ReloadAction = InputActionReloadRef.Object;
	}

	// C ??¶∞ ?™ÎÄ?Êø°ÏíïÎ±?
	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionCrouchRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/Input/IA_Crouch.IA_Crouch'" ) );
	if( nullptr != InputActionCrouchRef.Object )
	{
		CrouchAction = InputActionCrouchRef.Object;
	}

	// Shift ??ÅÊπ≤?™ÎÄ?Êø°ÏíïÎ±?
	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionSprintRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/Input/IA_Sprint.IA_Sprint'" ) );
	if( nullptr != InputActionSprintRef.Object )
	{
		SprintAction = InputActionSprintRef.Object;
	}

	// F Î®?üæ?™ÎÄ?Êø°ÏíïÎ±?
	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionFlashlightRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/Input/IA_Flashlight.IA_Flashlight'" ) );
	if( nullptr != InputActionFlashlightRef.Object )
	{
		FlashlightAction = InputActionFlashlightRef.Object;
	}

	// Í≥†Í≤¢??Î®?ó´ ?™ÎÄ?Êø°ÏíïÎ±?
	const ConstructorHelpers::FObjectFinder<UInputAction> InputActionAimRef( TEXT( "/Script/EnhancedInput.InputAction'/Game/Input/IA_Aim.IA_Aim'" ) );
	if( nullptr != InputActionAimRef.Object )
	{
		AimAction = InputActionAimRef.Object;
	}

	VoxelEditComp = CreateDefaultSubobject<UVoxelEditComponent>( TEXT( "VoxelEditComp" ) );
}

void ARTPSCharacterPlayer::SetupPlayerInputComponent( UInputComponent* PlayerInputComponent )
{
	Super::SetupPlayerInputComponent( PlayerInputComponent );

	check(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>( PlayerInputComponent );
	
	EnhancedInputComponent->BindAction( JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump );
	EnhancedInputComponent->BindAction( JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping );
	EnhancedInputComponent->BindAction( RespawnAction, ETriggerEvent::Triggered, this, &ARTPSCharacterPlayer::Respawn );
	EnhancedInputComponent->BindAction( ShoulderMoveAction, ETriggerEvent::Triggered, this, &ARTPSCharacterPlayer::Move );
	EnhancedInputComponent->BindAction( ShoulderLookAction, ETriggerEvent::Triggered, this, &ARTPSCharacterPlayer::Look );
	EnhancedInputComponent->BindAction( ChatAction, ETriggerEvent::Triggered, this, &ARTPSCharacterPlayer::ChatButtonPressed );

		// G Í≥πÏÉáÎ¨íÏäú
	if( InteractAction && VoxelEditComp )
	{
		EnhancedInputComponent->BindAction( InteractAction, ETriggerEvent::Started, this, &ARTPSCharacterPlayer::ApplyVoxelBrush );
	}

	// R Œº??
	if( ReloadAction )
	{
		// EnhancedInputComponent->BindAction( ReloadAction, ETriggerEvent::Started, this, &ARTPSCharacterPlayer::Reload );
	}

	// C ??¶∞ Ï¢?
	if( CrouchAction )
	{
		EnhancedInputComponent->BindAction( CrouchAction, ETriggerEvent::Started, this, &ARTPSCharacterPlayer::ToggleCrouch );
	}

	// Shift ??ÅÊπ≤(?- Ôß£ÏÑè??Íæ?1Ë∏∞Ïíñ?? ?´ÎÇÖÏ¶?
	if( SprintAction )
	{
		EnhancedInputComponent->BindAction( SprintAction, ETriggerEvent::Started, this, &ARTPSCharacterPlayer::StartSprint );
		EnhancedInputComponent->BindAction( SprintAction, ETriggerEvent::Completed, this, &ARTPSCharacterPlayer::StopSprint );
	}

	// F Î®?üæÏ¢?
	if( FlashlightAction )
	{
		EnhancedInputComponent->BindAction( FlashlightAction, ETriggerEvent::Started, this, &ARTPSCharacterPlayer::ToggleFlashlight );
	}

	// Í≥†Í≤¢??Î®?ó´ (?- Íæ®‚Ö§ÔßéÏíñ?? ?∞„àÉ ?´ÎÇÖÏ¶?
	if( AimAction && !bEnableVoxelShotPrototypeInput )
	{
		EnhancedInputComponent->BindAction( AimAction, ETriggerEvent::Started, this, &ARTPSCharacterPlayer::StartAim );
		EnhancedInputComponent->BindAction( AimAction, ETriggerEvent::Completed, this, &ARTPSCharacterPlayer::StopAim );
	}

	PlayerInputComponent->BindKey(EKeys::F4, IE_Pressed, this, &ARTPSCharacterPlayer::ToggleVoxelEditor);
	if( bEnableVoxelShotPrototypeInput )
	{
		PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ARTPSCharacterPlayer::FireRemoveVoxelShot);
		PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ARTPSCharacterPlayer::FireAddVoxelShot);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Voxel shot prototype input binding. Character=%s Enabled=%d RMB=%s"),
		*GetNameSafe(this),
		bEnableVoxelShotPrototypeInput ? 1 : 0,
		bEnableVoxelShotPrototypeInput ? TEXT("AddVoxelShot") : TEXT("Aim"));
}

#pragma region Actor lifecycle
void ARTPSCharacterPlayer::BeginPlay()
{
	Super::BeginPlay();

	RespawnLocation = GetActorLocation();
	RespawnRotation = GetActorRotation();

	if( APlayerController* PlayerCOntroller = Cast<APlayerController>( GetController() ) )
	{
		if( ULocalPlayer* LocalPlayer = PlayerCOntroller->GetLocalPlayer() )
		{
			if( UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>( LocalPlayer ) )
			{
				if( DefaultMappingContext )
				{
					Subsystem->AddMappingContext( DefaultMappingContext, 0 );
					// Subsystem->RemoveMappingContext(DefaultMappingContext); // Í≥†Íæ©Îø?Ôßç„ÖΩÎ∏®ÎåÅ?£Îçà??
				}
			}
		}
	}

}

void ARTPSCharacterPlayer::Tick( float DeltaTime )
{
	Super::Tick( DeltaTime );

	if( CameraBoom && FollowCamera )
	{
		const float TargetArmLength   = bIsAiming ? AimArmLength   : DefaultArmLength;
		const FVector TargetOffset    = bIsAiming ? AimSocketOffset : DefaultSocketOffset;
		const float TargetFOV         = bIsAiming ? AimFOV          : DefaultFOV;

		// ?∫¬Ä?ïÏú≠ËπÇÎãøÏª?
		CameraBoom->TargetArmLength = FMath::FInterpTo( CameraBoom->TargetArmLength, TargetArmLength, DeltaTime, AimInterpSpeed );
		CameraBoom->SocketOffset    = FMath::VInterpTo( CameraBoom->SocketOffset,    TargetOffset,    DeltaTime, AimInterpSpeed );
		FollowCamera->FieldOfView   = FMath::FInterpTo( FollowCamera->FieldOfView,   TargetFOV,       DeltaTime, AimInterpSpeed );

		// Î®?üæÍπÜÏì£ ÁßªÎ?Ï∞ìForward Ë´õ‚ë∫Îº?á∞Ï§?Î∫£Ï†π
		if( FlashlightComponent && FlashlightComponent->IsVisible() )
		{
			FlashlightComponent->SetWorldRotation( FollowCamera->GetComponentRotation() );
		}
	}
}
#pragma endregion

#pragma region Input actions
void ARTPSCharacterPlayer::ChatButtonPressed( const FInputActionValue& Value )
{
	ARTPSCommonPlayerController* PlayerController = Cast<ARTPSCommonPlayerController>( GetController() );
	if( PlayerController )
	{
		PlayerController->ActivateChatting();
	}
}

void ARTPSCharacterPlayer::Move( const FInputActionValue& Value )
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	const FRotator Rotation = Controller->GetControlRotation();
	const FRotator YawRotation( 0.f, Rotation.Yaw, 0.f );

	const FVector ForwardDirection = FRotationMatrix( YawRotation ).GetUnitAxis( EAxis::X );
	const FVector RightDirection = FRotationMatrix( YawRotation ).GetUnitAxis( EAxis::Y );

	AddMovementInput( ForwardDirection, MovementVector.X );
	AddMovementInput( RightDirection, MovementVector.Y );
}

void ARTPSCharacterPlayer::Look( const FInputActionValue& Value )
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	AddControllerYawInput( LookAxisVector.X );
	AddControllerPitchInput( LookAxisVector.Y );
}

void ARTPSCharacterPlayer::Respawn( const FInputActionValue& Value )
{
	SetActorLocation( RespawnLocation );
	SetActorRotation( RespawnRotation );
}
#pragma endregion


// ===================== G Í≥πÏÉáÎ¨íÏäú =====================
// BlueprintNativeEvent?ÄÊø°ÈáâÎ∂æÔºàÍæ®‚îõÎ™ÑÎøâ?ªÏæ≠?±Ïî†Â™õ¬Ävoid ARTPSCharacterPlayer::Interact_Implementation()
//{
//	// TODO: Í≥πÏÉáÎ¨íÏäú Â™õ¬ÄŒΩÎ∏??´ÍΩ£ Â™õÎ®Ø ?ΩÎªæ
//	UE_LOG( LogTemp, Log, TEXT("[RTPS] Interact() called by %s"), *GetName() );
//	if( GEngine )
//	{
//		GEngine->AddOnScreenDebugMessage( -1, 2.f, FColor::Cyan, TEXT("[G Í≥πÏÉáÎ¨íÏäú") );
//	}
//}

// ===================== R Œº??=====================
// BlueprintNativeEvent?ÄÊø°ÈáâÎ∂æÔºàÍæ®‚îõÎ™ÑÎøâ?ªÏæ≠?±Ïî†Â™õ¬Ävoid ARTPSCharacterPlayer::Reload_Implementation()
//{
//	// TODO: Íæ©Ïò± ŒºÍ∞ëËáæ?øÎ¶∞Œº??ÔßèÎÇÖÏ°?Íæ®Îññ
//	UE_LOG( LogTemp, Log, TEXT("[RTPS] Reload() called by %s"), *GetName() );
//	if( GEngine )
//	{
//		GEngine->AddOnScreenDebugMessage( -1, 2.f, FColor::Yellow, TEXT("[R Œº??) );
//	}
//}

// ===================== C ??¶∞ Ï¢?=====================
void ARTPSCharacterPlayer::ToggleCrouch()
{
	if( GetCharacterMovement()->IsCrouching() )
	{
		UnCrouch();
		if( GEngine )
		{
			GEngine->AddOnScreenDebugMessage( -1, 2.f, FColor::White, TEXT("[C] stand") );
		}
	}
	else
	{
		Crouch();
		if( GEngine )
		{
			GEngine->AddOnScreenDebugMessage( -1, 2.f, FColor::White, TEXT("[C] crouch") );
		}
	}
}

// ===================== Shift ??ÅÊπ≤(? =====================
void ARTPSCharacterPlayer::StartSprint()
{
	// ??Í≥πÍπ≠Î®?Ωå??ÅÊπ≤?∫Îçá
	if( GetCharacterMovement()->IsCrouching() )
	{
		return;
	}

	GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
	if( GEngine )
	{
		GEngine->AddOnScreenDebugMessage( 1, 1.f, FColor::Green, FString::Printf( TEXT("[Shift] sprint (%.0f)"), SprintSpeed ) );
	}
}

void ARTPSCharacterPlayer::StopSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	if( GEngine )
	{
		GEngine->AddOnScreenDebugMessage( 1, 1.f, FColor::Green, FString::Printf( TEXT("[Shift] walk (%.0f)"), WalkSpeed ) );
	}
}

// ===================== Í≥†Í≤¢??Î®?ó´ (? =====================
void ARTPSCharacterPlayer::StartAim()
{
	bIsAiming = true;

	// Î®?ó´ ‰ª•Ôß¶Î®?îÉÍ≥?ÁßªÎ?Ï∞ìË´õ?∫Îº¢?∞Ï§à ??üæ
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	if( GEngine )
	{
		GEngine->AddOnScreenDebugMessage( 3, 1.f, FColor::Red, TEXT("[RMB] aim") );
	}
}

void ARTPSCharacterPlayer::StopAim()
{
	bIsAiming = false;

	// Î®?ó´ ?ÅÏ†£ Î®?òí ??üæ Ë´õ‚ëπ?áÏá∞Ï§?ËπÇÎì¶
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	if( GEngine )
	{
		GEngine->AddOnScreenDebugMessage( 3, 1.f, FColor::Red, TEXT("[RMB] aim off") );
	}
}

// ===================== F Î®?üæÏ¢?=====================
void ARTPSCharacterPlayer::ToggleFlashlight()
{
	if( FlashlightComponent )
	{
		const bool bIsOn = FlashlightComponent->IsVisible();
		FlashlightComponent->SetVisibility( !bIsOn );
		if( GEngine )
		{
			GEngine->AddOnScreenDebugMessage( 2, 2.f, FColor::Orange, bIsOn ? TEXT("[F] flashlight off") : TEXT("[F] flashlight on") );
		}
	}
}

void ARTPSCharacterPlayer::FireRemoveVoxelShot()
{
	if (ARTPSPlayerController* PC = Cast<ARTPSPlayerController>(GetController()))
	{
		PC->RequestVoxelShot(EVoxelBrushMode::Remove);
	}
}

void ARTPSCharacterPlayer::FireAddVoxelShot()
{
	if (ARTPSPlayerController* PC = Cast<ARTPSPlayerController>(GetController()))
	{
		PC->RequestVoxelShot(EVoxelBrushMode::Add);
	}
}

//void ARTPSCharacterPlayer::Interact_Implementation()
//{
//}
//
//void ARTPSCharacterPlayer::Reload_Implementation()
//{
//}

void ARTPSCharacterPlayer::ApplyVoxelBrush()
{
	if( VoxelEditComp )
	{
		VoxelEditComp->RequestBrushAtScreenCenter();
	}
}

void ARTPSCharacterPlayer::ToggleVoxelEditor()
{
	ARTPSPlayerController* PC = Cast<ARTPSPlayerController>(GetController());
	if (!IsValid(PC))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Voxel editor enter skipped because controller is not ARTPSPlayerController. Character=%s Controller=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetController()));
		return;
	}

	PC->ServerEnterVoxelEditorMode();
}
