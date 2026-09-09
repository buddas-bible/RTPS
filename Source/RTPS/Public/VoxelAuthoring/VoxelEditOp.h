#pragma once

#include "CoreMinimal.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelEditOp.generated.h"

USTRUCT(BlueprintType)
struct RTPS_API FRTPSVoxelEditOp
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|EditOp")
	int64 ServerSequence = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|EditOp")
	FVoxelBrush Brush;
};
