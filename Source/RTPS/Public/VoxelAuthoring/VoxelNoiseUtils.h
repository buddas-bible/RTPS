#pragma once

#include "CoreMinimal.h"
#include "VoxelAuthoring/VoxelNoiseParams.h"

namespace VoxelNoiseUtils
{
	// Computes seed offset vector from NoiseSeed.
	FVector ComputeSeedOffset(int32 NoiseSeed);

	// Samples Fractal Brownian Motion noise at SamplePos.
	// SamplePos must already be scaled and seed-offset.
	// Returns normalized value in [-1, 1].
	float SampleFBM(const FVector& SamplePos, const FVoxelNoiseParams& Params);
}
