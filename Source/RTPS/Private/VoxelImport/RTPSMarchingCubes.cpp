#include "VoxelImport/RTPSMarchingCubes.h"

#include "Async/ParallelFor.h"
#include "VoxelImport/RTPSMarchingCubesTables.h"

namespace
{
	constexpr int32 EdgeCorners[12][2] =
	{
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	constexpr int32 CornerOffsets[8][3] =
	{
		{0, 0, 0},
		{1, 0, 0},
		{1, 1, 0},
		{0, 1, 0},
		{0, 0, 1},
		{1, 0, 1},
		{1, 1, 1},
		{0, 1, 1}
	};

#define GRADIENT
#define GRADIENT_A

// #define GRADIENT_B
#ifdef GRADIENT_B
	struct FSliceVertexData
	{
		FVector Position ;
		FVector GradientNormal = FVector::ZeroVector;
		FVector AccumulatedFaceNormal = FVector::ZeroVector;
		FVector2D UV = FVector2D::ZeroVector;
		FColor Color = FColor::White;
		FProcMeshTangent Tangent = FProcMeshTangent(FVector::ForwardVector, false);
	};

	struct FEdgeVertexKey
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		int32 Axis = 0; // 0 = X , 1 = Y, 2 = Z

		bool operator==(const FEdgeVertexKey& Other) const
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z && Axis == Other.Axis;
		}

		//FORINLINE uint32 GetTypeHash( const FEdgeVertexKey& Key )
		//{
		//	uint32 Hash = GetTypeHash( Key.X );
		//	Hash = HashCombine( Hash, GetTypeHash( Key.Y ) );
		//	Hash = HashCombine( Hash, GetTypeHash( Key.Z ) );
		//	Hash = HashCombine( Hash, GetTypeHash( Key.Axis ) );
		//	return Hash;
		//}
	};

	struct FSliceBuildData
	{
		TArray<FSliceVertexData> Vertices;
		TArray<int32> Triangles;
		TMap<FEdgeVertexKey, int32> EdgeVertexMap; 
	};

	FEdgeVertexKey MakeEdgeVertexKey( int32 X, int32 Y, int32 Z, int32 EdgeIndex )
	{
		switch( EdgeIndex )
		{
		case 0:  return { X,     Y,     Z,     0 };
		case 1:  return { X + 1, Y,     Z,     1 };
		case 2:  return { X,     Y + 1, Z,     0 };
		case 3:  return { X,     Y,     Z,     1 };
		case 4:  return { X,     Y,     Z + 1, 0 };
		case 5:  return { X + 1, Y,     Z + 1, 1 };
		case 6:  return { X,     Y + 1, Z + 1, 0 };
		case 7:  return { X,     Y,     Z + 1, 1 };
		case 8:  return { X,     Y,     Z,     2 };
		case 9:  return { X + 1, Y,     Z,     2 };
		case 10: return { X + 1, Y + 1, Z,     2 };
		case 11: return { X,     Y + 1, Z,     2 };
		default: return { X,	 Y,		Z,		0 };
		}
	}
#endif

	int32 SampleIndex(const FIntVector& SampleDimensions, int32 X, int32 Y, int32 Z)
	{
		return X + (Y * SampleDimensions.X) + (Z * SampleDimensions.X * SampleDimensions.Y);
	}

	float SampleDensityAtLattice(const TSet<FIntVector>& OccupiedWorldCells, const FIntVector& LatticeCoordinate)
	{
		int32 OccupiedCount = 0;
		for (int32 OffsetZ = -1; OffsetZ <= 0; ++OffsetZ)
		{
			for (int32 OffsetY = -1; OffsetY <= 0; ++OffsetY)
			{
				for (int32 OffsetX = -1; OffsetX <= 0; ++OffsetX)
				{
					const FIntVector AdjacentCell(
						LatticeCoordinate.X + OffsetX,
						LatticeCoordinate.Y + OffsetY,
						LatticeCoordinate.Z + OffsetZ);
					if (OccupiedWorldCells.Contains(AdjacentCell))
					{
						++OccupiedCount;
					}
				}
			}
		}

		const float OccupancyAverage = static_cast<float>(OccupiedCount) / 8.0f;
		return 1.0f - OccupancyAverage;
	}

#ifdef GRADIENT
	FVector SampleDensityGradient( TArrayView<const float> DensityData, const FIntVector & SampleDimensions, int32 X, int32 Y, int32 Z )
	{
		auto SafeSample = [&]( int32 SX, int32 SY, int32 SZ ) -> float
		{
			SX = FMath::Clamp( SX, 0, SampleDimensions.X - 1 );
			SY = FMath::Clamp( SY, 0, SampleDimensions.Y - 1 );
			SZ = FMath::Clamp( SZ, 0, SampleDimensions.Z - 1 );
			return DensityData[SampleIndex( SampleDimensions, SX, SY, SZ )];
		};
		
		const float Dx = SafeSample( X + 1, Y, Z ) - SafeSample( X - 1, Y, Z );
		const float Dy = SafeSample( X, Y + 1, Z ) - SafeSample( X, Y - 1, Z );
		const float Dz = SafeSample( X, Y, Z + 1 ) - SafeSample( X, Y, Z - 1 );
		
		FVector G( Dx, Dy, Dz );
		if( G.IsNearlyZero() )
		{
			G = FVector::UpVector;
		}

		return FVector( Dx, Dy, Dz ).GetSafeNormal();
		// return FVector( Dx, Dy, Dz );
	}
#endif

	FVector GridVertexToWorldCm(const FIntVector& GridVertex, float VoxelSizeCm)
	{
		return FVector(
			static_cast<float>(GridVertex.X) * VoxelSizeCm,
			static_cast<float>(GridVertex.Y) * VoxelSizeCm,
			static_cast<float>(GridVertex.Z) * VoxelSizeCm);
	}

	FVector InterpolateVertex(float IsoLevel, const FVector& A, const FVector& B, float DensityA, float DensityB)
	{
		if (FMath::IsNearlyEqual(IsoLevel, DensityA, KINDA_SMALL_NUMBER))
		{
			return A;
		}

		if (FMath::IsNearlyEqual(IsoLevel, DensityB, KINDA_SMALL_NUMBER))
		{
			return B;
		}

		if (FMath::IsNearlyEqual(DensityA, DensityB, KINDA_SMALL_NUMBER))
		{
			return (A + B) * 0.5f;
		}

		const float Mu = (IsoLevel - DensityA) / (DensityB - DensityA);
		return FMath::Lerp(A, B, Mu);
	}

#ifdef GRADIENT
	FVector InterpolateGradient( float IsoLevel, const FVector& GradA, const FVector& GradB, float DensityA, float DensityB )
	{
		if( FMath::IsNearlyEqual( DensityA, DensityB, KINDA_SMALL_NUMBER ) )
		{
			return ( GradA + GradB );
		}

		const float Mu = ( IsoLevel - DensityA ) / ( DensityB - DensityA );
		return FMath::Lerp( GradA, GradB, Mu ).GetSafeNormal();
		// return FMath::Lerp( GradA, GradB, Mu );
	}
#endif
}

bool RTPSVoxelImport::IsValidMarchingCubeTriangle(
	const FMarchingCubesMeshData& MeshData,
	int32 TriangleBaseIndex,
	float VertexEqualityToleranceCm,
	float MinTriangleAreaSquared)
{
	if (TriangleBaseIndex < 0 || TriangleBaseIndex + 2 >= MeshData.Triangles.Num())
	{
		return false;
	}

	const int32 I0 = MeshData.Triangles[TriangleBaseIndex];
	const int32 I1 = MeshData.Triangles[TriangleBaseIndex + 1];
	const int32 I2 = MeshData.Triangles[TriangleBaseIndex + 2];
	if (!MeshData.Vertices.IsValidIndex(I0) ||
		!MeshData.Vertices.IsValidIndex(I1) ||
		!MeshData.Vertices.IsValidIndex(I2))
	{
		return false;
	}

	const FVector& A = MeshData.Vertices[I0];
	const FVector& B = MeshData.Vertices[I1];
	const FVector& C = MeshData.Vertices[I2];
	auto IsFiniteVector = [](const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	};

	if (!IsFiniteVector(A) || !IsFiniteVector(B) || !IsFiniteVector(C))
	{
		return false;
	}

	const float VertexToleranceSquared = FMath::Square(FMath::Max(VertexEqualityToleranceCm, 0.0f));
	if (FVector::DistSquared(A, B) <= VertexToleranceSquared ||
		FVector::DistSquared(B, C) <= VertexToleranceSquared ||
		FVector::DistSquared(C, A) <= VertexToleranceSquared)
	{
		return false;
	}

	const FVector Cross = FVector::CrossProduct(B - A, C - A);
	if (!IsFiniteVector(Cross) || Cross.SizeSquared() <= FMath::Max(MinTriangleAreaSquared, 0.0f))
	{
		return false;
	}

	const FVector FaceNormal = Cross.GetSafeNormal();
	if (!IsFiniteVector(FaceNormal) || FaceNormal.IsNearlyZero())
	{
		return false;
	}

	if (!MeshData.Normals.IsEmpty())
	{
		if (!MeshData.Normals.IsValidIndex(I0) ||
			!MeshData.Normals.IsValidIndex(I1) ||
			!MeshData.Normals.IsValidIndex(I2))
		{
			return false;
		}

		if (!IsFiniteVector(MeshData.Normals[I0]) ||
			!IsFiniteVector(MeshData.Normals[I1]) ||
			!IsFiniteVector(MeshData.Normals[I2]))
		{
			return false;
		}
	}

	return true;
}

RTPSVoxelImport::FMarchingCubesMeshFilterStats RTPSVoxelImport::FilterDegenerateTriangles(
	FMarchingCubesMeshData& MeshData,
	float VertexEqualityToleranceCm,
	float MinTriangleAreaSquared)
{
	FMarchingCubesMeshFilterStats Stats;
	Stats.OriginalTriangleCount = MeshData.Triangles.Num() / 3;

	FMarchingCubesMeshData FilteredMeshData;
	FilteredMeshData.Vertices.Reserve(MeshData.Vertices.Num());
	FilteredMeshData.Triangles.Reserve(MeshData.Triangles.Num());
	FilteredMeshData.Normals.Reserve(MeshData.Normals.Num());
	FilteredMeshData.UV0.Reserve(MeshData.UV0.Num());
	FilteredMeshData.VertexColors.Reserve(MeshData.VertexColors.Num());
	FilteredMeshData.Tangents.Reserve(MeshData.Tangents.Num());

	for (int32 TriangleBaseIndex = 0; TriangleBaseIndex + 2 < MeshData.Triangles.Num(); TriangleBaseIndex += 3)
	{
		if (!IsValidMarchingCubeTriangle(MeshData, TriangleBaseIndex, VertexEqualityToleranceCm, MinTriangleAreaSquared))
		{
			++Stats.RemovedDegenerateTriangleCount;
			continue;
		}

		const int32 SourceIndices[3] =
		{
			MeshData.Triangles[TriangleBaseIndex],
			MeshData.Triangles[TriangleBaseIndex + 1],
			MeshData.Triangles[TriangleBaseIndex + 2]
		};

		const int32 NewBaseIndex = FilteredMeshData.Vertices.Num();
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			const int32 SourceIndex = SourceIndices[CornerIndex];
			const FVector& SourceVertex = MeshData.Vertices[SourceIndex];
			FilteredMeshData.Vertices.Add(SourceVertex);
			FilteredMeshData.Triangles.Add(NewBaseIndex + CornerIndex);

			if (MeshData.Normals.IsValidIndex(SourceIndex))
			{
				FilteredMeshData.Normals.Add(MeshData.Normals[SourceIndex]);
			}

			if (MeshData.UV0.IsValidIndex(SourceIndex))
			{
				FilteredMeshData.UV0.Add(MeshData.UV0[SourceIndex]);
			}

			if (MeshData.VertexColors.IsValidIndex(SourceIndex))
			{
				FilteredMeshData.VertexColors.Add(MeshData.VertexColors[SourceIndex]);
			}

			if (MeshData.Tangents.IsValidIndex(SourceIndex))
			{
				FilteredMeshData.Tangents.Add(MeshData.Tangents[SourceIndex]);
			}
		}
	}

	Stats.FinalTriangleCount = FilteredMeshData.Triangles.Num() / 3;
	MeshData = MoveTemp(FilteredMeshData);
	return Stats;
}

bool RTPSVoxelImport::BuildMarchingCubesChunkMesh(
	const FIntVector& ChunkDimensions,
	const FIntVector& ChunkOriginGrid,
	float VoxelSizeCm,
	float IsoLevel,
	const TSet<FIntVector>& OccupiedWorldCells,
	FMarchingCubesMeshData& OutMeshData)
{
	OutMeshData = FMarchingCubesMeshData();
	if (ChunkDimensions.X <= 0 || ChunkDimensions.Y <= 0 || ChunkDimensions.Z <= 0)
	{
		return false;
	}

	const FIntVector SampleDimensions(ChunkDimensions.X + 1, ChunkDimensions.Y + 1, ChunkDimensions.Z + 1);
	TArray<float> DensitySamples;
	DensitySamples.SetNumZeroed(SampleDimensions.X * SampleDimensions.Y * SampleDimensions.Z);

	const int32 NumDimensions = SampleDimensions.Z;
	ParallelFor( NumDimensions, [&] ( int32 DimensionZ )
	{
		for (int32 Y = 0; Y < SampleDimensions.Y; ++Y)
		{
			for (int32 X = 0; X < SampleDimensions.X; ++X)
			{
				const FIntVector LatticeCoordinate = ChunkOriginGrid + FIntVector(X, Y, DimensionZ);
				DensitySamples[SampleIndex(SampleDimensions, X, Y, DimensionZ)] = SampleDensityAtLattice(OccupiedWorldCells, LatticeCoordinate);
			}
		}
	});

	// for (int32 Z = 0; Z < ChunkDimensions.Z; ++Z)
	const int32 NumSlices = ChunkDimensions.Z;
	ParallelFor( NumSlices, [&] ( int32 SliceZ )
	{
		for (int32 Y = 0; Y < ChunkDimensions.Y; ++Y)
		{
			for (int32 X = 0; X < ChunkDimensions.X; ++X)
			{
				float CornerDensity[8];
				FVector CornerPosition[8];
#ifdef GRADIENT_A
				FVector CornerGradient[8];
#endif

				for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
				{
#ifdef GRADIENT_A
					const int CX = X + CornerOffsets[CornerIndex][0];
					const int CY = Y + CornerOffsets[CornerIndex][1];
					const int CZ = SliceZ + CornerOffsets[CornerIndex][2];
					CornerDensity[CornerIndex] = DensitySamples[SampleIndex( SampleDimensions, CX, CY, CZ )];
					CornerPosition[CornerIndex] = GridVertexToWorldCm( ChunkOriginGrid + FIntVector( CX, CY, CZ ), VoxelSizeCm );
					CornerGradient[CornerIndex] = SampleDensityGradient( DensitySamples, SampleDimensions, CX, CY, CZ );
#else
					const FIntVector LocalCorner(
						X + CornerOffsets[CornerIndex][0],
						Y + CornerOffsets[CornerIndex][1],
						SliceZ + CornerOffsets[CornerIndex][2]);
					CornerDensity[CornerIndex] = DensitySamples[SampleIndex(SampleDimensions, LocalCorner.X, LocalCorner.Y, LocalCorner.Z)];
					CornerPosition[CornerIndex] = GridVertexToWorldCm(ChunkOriginGrid + LocalCorner, VoxelSizeCm);
#endif // TEST
				}

				int32 CubeIndex = 0;
				for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
				{
					if (CornerDensity[CornerIndex] < IsoLevel)
					{
						CubeIndex |= (1 << CornerIndex);
					}
				}

				const int32 EdgeMask = RTPSVoxelImport::MarchingCubesTables::edgeTable[CubeIndex];
				if (EdgeMask == 0)
				{
					continue;
				}

				FVector VertexList[12];
#ifdef GRADIENT_A
				FVector GradientList[12];
#endif
				for (int32 EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
				{
					if ((EdgeMask & (1 << EdgeIndex)) == 0)
					{
						continue;
					}

					const int32 CornerA = EdgeCorners[EdgeIndex][0];
					const int32 CornerB = EdgeCorners[EdgeIndex][1];
					VertexList[EdgeIndex] = InterpolateVertex(
						IsoLevel,
						CornerPosition[CornerA],
						CornerPosition[CornerB],
						CornerDensity[CornerA],
						CornerDensity[CornerB] );
#ifdef GRADIENT_A
					GradientList[EdgeIndex] = InterpolateGradient(
						IsoLevel,
						CornerGradient[CornerA],
						CornerGradient[CornerB],
						CornerDensity[CornerA],
						CornerDensity[CornerB] );
#endif
				}

				for (int32 TableIndex = 0; RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex] != -1; TableIndex += 3)
				{
					const FVector& A = VertexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex]];
					const FVector& B = VertexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 1]];
					const FVector& C = VertexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 2]];

					const int32 BaseIndex = OutMeshData.Vertices.Num();
					OutMeshData.Vertices.Add(A);
					OutMeshData.Vertices.Add(B);
					OutMeshData.Vertices.Add(C);

					OutMeshData.Triangles.Add(BaseIndex);
					OutMeshData.Triangles.Add(BaseIndex + 1);
					OutMeshData.Triangles.Add(BaseIndex + 2);

#ifdef GRADIENT_A
					const FVector& NA = GradientList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex]];
					const FVector& NB = GradientList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 1]];
					const FVector& NC = GradientList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 2]];

					OutMeshData.Normals.Add( NA );
					OutMeshData.Normals.Add( NB );
					OutMeshData.Normals.Add( NC );
#else
					FVector TriangleNormal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
					if (TriangleNormal.IsNearlyZero())
					{
						TriangleNormal = FVector::UpVector;
					}

					OutMeshData.Normals.Add( TriangleNormal );
					OutMeshData.Normals.Add( TriangleNormal );
					OutMeshData.Normals.Add( TriangleNormal );
#endif

					OutMeshData.UV0.Add(FVector2D(A.X / VoxelSizeCm, A.Y / VoxelSizeCm));
					OutMeshData.UV0.Add(FVector2D(B.X / VoxelSizeCm, B.Y / VoxelSizeCm));
					OutMeshData.UV0.Add(FVector2D(C.X / VoxelSizeCm, C.Y / VoxelSizeCm));

					OutMeshData.VertexColors.Add(FColor::White);
					OutMeshData.VertexColors.Add(FColor::White);
					OutMeshData.VertexColors.Add(FColor::White);

					const FVector TangentVector = (B - A).GetSafeNormal();
					const FProcMeshTangent Tangent(TangentVector, false);
					OutMeshData.Tangents.Add(Tangent);
					OutMeshData.Tangents.Add(Tangent);
					OutMeshData.Tangents.Add(Tangent);
				}
			}
		}
	});

	return OutMeshData.Triangles.Num() > 0;
}


bool RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
	const FIntVector& ChunkDimensions,
	const FIntVector& ChunkOriginGrid,
	float VoxelSizeCm,
	float IsoLevel,
	TArrayView<const float> DensityData,
	FMarchingCubesMeshData& OutMeshData)
{
	OutMeshData = FMarchingCubesMeshData();

	if (ChunkDimensions.X <= 0 || ChunkDimensions.Y <= 0 || ChunkDimensions.Z <= 0)
	{
		return false;
	}

	const FIntVector SampleDimensions(
		ChunkDimensions.X + 1,
		ChunkDimensions.Y + 1,
		ChunkDimensions.Z + 1);

	const int32 ExpectedSamples = SampleDimensions.X * SampleDimensions.Y * SampleDimensions.Z;
	if (DensityData.Num() < ExpectedSamples)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("BuildMarchingCubesChunkMeshDensity: DensityData.Num()=%d < expected %d"),
			DensityData.Num(), ExpectedSamples);
		return false;
	}

	const int32 NumSlices = ChunkDimensions.Z;
#ifdef GRADIENT_B
	TArray<FSliceBuildData> SliceResults;
#else
	TArray<FMarchingCubesMeshData> SliceResults;
#endif
	SliceResults.SetNum(NumSlices);

	ParallelFor(NumSlices, [&](int32 SliceZ)
	{
		for (int32 Y = 0; Y < ChunkDimensions.Y; ++Y)
		{
			for (int32 X = 0; X < ChunkDimensions.X; ++X)
			{
#ifdef GRADIENT_B
				FSliceBuildData& SliceData = SliceResults[SliceZ];
#else
				FMarchingCubesMeshData& SliceData = SliceResults[SliceZ];
#endif // 
				float CornerDensity[8];
				FVector CornerPosition[8];
#ifdef GRADIENT
				FVector CornerGradient[8];
#endif
#ifdef GRADIENT_B
				int32 VertexIndexList[12];
#endif

				for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
				{
					const int32 CX = X + CornerOffsets[CornerIndex][0];
					const int32 CY = Y + CornerOffsets[CornerIndex][1];
					const int32 CZ = SliceZ + CornerOffsets[CornerIndex][2];
					CornerDensity[CornerIndex] = DensityData[SampleIndex( SampleDimensions, CX, CY, CZ )];
					CornerPosition[CornerIndex] = GridVertexToWorldCm( ChunkOriginGrid + FIntVector(CX, CY, CZ), VoxelSizeCm );
#ifdef GRADIENT
					CornerGradient[CornerIndex] = SampleDensityGradient( DensityData, SampleDimensions, CX, CY, CZ );
#endif
				}

				int32 CubeIndex = 0;
				for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
				{
					if (CornerDensity[CornerIndex] >= IsoLevel)
					{
						CubeIndex |= (1 << CornerIndex);
					}
				}

				const int32 EdgeMask = RTPSVoxelImport::MarchingCubesTables::edgeTable[CubeIndex];
				if (EdgeMask == 0)
				{
					continue;
				}

				FVector VertexList[12];
#ifdef GRADIENT
				FVector GradientList[12];
#endif
				for (int32 EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
				{
					if ((EdgeMask & (1 << EdgeIndex)) == 0)
					{
						continue;
					}
					const int32 CornerA = EdgeCorners[EdgeIndex][0];
					const int32 CornerB = EdgeCorners[EdgeIndex][1];
					VertexList[EdgeIndex] = InterpolateVertex(
						IsoLevel,
						CornerPosition[CornerA],
						CornerPosition[CornerB],
						CornerDensity[CornerA],
						CornerDensity[CornerB]);
#ifdef GRADIENT
					GradientList[EdgeIndex] = InterpolateGradient(
						IsoLevel,
						CornerGradient[CornerA],
						CornerGradient[CornerB],
						CornerDensity[CornerA],
						CornerDensity[CornerB] );
#endif

#ifdef GRADIENT_B
					const FEdgeVertexKey EdgeKey = MakeEdgeVertexKey(X, Y, SliceZ, EdgeIndex);

					if ( const int32* FoundIndex = SliceData.EdgeVertexMap.Find(EdgeKey) )
					{
						VertexIndexList[EdgeIndex] = *FoundIndex;
					}
					else
					{
						const int32 NewIndex = SliceData.Vertices.Num();
						FSliceVertexData& NewVertex = SliceData.Vertices.AddDefaulted_GetRef();
						NewVertex.Position = VertexList[EdgeIndex];
						NewVertex.GradientNormal = GradientList[EdgeIndex];
						NewVertex.UV = FVector2D( VertexList[EdgeIndex].X / VoxelSizeCm, VertexList[EdgeIndex].Y / VoxelSizeCm );
						NewVertex.Color = FColor::White;

						const FVector TangentVector = FVector::CrossProduct( 
						NewVertex.GradientNormal, FVector::UpVector ).GetSafeNormal();

						NewVertex.Tangent = FProcMeshTangent( 
						TangentVector.IsNearlyZero() ? FVector::ForwardVector : TangentVector, 
						false );

						SliceData.EdgeVertexMap.Add( EdgeKey, NewIndex );
						VertexIndexList[EdgeIndex] = NewIndex;
					}
#endif // 

				}

				for (int32 TableIndex = 0;
					RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex] != -1;
					TableIndex += 3)
				{

					const FVector& A = VertexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex]];
					const FVector& B = VertexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 1]];
					const FVector& C = VertexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 2]];

					const int32 BaseIndex = SliceData.Vertices.Num();
					SliceData.Vertices.Add( A );
					SliceData.Vertices.Add( B );
					SliceData.Vertices.Add( C );

					SliceData.Triangles.Add(BaseIndex);
					SliceData.Triangles.Add(BaseIndex + 1);
					SliceData.Triangles.Add(BaseIndex + 2);

#ifdef GRADIENT
					const FVector& NA = GradientList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex]];
					const FVector& NB = GradientList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 1]];
					const FVector& NC = GradientList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 2]];
					SliceData.Normals.Add( -NA );
					SliceData.Normals.Add( -NB );
					SliceData.Normals.Add( -NC );
#else
					FVector TriangleNormal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
					if (TriangleNormal.IsNearlyZero())
					{
						TriangleNormal = FVector::UpVector;
					}

					SliceData.Normals.Add(TriangleNormal);
					SliceData.Normals.Add(TriangleNormal);
					SliceData.Normals.Add(TriangleNormal);
#endif

					SliceData.UV0.Add(FVector2D(A.X / VoxelSizeCm, A.Y / VoxelSizeCm));
					SliceData.UV0.Add(FVector2D(B.X / VoxelSizeCm, B.Y / VoxelSizeCm));
					SliceData.UV0.Add(FVector2D(C.X / VoxelSizeCm, C.Y / VoxelSizeCm));

					SliceData.VertexColors.Add(FColor::White);
					SliceData.VertexColors.Add(FColor::White);
					SliceData.VertexColors.Add(FColor::White);

					const FVector TangentVector = (B - A).GetSafeNormal();
					const FProcMeshTangent Tangent(TangentVector, false);
					SliceData.Tangents.Add(Tangent);
					SliceData.Tangents.Add(Tangent);
					SliceData.Tangents.Add(Tangent);

#ifdef GRADIENT_B
					const int32 I0 = VertexIndexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex]] = BaseIndex;
					const int32 I1 = VertexIndexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 1]] = BaseIndex + 1;
					const int32 I2 = VertexIndexList[RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex + 2]] = BaseIndex + 2;

					SliceData.Triangles.Add( I0 );
					SliceData.Triangles.Add( I1 );
					SliceData.Triangles.Add( I2 );

					const FVector& A = SliceData.Vertices[I0].Position;
					const FVector& B = SliceData.Vertices[I1].Position;
					const FVector& C = SliceData.Vertices[I2].Position;

					FVector TriangleNormal = FVector::CrossProduct( C - A, B - A ).GetSafeNormal();
					if( TriangleNormal.IsNearlyZero() )
					{
						TriangleNormal = FVector::UpVector;
					}

					SliceData.Vertices[I0].AccumulatedFaceNormal += TriangleNormal;
					SliceData.Vertices[I1].AccumulatedFaceNormal += TriangleNormal;
					SliceData.Vertices[I2].AccumulatedFaceNormal += TriangleNormal;
#else
#endif
				}
			}
		}
	});

	for (const FMarchingCubesMeshData& Slice : SliceResults)
	{
		if (Slice.Vertices.Num() == 0)
		{
			continue;
		}

		const int32 BaseIndex = OutMeshData.Vertices.Num();
		for (int32 Idx : Slice.Triangles)
		{
			OutMeshData.Triangles.Add(BaseIndex + Idx);
		}

		OutMeshData.Vertices.Append(Slice.Vertices);
		OutMeshData.Normals.Append(Slice.Normals);
		OutMeshData.UV0.Append(Slice.UV0);
		OutMeshData.VertexColors.Append(Slice.VertexColors);
		OutMeshData.Tangents.Append(Slice.Tangents);
	}

	return OutMeshData.Triangles.Num() > 0;
}
