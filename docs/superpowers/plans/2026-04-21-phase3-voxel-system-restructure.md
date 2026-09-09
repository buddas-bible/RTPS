# Phase 3: 복셀 시스템 구조화 구현 플랜

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `ARuntimeAuthoringVolume`을 세 UActorComponent로 분리하여 밀도 데이터 / 시각화 / 파일 I/O 책임을 명확히 구분

**Architecture:** Actor는 IsoLevel·NoiseParams·입력 바인딩·컴포넌트 조율만 담당. 렌더 프리미티브(BlockHISM, MarchingMesh, WireframeLines)는 Actor가 생성하고 VisManager에 주입. 컴포넌트 간 직접 참조 없음 — Actor가 데이터를 중개.

**Tech Stack:** Unreal Engine 5.5, C++, UActorComponent, Perforce

---

## 파일 변경 목록

### 생성
- `Source/RTPS/Public/VoxelAuthoring/VoxelDensityGrid.h`
- `Source/RTPS/Private/VoxelAuthoring/VoxelDensityGrid.cpp`
- `Source/RTPS/Public/VoxelAuthoring/VoxelVisualizationManager.h`
- `Source/RTPS/Private/VoxelAuthoring/VoxelVisualizationManager.cpp`
- `Source/RTPS/Public/VoxelAuthoring/VoxelAuthoringPersistence.h`
- `Source/RTPS/Private/VoxelAuthoring/VoxelAuthoringPersistence.cpp`
- `Source/RTPS/Public/VoxelAuthoring/VoxelDensitySnapshot.h`

### 수정
- `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h`
- `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp`

---

## Task 1: FVoxelDensitySnapshot 구조체 생성

컴포넌트 간 밀도 데이터 전달에 쓰이는 경량 컨테이너.

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelDensitySnapshot.h`

- [ ] **Step 1: 파일 생성**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "VoxelDensitySnapshot.generated.h"

// Lightweight container for transferring density grid state between components.
USTRUCT(BlueprintType)
struct RTPS_API FVoxelDensitySnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<float> Grid;

    UPROPERTY()
    FIntVector Dimensions = FIntVector(0, 0, 0);

    UPROPERTY()
    float CellSize = 100.f;

    UPROPERTY()
    float IsoLevel = 0.5f;

    bool IsValid() const
    {
        return Dimensions.X > 0 && Dimensions.Y > 0 && Dimensions.Z > 0
            && Grid.Num() == Dimensions.X * Dimensions.Y * Dimensions.Z;
    }
};
```

- [ ] **Step 2: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 2: UVoxelDensityGrid 생성

밀도 데이터를 소유하고 조작하는 컴포넌트.

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelDensityGrid.h`
- Create: `Source/RTPS/Private/VoxelAuthoring/VoxelDensityGrid.cpp`

- [ ] **Step 1: VoxelDensityGrid.h 생성**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelAuthoring/VoxelNoiseParams.h"
#include "VoxelAuthoring/VoxelDensitySnapshot.h"
#include "VoxelDensityGrid.generated.h"

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelDensityGrid : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel")
    FIntVector GridDimensions = FIntVector(16, 16, 16);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel", meta = (ClampMin = "1.0"))
    float CellSize = 100.f;

    void EnsureAllocated();
    void Clear();
    void FillNoise(const FVoxelNoiseParams& Params);
    void FillSphere(FVector LocalCenter, float Radius, float Value);

    TArrayView<const float> GetGrid() const { return DensityGrid; }
    TArrayView<float> GetGridMutable() { return DensityGrid; }

    int32 LinearIndex(int32 X, int32 Y, int32 Z) const;

    FVoxelDensitySnapshot TakeSnapshot(float IsoLevel) const;
    void ApplySnapshot(const FVoxelDensitySnapshot& Snapshot);

private:
    UPROPERTY(Transient)
    TArray<float> DensityGrid;
};
```

- [ ] **Step 2: VoxelDensityGrid.cpp 생성**

```cpp
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
```

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 3: UVoxelVisualizationManager 생성

시각화 모드 전환과 렌더링을 담당하는 컴포넌트. 렌더 프리미티브는 Actor에서 주입받음.

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelVisualizationManager.h`
- Create: `Source/RTPS/Private/VoxelAuthoring/VoxelVisualizationManager.cpp`

- [ ] **Step 1: VoxelVisualizationManager.h 생성**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelVisualizationManager.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class ULineBatchComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UVoxelDebugVisualizer;

UENUM(BlueprintType)
enum class EVoxelVisMode : uint8
{
    Mode1_WireframeVoxels UMETA(DisplayName = "1: Wireframe Voxels"),
    Mode2_DensitySpheres  UMETA(DisplayName = "2: Density Spheres"),
    Mode3_MarchingCubes   UMETA(DisplayName = "3: Marching Cubes"),
    Mode4_Interactive     UMETA(DisplayName = "4: Interactive (Spheres+MC)"),
};

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelVisualizationManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel|Vis")
    EVoxelVisMode CurrentVisMode = EVoxelVisMode::Mode1_WireframeVoxels;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|Vis")
    FString LastRebuildStatus;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|Vis")
    int32 FilledCellCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Vis")
    TObjectPtr<UMaterialInterface> MarchingCubeMaterial;

    // Called by ARuntimeAuthoringVolume after construction to inject render primitives.
    void InitializeRenderTargets(
        UHierarchicalInstancedStaticMeshComponent* InBlockHISM,
        UProceduralMeshComponent* InMarchingMesh,
        ULineBatchComponent* InWireframeLines,
        UVoxelDebugVisualizer* InDebugVisualizer);

    void HideAll();
    void ApplyMode(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);
    void AdjustSurfaceLevel(float Delta, TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float& InOutIsoLevel);

    void SetMode(EVoxelVisMode NewMode, TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);

private:
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BlockHISM;
    TObjectPtr<UProceduralMeshComponent> MarchingMesh;
    TObjectPtr<ULineBatchComponent> WireframeLines;
    TObjectPtr<UVoxelDebugVisualizer> DebugVisualizer;

    void ApplyMode1_Wireframe(FIntVector GridDimensions, float CellSize);
    void ApplyMode2_DensitySpheres(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize);
    void ApplyMode3_MarchingCubes(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);
    void ApplyMode4_Interactive(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel);
};
```

- [ ] **Step 2: VoxelVisualizationManager.cpp 생성**

기존 `RuntimeAuthoringVolume.cpp`에서 시각화 관련 메서드 본문을 이식. 시그니처를 새 파라미터 방식으로 조정:

```cpp
#include "VoxelAuthoring/VoxelVisualizationManager.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
#include "VoxelImport/RTPSMarchingCubes.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "ProceduralMeshComponent.h"

void UVoxelVisualizationManager::InitializeRenderTargets(
    UHierarchicalInstancedStaticMeshComponent* InBlockHISM,
    UProceduralMeshComponent* InMarchingMesh,
    ULineBatchComponent* InWireframeLines,
    UVoxelDebugVisualizer* InDebugVisualizer)
{
    BlockHISM = InBlockHISM;
    MarchingMesh = InMarchingMesh;
    WireframeLines = InWireframeLines;
    DebugVisualizer = InDebugVisualizer;
}

void UVoxelVisualizationManager::HideAll()
{
    if (WireframeLines) { WireframeLines->Flush(); }
    if (BlockHISM) { BlockHISM->SetVisibility(false); }
    if (MarchingMesh)
    {
        MarchingMesh->SetVisibility(false);
        MarchingMesh->ClearAllMeshSections();
    }
    if (DebugVisualizer) { DebugVisualizer->ClearVisualizer(); }
}

void UVoxelVisualizationManager::ApplyMode(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel)
{
    HideAll();
    ApplyMode1_Wireframe(GridDimensions, CellSize);

    switch (CurrentVisMode)
    {
    case EVoxelVisMode::Mode1_WireframeVoxels:
        break;
    case EVoxelVisMode::Mode2_DensitySpheres:
        ApplyMode2_DensitySpheres(Grid, GridDimensions, CellSize);
        break;
    case EVoxelVisMode::Mode3_MarchingCubes:
        ApplyMode3_MarchingCubes(Grid, GridDimensions, CellSize, IsoLevel);
        break;
    case EVoxelVisMode::Mode4_Interactive:
        ApplyMode4_Interactive(Grid, GridDimensions, CellSize, IsoLevel);
        break;
    }
}

void UVoxelVisualizationManager::SetMode(EVoxelVisMode NewMode, TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel)
{
    CurrentVisMode = NewMode;
    ApplyMode(Grid, GridDimensions, CellSize, IsoLevel);
}

void UVoxelVisualizationManager::AdjustSurfaceLevel(float Delta, TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float& InOutIsoLevel)
{
    InOutIsoLevel = FMath::Clamp(InOutIsoLevel + Delta, 0.01f, 0.99f);
    if (DebugVisualizer)
    {
        DebugVisualizer->Threshold = InOutIsoLevel;
        DebugVisualizer->ApplyThreshold();
    }
    if (CurrentVisMode == EVoxelVisMode::Mode3_MarchingCubes ||
        CurrentVisMode == EVoxelVisMode::Mode4_Interactive)
    {
        ApplyMode3_MarchingCubes(Grid, GridDimensions, CellSize, InOutIsoLevel);
    }

    LastRebuildStatus = FString::Printf(TEXT("IsoLevel adjusted to %.3f"), InOutIsoLevel);
}

void UVoxelVisualizationManager::ApplyMode1_Wireframe(FIntVector GridDimensions, float CellSize)
{
    if (!WireframeLines) { return; }

    WireframeLines->Flush();
    const AActor* Owner = GetOwner();
    if (!Owner) { return; }

    const FTransform ActorToWorld = Owner->GetActorTransform();
    auto W = [&](FVector Local) { return ActorToWorld.TransformPosition(Local); };

    const FVector Min(0.f, 0.f, 0.f);
    const FVector Max(
        static_cast<float>(GridDimensions.X) * CellSize,
        static_cast<float>(GridDimensions.Y) * CellSize,
        static_cast<float>(GridDimensions.Z) * CellSize);
    const FColor Color = FColor::Green;

    // Bottom face
    WireframeLines->DrawLine(W(FVector(Min.X, Min.Y, Min.Z)), W(FVector(Max.X, Min.Y, Min.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Max.X, Min.Y, Min.Z)), W(FVector(Max.X, Max.Y, Min.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Max.X, Max.Y, Min.Z)), W(FVector(Min.X, Max.Y, Min.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Min.X, Max.Y, Min.Z)), W(FVector(Min.X, Min.Y, Min.Z)), Color, SDPG_World);
    // Top face
    WireframeLines->DrawLine(W(FVector(Min.X, Min.Y, Max.Z)), W(FVector(Max.X, Min.Y, Max.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Max.X, Min.Y, Max.Z)), W(FVector(Max.X, Max.Y, Max.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Max.X, Max.Y, Max.Z)), W(FVector(Min.X, Max.Y, Max.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Min.X, Max.Y, Max.Z)), W(FVector(Min.X, Min.Y, Max.Z)), Color, SDPG_World);
    // Vertical edges
    WireframeLines->DrawLine(W(FVector(Min.X, Min.Y, Min.Z)), W(FVector(Min.X, Min.Y, Max.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Max.X, Min.Y, Min.Z)), W(FVector(Max.X, Min.Y, Max.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Max.X, Max.Y, Min.Z)), W(FVector(Max.X, Max.Y, Max.Z)), Color, SDPG_World);
    WireframeLines->DrawLine(W(FVector(Min.X, Max.Y, Min.Z)), W(FVector(Min.X, Max.Y, Max.Z)), Color, SDPG_World);
}

void UVoxelVisualizationManager::ApplyMode2_DensitySpheres(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize)
{
    if (!DebugVisualizer) { return; }
    DebugVisualizer->bVisualizerEnabled = true;
    DebugVisualizer->RebuildVisualizer(TArray<float>(Grid.GetData(), Grid.Num()), GridDimensions, CellSize);
}

void UVoxelVisualizationManager::ApplyMode3_MarchingCubes(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel)
{
    if (!MarchingMesh) { return; }

    const FIntVector SampleDim(GridDimensions.X + 1, GridDimensions.Y + 1, GridDimensions.Z + 1);
    TArray<float> LatticeDensity;
    LatticeDensity.SetNumZeroed(SampleDim.X * SampleDim.Y * SampleDim.Z);

    for (int32 SZ = 0; SZ < SampleDim.Z; ++SZ)
    for (int32 SY = 0; SY < SampleDim.Y; ++SY)
    for (int32 SX = 0; SX < SampleDim.X; ++SX)
    {
        float Sum = 0.f;
        int32 Count = 0;
        for (int32 DZ = -1; DZ <= 0; ++DZ)
        for (int32 DY = -1; DY <= 0; ++DY)
        for (int32 DX = -1; DX <= 0; ++DX)
        {
            const int32 CX = SX + DX;
            const int32 CY = SY + DY;
            const int32 CZ = SZ + DZ;
            if (CX >= 0 && CX < GridDimensions.X &&
                CY >= 0 && CY < GridDimensions.Y &&
                CZ >= 0 && CZ < GridDimensions.Z)
            {
                Sum += Grid[CX + GridDimensions.X * (CY + GridDimensions.Y * CZ)];
                ++Count;
            }
        }
        LatticeDensity[SX + SampleDim.X * (SY + SampleDim.Y * SZ)] =
            (Count > 0) ? (Sum / static_cast<float>(Count)) : 0.f;
    }

    RTPSVoxelImport::FMarchingCubesMeshData MeshData;
    const bool bBuilt = RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
        GridDimensions, FIntVector::ZeroValue, CellSize, IsoLevel,
        TArrayView<const float>(LatticeDensity), MeshData);

    MarchingMesh->ClearAllMeshSections();
    if (bBuilt)
    {
        MarchingMesh->CreateMeshSection(0,
            MeshData.Vertices, MeshData.Triangles, MeshData.Normals,
            MeshData.UV0, MeshData.VertexColors, MeshData.Tangents, true);
        if (MarchingCubeMaterial)
        {
            MarchingMesh->SetMaterial(0, MarchingCubeMaterial);
        }
        MarchingMesh->SetVisibility(true);
        FilledCellCount = MeshData.GetTriangleCount();
    }
    else
    {
        MarchingMesh->SetVisibility(false);
        FilledCellCount = 0;
    }

    LastRebuildStatus = FString::Printf(
        TEXT("Mode3_MC Dim=%dx%dx%d Tris=%d IsoLevel=%.2f"),
        GridDimensions.X, GridDimensions.Y, GridDimensions.Z, FilledCellCount, IsoLevel);
}

void UVoxelVisualizationManager::ApplyMode4_Interactive(TArrayView<const float> Grid, FIntVector GridDimensions, float CellSize, float IsoLevel)
{
    ApplyMode2_DensitySpheres(Grid, GridDimensions, CellSize);
    ApplyMode3_MarchingCubes(Grid, GridDimensions, CellSize, IsoLevel);
}
```

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 4: UVoxelAuthoringPersistence 생성

JSON 저장/불러오기 담당 컴포넌트.

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelAuthoringPersistence.h`
- Create: `Source/RTPS/Private/VoxelAuthoring/VoxelAuthoringPersistence.cpp`

- [ ] **Step 1: VoxelAuthoringPersistence.h 생성**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelAuthoring/VoxelDensitySnapshot.h"
#include "VoxelAuthoringPersistence.generated.h"

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelAuthoringPersistence : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|IO")
    FString LastIOStatus;

    // Returns true on success. Writes status to LastIOStatus.
    bool ExportToFile(const FVoxelDensitySnapshot& Snapshot, const FString& FilePath);

    // Returns true on success. OutSnapshot is valid only on success.
    bool ImportFromFile(const FString& FilePath, FVoxelDensitySnapshot& OutSnapshot);
};
```

- [ ] **Step 2: VoxelAuthoringPersistence.cpp 생성**

기존 `RuntimeAuthoringVolume.cpp`의 `ExportDensityToFile()`·`ImportDensityFromFile()` 본문을 이식하여 Snapshot 파라미터 방식으로 조정:

```cpp
#include "VoxelAuthoring/VoxelAuthoringPersistence.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool UVoxelAuthoringPersistence::ExportToFile(const FVoxelDensitySnapshot& Snapshot, const FString& FilePath)
{
    FString ResolvedPath = FilePath;
    if (FPaths::IsRelative(ResolvedPath))
    {
        ResolvedPath = FPaths::Combine(FPaths::ProjectDir(), ResolvedPath);
    }
    FPaths::NormalizeFilename(ResolvedPath);

    const FString DirPath = FPaths::GetPath(ResolvedPath);
    if (!DirPath.IsEmpty())
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirPath);
    }

    const FIntVector& Dims = Snapshot.Dimensions;
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("schema"), TEXT("rtps.authoring.density.v1"));
    Root->SetNumberField(TEXT("gridX"), static_cast<double>(Dims.X));
    Root->SetNumberField(TEXT("gridY"), static_cast<double>(Dims.Y));
    Root->SetNumberField(TEXT("gridZ"), static_cast<double>(Dims.Z));
    Root->SetNumberField(TEXT("cellSize"), static_cast<double>(Snapshot.CellSize));
    Root->SetNumberField(TEXT("isoLevel"), static_cast<double>(Snapshot.IsoLevel));

    TArray<TSharedPtr<FJsonValue>> CellArray;
    for (int32 Z = 0; Z < Dims.Z; ++Z)
    for (int32 Y = 0; Y < Dims.Y; ++Y)
    for (int32 X = 0; X < Dims.X; ++X)
    {
        const float D = Snapshot.Grid[X + Dims.X * (Y + Dims.Y * Z)];
        if (D == 0.f) { continue; }
        TSharedRef<FJsonObject> Cell = MakeShared<FJsonObject>();
        Cell->SetNumberField(TEXT("x"), static_cast<double>(X));
        Cell->SetNumberField(TEXT("y"), static_cast<double>(Y));
        Cell->SetNumberField(TEXT("z"), static_cast<double>(Z));
        Cell->SetNumberField(TEXT("d"), static_cast<double>(D));
        CellArray.Add(MakeShared<FJsonValueObject>(Cell));
    }
    Root->SetArrayField(TEXT("nonZeroCells"), CellArray);

    FString OutputString;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&OutputString));

    if (!FFileHelper::SaveStringToFile(OutputString, *ResolvedPath))
    {
        LastIOStatus = FString::Printf(TEXT("Export failed: could not write to %s"), *ResolvedPath);
        return false;
    }

    LastIOStatus = FString::Printf(TEXT("Exported %d non-zero cells to %s"), CellArray.Num(), *ResolvedPath);
    return true;
}

bool UVoxelAuthoringPersistence::ImportFromFile(const FString& FilePath, FVoxelDensitySnapshot& OutSnapshot)
{
    FString ResolvedPath = FilePath;
    if (FPaths::IsRelative(ResolvedPath))
    {
        ResolvedPath = FPaths::Combine(FPaths::ProjectDir(), ResolvedPath);
    }
    FPaths::NormalizeFilename(ResolvedPath);

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *ResolvedPath))
    {
        LastIOStatus = FString::Printf(TEXT("Import failed: could not read %s"), *ResolvedPath);
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
    {
        LastIOStatus = FString::Printf(TEXT("Import failed: JSON parse error in %s"), *ResolvedPath);
        return false;
    }

    FString Schema;
    Root->TryGetStringField(TEXT("schema"), Schema);
    if (Schema != TEXT("rtps.authoring.density.v1"))
    {
        LastIOStatus = FString::Printf(TEXT("Import failed: unknown schema '%s'"), *Schema);
        return false;
    }

    double DX = 0, DY = 0, DZ = 0, NewCellSize = 100.0, NewIsoLevel = 0.5;
    Root->TryGetNumberField(TEXT("gridX"), DX);
    Root->TryGetNumberField(TEXT("gridY"), DY);
    Root->TryGetNumberField(TEXT("gridZ"), DZ);
    Root->TryGetNumberField(TEXT("cellSize"), NewCellSize);
    Root->TryGetNumberField(TEXT("isoLevel"), NewIsoLevel);

    const int32 NX = FMath::RoundToInt(DX);
    const int32 NY = FMath::RoundToInt(DY);
    const int32 NZ = FMath::RoundToInt(DZ);
    if (NX <= 0 || NY <= 0 || NZ <= 0)
    {
        LastIOStatus = FString::Printf(TEXT("Import failed: invalid dimensions %dx%dx%d"), NX, NY, NZ);
        return false;
    }

    OutSnapshot.Dimensions = FIntVector(NX, NY, NZ);
    OutSnapshot.CellSize = static_cast<float>(NewCellSize);
    OutSnapshot.IsoLevel = static_cast<float>(NewIsoLevel);
    OutSnapshot.Grid.SetNumZeroed(NX * NY * NZ);

    int32 LoadedCount = 0;
    const TArray<TSharedPtr<FJsonValue>>* CellArray = nullptr;
    if (Root->TryGetArrayField(TEXT("nonZeroCells"), CellArray) && CellArray)
    {
        for (const TSharedPtr<FJsonValue>& Val : *CellArray)
        {
            const TSharedPtr<FJsonObject> Cell = Val.IsValid() ? Val->AsObject() : nullptr;
            if (!Cell.IsValid()) { continue; }
            double CX = 0, CY = 0, CZ = 0, CD = 0;
            if (!Cell->TryGetNumberField(TEXT("x"), CX) ||
                !Cell->TryGetNumberField(TEXT("y"), CY) ||
                !Cell->TryGetNumberField(TEXT("z"), CZ) ||
                !Cell->TryGetNumberField(TEXT("d"), CD)) { continue; }
            const int32 IX = FMath::RoundToInt(CX);
            const int32 IY = FMath::RoundToInt(CY);
            const int32 IZ = FMath::RoundToInt(CZ);
            if (IX < 0 || IX >= NX || IY < 0 || IY >= NY || IZ < 0 || IZ >= NZ) { continue; }
            OutSnapshot.Grid[IX + NX * (IY + NY * IZ)] = static_cast<float>(CD);
            ++LoadedCount;
        }
    }

    LastIOStatus = FString::Printf(
        TEXT("Imported %d non-zero cells from %s (Dim=%dx%dx%d)"),
        LoadedCount, *ResolvedPath, NX, NY, NZ);
    return true;
}
```

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 5: RuntimeAuthoringVolume 리팩토링

Actor를 얇은 오케스트레이터로 교체. 기존 로직은 Task 2~4에서 생성한 컴포넌트로 이미 이식됨.

**Files:**
- Modify: `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h`
- Modify: `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp`

- [ ] **Step 1: RuntimeAuthoringVolume.h 교체**

> 기존 `EVoxelVisMode` enum은 `VoxelVisualizationManager.h`로 이동했으므로 헤더에서 제거.

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelAuthoring/VoxelNoiseParams.h"
#include "VoxelAuthoring/VoxelVisualizationManager.h"
#include "RuntimeAuthoringVolume.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class ULineBatchComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UVoxelDebugVisualizer;
class UVoxelDensityGrid;
class UVoxelVisualizationManager;
class UVoxelAuthoringPersistence;

RTPS_API DECLARE_LOG_CATEGORY_EXTERN(LogRTPSVoxelAuthoring, Log, All);

UCLASS(BlueprintType, Blueprintable)
class RTPS_API ARuntimeAuthoringVolume : public AActor
{
    GENERATED_BODY()

public:
    ARuntimeAuthoringVolume();
    virtual void BeginPlay() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Authoring", meta = (ClampMin = "0.01", ClampMax = "0.99"))
    float IsoLevel = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Noise")
    FVoxelNoiseParams NoiseParams;

    UPROPERTY(EditAnywhere, Category = "Authoring|Debug")
    FVector DebugSphereCenter = FVector(800.f, 800.f, 800.f);

    UPROPERTY(EditAnywhere, Category = "Authoring|Debug", meta = (ClampMin = "1.0"))
    float DebugSphereRadius = 500.f;

    UPROPERTY(EditAnywhere, Category = "Authoring|Debug", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DebugSphereValue = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Authoring|IO")
    FString DensityFilePath = TEXT("Saved/AuthoringData/density.json");

    // Components
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
    TObjectPtr<UVoxelDensityGrid> DensityGridComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
    TObjectPtr<UVoxelVisualizationManager> VisManagerComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring")
    TObjectPtr<UVoxelAuthoringPersistence> PersistenceComp;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring|Debug")
    TObjectPtr<UVoxelDebugVisualizer> DebugVisualizer;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring")
    void ApplyCurrentVisMode();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring")
    void HideAllVisualizations();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|Debug")
    void ApplyDebugSphereFill();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|NoiseFill")
    void FillNoiseDensity();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|Debug")
    void ClearDensity();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|IO")
    void ExportDensityToFile();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|IO")
    void ImportDensityFromFile();

    UFUNCTION(BlueprintCallable, Category = "Authoring|Debug")
    void FillSphereDensity(FVector LocalCenter, float Radius, float Value);

    void RebuildAuthoringMesh();
    void ClearAuthoringMesh();
    void RebuildDebugVisualizer();
    void ApplyDebugVisualizerThreshold();
    void ClearDebugVisualizer();

protected:
    UPROPERTY(VisibleAnywhere, Category = "Authoring")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Authoring")
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BlockHISM;

    UPROPERTY(VisibleAnywhere, Category = "Authoring")
    TObjectPtr<UProceduralMeshComponent> MarchingMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Authoring")
    TObjectPtr<UMaterialInterface> MarchingCubeMaterial;

    UPROPERTY(VisibleAnywhere, Category = "Authoring")
    TObjectPtr<ULineBatchComponent> WireframeLines;

private:
    void SetMode1();
    void SetMode2();
    void SetMode3();
    void SetMode4();
    void IncreaseSurfaceLevel();
    void DecreaseSurfaceLevel();
};
```

- [ ] **Step 2: RuntimeAuthoringVolume.cpp 교체**

```cpp
#include "VoxelAuthoring/RuntimeAuthoringVolume.h"
#include "VoxelAuthoring/VoxelDensityGrid.h"
#include "VoxelAuthoring/VoxelVisualizationManager.h"
#include "VoxelAuthoring/VoxelAuthoringPersistence.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/LineBatchComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogRTPSVoxelAuthoring);

ARuntimeAuthoringVolume::ARuntimeAuthoringVolume()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    BlockHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BlockHISM"));
    BlockHISM->SetupAttachment(SceneRoot);
    BlockHISM->SetMobility(EComponentMobility::Movable);
    BlockHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BlockHISM->SetCastShadow(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshRef(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMeshRef.Succeeded()) { BlockHISM->SetStaticMesh(CubeMeshRef.Object); }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialRef(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BasicMaterialRef.Succeeded()) { BlockHISM->SetMaterial(0, BasicMaterialRef.Object); }

    MarchingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MarchingMesh"));
    MarchingMesh->SetupAttachment(SceneRoot);
    MarchingMesh->SetMobility(EComponentMobility::Movable);
    MarchingMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MarchingMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    MarchingMesh->bUseAsyncCooking = true;
    MarchingMesh->SetVisibility(false);

    WireframeLines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("WireframeLines"));
    WireframeLines->SetupAttachment(SceneRoot);
    WireframeLines->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    DebugVisualizer = CreateDefaultSubobject<UVoxelDebugVisualizer>(TEXT("DebugVisualizer"));

    DensityGridComp = CreateDefaultSubobject<UVoxelDensityGrid>(TEXT("DensityGrid"));
    VisManagerComp = CreateDefaultSubobject<UVoxelVisualizationManager>(TEXT("VisManager"));
    PersistenceComp = CreateDefaultSubobject<UVoxelAuthoringPersistence>(TEXT("Persistence"));
}

void ARuntimeAuthoringVolume::BeginPlay()
{
    Super::BeginPlay();

    VisManagerComp->InitializeRenderTargets(BlockHISM, MarchingMesh, WireframeLines, DebugVisualizer);

    if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        EnableInput(PC);
        if (InputComponent)
        {
            InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode1);
            InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode2);
            InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode3);
            InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode4);
            InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ARuntimeAuthoringVolume::DecreaseSurfaceLevel);
            InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ARuntimeAuthoringVolume::IncreaseSurfaceLevel);
        }
    }
}

#if WITH_EDITOR
void ARuntimeAuthoringVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropName = PropertyChangedEvent.GetPropertyName();

    if (PropName == GET_MEMBER_NAME_CHECKED(ARuntimeAuthoringVolume, IsoLevel))
    {
        if (DebugVisualizer)
        {
            DebugVisualizer->Threshold = IsoLevel;
            DebugVisualizer->ApplyThreshold();
        }
        ApplyCurrentVisMode();
        return;
    }

    static const FName NoiseParamNames[] = {
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseScale),
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseWeight),
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseOctaves),
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseLacunarity),
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoisePersistence),
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseFloorOffset),
        GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseSeed),
    };

    for (const FName& NoiseProp : NoiseParamNames)
    {
        if (PropName == NoiseProp)
        {
            FillNoiseDensity();
            ApplyCurrentVisMode();
            return;
        }
    }
}
#endif

void ARuntimeAuthoringVolume::ApplyCurrentVisMode()
{
    if (DensityGridComp && VisManagerComp)
    {
        VisManagerComp->ApplyMode(
            DensityGridComp->GetGrid(),
            DensityGridComp->GridDimensions,
            DensityGridComp->CellSize,
            IsoLevel);
    }
}

void ARuntimeAuthoringVolume::HideAllVisualizations()
{
    if (VisManagerComp) { VisManagerComp->HideAll(); }
}

void ARuntimeAuthoringVolume::FillNoiseDensity()
{
    if (DensityGridComp)
    {
        DensityGridComp->FillNoise(NoiseParams);
        UE_LOG(LogRTPSVoxelAuthoring, Log,
            TEXT("FillNoiseDensity: Dim=%dx%dx%d Scale=%.4f Seed=%d"),
            DensityGridComp->GridDimensions.X,
            DensityGridComp->GridDimensions.Y,
            DensityGridComp->GridDimensions.Z,
            NoiseParams.NoiseScale,
            NoiseParams.NoiseSeed);
    }
}

void ARuntimeAuthoringVolume::ClearDensity()
{
    if (DensityGridComp) { DensityGridComp->Clear(); }
}

void ARuntimeAuthoringVolume::ApplyDebugSphereFill()
{
    FillSphereDensity(DebugSphereCenter, DebugSphereRadius, DebugSphereValue);
}

void ARuntimeAuthoringVolume::FillSphereDensity(FVector LocalCenter, float Radius, float Value)
{
    if (DensityGridComp) { DensityGridComp->FillSphere(LocalCenter, Radius, Value); }
}

void ARuntimeAuthoringVolume::ExportDensityToFile()
{
    if (!DensityGridComp || !PersistenceComp) { return; }
    const FVoxelDensitySnapshot Snap = DensityGridComp->TakeSnapshot(IsoLevel);
    PersistenceComp->ExportToFile(Snap, DensityFilePath);
}

void ARuntimeAuthoringVolume::ImportDensityFromFile()
{
    if (!DensityGridComp || !PersistenceComp) { return; }
    FVoxelDensitySnapshot Snap;
    if (PersistenceComp->ImportFromFile(DensityFilePath, Snap))
    {
        IsoLevel = Snap.IsoLevel;
        DensityGridComp->ApplySnapshot(Snap);
    }
}

void ARuntimeAuthoringVolume::RebuildAuthoringMesh() { ApplyCurrentVisMode(); }

void ARuntimeAuthoringVolume::ClearAuthoringMesh()
{
    HideAllVisualizations();
    if (BlockHISM) { BlockHISM->ClearInstances(); }
}

void ARuntimeAuthoringVolume::RebuildDebugVisualizer()
{
    if (DensityGridComp && VisManagerComp)
    {
        VisManagerComp->HideAll();
        VisManagerComp->ApplyMode(
            DensityGridComp->GetGrid(),
            DensityGridComp->GridDimensions,
            DensityGridComp->CellSize,
            IsoLevel);
    }
}

void ARuntimeAuthoringVolume::ApplyDebugVisualizerThreshold()
{
    if (DebugVisualizer)
    {
        DebugVisualizer->Threshold = IsoLevel;
        DebugVisualizer->ApplyThreshold();
    }
}

void ARuntimeAuthoringVolume::ClearDebugVisualizer()
{
    if (DebugVisualizer) { DebugVisualizer->ClearVisualizer(); }
}

void ARuntimeAuthoringVolume::SetMode1()
{
    if (DensityGridComp && VisManagerComp)
        VisManagerComp->SetMode(EVoxelVisMode::Mode1_WireframeVoxels, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::SetMode2()
{
    if (DensityGridComp && VisManagerComp)
        VisManagerComp->SetMode(EVoxelVisMode::Mode2_DensitySpheres, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::SetMode3()
{
    if (DensityGridComp && VisManagerComp)
        VisManagerComp->SetMode(EVoxelVisMode::Mode3_MarchingCubes, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::SetMode4()
{
    if (DensityGridComp && VisManagerComp)
        VisManagerComp->SetMode(EVoxelVisMode::Mode4_Interactive, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::IncreaseSurfaceLevel()
{
    if (DensityGridComp && VisManagerComp)
        VisManagerComp->AdjustSurfaceLevel(+0.05f, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::DecreaseSurfaceLevel()
{
    if (DensityGridComp && VisManagerComp)
        VisManagerComp->AdjustSurfaceLevel(-0.05f, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}
```

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

- [ ] **Step 4: 에디터 확인**

UE5 에디터 실행 → VoxelTestMap 열기 → `RuntimeAuthoringVolume` 선택:
- 디테일 패널에 `DensityGrid`, `VisManager`, `Persistence` 컴포넌트 표시 확인
- `FillNoiseDensity` → `ApplyCurrentVisMode` 순서로 실행해서 노이즈 지형 생성 확인
- 1/2/3/4 키로 시각화 모드 전환 확인
- `ExportDensityToFile` → `ImportDensityFromFile` 왕복 확인

- [ ] **Step 5: Perforce 체크인**

설명: `refactor: Phase3 - split RuntimeAuthoringVolume into UVoxelDensityGrid, UVoxelVisualizationManager, UVoxelAuthoringPersistence`

---

## 완료 기준

- [ ] 모든 Task 빌드 통과
- [ ] `RuntimeAuthoringVolume.cpp` 에서 JSON, 렌더링, 밀도 조작 로직이 제거됨
- [ ] 에디터에서 노이즈 생성 + 시각화 모드 전환 + Export/Import 정상 동작
- [ ] 키보드 1/2/3/4, `[`, `]` 입력 정상 동작
