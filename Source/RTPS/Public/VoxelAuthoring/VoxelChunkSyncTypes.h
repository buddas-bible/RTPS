#pragma once

#include "CoreMinimal.h"
#include "VoxelAuthoring/VoxelEditOp.h"
#include "VoxelChunkSyncTypes.generated.h"

UENUM(BlueprintType)
enum class ERTPSVoxelChunkSyncMode : uint8
{
	Skip UMETA(DisplayName = "Skip"),
	FullSnapshot UMETA(DisplayName = "FullSnapshot"),
	Delta UMETA(DisplayName = "Delta")
};

USTRUCT(BlueprintType)
struct RTPS_API FRTPSVoxelChunkStatePayload
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	FIntVector ChunkCoord = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int32 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int32 SnapshotRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int64 SnapshotServerSequence = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	TArray<float> LatticeDensity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	bool bHasDensity = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	FString FailureReason;
};

USTRUCT(BlueprintType)
struct RTPS_API FRTPSVoxelChunkDeltaPayload
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	FIntVector ChunkCoord = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	bool bRequiresFullSnapshot = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	FString FailureReason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int32 FromRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int32 ToRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int32 SnapshotRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	int64 SnapshotServerSequence = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkSync")
	TArray<FRTPSVoxelEditOp> EditOps;
};
