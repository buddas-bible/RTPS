#pragma once

#include "CoreMinimal.h"
#include "VoxelAuthoring/VoxelEditOp.h"
#include "VoxelChunkState.generated.h"

USTRUCT(BlueprintType)
struct RTPS_API FRTPSVoxelRecentEditOp
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int32 RevisionBeforeApply = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int32 RevisionAfterApply = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int64 ServerSequence = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	FRTPSVoxelEditOp EditOp;

	FRTPSVoxelRecentEditOp() = default;

	FRTPSVoxelRecentEditOp(int32 InRevisionBeforeApply, int32 InRevisionAfterApply, const FRTPSVoxelEditOp& InEditOp)
		: RevisionBeforeApply(InRevisionBeforeApply)
		, RevisionAfterApply(InRevisionAfterApply)
		, ServerSequence(InEditOp.ServerSequence)
		, EditOp(InEditOp)
	{
	}
};

USTRUCT(BlueprintType)
struct RTPS_API FRTPSVoxelChunkState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	FIntVector ChunkCoord = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	bool bHasDensity = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	bool bDirty = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	TArray<float> LatticeDensity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int32 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int32 SnapshotRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int64 SnapshotServerSequence = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	int32 TotalCompactionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|ChunkState")
	TArray<FRTPSVoxelRecentEditOp> RecentOps;
};
