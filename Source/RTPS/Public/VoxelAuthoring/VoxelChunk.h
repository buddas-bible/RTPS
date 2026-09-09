#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelNoiseParams.h"
#include "VoxelChunk.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
namespace RTPSVoxelImport { struct FMarchingCubesMeshData; }

UENUM(BlueprintType)
enum class EVoxelChunkState : uint8
{
	Uninitialized,
	Loading,
	Ready,
};

UENUM(BlueprintType)
enum class EVoxelChunkSource : uint8
{
	RandomNoise      UMETA(DisplayName = "Random Noise"),
	ChunkData        UMETA(DisplayName = "Chunk Data"),
	ChunkDataOrNoise UMETA(DisplayName = "Chunk Data or Noise"),
	WorldData        UMETA(DisplayName = "World Data"),
};

UCLASS()
class RTPS_API AVoxelChunk : public AActor
{
	GENERATED_BODY()

public:
	AVoxelChunk();

	// Set by manager before async load begins
	FIntVector ChunkCoord = FIntVector::ZeroValue;
	FIntVector ChunkDimensions = FIntVector(16, 16, 16);
	float CellSize = 100.f;
	float IsoLevel = 0.5f;
	FVoxelNoiseParams NoiseParams;
	EVoxelChunkSource Source = EVoxelChunkSource::RandomNoise;
	TArray<float> ExternalDensity;
	FString SaveDir;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ChunkMaterial;

	UPROPERTY(VisibleAnywhere)
	EVoxelChunkState State = EVoxelChunkState::Uninitialized;

	UPROPERTY(VisibleAnywhere)
	bool bModified = false;

	// Runtime mesh rebuild diagnostics are opt-in and controlled by the owning manager for spawned chunks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Debug")
	bool bDebugVoxelMeshRebuilds = false;

	// Kick off async generation (noise or file load)
	void BeginAsyncLoad();
	void ApplyBrush(const FVoxelBrush& Brush);
	void RebuildMeshAsync(const TCHAR* Reason = TEXT("Unknown"));
	static bool BuildInitialLatticeDensity(
		const FIntVector& InChunkCoord,
		const FIntVector& InChunkDimensions,
		const FVoxelNoiseParams& InNoiseParams,
		EVoxelChunkSource InSource,
		const TArray<float>& InExternalDensity,
		const FString& InSaveDir,
		TArray<float>& OutLatticeDensity);
	static bool ApplyBrushToLatticeDensity(
		TArray<float>& InOutLatticeDensity,
		const FIntVector& InChunkCoord,
		const FIntVector& InChunkDimensions,
		float InCellSize,
		const FVoxelBrush& Brush,
		float InIsoLevel = 0.5f);

	// Called from game thread after async work completes
	void ApplyMeshData(RTPSVoxelImport::FMarchingCubesMeshData MeshData,
		TArray<float> InLatticeDensity,
		bool bMarkCleanAfterApply,
		const TCHAR* RebuildReason = TEXT("Unknown"),
		double RebuildStartSeconds = -1.0,
		bool bAsyncRebuild = false);

	// Save modified density to disk (game thread)
	bool SaveToDisk() const;

	FString GetSaveFilePath() const;
	static FString GetSaveFilePathForChunk(const FString& InSaveDir, const FIntVector& InChunkCoord);
	bool HasSaveFile() const;
	const TArray<float>& GetLatticeDensity() const { return LatticeDensity; }
	int32 GetExpectedLatticeSampleCount() const;
	bool HasValidLatticeDensity() const;
	bool ApplyAuthoritativeDensitySnapshot(const TArray<float>& InDensity, int32 Revision, const TCHAR* Reason);

	// Returns true if the mesh was rendered in the last frame (frustum + occlusion combined)
	bool IsRendered() const;

	// Lattice density: (ChunkDimensions+1)^3 floats
	// Index: X + Y*(Dim.X+1) + Z*(Dim.X+1)*(Dim.Y+1)
	TArray<float> LatticeDensity;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;


};
