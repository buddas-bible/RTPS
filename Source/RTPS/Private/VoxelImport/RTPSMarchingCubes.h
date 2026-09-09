#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

namespace RTPSVoxelImport
{
	struct FMarchingCubesMeshData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;

		int32 GetTriangleCount() const
		{
			return Triangles.Num() / 3;
		}
	};

	struct FMarchingCubesMeshFilterStats
	{
		int32 OriginalTriangleCount = 0;
		int32 RemovedDegenerateTriangleCount = 0;
		int32 FinalTriangleCount = 0;
	};

	bool IsValidMarchingCubeTriangle(
		const FMarchingCubesMeshData& MeshData,
		int32 TriangleBaseIndex,
		float VertexEqualityToleranceCm = 0.01f,
		float MinTriangleAreaSquared = 1.0e-4f);

	FMarchingCubesMeshFilterStats FilterDegenerateTriangles(
		FMarchingCubesMeshData& MeshData,
		float VertexEqualityToleranceCm = 0.01f,
		float MinTriangleAreaSquared = 1.0e-4f);

	bool BuildMarchingCubesChunkMesh(
		const FIntVector& ChunkDimensions,
		const FIntVector& ChunkOriginGrid,
		float VoxelSizeCm,
		float IsoLevel,
		const TSet<FIntVector>& OccupiedWorldCells,
		FMarchingCubesMeshData& OutMeshData);

	// float density 배열 직접 입력 버전 (RuntimeAuthoringVolume 용)
	// DensityData 크기: (ChunkDimensions + FIntVector(1,1,1)) 의 X*Y*Z
	// 인덱싱: X + (Y * SampleDim.X) + (Z * SampleDim.X * SampleDim.Y)
	bool BuildMarchingCubesChunkMeshDensity(
		const FIntVector& ChunkDimensions,
		const FIntVector& ChunkOriginGrid,
		float VoxelSizeCm,
		float IsoLevel,
		TArrayView<const float> DensityData,
		FMarchingCubesMeshData& OutMeshData);
}
