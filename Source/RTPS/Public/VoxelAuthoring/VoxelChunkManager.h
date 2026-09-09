#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelAuthoring/VoxelChunk.h"
#include "VoxelAuthoring/VoxelChunkState.h"
#include "VoxelAuthoring/VoxelChunkSyncTypes.h"
#include "VoxelAuthoring/VoxelEditOp.h"
#include "VoxelNoiseParams.h"
#include "VoxelChunkManager.generated.h"

class AVoxelChunk;
class ARTPSPlayerController;
class UMaterialInterface;
class USceneComponent;

struct FRTPSVoxelChunkBoundaryDebugInfo
{
	FIntVector HitChunkCoord = FIntVector::ZeroValue;
	FVector LocalPosition = FVector::ZeroVector;
	FVector ChunkWorldOrigin = FVector::ZeroVector;
	FVector ChunkWorldSize = FVector::ZeroVector;
	FVector DistanceToMinBoundary = FVector::ZeroVector;
	FVector DistanceToMaxBoundary = FVector::ZeroVector;
	TArray<FString> BoundaryAxes;
	TArray<FIntVector> ExpectedNeighborChunkCoords;

	bool IsNearBoundary() const
	{
		return !BoundaryAxes.IsEmpty();
	}
};

UENUM(BlueprintType)
enum class EVoxelStreamingMode : uint8
{
	Free UMETA(DisplayName = "Free (no auto streaming)"),
	Streaming UMETA(DisplayName = "Streaming (auto around camera)"),
};

UCLASS(BlueprintType, Blueprintable)
class RTPS_API AVoxelChunkManager : public AActor
{
	GENERATED_BODY()

public:
	AVoxelChunkManager();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- Mode ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager")
	EVoxelStreamingMode StreamingMode = EVoxelStreamingMode::Free;

	// ---- Chunk Config ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Chunk")
	FIntVector ChunkDimensions = FIntVector(16, 16, 16);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Chunk", meta = (ClampMin = "1.0"))
	float CellSize = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Chunk", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float IsoLevel = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Chunk")
	TObjectPtr<UMaterialInterface> ChunkMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Chunk")
	FString ChunkSaveDir = TEXT("Saved/VoxelChunks");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Chunk")
	EVoxelChunkSource GenerationSource = EVoxelChunkSource::ChunkDataOrNoise;

	// ---- Noise ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Noise")
	FVoxelNoiseParams NoiseParams;

	// ---- Streaming ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|FixedArena")
	bool bUseFixedArenaBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|FixedArena")
	FIntVector FixedArenaMinChunkCoord = FIntVector(-1, -1, 0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|FixedArena")
	FIntVector FixedArenaMaxChunkCoord = FIntVector(1, 1, 0);

	// Forward-facing load distance in chunks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Streaming", meta = (ClampMin = "1"))
	int32 ViewDistanceFront = 5;

	// Back-facing load distance in chunks. Recommended to keep smaller than front.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Streaming", meta = (ClampMin = "1"))
	int32 ViewDistanceBack = 2;

	// Forward-facing unload distance in chunks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Streaming", meta = (ClampMin = "1"))
	int32 UnloadDistanceFront = 6;

	// Back-facing unload distance in chunks to avoid unnecessary regeneration after turning around.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Streaming", meta = (ClampMin = "1"))
	int32 UnloadDistanceBack = 9;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Streaming", meta = (ClampMin = "0.1"))
	float StreamingTickInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug")
	bool bDebugShowChunkWireframes = false;

	// All prototype diagnostics are opt-in; the default runtime path stays quiet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug")
	bool bDebugVoxelBrushApplication = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug")
	bool bDebugVoxelChunkBoundaries = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug")
	bool bDrawVoxelChunkBoundaryDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug")
	bool bDebugVoxelMeshRebuilds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug", meta = (ClampMin = "0.0"))
	float BoundaryDebugThresholdWorldUnits = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Debug", meta = (ClampMin = "0.1"))
	float VoxelChunkBoundaryDebugDrawLifetimeSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety")
	bool bEnableVoxelEditPlayerSafetyPass = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety")
	bool bDebugVoxelEditPlayerSafetyPass = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety")
	bool bDrawVoxelEditSafetyDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety", meta = (ClampMin = "0.0"))
	float VoxelEditSafetyBoundsPadding = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety", meta = (ClampMin = "0.0"))
	float VoxelEditSafetyVerticalSearchDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety", meta = (ClampMin = "0.0"))
	float VoxelEditSafetyLiftOffset = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety", meta = (ClampMin = "1"))
	int32 VoxelEditSafetyRetryCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety", meta = (ClampMin = "0.0"))
	float VoxelEditSafetyRetryInterval = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|Safety", meta = (ClampMin = "0.0"))
	float VoxelEditSafetyMaxCorrectionDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|State", meta = (ClampMin = "1"))
	int32 MaxRecentOpsPerChunk = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|State", meta = (ClampMin = "0.25"))
	float ChunkSubscribeRetryInterval = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|State", meta = (ClampMin = "1"))
	int32 MaxChunkStatePayloadsPerClientPerTick = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkManager|State", meta = (ClampMin = "0.0"))
	float MinChunkStateFlushIntervalSecondsPerClient = 0.05f;

	// ---- Editor Buttons ----
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ChunkManager")
	void LoadChunksAroundCamera();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ChunkManager|FixedArena")
	void LoadFixedArenaBounds();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ChunkManager")
	void UnloadAllChunks();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ChunkManager")
	void SaveAllModifiedChunks();

	FRTPSVoxelChunkBoundaryDebugInfo BuildChunkBoundaryDebugInfo(const AVoxelChunk& HitChunk, const FVector& HitWorldPosition) const;
	void DrawChunkBoundaryDebug(const FRTPSVoxelChunkBoundaryDebugInfo& BoundaryInfo, float LifetimeSeconds = -1.f) const;
	void GetBrushDebugChunkCoverage(
		const FVoxelBrush& Brush,
		TArray<FIntVector>& OutDensityAffectedChunkCoords,
		TArray<FIntVector>& OutMeshDirtyChunkCoords) const;

	void ApplyBrushAuthoritative(const FVoxelBrush& Brush);
	int32 ApplyEditOpLocal(const FRTPSVoxelEditOp& EditOp);
	bool BuildChunkStatePayload(FIntVector ChunkCoord, FRTPSVoxelChunkStatePayload& OutPayload) const;
	void SubscribeToAuthoritativeChunkStateIfClient(const FIntVector& ChunkCoord, int32 LocalKnownRevision, const TCHAR* Reason);
	bool ApplyChunkStatePayloadLocal(const FRTPSVoxelChunkStatePayload& Payload, const TCHAR* Reason);
	bool ApplyChunkDeltaPayloadLocal(const FRTPSVoxelChunkDeltaPayload& Payload, const TCHAR* Reason);
	void MarkChunkForFullSnapshotResync(FIntVector ChunkCoord, const TCHAR* Reason);
	int32 SelectClientSubscribeKnownRevision(const FIntVector& ChunkCoord, int32 LocalKnownRevision) const;
	bool ShouldBypassClientSubscribeRevisionThrottle(const FIntVector& ChunkCoord) const;
	void SubscribePlayerToChunk(ARTPSPlayerController* PlayerController, const FIntVector& ChunkCoord, int32 ClientKnownRevision, const TCHAR* Reason);
	void UnsubscribePlayerFromChunk(ARTPSPlayerController* PlayerController, const FIntVector& ChunkCoord, const TCHAR* Reason);
	void UnsubscribePlayerFromAllChunks(ARTPSPlayerController* PlayerController, const TCHAR* Reason);

	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "ChunkManager|Brush")
	void Server_ApplyBrush(FVoxelBrush Brush);

	UFUNCTION(NetMulticast, Reliable, Category = "ChunkManager|Brush")
	void Multicast_ApplyBrush(FVoxelBrush Brush);

	UFUNCTION(NetMulticast, Reliable, Category = "ChunkManager|Brush")
	void Multicast_ApplyVoxelEditOp(FRTPSVoxelEditOp EditOp);

	// ---- State (read-only) ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ChunkManager")
	int32 LoadedChunkCount = 0;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FRTPSVoxelEditOpLocalApplyTest;
	friend class FRTPSVoxelChunkStateMirrorTracksLoadedEditTest;
	friend class FRTPSVoxelPendingOpsReplayWhenChunkBecomesReadyTest;
	friend class FRTPSVoxelRevisionAndRecentOpsTrackUniqueEditsTest;
	friend class FRTPSVoxelRecentOpsCapTrimsOldestTest;
	friend class FRTPSVoxelDuplicateEditDoesNotAdvanceRevisionTest;
	friend class FRTPSVoxelRecentOpEntryStoresRevisionBeforeAfterAndServerSequenceTest;
	friend class FRTPSVoxelCompactionUsesWrappedRecentOpsToFindLatestServerSequenceTest;
	friend class FRTPSVoxelBuildDeltaPayloadFromRecentOpsCoversFullRangeTest;
	friend class FRTPSVoxelBuildDeltaPayloadFailsWhenFromRevisionBeforeSnapshotTest;
	friend class FRTPSVoxelBuildDeltaPayloadFailsWhenFromRevisionEqualsToRevisionTest;
	friend class FRTPSVoxelBuildDeltaPayloadFailsOnRecentOpsGapTest;
	friend class FRTPSVoxelCanBuildCompleteDeltaRejectsFutureClientRevisionTest;
	friend class FRTPSVoxelChooseSyncModeSkipsWhenClientUpToDateTest;
	friend class FRTPSVoxelChooseSyncModeFullWhenClientOlderThanSnapshotTest;
	friend class FRTPSVoxelChooseSyncModeDeltaWhenClientWithinRecentWindowTest;
	friend class FRTPSVoxelChooseSyncModeFullWhenRecentOpsCoverageIncompleteTest;
	friend class FRTPSVoxelChooseSyncModeFullWhenClientReportsFutureRevisionTest;
	friend class FRTPSVoxelChooseSyncModeSkipsWhenNoAuthoritativeDensityTest;
	friend class FRTPSVoxelChooseSyncModeUsesDeltaBuildCoverageHelperTest;
	friend class FRTPSVoxelApplyDeltaRejectsWhenLocalRevisionDoesNotMatchFromRevisionTest;
	friend class FRTPSVoxelApplyDeltaRejectsWhenChunkNotReadyTest;
	friend class FRTPSVoxelApplyDeltaCommitsAtomicallyOnAllOpsSuccessTest;
	friend class FRTPSVoxelApplyDeltaRejectsAllDuplicateOpsTest;
	friend class FRTPSVoxelApplyDeltaFailureDoesNotRecordPartialSequencesTest;
	friend class FRTPSVoxelApplyDeltaSuccessMirrorsChunkDensityToLocalStateTest;
	friend class FRTPSVoxelMarkFullSnapshotResyncInvalidatesClientSubscribeStateTest;
	friend class FRTPSVoxelFullSnapshotApplyClearsFullResyncMarkerTest;
	friend class FRTPSVoxelDestroyChunkClearsFullResyncMarkerTest;
	friend class FRTPSVoxelDeltaFailureMarksFullSnapshotResyncTest;
	friend class FRTPSVoxelQueueDeltaPayloadForClientQueuesValidDeltaTest;
	friend class FRTPSVoxelQueueDeltaDropsWhenFullSnapshotAlreadyQueuedTest;
	friend class FRTPSVoxelQueueFullSnapshotSupersedesQueuedDeltaTest;
	friend class FRTPSVoxelFlushQueuedPayloadsHonorsCombinedThrottleTest;
	friend class FRTPSVoxelSubscribeQueuesDeltaWhenSyncModeDeltaTest;
	friend class FRTPSVoxelSubscribeQueuesFullWhenClientOlderThanSnapshotTest;
	friend class FRTPSVoxelSubscribeSkipsWhenClientUpToDateTest;
	friend class FRTPSVoxelFullResyncMarkerForcesUnknownRevisionOnSubscribeTest;
	friend class FRTPSVoxelSubscribeFallsBackFullWhenDeltaBuildFailsTest;
	friend class FRTPSVoxelUnsubscribePlayerFromChunkRemovesQueuedDeltaForChunkTest;
	friend class FRTPSVoxelUnsubscribePlayerFromAllChunksRemovesQueuedDeltaPayloadsTest;
	friend class FRTPSVoxelUnsubscribePlayerFromChunkRemovesFullAndDeltaQueuesForSameChunkTest;
	friend class FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsNoneWhenNoBaselineTest;
	friend class FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsLocalWhenBaselineKnownTest;
	friend class FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsNoneWhenMarkerSetTest;
	friend class FRTPSVoxelSubscribeKnownRevisionBypassesKnownRevisionThrottleWhenNoBaselineTest;
	friend class FRTPSVoxelFlushSkipsClientWhenWithinMinIntervalEvenIfQueuedTest;
	friend class FRTPSVoxelFlushAllowsClientAfterMinIntervalElapsedTest;
	friend class FRTPSVoxelFlushDoesNotPenalizeOtherClientsWhenOneClientThrottledTest;
	friend class FRTPSVoxelMinChunkStateFlushIntervalCleanedUpOnStaleClientTest;
	friend class FRTPSVoxelBuildChunkStatePayloadFromMirrorTest;
	friend class FRTPSVoxelApplyChunkStatePayloadRebuildsLocalChunkTest;
	friend class FRTPSVoxelIncomingPayloadQueuesUntilChunkReadyTest;
	friend class FRTPSVoxelSubscribeSkipsPayloadWhenRevisionUpToDateTest;
	friend class FRTPSVoxelSubscribeQueuesPayloadWhenClientBehindTest;
	friend class FRTPSVoxelPayloadQueueIsThrottledTest;
	friend class FRTPSVoxelLiveEditTargetsOnlyChunkSubscribersTest;
	friend class FRTPSVoxelLiveEditTargetsDensityAffectedSubscribersOnlyTest;
	friend class FRTPSVoxelMeshDirtyOnlySubscriberDoesNotReceiveLiveEditOpTest;
	friend class FRTPSVoxelLiveEditTargetSelectionDoesNotUseMeshDirtyHaloTest;
	friend class FRTPSVoxelClientLiveEditAppliedChunksZeroDoesNotAdvanceRemoteRevisionTest;
	friend class FRTPSVoxelBoundaryEditMarksNeighborChunkDirtyTest;
	friend class FRTPSVoxelLargeBrushQueuesUnloadedAffectedNeighborTest;
	friend class FRTPSVoxelLargeBrushPendingNeighborReplaysWhenReadyTest;
	friend class FRTPSVoxelMeshDirtyNeighborDoesNotReplaceDensityAffectedPendingTest;
	friend class FRTPSVoxelDensityAffectedAccountingIsCompleteTest;
	friend class FRTPSVoxelEditPlayerSafetyBoundsTest;
	friend class FRTPSVoxelSubscribeMaterializesPendingOnlyChunkTest;
	friend class FRTPSVoxelPendingOnlySubscribeQueuesPayloadAfterMaterializeTest;
	friend class FRTPSVoxelMaterializedPendingChunkDoesNotReplayTwiceTest;
	friend class FRTPSVoxelSubscribeSkipsUnchangedBaseChunkWithoutMaterializeTest;
	friend class FRTPSVoxelMaterializationFailurePreservesPendingOpsTest;
	friend class FRTPSVoxelSnapshotCompactionUpdatesSnapshotRevisionTest;
	friend class FRTPSVoxelSnapshotCompactionDoesNotAdvanceRevisionTest;
	friend class FRTPSVoxelSnapshotCompactionTrimsRecentOpsTest;
	friend class FRTPSVoxelRecentOpsTriggerSnapshotCompactionTest;
	friend class FRTPSVoxelPendingMaterializationCanCompactSafelyTest;
	friend class FRTPSVoxelClientChunkDestroyClearsAppliedSequencesAndPendingPayloadsTest;
#endif

	struct FRTPSVoxelDensityApplyResult
	{
		int32 AppliedChunks = 0;
		int32 QueuedChunks = 0;
		int32 DuplicateChunks = 0;
		int32 DroppedChunks = 0;
		TSet<FIntVector> AppliedChunkCoords;
	};

	struct FVoxelEditSafetyRequest
	{
		int64 ServerSequence = INDEX_NONE;
		FBox AffectedBounds = FBox(ForceInit);
		FBox BrushBounds = FBox(ForceInit);
		TArray<FIntVector> MeshDirtyChunkCoords;
		int32 RetryIndex = 0;
	};

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "ChunkManager", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TMap<FIntVector, TObjectPtr<AVoxelChunk>> LoadedChunks;

	UPROPERTY(Transient)
	TMap<FIntVector, FRTPSVoxelChunkState> ChunkStates;

	TMap<FIntVector, TArray<FRTPSVoxelEditOp>> PendingOpsByChunk;
	TMap<FIntVector, TSet<int64>> AppliedEditSequencesByChunk;
	TMap<FIntVector, FRTPSVoxelChunkStatePayload> PendingIncomingChunkPayloads;
	TMap<FIntVector, int32> LastSubscribedKnownRevisionByCoord;
	TMap<FIntVector, int32> LastAppliedRemoteRevisionByCoord;
	TMap<FIntVector, double> LastChunkSubscribeRequestTimeByCoord;
	TMap<FIntVector, TSet<TWeakObjectPtr<ARTPSPlayerController>>> ChunkSubscribers;
	TMap<TWeakObjectPtr<ARTPSPlayerController>, TSet<FIntVector>> ClientSubscribedChunks;
	TMap<TWeakObjectPtr<ARTPSPlayerController>, TArray<FRTPSVoxelChunkStatePayload>> PendingChunkStatePayloadsByClient;
	TMap<TWeakObjectPtr<ARTPSPlayerController>, TArray<FRTPSVoxelChunkDeltaPayload>> PendingChunkDeltaPayloadsByClient;
	TMap<TWeakObjectPtr<ARTPSPlayerController>, double> LastChunkPayloadFlushTimeByClient;
	TSet<FIntVector> ChunksNeedingFullSnapshotResync;

	float StreamingAccumulator = 0.f;
	int64 NextVoxelEditSequence = 1;

	FVector GetCameraWorldLocation() const;
	FVector GetCameraForwardVector() const;
	FIntVector WorldToChunkCoord(FVector WorldPos) const;
	FVector ChunkCoordToWorldCenter(FIntVector ChunkCoord) const;
	float ChunkDistanceFromCamera(FIntVector ChunkCoord, FVector CameraPos) const;
	bool ShouldUseCameraStreaming() const;
	static TArray<FIntVector> BuildExpectedBoundaryNeighborChunkCoords(
		const FIntVector& HitChunkCoord,
		bool bNearMinX,
		bool bNearMaxX,
		bool bNearMinY,
		bool bNearMaxY,
		bool bNearMinZ,
		bool bNearMaxZ);

	void UpdateStreaming(FVector CameraPos, FVector CameraForward);
	void SpawnChunk(FIntVector ChunkCoord);
	void DestroyChunk(FIntVector ChunkCoord);
	void ApplyEditOpAuthoritative(const FRTPSVoxelEditOp& EditOp);
	TArray<FIntVector> GetDensityAffectedChunkCoordsForEditOp(const FRTPSVoxelEditOp& EditOp) const;
	TArray<FIntVector> GetAffectedChunkCoords(const FRTPSVoxelEditOp& EditOp) const;
	TArray<FIntVector> GetMeshDirtyChunkCoordsForEditOp(const FRTPSVoxelEditOp& EditOp) const;
	TArray<ARTPSPlayerController*> GetSubscribersForAffectedChunks(const TArray<FIntVector>& AffectedChunkCoords);
	int32 RebuildMeshDirtyChunksForEditOp(const FRTPSVoxelEditOp& EditOp, const TSet<FIntVector>& DensityAppliedChunkCoords, const TCHAR* Reason);
	FRTPSVoxelDensityApplyResult ApplyEditOpToDensityAffectedChunksAuthoritative(const FRTPSVoxelEditOp& EditOp, const TArray<FIntVector>& DensityAffectedChunkCoords);
	bool DoesEditOpOverlapChunk(const FRTPSVoxelEditOp& EditOp, const FIntVector& ChunkCoord) const;
	bool IsChunkReadyForEdit(const FIntVector& ChunkCoord, AVoxelChunk*& OutChunk) const;
	bool HasAppliedSequenceToChunk(const FIntVector& ChunkCoord, int64 ServerSequence) const;
	void MarkSequenceAppliedToChunk(const FIntVector& ChunkCoord, int64 ServerSequence, const TCHAR* Reason);
	bool IsPendingSequenceForChunk(const FIntVector& ChunkCoord, int64 ServerSequence) const;
	void QueuePendingOpForChunk(const FIntVector& ChunkCoord, const FRTPSVoxelEditOp& EditOp, const TCHAR* Reason);
	int32 QueuePendingOpsForMissingOrNotReadyChunks(const FRTPSVoxelEditOp& EditOp, const TArray<FIntVector>& AffectedChunkCoords);
	bool ApplyEditOpToReadyChunk(const FIntVector& ChunkCoord, AVoxelChunk& Chunk, const FRTPSVoxelEditOp& EditOp, const TCHAR* Reason);
	bool MaterializePendingChunkStateForSubscribe(const FIntVector& ChunkCoord, const TCHAR* Reason);
	void ReplayPendingOpsForChunk(const FIntVector& ChunkCoord, AVoxelChunk& Chunk, const TCHAR* Reason);
	FRTPSVoxelChunkState& FindOrCreateChunkState(const FIntVector& ChunkCoord);
	const FRTPSVoxelChunkState* FindChunkState(const FIntVector& ChunkCoord) const;
	bool CanBuildCompleteDeltaFromRecentOps(const FRTPSVoxelChunkState& ChunkState, int32 FromRevision, int32 ToRevision, FString& OutFailureReason) const;
	ERTPSVoxelChunkSyncMode ChooseChunkSyncMode(
		const FRTPSVoxelChunkState& ChunkState,
		int32 ClientKnownRevision,
		FString& OutReason) const;
	bool BuildChunkDeltaPayload(FIntVector ChunkCoord, int32 ClientKnownRevision, FRTPSVoxelChunkDeltaPayload& OutPayload) const;
	void MirrorChunkDensityToState(const FIntVector& ChunkCoord, const AVoxelChunk& Chunk, const TCHAR* Reason);
	void MarkChunkStateDirty(const FIntVector& ChunkCoord, const TCHAR* Reason);
	void RecordAppliedEditOpForChunk(FRTPSVoxelChunkState& ChunkState, const FRTPSVoxelEditOp& EditOp, const TCHAR* Reason);
	bool ShouldCompactChunkStateSnapshot(const FRTPSVoxelChunkState& ChunkState) const;
	bool CompactChunkStateSnapshot(FRTPSVoxelChunkState& ChunkState, const TCHAR* Reason);
	void TrimRecentOps(FRTPSVoxelChunkState& ChunkState, const TCHAR* Reason);
	void QueueChunkStatePayloadForClient(ARTPSPlayerController* PlayerController, const FRTPSVoxelChunkStatePayload& Payload, const TCHAR* Reason);
	void QueueChunkDeltaPayloadForClient(ARTPSPlayerController* PlayerController, const FRTPSVoxelChunkDeltaPayload& Payload, const TCHAR* Reason);
	int32 FlushQueuedChunkStatePayloads(float DeltaSeconds);
	void SendEditOpToChunkSubscribers(const FRTPSVoxelEditOp& EditOp, const TArray<FIntVector>& DensityAffectedChunkCoords, int32 MeshDirtyChunkCount, const TCHAR* Reason);
	void PruneStaleVoxelChunkSubscriptions(const TCHAR* Reason);
	bool ApplyPendingIncomingChunkPayloadIfReady(const FIntVector& ChunkCoord, AVoxelChunk& Chunk, const TCHAR* Reason);
	void RefreshReadyChunkStateMirrors(const TCHAR* Reason);
	FBox BuildWorldBoundsForChunkCoords(const TArray<FIntVector>& ChunkCoords) const;
	static FBox BuildVoxelEditSafetyExpandedBounds(const FBox& AffectedBounds, float CapsuleRadius, float CapsuleHalfHeight, float Padding);
	static bool IsCapsuleNearVoxelEditSafetyBounds(const FVector& CapsuleCenter, float CapsuleRadius, float CapsuleHalfHeight, const FBox& AffectedBounds, float Padding);
	static bool IsVoxelEditSafetyCorrectionAllowed(float UpwardCorrectionDistance, float MaxCorrectionDistance);
	void ScheduleVoxelEditPlayerSafetyPass(const FRTPSVoxelEditOp& EditOp, const TArray<FIntVector>& MeshDirtyChunkCoords);
	void ScheduleVoxelEditPlayerSafetyRetry(const FVoxelEditSafetyRequest& Request);
	void RunVoxelEditPlayerSafetyPass(FVoxelEditSafetyRequest Request);
	void RunVoxelEditPlayerSafetyPassForCharacter(class ACharacter& Character, const FVoxelEditSafetyRequest& Request, int32& InOutCorrectedCount, int32& InOutSkippedCount);
};
