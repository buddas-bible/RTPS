#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelAuthoring/VoxelNoiseParams.h"
#include "VoxelAuthoring/VoxelVisualizationManager.h"
#include "RuntimeAuthoringVolume.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class ULineBatchComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UVoxelDebugVisualizer;
class UVoxelDensityGrid;
class UVoxelVisualizationManager;
class UVoxelAuthoringPersistence;

RTPS_API DECLARE_LOG_CATEGORY_EXTERN(LogRTPSVoxelAuthoring, Log, All);

UCLASS(BlueprintType, Blueprintable)
class RTPS_API ARuntimeAuthoringVolume : public AActor
{
	GENERATED_BODY()

public:
	ARuntimeAuthoringVolume();
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Authoring", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float IsoLevel = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Noise")
	FVoxelNoiseParams NoiseParams;

	UPROPERTY(EditAnywhere, Category = "Authoring|Debug")
	FVector DebugSphereCenter = FVector(800.f, 800.f, 800.f);

	UPROPERTY(EditAnywhere, Category = "Authoring|Debug", meta = (ClampMin = "1.0"))
	float DebugSphereRadius = 500.f;

	UPROPERTY(EditAnywhere, Category = "Authoring|Debug", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DebugSphereValue = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Authoring|IO")
	FString DensityFilePath = TEXT("Saved/AuthoringData/density.json");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
	TObjectPtr<UVoxelDensityGrid> DensityGridComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
	TObjectPtr<UVoxelVisualizationManager> VisManagerComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
	TObjectPtr<UVoxelAuthoringPersistence> PersistenceComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring|Debug")
	TObjectPtr<UVoxelDebugVisualizer> DebugVisualizer;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring")
	void ApplyCurrentVisMode();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring")
	void HideAllVisualizations();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|Debug")
	void ApplyDebugSphereFill();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|NoiseFill")
	void FillNoiseDensity();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|Debug")
	void ClearDensity();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|IO")
	void ExportDensityToFile();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|IO")
	void ImportDensityFromFile();

	UFUNCTION(BlueprintCallable, Category = "Authoring|Debug")
	void FillSphereDensity(FVector LocalCenter, float Radius, float Value);

	void RebuildAuthoringMesh();
	void ClearAuthoringMesh();
	void RebuildDebugVisualizer();
	void ApplyDebugVisualizerThreshold();
	void ClearDebugVisualizer();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Authoring")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Authoring")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BlockHISM;

	UPROPERTY(VisibleAnywhere, Category = "Authoring")
	TObjectPtr<UProceduralMeshComponent> MarchingMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Authoring")
	TObjectPtr<UMaterialInterface> MarchingCubeMaterial;

	UPROPERTY(VisibleAnywhere, Category = "Authoring")
	TObjectPtr<ULineBatchComponent> WireframeLines;

private:
	void SetMode1();
	void SetMode2();
	void SetMode3();
	void SetMode4();
	void IncreaseSurfaceLevel();
	void DecreaseSurfaceLevel();
};