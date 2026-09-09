#include "VoxelAuthoring/VoxelEditorPawn.h"

#include "Controller/RTPSPlayerController.h"
#include "VoxelAuthoring/VoxelChunkManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

AVoxelEditorPawn::AVoxelEditorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = true;
	bUseControllerRotationRoll = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
}

void AVoxelEditorPawn::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(ChunkManager))
	{
		ChunkManager = Cast<AVoxelChunkManager>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelChunkManager::StaticClass()));
	}
}

void AVoxelEditorPawn::EnterEditorMode(APawn* InOriginalPawn, UInputMappingContext* InCharacterIMC)
{
	OriginalPawn = InOriginalPawn;
	CachedCharacterIMC = InCharacterIMC;

	if (IsValid(InOriginalPawn))
	{
		const FVector SpawnLoc = InOriginalPawn->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		SetActorLocationAndRotation(SpawnLoc, InOriginalPawn->GetActorRotation());

		if (APlayerController* PC = Cast<APlayerController>(InOriginalPawn->GetController()))
		{
			PC->SetControlRotation(InOriginalPawn->GetActorRotation());
		}
	}

	RemoveCharacterIMC();
}

void AVoxelEditorPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC))
	{
		return;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	AddControllerYawInput(MouseX * MouseSensitivity);
	AddControllerPitchInput(-MouseY * MouseSensitivity);

	// Scroll wheel - Ctrl held: adjust strength, otherwise: adjust radius
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		if (bCtrl) { BrushStrength = FMath::Clamp(BrushStrength + 0.05f, 0.01f, 1.f); }
		else { BrushRadius = FMath::Min(BrushRadius + BrushRadiusStep, 2000.f); }
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		if (bCtrl) { BrushStrength = FMath::Clamp(BrushStrength - 0.05f, 0.01f, 1.f); }
		else { BrushRadius = FMath::Max(BrushRadius - BrushRadiusStep, 50.f); }
	}

	// Ctrl+S: save
	static bool bSaveKeyWasDown = false;
	const bool bSKeyDown = PC->IsInputKeyDown(EKeys::S);
	if (bCtrl && bSKeyDown && !bSaveKeyWasDown)
	{
		OnSave();
	}
	bSaveKeyWasDown = bSKeyDown;

	const FRotator CtrlRot = GetControlRotation();
	const FVector Forward = FRotationMatrix(CtrlRot).GetScaledAxis(EAxis::X);
	const FVector Right = FRotationMatrix(CtrlRot).GetScaledAxis(EAxis::Y);
	const FVector Up = FVector::UpVector;

	FVector Delta = FVector::ZeroVector;
	if (bMoveForward) { Delta += Forward; }
	if (bMoveBack) { Delta -= Forward; }
	if (bMoveRight) { Delta += Right; }
	if (bMoveLeft) { Delta -= Right; }
	if (PC->IsInputKeyDown(EKeys::E)) { Delta += Up; }
	if (PC->IsInputKeyDown(EKeys::Q)) { Delta -= Up; }

	if (!Delta.IsZero())
	{
		const bool bFast = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
		const float Speed = CameraSpeed * (bFast ? FastSpeedMultiplier : 1.f);
		AddActorWorldOffset(Delta.GetSafeNormal() * Speed * DeltaTime);
	}

	MaybeSendEditorViewTransform(false);
	DrawHintsAndPreview();
}

void AVoxelEditorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindKey(EKeys::W, IE_Pressed, this, &AVoxelEditorPawn::OnMoveForwardPressed);
	PlayerInputComponent->BindKey(EKeys::W, IE_Released, this, &AVoxelEditorPawn::OnMoveForwardReleased);
	PlayerInputComponent->BindKey(EKeys::S, IE_Pressed, this, &AVoxelEditorPawn::OnMoveBackPressed);
	PlayerInputComponent->BindKey(EKeys::S, IE_Released, this, &AVoxelEditorPawn::OnMoveBackReleased);
	PlayerInputComponent->BindKey(EKeys::D, IE_Pressed, this, &AVoxelEditorPawn::OnMoveRightPressed);
	PlayerInputComponent->BindKey(EKeys::D, IE_Released, this, &AVoxelEditorPawn::OnMoveRightReleased);
	PlayerInputComponent->BindKey(EKeys::A, IE_Pressed, this, &AVoxelEditorPawn::OnMoveLeftPressed);
	PlayerInputComponent->BindKey(EKeys::A, IE_Released, this, &AVoxelEditorPawn::OnMoveLeftReleased);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AVoxelEditorPawn::OnBrushAdd);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AVoxelEditorPawn::OnBrushRemove);

	PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &AVoxelEditorPawn::OnRadiusDecrease);
	PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &AVoxelEditorPawn::OnRadiusIncrease);

	PlayerInputComponent->BindKey(EKeys::F4, IE_Pressed, this, &AVoxelEditorPawn::OnExitEditorMode);
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AVoxelEditorPawn::OnCycleShape);
}

void AVoxelEditorPawn::OnMoveForwardPressed() { bMoveForward = true; }
void AVoxelEditorPawn::OnMoveForwardReleased() { bMoveForward = false; }
void AVoxelEditorPawn::OnMoveBackPressed() { bMoveBack = true; }
void AVoxelEditorPawn::OnMoveBackReleased() { bMoveBack = false; }
void AVoxelEditorPawn::OnMoveRightPressed() { bMoveRight = true; }
void AVoxelEditorPawn::OnMoveRightReleased() { bMoveRight = false; }
void AVoxelEditorPawn::OnMoveLeftPressed() { bMoveLeft = true; }
void AVoxelEditorPawn::OnMoveLeftReleased() { bMoveLeft = false; }
void AVoxelEditorPawn::OnMoveUpPressed() { bMoveUp = true; }
void AVoxelEditorPawn::OnMoveUpReleased() { bMoveUp = false; }
void AVoxelEditorPawn::OnMoveDownPressed() { bMoveDown = true; }
void AVoxelEditorPawn::OnMoveDownReleased() { bMoveDown = false; }

void AVoxelEditorPawn::OnRadiusIncrease()
{
	BrushRadius = FMath::Min(BrushRadius + BrushRadiusStep, 2000.f);
}

void AVoxelEditorPawn::OnRadiusDecrease()
{
	BrushRadius = FMath::Max(BrushRadius - BrushRadiusStep, 50.f);
}

void AVoxelEditorPawn::OnBrushAdd() { ApplyBrush(EVoxelBrushMode::Add); }
void AVoxelEditorPawn::OnBrushRemove() { ApplyBrush(EVoxelBrushMode::Remove); }

void AVoxelEditorPawn::ApplyBrush(EVoxelBrushMode Mode)
{
	if (!IsValid(ChunkManager))
	{
		return;
	}

	FVector HitPos;
	if (!TryLineTrace(HitPos))
	{
		return;
	}
	ARTPSPlayerController* RTPSPlayerController = Cast<ARTPSPlayerController>(GetController());
	if (!IsValid(RTPSPlayerController))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Voxel editor brush request skipped because the controller is not ARTPSPlayerController. VoxelEditorPawn=%s"),
			*GetNameSafe(this));
		return;
	}

	FVoxelBrush Brush;
	Brush.WorldPosition = HitPos;
	Brush.Radius = BrushRadius;
	Brush.Strength = BrushStrength;
	Brush.Mode = Mode;
	Brush.Shape = CurrentShape;

	MaybeSendEditorViewTransform(true);
	RTPSPlayerController->ServerApplyVoxelBrush(Brush);
}

void AVoxelEditorPawn::MaybeSendEditorViewTransform(bool bForce)
{
	ARTPSPlayerController* RTPSPlayerController = Cast<ARTPSPlayerController>(GetController());
	if (!IsValid(RTPSPlayerController) || !RTPSPlayerController->IsLocalController())
	{
		return;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const float EffectiveInterval = FMath::Max(ViewTransformUpdateInterval, 0.01f);
	if (!bForce
		&& LastViewTransformUpdateClientTime >= 0.0
		&& CurrentTime - LastViewTransformUpdateClientTime < static_cast<double>(EffectiveInterval))
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetEditorViewTransform(ViewLocation, ViewRotation);

	const float DistanceThresholdSquared = FMath::Square(FMath::Max(ViewTransformUpdateDistanceThreshold, 0.f));
	const bool bMovedEnough = !bHasSentViewTransform
		|| FVector::DistSquared(LastSentViewLocation, ViewLocation) >= DistanceThresholdSquared;
	const bool bRotatedEnough = !bHasSentViewTransform
		|| !ViewRotation.Equals(LastSentViewRotation, FMath::Max(ViewTransformUpdateRotationThresholdDegrees, 0.f));

	if (!bForce && !bMovedEnough && !bRotatedEnough)
	{
		return;
	}

	RTPSPlayerController->ServerUpdateVoxelEditorViewTransform(ViewLocation, ViewRotation);
	LastSentViewLocation = ViewLocation;
	LastSentViewRotation = ViewRotation;
	LastViewTransformUpdateClientTime = CurrentTime;
	bHasSentViewTransform = true;
}

void AVoxelEditorPawn::GetEditorViewTransform(FVector& OutViewLocation, FRotator& OutViewRotation) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (IsValid(PC) && IsValid(PC->PlayerCameraManager))
	{
		OutViewLocation = PC->PlayerCameraManager->GetCameraLocation();
		OutViewRotation = PC->PlayerCameraManager->GetCameraRotation();
		return;
	}

	if (IsValid(Camera))
	{
		OutViewLocation = Camera->GetComponentLocation();
		OutViewRotation = Camera->GetComponentRotation();
		return;
	}

	OutViewLocation = GetActorLocation();
	OutViewRotation = GetActorRotation();
}

bool AVoxelEditorPawn::TryLineTrace(FVector& OutHitWorldPos) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) || !IsValid(PC->PlayerCameraManager))
	{
		return false;
	}

	const FVector Start = PC->PlayerCameraManager->GetCameraLocation();
	const FVector End = Start + PC->PlayerCameraManager->GetCameraRotation().Vector() * TraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelEditorTrace), false);
	Params.AddIgnoredActor(this);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return false;
	}

	OutHitWorldPos = Hit.ImpactPoint;
	return true;
}

void AVoxelEditorPawn::OnExitEditorMode()
{
	ARTPSPlayerController* PC = Cast<ARTPSPlayerController>(GetController());
	if (!IsValid(PC))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Voxel editor exit skipped because controller is not ARTPSPlayerController. VoxelEditorPawn=%s Controller=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetController()));
		return;
	}

	PC->ServerExitVoxelEditorMode();
}

void AVoxelEditorPawn::DrawHintsAndPreview() const
{
	FVector HitPos;
	if (TryLineTrace(HitPos))
	{
		// Brush shape preview
		FColor PreviewColor = FColor::Cyan;
		switch (CurrentShape)
		{
		case EVoxelBrushShape::Box:     PreviewColor = FColor::Yellow; break;
		case EVoxelBrushShape::Flatten: PreviewColor = FColor::Green;  break;
		case EVoxelBrushShape::Smooth:  PreviewColor = FColor::Purple; break;
		default: break;
		}

		if (CurrentShape == EVoxelBrushShape::Box)
		{
			const FVector Half(BrushRadius, BrushRadius, BrushRadius);
			DrawDebugBox(GetWorld(), HitPos, Half, PreviewColor, false, -1.f, 0, 2.f);
		}
		else
		{
			DrawDebugSphere(GetWorld(), HitPos, BrushRadius, 16, PreviewColor, false, -1.f, 0, 2.f);
		}
		DrawDebugPoint(GetWorld(), HitPos, 10.f, FColor::White, false, -1.f);
	}

	if (GEngine)
	{
		static const TArray<FString> ShapeNames = {TEXT("Sphere"), TEXT("Box"), TEXT("Flatten"), TEXT("Smooth")};
		const FString ShapeName = ShapeNames.IsValidIndex(static_cast<int32>(CurrentShape))
			? ShapeNames[static_cast<int32>(CurrentShape)] : TEXT("?");

		GEngine->AddOnScreenDebugMessage(901, 0.f, FColor::Yellow,  TEXT("Voxel Editor Mode"));
		GEngine->AddOnScreenDebugMessage(902, 0.f, FColor::White,   TEXT("WASD / QE      Move camera  (Shift: fast)"));
		GEngine->AddOnScreenDebugMessage(903, 0.f, FColor::White,   TEXT("Mouse          Look"));
		GEngine->AddOnScreenDebugMessage(904, 0.f, FColor::Cyan,    TEXT("LMB            Add"));
		GEngine->AddOnScreenDebugMessage(905, 0.f, FColor::Orange,  TEXT("RMB            Remove"));
		GEngine->AddOnScreenDebugMessage(906, 0.f, FColor::White,   FString::Printf(TEXT("[ / ]          Brush radius: %.0f"), BrushRadius));
		GEngine->AddOnScreenDebugMessage(907, 0.f, FColor::White,   FString::Printf(TEXT("Ctrl+Wheel     Brush strength: %.2f"), BrushStrength));
		GEngine->AddOnScreenDebugMessage(908, 0.f, FColor::White,   FString::Printf(TEXT("V              Brush shape: %s"), *ShapeName));
		GEngine->AddOnScreenDebugMessage(909, 0.f, FColor::White,   TEXT("Ctrl+S         Save"));
		GEngine->AddOnScreenDebugMessage(910, 0.f, FColor::Green,   TEXT("F4             Return to character"));
	}
}

void AVoxelEditorPawn::RemoveCharacterIMC()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) && IsValid(OriginalPawn))
	{
		PC = Cast<APlayerController>(OriginalPawn->GetController());
	}
	if (!IsValid(PC) || !IsValid(CachedCharacterIMC))
	{
		return;
	}

	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			Sub->RemoveMappingContext(CachedCharacterIMC);
		}
	}
}

void AVoxelEditorPawn::RestoreCharacterIMC()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) || !IsValid(CachedCharacterIMC))
	{
		return;
	}

	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			Sub->AddMappingContext(CachedCharacterIMC, 0);
		}
	}
}

void AVoxelEditorPawn::OnCycleShape()
{
	const int32 Count = static_cast<int32>(EVoxelBrushShape::Smooth) + 1;
	CurrentShape = static_cast<EVoxelBrushShape>((static_cast<int32>(CurrentShape) + 1) % Count);
}

void AVoxelEditorPawn::OnStrengthIncrease()
{
	BrushStrength = FMath::Clamp(BrushStrength + 0.05f, 0.01f, 1.f);
}

void AVoxelEditorPawn::OnStrengthDecrease()
{
	BrushStrength = FMath::Clamp(BrushStrength - 0.05f, 0.01f, 1.f);
}

void AVoxelEditorPawn::OnSave()
{
	if (IsValid(ChunkManager))
	{
		ChunkManager->SaveAllModifiedChunks();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(910, 3.f, FColor::Green, TEXT("Chunks saved"));
		}
	}
}
