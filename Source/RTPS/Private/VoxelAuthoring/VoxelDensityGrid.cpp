#include "VoxelAuthoring/VoxelDensityGrid.h"
#include "VoxelAuthoring/VoxelNoiseUtils.h"

void UVoxelDensityGrid::EnsureAllocated()
{
	const int32 Total = GridDimensions.X * GridDimensions.Y * GridDimensions.Z;
	if (DensityGrid.Num() != Total)
	{
		DensityGrid.SetNumZeroed(Total);
	}
}

void UVoxelDensityGrid::Clear()
{
	EnsureAllocated();
	for (float& D : DensityGrid)
	{
		D = 0.f;
	}
}

void UVoxelDensityGrid::FillNoise(const FVoxelNoiseParams& Params)
{
	EnsureAllocated();

	const FVector SeedOffset = VoxelNoiseUtils::ComputeSeedOffset(Params.NoiseSeed);

	for (int32 Z = 0; Z < GridDimensions.Z; ++Z)
	{
		for (int32 Y = 0; Y < GridDimensions.Y; ++Y)
		{
			for (int32 X = 0; X < GridDimensions.X; ++X)
			{
				const FVector SamplePos(
					(static_cast<float>(X) + 0.5f) * Params.NoiseScale + SeedOffset.X,
					(static_cast<float>(Y) + 0.5f) * Params.NoiseScale + SeedOffset.Y,
					(static_cast<float>(Z) + 0.5f) * Params.NoiseScale + SeedOffset.Z);

				const float NormalizedNoise = VoxelNoiseUtils::SampleFBM(SamplePos, Params);

				const float NormalizedY = static_cast<float>(Z) / FMath::Max(static_cast<float>(GridDimensions.Z - 1), 1.f);
				const float HeightGradient = 1.0f - NormalizedY;
				const float RawDensity = HeightGradient - Params.NoiseFloorOffset + NormalizedNoise * Params.NoiseWeight;
				DensityGrid[LinearIndex(X, Y, Z)] = FMath::Clamp(RawDensity, 0.f, 1.f);
			}
		}
	}
}

void UVoxelDensityGrid::FillSphere(FVector LocalCenter, float Radius, float Value)
{
	EnsureAllocated();

	const float RadiusSq = Radius * Radius;
	for (int32 Z = 0; Z < GridDimensions.Z; ++Z)
	{
		for (int32 Y = 0; Y < GridDimensions.Y; ++Y)
		{
			for (int32 X = 0; X < GridDimensions.X; ++X)
			{
				const FVector CellCenter(
					(X + 0.5f) * CellSize,
					(Y + 0.5f) * CellSize,
					(Z + 0.5f) * CellSize);

				if (FVector::DistSquared(CellCenter, LocalCenter) <= RadiusSq)
				{
					DensityGrid[LinearIndex(X, Y, Z)] = Value;
				}
			}
		}
	}
}

int32 UVoxelDensityGrid::LinearIndex(int32 X, int32 Y, int32 Z) const
{
	return X + GridDimensions.X * (Y + GridDimensions.Y * Z);
}

FVoxelDensitySnapshot UVoxelDensityGrid::TakeSnapshot(float IsoLevel) const
{
	FVoxelDensitySnapshot Snap;
	Snap.Grid = DensityGrid;
	Snap.Dimensions = GridDimensions;
	Snap.CellSize = CellSize;
	Snap.IsoLevel = IsoLevel;
	return Snap;
}

void UVoxelDensityGrid::ApplySnapshot(const FVoxelDensitySnapshot& Snapshot)
{
	if (!Snapshot.IsValid())
	{
		return;
	}
	GridDimensions = Snapshot.Dimensions;
	CellSize = Snapshot.CellSize;
	DensityGrid = Snapshot.Grid;
}
