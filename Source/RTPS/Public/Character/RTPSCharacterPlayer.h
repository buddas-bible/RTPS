/// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/RTPSCharacterBase.h"
#include "InputActionValue.h"
#include "RTPSCharacterPlayer.generated.h"

class UVoxelEditComponent;
class AVoxelEditorPawn;
class UInputMappingContext;

/**
 * 
 */
UCLASS()
class RTPS_API ARTPSCharacterPlayer : public ARTPSCharacterBase
{
	GENERATED_BODY()

public:
	ARTPSCharacterPlayer();
	// APawn / ACharacter interface
	void SetupPlayerInputComponent( class UInputComponent* PlayerInputComponent ) override;
	UInputMappingContext* GetDefaultMappingContext() const { return DefaultMappingContext; }

protected:
#pragma region Actor lifecycle
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
#pragma endregion

#pragma region Input actions
	void ChatButtonPressed(const FInputActionValue& Value);
	void Move( const FInputActionValue& Value );
	void Look( const FInputActionValue& Value );
	void Respawn( const FInputActionValue& Value );
#pragma endregion

#pragma region Camera
	UPROPERTY( VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = ( AllowPrivateAccess = "true" ) )
	TObjectPtr<class USpringArmComponent> CameraBoom;

	UPROPERTY( VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = ( AllowPrivateAccess = "true" ) )
	TObjectPtr<class UCameraComponent> FollowCamera;
#pragma endregion

#pragma region Input assets
	// 占쎈Ⅸ 占쎌뀑占쎈줈 蹂寃쏀븷 占쎈룄濡ㅺ퀎占쎄린 占쏀빐 EditAnywhere 吏占쎌옄瑜ъ슜
	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Input, Meta = ( AllowPrivateAccess = "Ture" ) )
	TObjectPtr<class UInputMappingContext> DefaultMappingContext;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Input, Meta = ( AllowPrivateAccess = "Ture" ) )
	TObjectPtr<class UInputAction> RespawnAction;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Input, Meta = ( AllowPrivateAccess = "Ture" ) )
	TObjectPtr<class UInputAction> JumpAction;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Input, Meta = ( AllowPrivateAccess = "Ture" ) )
	TObjectPtr<class UInputAction> ShoulderMoveAction;

	UPROPERTY( EditAnywhere, BlueprintReadOnly, Category = Input, Meta = ( AllowPrivateAccess = "Ture" ) )
	TObjectPtr<class UInputAction> ShoulderLookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "Ture" ))
	TObjectPtr<class UInputAction> ChatAction;

	// G 占쏀샇占쎌슜
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInputAction> InteractAction;

	// R 占쎌쟾
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInputAction> ReloadAction;

	// C 占쎄린 (占쏙옙)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInputAction> CrouchAction;

	// Shift 占쎈━占?占?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInputAction> SprintAction;

	// F 占쎌쟾(占쏙옙)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInputAction> FlashlightAction;

	// 占쏀겢占?占쎌엫 (占?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UInputAction> AimAction;

	// Prototype default: RMB is reserved for Add voxel shot instead of Aim while this is enabled.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Prototype", Meta = (AllowPrivateAccess = "true"))
	bool bEnableVoxelShotPrototypeInput = true;
#pragma endregion

		// 占쎌쟾而댄룷占쏀듃
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flashlight", Meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class USpotLightComponent> FlashlightComponent;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UVoxelEditComponent> VoxelEditComp;

protected:

	// 占쎈━湲띾룄 (占쎈뵒占쎌뿉議곗젅 媛
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", Meta = (AllowPrivateAccess = "true"))
	float SprintSpeed = 900.f;

	// 湲곕낯 嫄룰린 占쎈룄 (占쎈뵒占쎌뿉議곗젅 媛
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", Meta = (AllowPrivateAccess = "true"))
	float WalkSpeed = 500.f;

	// ===================== 占쎌엫 移대찓占쎌젙 (占쎈뵒占쎌뿉議곗젅 媛 =====================

	// 湲곕낯 移대찓嫄곕━
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	float DefaultArmLength = 400.f;

	// 占쎌엫 移대찓嫄곕━
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	float AimArmLength = 180.f;

	// 湲곕낯 移대찓占쏀봽(罹먮┃湲곤옙)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	FVector DefaultSocketOffset = FVector(0.f, 0.f, 0.f);

	// 占쎌엫 移대찓占쏀봽(占쎈Ⅸ履닿묠 占쎈줈 占쎈룞)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	FVector AimSocketOffset = FVector(0.f, 70.f, 20.f);

	// 湲곕낯 FOV
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	float DefaultFOV = 90.f;

	// 占쎌엫 FOV
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	float AimFOV = 65.f;

	// 移대찓蹂닿컙 占쎈룄
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Aim", Meta = (AllowPrivateAccess = "true"))
	float AimInterpSpeed = 10.f;

	// 占쏀샇占쎌슜 (G - 占쎈쾭占쎌씠媛占쏀븯占쎈줉 virtual
	//UFUNCTION(BlueprintNativeEvent, Category = "Action")
	//void Interact();
	//virtual void Interact_Implementation();

	// 占쎌쟾 (R - 占쎈쾭占쎌씠媛占쏀븯占쎈줉 virtual
	//UFUNCTION(BlueprintNativeEvent, Category = "Action")
	//void Reload();
	//virtual void Reload_Implementation();

	// 占쎄린 占쏙옙 (C
	void ToggleCrouch();

	// 占쎈━湲쒖옉/醫낅즺 (Shift 占?
	void StartSprint();
	void StopSprint();

	// 占쎌쟾占쏙옙 (F
	void ToggleFlashlight();

	// 占쎌엫 占쎌옉/醫낅즺 (占쏀겢由
	void StartAim();
	void StopAim();
	void FireRemoveVoxelShot();
	void FireAddVoxelShot();

private:
	void ApplyVoxelBrush();
	void ToggleVoxelEditor();

	FVector RespawnLocation;
	FRotator RespawnRotation;
	bool bIsAiming = false;
};
