#pragma once

#include "CoreMinimal.h"
#include "VoxelNoiseParams.generated.h"

USTRUCT(BlueprintType)
struct RTPS_API FVoxelNoiseParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0001"))
	float NoiseScale = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float NoiseWeight = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "8"))
	int32 NoiseOctaves = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	float NoiseLacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NoisePersistence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float NoiseFloorOffset = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NoiseSeed = 0;

	// World height in cells above which terrain fades to air.
	// Lattice vertices above this Z are fully air.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
	float TerrainHeightCells = 32.f;
};
