#include "VoxelAuthoring/VoxelNoiseUtils.h"
#include "VoxelAuthoring/VoxelNoiseConstants.h"
#include "Math/UnrealMathUtility.h"

namespace VoxelNoiseUtils
{
	FVector ComputeSeedOffset(int32 NoiseSeed)
	{
		return FVector(
			static_cast<float>(NoiseSeed) * VoxelNoiseConstants::SeedOffsetX,
			static_cast<float>(NoiseSeed) * VoxelNoiseConstants::SeedOffsetY,
			static_cast<float>(NoiseSeed) * VoxelNoiseConstants::SeedOffsetZ);
	}

	float SampleFBM(const FVector& SamplePos, const FVoxelNoiseParams& Params)
	{
		float Amplitude = 1.0f;
		float Frequency = 1.0f;
		float NoiseSum = 0.0f;
		float MaxAmplitude = 0.0f;

		for (int32 Oct = 0; Oct < Params.NoiseOctaves; ++Oct)
		{
			NoiseSum += FMath::PerlinNoise3D(SamplePos * Frequency) * Amplitude;
			MaxAmplitude += Amplitude;
			Amplitude *= Params.NoisePersistence;
			Frequency *= Params.NoiseLacunarity;
		}

		return (MaxAmplitude > 0.f) ? (NoiseSum / MaxAmplitude) : 0.f;
	}
}
