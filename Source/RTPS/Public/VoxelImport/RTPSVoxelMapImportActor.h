// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTPSVoxelMapImportActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMesh;
class UMaterialInterface;

UENUM(BlueprintType)
enum class ERTPSVoxelSurfaceMode : uint8
{
	Block UMETA(DisplayName = "Block"),
	MarchingCubes UMETA(DisplayName = "Marching Cubes")
};

UCLASS(BlueprintType, Blueprintable)
class RTPS_API ARTPSVoxelMapImportActor : public AActor
{
	GENERATED_BODY()

public:
	ARTPSVoxelMapImportActor();

	virtual void BeginPlay() override;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Voxel Import")
	void RebuildFromJson();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Voxel Import")
	void ClearImportedContent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	FFilePath MapJsonFile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	ERTPSVoxelSurfaceMode SurfaceMode = ERTPSVoxelSurfaceMode::Block;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	bool bRebuildOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	bool bImportMarkers = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	bool bCreateMarkerLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import", meta = (ClampMin = "0.1"))
	float PointMarkerVisualScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import|Marching Cubes", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float MarchingIsoLevel = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Import|Marching Cubes")
	bool bGenerateSurfaceCollision = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	int32 ImportedVoxelCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	int32 ImportedMarkerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	int32 GeneratedSurfaceChunkCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	int32 GeneratedSurfaceTriangleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Import")
	FString LastImportStatus;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Voxel Import")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Voxel Import")
	TObjectPtr<USceneComponent> ImportedVoxelRoot;

	UPROPERTY(VisibleAnywhere, Category = "Voxel Import")
	TObjectPtr<USceneComponent> ImportedMarkerRoot;

private:
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMeshAsset;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMeshAsset;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BasicShapeMaterialAsset;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> GeneratedComponents;
};
