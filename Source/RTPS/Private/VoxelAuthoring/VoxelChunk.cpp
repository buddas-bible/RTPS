#include "VoxelAuthoring/VoxelChunk.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelImport/RTPSMarchingCubes.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Components/SceneComponent.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
#include "VoxelAuthoring/VoxelNoiseUtils.h"

namespace
{
	int32 GetExpectedLatticeSamples(const FIntVector& ChunkDimensions)
	{
		return
			(ChunkDimensions.X + 1) *
			(ChunkDimensions.Y + 1) *
			(ChunkDimensions.Z + 1);
	}

	float EvaluateVoxelBrushFalloff(EVoxelBrushFalloff Falloff, float NormalizedDistance)
	{
		const float D = FMath::Clamp(NormalizedDistance, 0.f, 1.f);
		switch (Falloff)
		{
		case EVoxelBrushFalloff::Linear:
			return 1.f - D;
		case EVoxelBrushFalloff::Spherical:
			return FMath::Sqrt(FMath::Max(0.f, 1.f - (D * D)));
		case EVoxelBrushFalloff::Plateau:
		{
			constexpr float InnerAlpha = 0.5f;
			if (D <= InnerAlpha)
			{
				return 1.f;
			}
			const float EdgeAlpha = FMath::Clamp((D - InnerAlpha) / (1.f - InnerAlpha), 0.f, 1.f);
			return FMath::Square(1.f - FMath::SmoothStep(0.f, 1.f, EdgeAlpha));
		}
		case EVoxelBrushFalloff::Smooth:
		default:
			return FMath::Square(1.f - FMath::SmoothStep(0.f, 1.f, D));
		}
	}

	TArray<float> GenerateNoiseLatticeData(
		const FIntVector& ChunkCoord,
		const FIntVector& ChunkDimensions,
		const FVoxelNoiseParams& NoiseParams)
	{
		const int32 SizeX = ChunkDimensions.X + 1;
		const int32 SizeY = ChunkDimensions.Y + 1;
		const int32 SizeZ = ChunkDimensions.Z + 1;

		TArray<float> Lattice;
		Lattice.SetNumUninitialized(SizeX * SizeY * SizeZ);

		const FVector SeedOffset = VoxelNoiseUtils::ComputeSeedOffset(NoiseParams.NoiseSeed);

		for (int32 SampleZ = 0; SampleZ < SizeZ; ++SampleZ)
		{
			for (int32 SampleY = 0; SampleY < SizeY; ++SampleY)
			{
				for (int32 SampleX = 0; SampleX < SizeX; ++SampleX)
				{
					const int32 WorldX = ChunkCoord.X * ChunkDimensions.X + SampleX;
					const int32 WorldY = ChunkCoord.Y * ChunkDimensions.Y + SampleY;
					const int32 WorldZ = ChunkCoord.Z * ChunkDimensions.Z + SampleZ;

					const FVector Base(
						static_cast<float>(WorldX) * NoiseParams.NoiseScale + SeedOffset.X,
						static_cast<float>(WorldY) * NoiseParams.NoiseScale + SeedOffset.Y,
						static_cast<float>(WorldZ) * NoiseParams.NoiseScale + SeedOffset.Z);

					const float NormalizedNoise = VoxelNoiseUtils::SampleFBM(Base, NoiseParams);
					const float HeightGrad = 1.0f - FMath::Clamp(
						static_cast<float>(WorldZ) / FMath::Max(NoiseParams.TerrainHeightCells, 1.0f),
						0.0f,
						1.0f);
					const float Density = FMath::Clamp(
						HeightGrad - NoiseParams.NoiseFloorOffset + NormalizedNoise * NoiseParams.NoiseWeight,
						0.0f,
						1.0f);

					const int32 Index = SampleX + SizeX * (SampleY + SizeY * SampleZ);
					Lattice[Index] = Density;
				}
			}
		}

		return Lattice;
	}

	bool LoadLatticeFromFilePath(
		const FString& FilePath,
		const FIntVector& ExpectedChunkCoord,
		const FIntVector& ExpectedDimensions,
		TArray<float>& OutLattice)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
		{
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		FString Schema;
		if (!RootObject->TryGetStringField(TEXT("schema"), Schema) ||
			Schema != TEXT("rtps.chunk.lattice.v1"))
		{
			return false;
		}

		double ChunkX = 0.0;
		double ChunkY = 0.0;
		double ChunkZ = 0.0;
		double DimX = 0.0;
		double DimY = 0.0;
		double DimZ = 0.0;

		if (!RootObject->TryGetNumberField(TEXT("chunkX"), ChunkX) ||
			!RootObject->TryGetNumberField(TEXT("chunkY"), ChunkY) ||
			!RootObject->TryGetNumberField(TEXT("chunkZ"), ChunkZ) ||
			!RootObject->TryGetNumberField(TEXT("dimX"), DimX) ||
			!RootObject->TryGetNumberField(TEXT("dimY"), DimY) ||
			!RootObject->TryGetNumberField(TEXT("dimZ"), DimZ))
		{
			return false;
		}

		if (FMath::RoundToInt(ChunkX) != ExpectedChunkCoord.X ||
			FMath::RoundToInt(ChunkY) != ExpectedChunkCoord.Y ||
			FMath::RoundToInt(ChunkZ) != ExpectedChunkCoord.Z ||
			FMath::RoundToInt(DimX) != ExpectedDimensions.X ||
			FMath::RoundToInt(DimY) != ExpectedDimensions.Y ||
			FMath::RoundToInt(DimZ) != ExpectedDimensions.Z)
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* LatticeValues = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("lattice"), LatticeValues) || LatticeValues == nullptr)
		{
			return false;
		}

		const int32 ExpectedSamples = GetExpectedLatticeSamples(ExpectedDimensions);
		if (LatticeValues->Num() != ExpectedSamples)
		{
			return false;
		}

		OutLattice.Reset(ExpectedSamples);
		OutLattice.Reserve(ExpectedSamples);

		for (const TSharedPtr<FJsonValue>& Value : *LatticeValues)
		{
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				OutLattice.Reset();
				return false;
			}

			OutLattice.Add(static_cast<float>(Value->AsNumber()));
		}

		return OutLattice.Num() == ExpectedSamples;
	}
}

AVoxelChunk::AVoxelChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->bUseAsyncCooking = true;
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCastShadow(true);
}

FString AVoxelChunk::GetSaveFilePath() const
{
	return GetSaveFilePathForChunk(SaveDir, ChunkCoord);
}

FString AVoxelChunk::GetSaveFilePathForChunk(const FString& InSaveDir, const FIntVector& InChunkCoord)
{
	FString Dir = InSaveDir;
	if (FPaths::IsRelative(Dir))
	{
		Dir = FPaths::Combine(FPaths::ProjectDir(), Dir);
	}

	return FPaths::Combine(
		Dir,
		FString::Printf(TEXT("chunk_%d_%d_%d.json"), InChunkCoord.X, InChunkCoord.Y, InChunkCoord.Z));
}

bool AVoxelChunk::HasSaveFile() const
{
	return FPaths::FileExists(GetSaveFilePath());
}

int32 AVoxelChunk::GetExpectedLatticeSampleCount() const
{
	return GetExpectedLatticeSamples(ChunkDimensions);
}

bool AVoxelChunk::HasValidLatticeDensity() const
{
	return LatticeDensity.Num() == GetExpectedLatticeSampleCount();
}

bool AVoxelChunk::BuildInitialLatticeDensity(
	const FIntVector& InChunkCoord,
	const FIntVector& InChunkDimensions,
	const FVoxelNoiseParams& InNoiseParams,
	EVoxelChunkSource InSource,
	const TArray<float>& InExternalDensity,
	const FString& InSaveDir,
	TArray<float>& OutLatticeDensity)
{
	OutLatticeDensity.Reset();

	const int32 ExpectedSamples = GetExpectedLatticeSamples(InChunkDimensions);
	if (ExpectedSamples <= 0)
	{
		return false;
	}

	bool bLoaded = false;
	if (InSource == EVoxelChunkSource::WorldData)
	{
		if (InExternalDensity.Num() == ExpectedSamples)
		{
			OutLatticeDensity = InExternalDensity;
			bLoaded = true;
		}
	}
	else if (InSource == EVoxelChunkSource::ChunkData || InSource == EVoxelChunkSource::ChunkDataOrNoise)
	{
		const FString SaveFilePath = GetSaveFilePathForChunk(InSaveDir, InChunkCoord);
		if (FPaths::FileExists(SaveFilePath))
		{
			bLoaded = LoadLatticeFromFilePath(SaveFilePath, InChunkCoord, InChunkDimensions, OutLatticeDensity);
		}
	}

	if (!bLoaded)
	{
		if (InSource == EVoxelChunkSource::ChunkData)
		{
			OutLatticeDensity.SetNumZeroed(ExpectedSamples);
		}
		else
		{
			OutLatticeDensity = GenerateNoiseLatticeData(InChunkCoord, InChunkDimensions, InNoiseParams);
		}
	}

	if (OutLatticeDensity.Num() != ExpectedSamples)
	{
		OutLatticeDensity.Reset();
		return false;
	}

	return true;
}

bool AVoxelChunk::ApplyAuthoritativeDensitySnapshot(const TArray<float>& InDensity, int32 Revision, const TCHAR* Reason)
{
	const int32 ExpectedSamples = GetExpectedLatticeSampleCount();
	if (InDensity.Num() != ExpectedSamples)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Authoritative voxel density snapshot rejected. Chunk=%s ChunkCoord=(%d,%d,%d) Revision=%d DensityCount=%d ExpectedCount=%d Reason=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			Revision,
			InDensity.Num(),
			ExpectedSamples,
			Reason != nullptr ? Reason : TEXT("Unknown"));
		return false;
	}

	LatticeDensity = InDensity;
	bModified = false;
	State = EVoxelChunkState::Loading;

	TWeakObjectPtr<AVoxelChunk> WeakThis(this);
	const FIntVector ChunkCoordValue = ChunkCoord;
	const FIntVector ChunkDimensionsValue = ChunkDimensions;
	const float CellSizeValue = CellSize;
	const float IsoLevelValue = IsoLevel;
	TArray<float> LatticeCopy = LatticeDensity;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Authoritative voxel density snapshot accepted. Chunk=%s ChunkCoord=(%d,%d,%d) Revision=%d DensityCount=%d Reason=%s"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		Revision,
		LatticeCopy.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"));

	Async(EAsyncExecution::ThreadPool, [WeakThis, ChunkCoordValue, ChunkDimensionsValue, CellSizeValue, IsoLevelValue, LatticeCopy = MoveTemp(LatticeCopy)]() mutable
	{
		RTPSVoxelImport::FMarchingCubesMeshData MeshData;
		const FIntVector WorldOrigin = ChunkCoordValue * ChunkDimensionsValue;
		RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
			ChunkDimensionsValue,
			WorldOrigin,
			CellSizeValue,
			IsoLevelValue,
			TArrayView<const float>(LatticeCopy),
			MeshData);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, MeshData = MoveTemp(MeshData), LatticeCopy = MoveTemp(LatticeCopy)]() mutable
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ApplyMeshData(MoveTemp(MeshData), MoveTemp(LatticeCopy), true);
			}
		});
	});

	return true;
}

void AVoxelChunk::BeginAsyncLoad()
{
	State = EVoxelChunkState::Loading;

	TWeakObjectPtr<AVoxelChunk> WeakThis(this);
	const FIntVector ChunkCoordValue = ChunkCoord;
	const FIntVector ChunkDimensionsValue = ChunkDimensions;
	const float CellSizeValue = CellSize;
	const float IsoLevelValue = IsoLevel;
	const FVoxelNoiseParams NoiseParamsValue = NoiseParams;
	const EVoxelChunkSource SourceValue = Source;
	TArray<float> ExternalDensityValue = ExternalDensity;
	const FString SaveDirValue = SaveDir;

	Async(EAsyncExecution::ThreadPool, [WeakThis, ChunkCoordValue, ChunkDimensionsValue, CellSizeValue, IsoLevelValue, NoiseParamsValue, SourceValue, ExternalDensityValue = MoveTemp(ExternalDensityValue), SaveDirValue]() mutable
	{
		TArray<float> Lattice;
		if (!AVoxelChunk::BuildInitialLatticeDensity(
			ChunkCoordValue,
			ChunkDimensionsValue,
			NoiseParamsValue,
			SourceValue,
			ExternalDensityValue,
			SaveDirValue,
			Lattice))
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, ChunkCoordValue, ChunkDimensionsValue]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->State = EVoxelChunkState::Uninitialized;
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[RTPSValidation] Voxel chunk initial lattice build failed. Chunk=%s ChunkCoord=(%d,%d,%d) ChunkDimensions=(%d,%d,%d)"),
						*GetNameSafe(WeakThis.Get()),
						ChunkCoordValue.X,
						ChunkCoordValue.Y,
						ChunkCoordValue.Z,
						ChunkDimensionsValue.X,
						ChunkDimensionsValue.Y,
						ChunkDimensionsValue.Z);
				}
			});
			return;
		}

		RTPSVoxelImport::FMarchingCubesMeshData MeshData;
		const FIntVector WorldOrigin = ChunkCoordValue * ChunkDimensionsValue;
		RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
			ChunkDimensionsValue,
			WorldOrigin,
			CellSizeValue,
			IsoLevelValue,
			TArrayView<const float>(Lattice),
			MeshData);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, MeshData = MoveTemp(MeshData), Lattice = MoveTemp(Lattice)]() mutable
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ApplyMeshData(MoveTemp(MeshData), MoveTemp(Lattice), true);
			}
		});
	});
}

bool AVoxelChunk::ApplyBrushToLatticeDensity(
	TArray<float>& InOutLatticeDensity,
	const FIntVector& InChunkCoord,
	const FIntVector& InChunkDimensions,
	float InCellSize,
	const FVoxelBrush& Brush,
	float InIsoLevel)
{
    const int32 SizeX = InChunkDimensions.X + 1;
    const int32 SizeY = InChunkDimensions.Y + 1;
    const int32 SizeZ = InChunkDimensions.Z + 1;

    const int32 ExpectedSamples = SizeX * SizeY * SizeZ;
    if (InOutLatticeDensity.Num() != ExpectedSamples || ExpectedSamples <= 0)
    {
        return false;
    }

    // Smooth needs a read-only snapshot so writes don't affect the same pass
    TArray<float> DensitySnapshot;
    if (Brush.Shape == EVoxelBrushShape::Smooth)
    {
        DensitySnapshot = InOutLatticeDensity;
    }

    bool bChanged = false;

    for (int32 SampleZ = 0; SampleZ < SizeZ; ++SampleZ)
    {
        for (int32 SampleY = 0; SampleY < SizeY; ++SampleY)
        {
            for (int32 SampleX = 0; SampleX < SizeX; ++SampleX)
            {
                const int32 WorldSampleX = InChunkCoord.X * InChunkDimensions.X + SampleX;
                const int32 WorldSampleY = InChunkCoord.Y * InChunkDimensions.Y + SampleY;
                const int32 WorldSampleZ = InChunkCoord.Z * InChunkDimensions.Z + SampleZ;

                const FVector SampleWorldPos(
                    WorldSampleX * InCellSize,
                    WorldSampleY * InCellSize,
                    WorldSampleZ * InCellSize);

                const int32 Index = SampleX + SizeX * (SampleY + SizeY * SampleZ);
                float& Density = InOutLatticeDensity[Index];

                switch (Brush.Shape)
                {
                case EVoxelBrushShape::Sphere:
                {
                    const float Distance = FVector::Dist(SampleWorldPos, Brush.WorldPosition);
                    if (Distance >= Brush.Radius) { continue; }

                    bChanged = true;
                    const float T = Distance / Brush.Radius;
                    if (Brush.BlendMode == EVoxelBrushBlendMode::Additive)
                    {
                        // Legacy additive sphere path. Editor/camera brush callers keep this behavior unless they opt into TargetLerp/TargetMax.
                        const float LegacyFalloff = 1.f - T * T;
                        const float Delta = Brush.Strength * LegacyFalloff;
                        if (Brush.Mode == EVoxelBrushMode::Add)
                        {
                            Density = FMath::Clamp(Density + Delta, 0.f, 1.f);
                        }
                        else
                        {
                            Density = FMath::Clamp(Density - Delta, 0.f, 1.f);
                        }
                        break;
                    }

                    const float FalloffWeight = EvaluateVoxelBrushFalloff(Brush.Falloff, T);
                    if (FalloffWeight <= 0.f)
                    {
                        continue;
                    }

                    float NewDensity = Density;
                    if (Brush.Mode == EVoxelBrushMode::Add)
                    {
                        const float TargetDensity = FMath::Clamp(InIsoLevel + Brush.Strength * FalloffWeight, 0.f, 1.f);
                        if (Brush.BlendMode == EVoxelBrushBlendMode::TargetMax)
                        {
                            NewDensity = FMath::Max(Density, TargetDensity);
                        }
                        else
                        {
                            NewDensity = TargetDensity > Density
                                ? FMath::Lerp(Density, TargetDensity, FMath::Clamp(Brush.ConvergenceAlpha, 0.f, 1.f))
                                : Density;
                        }
                    }
                    else
                    {
                        const float TargetDensity = FMath::Clamp(InIsoLevel - Brush.Strength * FalloffWeight, 0.f, 1.f);
                        if (Brush.BlendMode == EVoxelBrushBlendMode::TargetMax)
                        {
                            NewDensity = FMath::Min(Density, TargetDensity);
                        }
                        else
                        {
                            NewDensity = TargetDensity < Density
                                ? FMath::Lerp(Density, TargetDensity, FMath::Clamp(Brush.ConvergenceAlpha, 0.f, 1.f))
                                : Density;
                        }
                    }

                    Density = FMath::Clamp(NewDensity, 0.f, 1.f);
                    break;
                }

                case EVoxelBrushShape::Box:
                {
                    const FVector Half(Brush.Radius, Brush.Radius, Brush.Radius);
                    const FBox   Box(Brush.WorldPosition - Half, Brush.WorldPosition + Half);
                    if (!Box.IsInsideOrOn(SampleWorldPos)) { continue; }

                    if (Brush.Mode == EVoxelBrushMode::Add)
                        Density = FMath::Clamp(Density + Brush.Strength, 0.f, 1.f);
                    else
                        Density = FMath::Clamp(Density - Brush.Strength, 0.f, 1.f);

                    bChanged = true;
                    break;
                }

                case EVoxelBrushShape::Flatten:
                {
                    // XY-only radius; levels terrain to the brush hit Z
                    const float XYDist = FMath::Sqrt(
                        FMath::Square(SampleWorldPos.X - Brush.WorldPosition.X) +
                        FMath::Square(SampleWorldPos.Y - Brush.WorldPosition.Y));
                    if (XYDist >= Brush.Radius) { continue; }

                    const float T       = XYDist / Brush.Radius;
                    const float Falloff = 1.f - T * T;

                    // Smooth height ramp: solid well below BrushZ, air well above, continuous transition
                    const float TransitionBand = FMath::Max(InCellSize * 2.f, 1.f);
                    const float HeightDelta    = (SampleWorldPos.Z - Brush.WorldPosition.Z) / TransitionBand;
                    const float TargetDensity  = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(0.5f - HeightDelta, 0.f, 1.f));
                    const float Delta          = (TargetDensity - Density) * Brush.Strength * Falloff;

                    Density  = FMath::Clamp(Density + Delta, 0.f, 1.f);
                    bChanged = true;
                    break;
                }

                case EVoxelBrushShape::Smooth:
                {
                    const float Distance = FVector::Dist(SampleWorldPos, Brush.WorldPosition);
                    if (Distance >= Brush.Radius) { continue; }

                    // Average the 6-connected neighbours from the snapshot
                    const int32 Offsets[6][3] = {
                        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
                    };

                    float Sum = 0.f;
                    for (const auto& Off : Offsets)
                    {
                        const int32 NX = FMath::Clamp(SampleX + Off[0], 0, SizeX - 1);
                        const int32 NY = FMath::Clamp(SampleY + Off[1], 0, SizeY - 1);
                        const int32 NZ = FMath::Clamp(SampleZ + Off[2], 0, SizeZ - 1);
                        Sum += DensitySnapshot[NX + SizeX * (NY + SizeY * NZ)];
                    }
                    constexpr int32 Count = 6;

                    const float T       = Distance / Brush.Radius;
                    const float Falloff = 1.f - T * T;
                    const float Average = Sum / static_cast<float>(Count);
                    Density  = FMath::Clamp(
                        FMath::Lerp(DensitySnapshot[Index], Average, Brush.Strength * Falloff),
                        0.f, 1.f);
                    bChanged = true;
                    break;
                }

                case EVoxelBrushShape::SurfaceBlob:
                {
                    // 실험용: 평평한 표면 스플랫 연구용으로 유지하지만 기본 Voxel 사격 경로에서는 사용하지 않는다.
                    if (Brush.Mode != EVoxelBrushMode::Add || Brush.Radius <= 0.f || Brush.SurfaceDepth <= 0.f)
                    {
                        continue;
                    }

                    const FVector SurfaceNormal = Brush.SurfaceNormal.GetSafeNormal();
                    if (SurfaceNormal.IsNearlyZero())
                    {
                        continue;
                    }

                    const FVector Delta = SampleWorldPos - Brush.WorldPosition;
                    const float Height = FVector::DotProduct(Delta, SurfaceNormal);
                    if (Height < 0.f || Height >= Brush.SurfaceDepth)
                    {
                        continue;
                    }

                    const FVector TangentDelta = Delta - (SurfaceNormal * Height);
                    const float RadialDistance = TangentDelta.Size();
                    if (RadialDistance >= Brush.Radius)
                    {
                        continue;
                    }

                    const float RadialAlpha = FMath::Clamp(RadialDistance / Brush.Radius, 0.f, 1.f);
                    const float HeightAlpha = FMath::Clamp(Height / Brush.SurfaceDepth, 0.f, 1.f);
                    const float RadialFalloff = 1.f - FMath::SmoothStep(0.f, 1.f, RadialAlpha);
                    const float HeightFalloff = 1.f - FMath::SmoothStep(0.f, 1.f, HeightAlpha);
                    const float DeltaDensity = Brush.Strength * RadialFalloff * HeightFalloff;

                    if (DeltaDensity <= 0.f)
                    {
                        continue;
                    }

                    Density = FMath::Clamp(Density + DeltaDensity, 0.f, 1.f);
                    bChanged = true;
                    break;
                }

                case EVoxelBrushShape::TerrainMudBlob:
                {
                    // 실험용: MC 지형에서 판/층 누적이 보여 기본 경로에서 제외했다. 향후 mud/clay 연구용으로 보존한다.
                    if (Brush.Mode != EVoxelBrushMode::Add || Brush.Radius <= 0.f || Brush.SurfaceDepth <= 0.f || Brush.BackDepth < 0.f)
                    {
                        continue;
                    }

                    const FVector SurfaceNormal = Brush.SurfaceNormal.GetSafeNormal();
                    if (SurfaceNormal.IsNearlyZero())
                    {
                        continue;
                    }

                    // TerrainMudBlob v2 uses one embedded asymmetric superellipsoid field.
                    // This avoids the stacked-disc look caused by independent radial * height slab falloffs.
                    const FVector Delta = SampleWorldPos - Brush.WorldPosition;
                    const float Height = FVector::DotProduct(Delta, SurfaceNormal);

                    const FVector TangentDelta = Delta - (SurfaceNormal * Height);
                    const float RadialDistance = TangentDelta.Size();
                    if (RadialDistance >= Brush.Radius)
                    {
                        continue;
                    }

                    float NormalizedHeight = 0.f;
                    if (Height >= 0.f)
                    {
                        if (Height >= Brush.SurfaceDepth)
                        {
                            continue;
                        }
                        NormalizedHeight = Height / Brush.SurfaceDepth;
                    }
                    else
                    {
                        if (Brush.BackDepth <= KINDA_SMALL_NUMBER || Height <= -Brush.BackDepth)
                        {
                            continue;
                        }
                        NormalizedHeight = -Height / Brush.BackDepth;
                    }

                    const float RoundnessPower = FMath::Max(Brush.ClumpRoundnessPower, 1.f);
                    const float NormalizedRadial = FMath::Clamp(RadialDistance / Brush.Radius, 0.f, 1.f);
                    const float ShapeValue =
                        FMath::Pow(NormalizedRadial, RoundnessPower) +
                        FMath::Pow(FMath::Abs(NormalizedHeight), RoundnessPower);
                    if (ShapeValue >= 1.f)
                    {
                        continue;
                    }

                    const float ShapeAlpha = FMath::Clamp(ShapeValue, 0.f, 1.f);
                    const float Weight = FMath::Square(1.f - FMath::SmoothStep(0.f, 1.f, ShapeAlpha));
                    if (Weight <= 0.f)
                    {
                        continue;
                    }

                    bChanged = true;
                    float NewDensity = Density;
                    switch (Brush.BlendMode)
                    {
                    case EVoxelBrushBlendMode::Additive:
                        NewDensity = Density + (Brush.Strength * Weight);
                        break;
                    case EVoxelBrushBlendMode::TargetMax:
                    {
                        const float TargetDensity = FMath::Clamp(InIsoLevel + Brush.Strength * Weight, 0.f, 1.f);
                        NewDensity = FMath::Max(Density, TargetDensity);
                        break;
                    }
                    case EVoxelBrushBlendMode::TargetLerp:
                    default:
                    {
                        const float TargetDensity = FMath::Clamp(InIsoLevel + Brush.Strength * Weight, 0.f, 1.f);
                        if (TargetDensity > Density)
                        {
                            NewDensity = FMath::Lerp(Density, TargetDensity, FMath::Clamp(Brush.ConvergenceAlpha, 0.f, 1.f));
                        }
                        break;
                    }
                    }

                    NewDensity = FMath::Clamp(NewDensity, 0.f, 1.f);
                    if (!FMath::IsNearlyEqual(NewDensity, Density))
                    {
                        Density = NewDensity;
                    }
                    break;
                }

                default:
                    break;
                }
            }
        }
    }

    return bChanged;
}

void AVoxelChunk::ApplyBrush(const FVoxelBrush& Brush)
{
    if (ApplyBrushToLatticeDensity(LatticeDensity, ChunkCoord, ChunkDimensions, CellSize, Brush, IsoLevel))
    {
        bModified = true;
        RebuildMeshAsync(TEXT("DensityAffectedChunk"));
    }
}

void AVoxelChunk::RebuildMeshAsync(const TCHAR* Reason)
{
	TWeakObjectPtr<AVoxelChunk> WeakThis(this);
	const FIntVector ChunkCoordValue = ChunkCoord;
	const FIntVector ChunkDimensionsValue = ChunkDimensions;
	const float CellSizeValue = CellSize;
	const float IsoLevelValue = IsoLevel;
	const FString RebuildReason = Reason != nullptr ? Reason : TEXT("Unknown");
	const double RebuildStartSeconds = FPlatformTime::Seconds();
	const bool bDebugMeshRebuilds = bDebugVoxelMeshRebuilds;
	TArray<float> LatticeCopy = LatticeDensity;

	if (bDebugMeshRebuilds)
	{
		UE_LOG(
			LogRTPSVoxelDebug,
			Log,
			TEXT("[VoxelMeshRebuildDebug] Start Chunk=%s ChunkCoord=(%d,%d,%d) Trigger=%s Async=1 CollisionEnabled=%d Modified=%d"),
			*GetNameSafe(this),
			ChunkCoordValue.X,
			ChunkCoordValue.Y,
			ChunkCoordValue.Z,
			*RebuildReason,
			MeshComponent != nullptr && MeshComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision ? 1 : 0,
			bModified ? 1 : 0);
	}

	Async(EAsyncExecution::ThreadPool, [WeakThis, ChunkCoordValue, ChunkDimensionsValue, CellSizeValue, IsoLevelValue, RebuildReason, RebuildStartSeconds, bDebugMeshRebuilds, LatticeCopy = MoveTemp(LatticeCopy)]() mutable
	{
		RTPSVoxelImport::FMarchingCubesMeshData MeshData;
		const FIntVector WorldOrigin = ChunkCoordValue * ChunkDimensionsValue;
		RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
			ChunkDimensionsValue,
			WorldOrigin,
			CellSizeValue,
			IsoLevelValue,
			TArrayView<const float>(LatticeCopy),
			MeshData);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, MeshData = MoveTemp(MeshData), LatticeCopy = MoveTemp(LatticeCopy), RebuildReason, RebuildStartSeconds, bDebugMeshRebuilds]() mutable
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ApplyMeshData(
					MoveTemp(MeshData),
					MoveTemp(LatticeCopy),
					false,
					*RebuildReason,
					RebuildStartSeconds,
					bDebugMeshRebuilds);
			}
		});
	});
}

void AVoxelChunk::ApplyMeshData(
	RTPSVoxelImport::FMarchingCubesMeshData MeshData,
	TArray<float> InLatticeDensity,
	bool bMarkCleanAfterApply,
	const TCHAR* RebuildReason,
	double RebuildStartSeconds,
	bool bAsyncRebuild)
{
	const RTPSVoxelImport::FMarchingCubesMeshFilterStats FilterStats =
		RTPSVoxelImport::FilterDegenerateTriangles(MeshData);
	if (FilterStats.OriginalTriangleCount > 0 || FilterStats.RemovedDegenerateTriangleCount > 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] MarchingCubes degenerate triangle filter summary. Chunk=%s ChunkCoord=(%d,%d,%d) OriginalTriangleCount=%d RemovedDegenerateTriangleCount=%d FinalTriangleCount=%d CollisionTriangleCount=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			FilterStats.OriginalTriangleCount,
			FilterStats.RemovedDegenerateTriangleCount,
			FilterStats.FinalTriangleCount,
			FilterStats.FinalTriangleCount);
	}

	LatticeDensity = MoveTemp(InLatticeDensity);
	if (bMarkCleanAfterApply)
	{
		bModified = false;
	}

	if (!MeshComponent)
	{
		State = EVoxelChunkState::Ready;
		return;
	}

	if (MeshData.Vertices.Num() > 0 && MeshData.Triangles.Num() > 0)
	{
		// Marching cubes currently emits world-space vertices, but chunk actors are spawned at
		// their chunk world origins. Convert to actor-local space before creating the mesh section.
		const FVector ActorOffset = GetActorLocation();
		for (FVector& Vertex : MeshData.Vertices)
		{
			Vertex -= ActorOffset;
		}

		MeshComponent->CreateMeshSection(
			0,
			MeshData.Vertices,
			MeshData.Triangles,
			MeshData.Normals,
			MeshData.UV0,
			MeshData.VertexColors,
			MeshData.Tangents,
			true);

		if (ChunkMaterial)
		{
			MeshComponent->SetMaterial(0, ChunkMaterial);
		}
	}
	else
	{
		MeshComponent->ClearMeshSection(0);
	}

	State = EVoxelChunkState::Ready;

	if (bDebugVoxelMeshRebuilds && bAsyncRebuild)
	{
		const double RebuildDurationMs = RebuildStartSeconds >= 0.0
			? (FPlatformTime::Seconds() - RebuildStartSeconds) * 1000.0
			: -1.0;
		UE_LOG(
			LogRTPSVoxelDebug,
			Log,
			TEXT("[VoxelMeshRebuildDebug] Finish Chunk=%s ChunkCoord=(%d,%d,%d) Trigger=%s Async=1 VertexCount=%d TriangleCount=%d CollisionEnabled=%d Modified=%d DurationMs=%.2f"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			RebuildReason != nullptr ? RebuildReason : TEXT("Unknown"),
			MeshData.Vertices.Num(),
			MeshData.Triangles.Num() / 3,
			MeshComponent != nullptr && MeshComponent->GetCollisionEnabled() != ECollisionEnabled::NoCollision ? 1 : 0,
			bModified ? 1 : 0,
			RebuildDurationMs);
	}
}


bool AVoxelChunk::SaveToDisk() const
{
	const int32 ExpectedSamples = GetExpectedLatticeSamples(ChunkDimensions);
	if (LatticeDensity.Num() != ExpectedSamples)
	{
		return false;
	}

	const FString FilePath = GetSaveFilePath();
	const FString Directory = FPaths::GetPath(FilePath);
	if (!Directory.IsEmpty())
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("schema"), TEXT("rtps.chunk.lattice.v1"));
	RootObject->SetNumberField(TEXT("chunkX"), static_cast<double>(ChunkCoord.X));
	RootObject->SetNumberField(TEXT("chunkY"), static_cast<double>(ChunkCoord.Y));
	RootObject->SetNumberField(TEXT("chunkZ"), static_cast<double>(ChunkCoord.Z));
	RootObject->SetNumberField(TEXT("dimX"), static_cast<double>(ChunkDimensions.X));
	RootObject->SetNumberField(TEXT("dimY"), static_cast<double>(ChunkDimensions.Y));
	RootObject->SetNumberField(TEXT("dimZ"), static_cast<double>(ChunkDimensions.Z));
	RootObject->SetNumberField(TEXT("isoLevel"), static_cast<double>(IsoLevel));
	RootObject->SetNumberField(TEXT("cellSize"), static_cast<double>(CellSize));

	TArray<TSharedPtr<FJsonValue>> LatticeValues;
	LatticeValues.Reserve(LatticeDensity.Num());
	for (const float DensityValue : LatticeDensity)
	{
		LatticeValues.Add(MakeShared<FJsonValueNumber>(static_cast<double>(DensityValue)));
	}
	RootObject->SetArrayField(TEXT("lattice"), LatticeValues);

	FString OutputJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(OutputJson, *FilePath);
}

bool AVoxelChunk::IsRendered() const
{
	return MeshComponent && MeshComponent->WasRecentlyRendered(0.1f);
}
