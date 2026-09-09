#pragma once

#include "CoreMinimal.h"
#include "VoxelDensitySnapshot.generated.h"

// Lightweight container for transferring density grid state between components.
USTRUCT(BlueprintType)
struct RTPS_API FVoxelDensitySnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<float> Grid;

	UPROPERTY()
	FIntVector Dimensions = FIntVector(0, 0, 0);

	UPROPERTY()
	float CellSize = 100.f;

	UPROPERTY()
	float IsoLevel = 0.5f;

	bool IsValid() const
	{
		return Dimensions.X > 0 && Dimensions.Y > 0 && Dimensions.Z > 0
			&& Grid.Num() == Dimensions.X * Dimensions.Y * Dimensions.Z;
	}
};
