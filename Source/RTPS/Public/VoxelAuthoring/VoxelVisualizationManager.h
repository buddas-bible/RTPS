#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelVisualizationManager.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class ULineBatchComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UVoxelDebugVisualizer;

UENUM(BlueprintType)
enum class EVoxelVisMode : uint8
{
	Mode1_WireframeVoxels UMETA(DisplayName = "1: Wireframe Voxels"),
	Mode2_DensitySpheres  UMETA(DisplayName = "2: Density Spheres"),
	Mode3_MarchingCubes   UMETA(DisplayName = "3: Marching Cubes"),
	Mode4_Interactive     UMETA(DisplayName = "4: Interactive (Spheres+MC)"),
};

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelVisualizationManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel|Vis")
	EVoxelVisMode CurrentVisMode = EVoxelVisMode::Mode1_WireframeVoxels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|Vis")
	FString LastRebuildStatus;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|Vis")
	int32 FilledCellCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Vis")
	TObjectPtr<UMaterialInterface> MarchingCubeMaterial;

	void InitializeRenderTargets(
		UHierarchicalInstancedStaticMeshComponent* InBlockHISM,
		UProceduralMeshComponent* InMarchingMesh,
		ULineBatchComponent* InWireframeLines,
		UVoxelDebugVisualizer* InDebugVisualizer);

	void HideAll();
	void ApplyMode(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);
	void AdjustSurfaceLevel(float Delta, TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float& InOutIsoLevel);
	void SetMode(EVoxelVisMode NewMode, TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);

private:
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BlockHISM;
	TObjectPtr<UProceduralMeshComponent> MarchingMesh;
	TObjectPtr<ULineBatchComponent> WireframeLines;
	TObjectPtr<UVoxelDebugVisualizer> DebugVisualizer;

	void ApplyMode1_Wireframe(FIntVector GridDimensions, float CellSize);
	void ApplyMode2_DensitySpheres(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize);
	void ApplyMode3_MarchingCubes(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);
	void ApplyMode4_Interactive(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);
};
