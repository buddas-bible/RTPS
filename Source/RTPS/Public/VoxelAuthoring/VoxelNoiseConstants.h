#pragma once

namespace VoxelNoiseConstants
{
	// Seed offset multipliers — shift Perlin sample positions per NoiseSeed value.
	// Values chosen to be irrational-like to avoid grid artifacts.
	constexpr float SeedOffsetX = 3.9812f;
	constexpr float SeedOffsetY = 7.1543f;
	constexpr float SeedOffsetZ = 5.4321f;
}
