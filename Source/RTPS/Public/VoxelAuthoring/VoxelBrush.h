#pragma once
#include "CoreMinimal.h"
#include "VoxelBrush.generated.h"

UENUM(BlueprintType)
enum class EVoxelBrushMode : uint8
{
    Add    UMETA(DisplayName = "Add"),
    Remove UMETA(DisplayName = "Remove"),
};

UENUM(BlueprintType)
enum class EVoxelBrushShape : uint8
{
    Sphere  UMETA(DisplayName = "Sphere"),
    Box     UMETA(DisplayName = "Box"),
    Flatten UMETA(DisplayName = "Flatten"),
    Smooth  UMETA(DisplayName = "Smooth"),
    // 실험용: 기본 Voxel 사격 프로토타입에서는 현재 사용하지 않는다.
    // 평평한 Floor/Wall의 SurfaceBlob은 나중에 표면 스플랫 무기나 특수효과에 재사용할 수 있다.
    SurfaceBlob UMETA(DisplayName = "Surface Blob"),
    // 실험용: Marching Cubes 지형에서 TerrainMudBlob은 원반/판/층 누적이 보여 기본 경로에서 제외했다.
    // 기본 LMB/RMB 사격은 일관성을 위해 Sphere + Falloff + TargetLerp를 사용한다.
    TerrainMudBlob UMETA(DisplayName = "Terrain Mud Blob"),
};

UENUM(BlueprintType)
enum class EVoxelBrushBlendMode : uint8
{
    Additive  UMETA(DisplayName = "Additive"),
    TargetMax UMETA(DisplayName = "Target Max"),
    TargetLerp UMETA(DisplayName = "Target Lerp"),
};

UENUM(BlueprintType)
enum class EVoxelBrushFalloff : uint8
{
    Linear    UMETA(DisplayName = "Linear"),
    Smooth    UMETA(DisplayName = "Smooth"),
    Spherical UMETA(DisplayName = "Spherical"),
    Plateau   UMETA(DisplayName = "Plateau"),
};

USTRUCT(BlueprintType)
struct RTPS_API FVoxelBrush
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector WorldPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
    float Radius = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float Strength = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EVoxelBrushMode Mode = EVoxelBrushMode::Add;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EVoxelBrushShape Shape = EVoxelBrushShape::Sphere;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EVoxelBrushFalloff Falloff = EVoxelBrushFalloff::Smooth;

    // Used by SurfaceBlob and TerrainMudBlob. Other brush shapes keep their existing behavior.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector SurfaceNormal = FVector::UpVector;

    // Used by SurfaceBlob and TerrainMudBlob. Radius remains the tangent-plane/clump tangent radius.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
    float SurfaceDepth = 100.f;

    // Used by TerrainMudBlob only; negative-normal clump half-extent from the embedded center.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float BackDepth = 20.f;

    // Used by TerrainMudBlob only; records how far the clump center is embedded behind the hit surface.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
    float EmbedDepth = 30.f;

    // Used by TerrainMudBlob only; controls the superellipsoid falloff shape. 2.0 is ellipsoid-like, higher values are boxier.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
    float ClumpRoundnessPower = 2.5f;

    // Used by convergence-capable brush paths such as unified sphere shots and TerrainMudBlob.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConvergenceAlpha = 0.45f;

    // Additive preserves legacy non-shot brush behavior; voxel shot code explicitly opts into TargetLerp by default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EVoxelBrushBlendMode BlendMode = EVoxelBrushBlendMode::Additive;
};
