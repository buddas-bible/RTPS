#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelAuthoring/VoxelNoiseParams.h"
#include "VoxelAuthoring/VoxelDensitySnapshot.h"
#include "VoxelDensityGrid.generated.h"

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelDensityGrid : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel")
	FIntVector GridDimensions = FIntVector(16, 16, 16);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel", meta = (ClampMin = "1.0"))
	float CellSize = 100.f;

	void EnsureAllocated();
	void Clear();
	void FillNoise(const FVoxelNoiseParams& Params);
	void FillSphere(FVector LocalCenter, float Radius, float Value);

	TArrayView<const float> GetGrid() const { return DensityGrid; }
	TArrayView<float> GetGridMutable() { return DensityGrid; }

	int32 LinearIndex(int32 X, int32 Y, int32 Z) const;

	FVoxelDensitySnapshot TakeSnapshot(float IsoLevel) const;
	void ApplySnapshot(const FVoxelDensitySnapshot& Snapshot);

private:
	UPROPERTY(Transient)
	TArray<float> DensityGrid;
};
