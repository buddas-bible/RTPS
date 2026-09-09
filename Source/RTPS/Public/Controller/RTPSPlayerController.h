#pragma once

#include "CoreMinimal.h"
#include "Controller/RTPSCommonPlayerController.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelAuthoring/VoxelChunkSyncTypes.h"
#include "VoxelAuthoring/VoxelEditOp.h"
#include "RTPSPlayerController.generated.h"

class ARTPSCharacterPlayer;
class AActor;
class AVoxelEditorPawn;
class UActorComponent;
class UInputMappingContext;
struct FRTPSVoxelChunkBoundaryDebugInfo;

enum class ERTPSVoxelShotHitKind : uint8
{
	None,
	VoxelChunkHit,
	BuildSurfaceHit,
	NonVoxelRejected,
};

UCLASS()
class RTPS_API ARTPSPlayerController : public ARTPSCommonPlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

public:
	UFUNCTION(BlueprintCallable)
	void RequestDebugMinorKill();

	UFUNCTION(BlueprintCallable)
	void RequestDebugBossKill();

	UFUNCTION(BlueprintCallable)
	void ToggleAutoRun();

	UFUNCTION(BlueprintCallable)
	void RequestVoxelShot(EVoxelBrushMode Mode);

	virtual TArray<FString> GetControlHintLines() const override;

	UFUNCTION(Server, Reliable)
	void ServerRegisterDebugMinorKill();

	UFUNCTION(Server, Reliable)
	void ServerRegisterDebugBossKill();

	UFUNCTION(Server, Reliable)
	void ServerApplyVoxelBrush(FVoxelBrush Brush);

	UFUNCTION(Server, Reliable)
	void ServerFireVoxelShot(EVoxelBrushMode Mode);

	UFUNCTION(Server, Reliable)
	void ServerEnterVoxelEditorMode();

	UFUNCTION(Server, Reliable)
	void ServerExitVoxelEditorMode();

	UFUNCTION(Server, Reliable)
	void ServerUpdateVoxelEditorViewTransform(FVector ViewLocation, FRotator ViewRotation);

	UFUNCTION(Server, Reliable)
	void ServerRequestVoxelChunkState(FIntVector ChunkCoord, int32 ClientKnownRevision);

	UFUNCTION(Server, Reliable)
	void ServerSubscribeVoxelChunk(FIntVector ChunkCoord, int32 ClientKnownRevision);

	UFUNCTION(Server, Reliable)
	void ServerUnsubscribeVoxelChunk(FIntVector ChunkCoord);

	UFUNCTION(Server, Reliable)
	void ServerUnsubscribeAllVoxelChunks();

	UFUNCTION(Client, Reliable)
	void ClientReceiveVoxelChunkState(const FRTPSVoxelChunkStatePayload& Payload);

	UFUNCTION(Client, Reliable)
	void ClientReceiveVoxelChunkDelta(const FRTPSVoxelChunkDeltaPayload& Payload);

	UFUNCTION(Client, Reliable)
	void ClientReceiveVoxelEditOp(const FRTPSVoxelEditOp& EditOp);

	bool ValidateVoxelBrushRequest(
		const FVoxelBrush& Brush,
		FString& OutReason,
		FString* OutValidationSource = nullptr,
		FString* OutValidationSourceReason = nullptr,
		float* OutDistance = nullptr,
		float* OutMaxDistance = nullptr) const;
	bool IsVoxelBrushFinite(const FVoxelBrush& Brush) const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FRTPSVoxelShotBuildSurfacePolicyTest;
#endif

	void BindGameplayInput();
	void CleanupVoxelEditorPawn(const TCHAR* Reason);
	void CleanupVoxelChunkSubscriptions(const TCHAR* Reason);
	void SetVoxelEditorCharacterMappingLocal(UInputMappingContext* CharacterMappingContext, bool bEnableMapping);
	void ResetVoxelEditorViewTransformState();
	bool IsVoxelEditorViewTransformFinite(const FVector& ViewLocation, const FRotator& ViewRotation) const;
	bool TryGetApprovedVoxelEditorViewLocation(FVector& OutViewLocation, FString& OutReason) const;
	bool TryBuildVoxelShotBrush(
		EVoxelBrushMode Mode,
		FVoxelBrush& OutBrush,
		FHitResult& OutHitResult,
		FVector& OutTraceStart,
		FVector& OutTraceEnd,
		FString& OutFailureReason,
		ERTPSVoxelShotHitKind& OutHitKind) const;
	void ConfigureVoxelShotBrushForHit(
		EVoxelBrushMode Mode,
		ERTPSVoxelShotHitKind HitKind,
		const FHitResult& HitResult,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		FVoxelBrush& OutBrush) const;
	bool IsVoxelBuildSurface(const AActor* HitActor, const UActorComponent* HitComponent) const;
	ERTPSVoxelShotHitKind ClassifyVoxelShotHit(
		EVoxelBrushMode Mode,
		const AActor* HitActor,
		const UActorComponent* HitComponent,
		FString& OutFailureReason) const;
	void LogVoxelShotRequestDebug(
		int64 ShotId,
		EVoxelBrushMode Mode,
		const FVector& TraceStart,
		const FVector& TraceEnd,
		float CooldownSeconds,
		double CooldownElapsedSeconds,
		bool bAccepted,
		const TCHAR* RejectionReason) const;
	void DrawVoxelShotDebug(
		const FVector& TraceStart,
		const FVector& TraceEnd,
		const FHitResult& HitResult,
		const FVoxelBrush* Brush,
		const class AVoxelChunk* HitChunk,
		bool bAccepted) const;

	UFUNCTION(Client, Reliable)
	void ClientSetVoxelEditorCharacterMapping(UInputMappingContext* CharacterMappingContext, bool bEnableMapping);

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "1.0"))
	float MaxVoxelEditDistance = 5000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "1.0"))
	float MaxVoxelBrushRadius = 2000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "0.01"))
	float MaxVoxelBrushStrength = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "0.0"))
	float MinVoxelBrushInterval = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Shot", meta = (ClampMin = "1.0"))
	float VoxelShotRange = 5000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Shot", meta = (ClampMin = "1.0"))
	float VoxelShotRadius = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Shot", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float VoxelShotStrength = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Shot", meta = (ClampMin = "0.0"))
	float VoxelShotCooldownSeconds = 0.2f;

	// 현재 기본 프로토타입 경로: 모든 허용된 Add/Remove 사격은 Sphere + Falloff + TargetLerp를 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true"))
	bool bUseUnifiedSphereVoxelShots = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true"))
	EVoxelBrushFalloff VoxelShotFalloff = EVoxelBrushFalloff::Smooth;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true"))
	EVoxelBrushBlendMode VoxelShotBlendMode = EVoxelBrushBlendMode::TargetLerp;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float VoxelShotConvergenceAlpha = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float VoxelAddSphereRadius = 220.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", ClampMax = "1.0"))
	float VoxelAddSphereStrength = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float VoxelRemoveSphereRadius = 220.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", ClampMax = "1.0"))
	float VoxelRemoveSphereStrength = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float VoxelChunkAddSphereEmbedDepth = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|UnifiedSphere", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float BuildSurfaceAddSphereOffset = 60.f;

	// Add shots may use only explicitly tagged non-voxel build surfaces; arbitrary WorldStatic actors stay rejected.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|BuildSurface", meta = (AllowPrivateAccess = "true"))
	FName VoxelBuildSurfaceTag = TEXT("VoxelBuildSurface");

	// 실험용: SurfaceBlob은 평평한 Floor/Wall 스플랫에는 쓸 수 있지만 기본 사격 경로에서는 현재 비활성화한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|BuildSurface", meta = (AllowPrivateAccess = "true"))
	bool bUseSurfaceBlobForBuildSurfaceAdd = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|BuildSurface", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float BuildSurfaceBlobRadius = 240.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|BuildSurface", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float BuildSurfaceBlobDepth = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|BuildSurface", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", ClampMax = "1.0"))
	float BuildSurfaceBlobStrength = 0.35f;

	// Legacy fallback only when unified sphere mode and experimental SurfaceBlob mode are both disabled.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|BuildSurface", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AddShotBuildSurfaceNormalOffset = 60.f;

	// 실험용: TerrainMudBlob은 Marching Cubes 지형에서 원반/판/층 누적이 보여 기본 사격 경로에서는 현재 비활성화한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true"))
	bool bUseTerrainMudBlobForVoxelChunkAdd = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float TerrainMudBlobRadius = 220.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float TerrainMudBlobDepth = 130.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", ClampMax = "1.0"))
	float TerrainMudBlobStrength = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float TerrainMudBlobBackDepth = 35.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float TerrainMudBlobEmbedDepth = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float TerrainMudBlobRoundnessPower = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float TerrainMudBlobConvergenceAlpha = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float TerrainMudBlobShotDirectionBlend = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|VoxelChunk", meta = (AllowPrivateAccess = "true"))
	EVoxelBrushBlendMode TerrainMudBlobBlendMode = EVoxelBrushBlendMode::TargetLerp;

	// Explicitly opt-in so prototype observability never changes default project logging behavior.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDebugVoxelShots = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|Debug", meta = (AllowPrivateAccess = "true"))
	bool bDrawVoxelShotDebug = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|Debug", meta = (AllowPrivateAccess = "true"))
	bool bShowVoxelShotDebugOnScreen = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Voxel|Shot|Debug", meta = (AllowPrivateAccess = "true", ClampMin = "0.1"))
	float VoxelShotDebugDrawLifetimeSeconds = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "0.05"))
	float MaxVoxelEditorViewTransformAge = 0.75f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "1.0"))
	float MaxVoxelEditorViewUpdateDistance = 10000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "1.0"))
	float MaxVoxelEditorViewDistanceFromOriginalPawn = 100000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Voxel|Validation", meta = (ClampMin = "0.0"))
	float VoxelEditorViewTransformLogInterval = 2.0f;

	double LastVoxelBrushRequestServerTime = -1.0;
	double LastVoxelShotRequestServerTime = -1.0;
	int64 NextVoxelShotDebugId = 1;
	double LastApprovedVoxelEditorViewServerTime = -1.0;
	double LastAcceptedVoxelEditorViewLogServerTime = -1.0;
	double LastRejectedVoxelEditorViewLogServerTime = -1.0;
	bool bAutoRunEnabled = false;
	bool bHasApprovedVoxelEditorViewTransform = false;

	FVector ApprovedVoxelEditorViewLocation = FVector::ZeroVector;
	FRotator ApprovedVoxelEditorViewRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	TObjectPtr<ARTPSCharacterPlayer> VoxelEditorOriginalPawn;

	UPROPERTY(Transient)
	TObjectPtr<AVoxelEditorPawn> ActiveVoxelEditorPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CachedVoxelEditorCharacterIMC;
};
