#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelEditorPawn.generated.h"

class UCameraComponent;
class AVoxelChunkManager;
class UInputMappingContext;

UCLASS()
class RTPS_API AVoxelEditorPawn : public APawn
{
	GENERATED_BODY()

public:
	AVoxelEditorPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Called by RTPSCharacterPlayer before Possess()
	void EnterEditorMode(APawn* InOriginalPawn, UInputMappingContext* InCharacterIMC);
	APawn* GetOriginalPawn() const { return OriginalPawn.Get(); }

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Camera")
	float CameraSpeed = 800.f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Camera")
	float FastSpeedMultiplier = 4.f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Camera")
	float MouseSensitivity = 0.2f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Brush")
	float BrushRadius = 200.f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Brush")
	float BrushRadiusStep = 50.f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Brush")
	float TraceDistance = 5000.f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Brush")
	float BrushStrength = 0.15f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Network", meta = (ClampMin = "0.01"))
	float ViewTransformUpdateInterval = 0.1f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Network", meta = (ClampMin = "0.0"))
	float ViewTransformUpdateDistanceThreshold = 25.f;

	UPROPERTY(EditAnywhere, Category = "VoxelEditor|Network", meta = (ClampMin = "0.0"))
	float ViewTransformUpdateRotationThresholdDegrees = 2.f;

	UPROPERTY(EditInstanceOnly, Category = "VoxelEditor")
	TObjectPtr<AVoxelChunkManager> ChunkManager;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<APawn> OriginalPawn;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> CachedCharacterIMC;

	// Movement flags
	bool bMoveForward = false;
	bool bMoveBack = false;
	bool bMoveRight = false;
	bool bMoveLeft = false;
	bool bMoveUp = false;
	bool bMoveDown = false;

	EVoxelBrushShape CurrentShape = EVoxelBrushShape::Sphere;
	double LastViewTransformUpdateClientTime = -1.0;
	bool bHasSentViewTransform = false;
	FVector LastSentViewLocation = FVector::ZeroVector;
	FRotator LastSentViewRotation = FRotator::ZeroRotator;

	void OnMoveForwardPressed();  void OnMoveForwardReleased();
	void OnMoveBackPressed();     void OnMoveBackReleased();
	void OnMoveRightPressed();    void OnMoveRightReleased();
	void OnMoveLeftPressed();     void OnMoveLeftReleased();
	void OnMoveUpPressed();       void OnMoveUpReleased();
	void OnMoveDownPressed();     void OnMoveDownReleased();

	void OnBrushAdd();
	void OnBrushRemove();
	void OnRadiusIncrease();
	void OnRadiusDecrease();
	void OnExitEditorMode();
	void OnCycleShape();
	void OnStrengthIncrease();
	void OnStrengthDecrease();
	void OnSave();

	bool TryLineTrace(FVector& OutHitWorldPos) const;
	void ApplyBrush(EVoxelBrushMode Mode);
	void MaybeSendEditorViewTransform(bool bForce);
	void GetEditorViewTransform(FVector& OutViewLocation, FRotator& OutViewRotation) const;
	void DrawHintsAndPreview() const;

	void RemoveCharacterIMC();
	void RestoreCharacterIMC();
};
