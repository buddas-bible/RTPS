# Voxel Terrain Runtime Editor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Marching Cubes 기반 런타임 지형 편집 시스템을 단계적으로 구축 — 먼저 밀도 기반 MC + 디버그 시각화(구 메시 오버레이)를 완성한다.

**Architecture:** `RuntimeAuthoringVolume`의 `DensityGrid(float[])` 를 직접 받는 MC 오버로드를 추가하고, `UProceduralMeshComponent`로 F2 렌더링을 구현한다. `UVoxelDebugVisualizer` 컴포넌트가 HISM PerInstanceCustomData로 각 격자점 밀도 값을 실시간 구 메시로 표시하며 Threshold 슬라이더로 경계를 확인한다.

**Tech Stack:** UE 5.5.4 C++, ProceduralMeshComponent, HISM (HierarchicalInstancedStaticMeshComponent), UE Automation Test Framework, Unreal Material Editor (사용자 셰이더 작업)

---

## 파일 맵

| 역할 | 파일 경로 | 변경 종류 |
|---|---|---|
| MC 알고리즘 헤더 | `Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.h` | Modify |
| MC 알고리즘 구현 | `Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.cpp` | Modify |
| AuthoringVolume 헤더 | `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h` | Modify |
| AuthoringVolume 구현 | `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp` | Modify |
| DebugVisualizer 헤더 | `Source/RTPS/Public/VoxelAuthoring/VoxelDebugVisualizer.h` | Create |
| DebugVisualizer 구현 | `Source/RTPS/Private/VoxelAuthoring/VoxelDebugVisualizer.cpp` | Create |
| 자동화 테스트 | `Source/RTPS/Private/Tests/RTPSVoxelImportAutomation.cpp` | Modify |

---

## Task 1: float density 배열 기반 MC 오버로드 추가

**Files:**
- Modify: `Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.h`
- Modify: `Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.cpp`
- Modify: `Source/RTPS/Private/Tests/RTPSVoxelImportAutomation.cpp`

### 배경

기존 `BuildMarchingCubesChunkMesh`는 `TSet<FIntVector>` 이진 점유를 받는다.
`RuntimeAuthoringVolume`은 이미 `float[] DensityGrid`를 가지므로 float 배열 직접 입력 오버로드가 필요하다.
기존 함수는 그대로 유지한다.

- [ ] **Step 1: 테스트 코드 작성 (RTPSVoxelImportAutomation.cpp)**

`#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR` 블록 내부, 기존 테스트들 아래에 추가:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRTPSDensityMCBasicTest,
    "RTPS.VoxelAuthoring.DensityMC.BasicOutput",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FRTPSDensityMCBasicTest::RunTest(const FString& Parameters)
{
    // 2x2x2 청크 → 3x3x3 = 27개 샘플 필요
    const FIntVector Dim(2, 2, 2);
    TArray<float> DensityData;
    DensityData.SetNumZeroed(3 * 3 * 3);

    // (1,1,1) 샘플만 solid(1.0) → 경계면 생성 기대
    DensityData[1 + 3 * (1 + 3 * 1)] = 1.f;

    RTPSVoxelImport::FMarchingCubesMeshData MeshData;
    const bool bBuilt = RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
        Dim, FIntVector::ZeroValue, 100.f, 0.5f,
        TArrayView<const float>(DensityData), MeshData);

    TestTrue(TEXT("Density MC: 단일 솔리드 샘플로 메시 생성"), bBuilt);
    TestTrue(TEXT("Density MC: 버텍스 존재"), MeshData.Vertices.Num() > 0);
    TestEqual(TEXT("Density MC: Triangles는 3의 배수"), MeshData.Triangles.Num() % 3, 0);
    TestEqual(TEXT("Density MC: Vertices == Normals 길이"), MeshData.Vertices.Num(), MeshData.Normals.Num());
    TestEqual(TEXT("Density MC: Vertices == UV0 길이"), MeshData.Vertices.Num(), MeshData.UV0.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRTPSDensityMCEmptyTest,
    "RTPS.VoxelAuthoring.DensityMC.EmptyGrid",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FRTPSDensityMCEmptyTest::RunTest(const FString& Parameters)
{
    // 완전히 비어있는 그리드 → 메시 없음
    const FIntVector Dim(4, 4, 4);
    TArray<float> DensityData;
    DensityData.SetNumZeroed(5 * 5 * 5);

    RTPSVoxelImport::FMarchingCubesMeshData MeshData;
    const bool bBuilt = RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
        Dim, FIntVector::ZeroValue, 100.f, 0.5f,
        TArrayView<const float>(DensityData), MeshData);

    TestFalse(TEXT("Density MC: 빈 그리드는 false 반환"), bBuilt);
    TestEqual(TEXT("Density MC: 빈 그리드 버텍스 없음"), MeshData.Vertices.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRTPSDensityMCFullTest,
    "RTPS.VoxelAuthoring.DensityMC.FullySolidGrid",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FRTPSDensityMCFullTest::RunTest(const FString& Parameters)
{
    // 완전히 채워진 그리드 → MC 내부만 → 메시 없음
    const FIntVector Dim(2, 2, 2);
    TArray<float> DensityData;
    DensityData.Init(1.f, 3 * 3 * 3);

    RTPSVoxelImport::FMarchingCubesMeshData MeshData;
    const bool bBuilt = RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
        Dim, FIntVector::ZeroValue, 100.f, 0.5f,
        TArrayView<const float>(DensityData), MeshData);

    TestFalse(TEXT("Density MC: 완전 채워진 그리드는 false 반환"), bBuilt);
    return true;
}
```

- [ ] **Step 2: 테스트 실행하여 컴파일 에러 확인**

UE 에디터 → Session Frontend → Automation 탭에서 다음을 실행:
```
RTPS.VoxelAuthoring.DensityMC.*
```
예상 결과: `BuildMarchingCubesChunkMeshDensity` 미정의 컴파일 에러

- [ ] **Step 3: 헤더에 오버로드 선언 추가**

`Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.h` 의 `BuildMarchingCubesChunkMesh` 선언 아래에 추가:

```cpp
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
```

- [ ] **Step 4: 구현 추가 (RTPSMarchingCubes.cpp)**

파일 끝 (`return OutMeshData.Triangles.Num() > 0;` 이후) 에 아래 함수 전체를 추가:

```cpp
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

    for (int32 Z = 0; Z < ChunkDimensions.Z; ++Z)
    {
        for (int32 Y = 0; Y < ChunkDimensions.Y; ++Y)
        {
            for (int32 X = 0; X < ChunkDimensions.X; ++X)
            {
                float CornerDensity[8];
                FVector CornerPosition[8];

                for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
                {
                    const int32 CX = X + CornerOffsets[CornerIndex][0];
                    const int32 CY = Y + CornerOffsets[CornerIndex][1];
                    const int32 CZ = Z + CornerOffsets[CornerIndex][2];
                    CornerDensity[CornerIndex] = DensityData[SampleIndex(SampleDimensions, CX, CY, CZ)];
                    CornerPosition[CornerIndex] = GridVertexToWorldCm(
                        ChunkOriginGrid + FIntVector(CX, CY, CZ), VoxelSizeCm);
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
                }

                for (int32 TableIndex = 0;
                     RTPSVoxelImport::MarchingCubesTables::triTable[CubeIndex][TableIndex] != -1;
                     TableIndex += 3)
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

                    FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
                    if (TriangleNormal.IsNearlyZero())
                    {
                        TriangleNormal = FVector::UpVector;
                    }

                    OutMeshData.Normals.Add(TriangleNormal);
                    OutMeshData.Normals.Add(TriangleNormal);
                    OutMeshData.Normals.Add(TriangleNormal);

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
    }

    return OutMeshData.Triangles.Num() > 0;
}
```

> **주의:** 기존 `BuildMarchingCubesChunkMesh`의 `CubeIndex` 계산은 `density < IsoLevel`이면 비트 세트 (비어있음=1).
> 신규 함수는 **`density >= IsoLevel`이면 비트 세트** (채워짐=1) — RuntimeAuthoringVolume의 density 의미(1=solid)와 일치.

- [ ] **Step 5: 프로젝트 빌드 (컴파일 확인)**

```
도구 → Visual Studio에서 열기 → 빌드 → RTPS 솔루션 빌드
```
또는 터미널에서:
```powershell
& "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" `
  RTPS Win64 Development `
  -project="E:\perforce\Project\RTPS\RTPS.uproject" -waitmutex
```
예상: 빌드 성공, 경고 없음

- [ ] **Step 6: 테스트 실행하여 통과 확인**

UE 에디터 → Session Frontend → Automation → `RTPS.VoxelAuthoring.DensityMC.*`  
예상: 3개 테스트 모두 PASS

- [ ] **Step 7: 커밋**

```bash
git add Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.h
git add Source/RTPS/Private/VoxelImport/RTPSMarchingCubes.cpp
git add Source/RTPS/Private/Tests/RTPSVoxelImportAutomation.cpp
git commit -m "feat(voxel): add float density array overload for MarchingCubes"
```

---

## Task 2: RuntimeAuthoringVolume — F2 MarchingCubes 렌더링 구현

**Files:**
- Modify: `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h`
- Modify: `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp`

### 배경

F2_MarchingCubes 는 현재 "Reserved" 상태. `DensityGrid`(float[])를 Task 1의 새 함수로 넘겨 `UProceduralMeshComponent`에 적용한다.

- [ ] **Step 1: 헤더에 ProceduralMeshComponent 추가**

`RuntimeAuthoringVolume.h` 의 private 멤버 섹션에서:

```cpp
// 기존
UPROPERTY(VisibleAnywhere, Category = "Authoring")
TObjectPtr<class UHierarchicalInstancedStaticMeshComponent> BlockHISM;

// 아래에 추가
UPROPERTY(VisibleAnywhere, Category = "Authoring")
TObjectPtr<class UProceduralMeshComponent> MarchingMesh;
```

헤더 상단 forward declaration 추가 (기존 `UHierarchicalInstancedStaticMeshComponent` 아래):
```cpp
class UProceduralMeshComponent;
```

private 함수 섹션에 추가:
```cpp
void RebuildF2_MarchingCubes();
```

- [ ] **Step 2: 생성자에서 ProceduralMeshComponent 초기화**

`RuntimeAuthoringVolume.cpp` 의 생성자(`ARuntimeAuthoringVolume::ARuntimeAuthoringVolume()`) 안, `BlockHISM` 초기화 블록 아래에 추가:

```cpp
#include "ProceduralMeshComponent.h"  // 파일 상단 include에도 추가
```

생성자 내:
```cpp
MarchingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MarchingMesh"));
MarchingMesh->SetupAttachment(SceneRoot);
MarchingMesh->SetMobility(EComponentMobility::Movable);
MarchingMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
MarchingMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
MarchingMesh->bUseAsyncCooking = true;
MarchingMesh->SetVisibility(false); // F1 기본값이므로 처음엔 숨김
```

- [ ] **Step 3: RebuildAuthoringMesh에서 F2 분기 연결**

`RebuildAuthoringMesh()` 함수를:

```cpp
void ARuntimeAuthoringVolume::RebuildAuthoringMesh()
{
    if (Representation == ERTPSAuthoringRepresentation::F1_Block)
    {
        MarchingMesh->SetVisibility(false);
        RebuildF1_Block();
        return;
    }

    if (Representation == ERTPSAuthoringRepresentation::F2_MarchingCubes_Reserved)
    {
        BlockHISM->SetVisibility(false);
        RebuildF2_MarchingCubes();
        return;
    }

    // F3 등 미구현 케이스
    const TCHAR* RepName = TEXT("F3_MarchingCubes");
    LastRebuildStatus = FString::Printf(TEXT("%s deferred - not implemented in this slice"), RepName);
    UE_LOG(LogRTPSVoxelAuthoring, Warning, TEXT("RebuildAuthoringMesh: %s"), *LastRebuildStatus);
}
```

- [ ] **Step 4: RebuildF2_MarchingCubes 구현 추가**

`RuntimeAuthoringVolume.cpp` 하단에 추가. `#include "VoxelImport/RTPSMarchingCubes.h"` 를 파일 상단에도 추가:

```cpp
void ARuntimeAuthoringVolume::RebuildF2_MarchingCubes()
{
    if (!MarchingMesh)
    {
        LastRebuildStatus = TEXT("F2_MarchingCubes: MarchingMesh component is null");
        UE_LOG(LogRTPSVoxelAuthoring, Error, TEXT("%s"), *LastRebuildStatus);
        return;
    }

    EnsureGridAllocated();

    const FIntVector SampleDim(
        GridDimensions.X + 1,
        GridDimensions.Y + 1,
        GridDimensions.Z + 1);
    const int32 ExpectedSamples = SampleDim.X * SampleDim.Y * SampleDim.Z;

    // DensityGrid는 GridDimensions.X*Y*Z 크기 — MC는 (Dim+1)³ 샘플 필요.
    // 보간 없이 각 샘플 위치에서 인접 셀 밀도 평균으로 래티스 밀도 구성.
    TArray<float> LatticeDensity;
    LatticeDensity.SetNumZeroed(ExpectedSamples);

    for (int32 SZ = 0; SZ < SampleDim.Z; ++SZ)
    {
        for (int32 SY = 0; SY < SampleDim.Y; ++SY)
        {
            for (int32 SX = 0; SX < SampleDim.X; ++SX)
            {
                float Sum = 0.f;
                int32 Count = 0;
                for (int32 DZ = -1; DZ <= 0; ++DZ)
                {
                    for (int32 DY = -1; DY <= 0; ++DY)
                    {
                        for (int32 DX = -1; DX <= 0; ++DX)
                        {
                            const int32 CX = SX + DX;
                            const int32 CY = SY + DY;
                            const int32 CZ = SZ + DZ;
                            if (CX >= 0 && CX < GridDimensions.X &&
                                CY >= 0 && CY < GridDimensions.Y &&
                                CZ >= 0 && CZ < GridDimensions.Z)
                            {
                                Sum += DensityGrid[LinearIndex(CX, CY, CZ)];
                                ++Count;
                            }
                        }
                    }
                }
                const int32 LatticeIdx = SX + SampleDim.X * (SY + SampleDim.Y * SZ);
                LatticeDensity[LatticeIdx] = (Count > 0) ? (Sum / static_cast<float>(Count)) : 0.f;
            }
        }
    }

    RTPSVoxelImport::FMarchingCubesMeshData MeshData;
    const bool bBuilt = RTPSVoxelImport::BuildMarchingCubesChunkMeshDensity(
        GridDimensions,
        FIntVector::ZeroValue,
        CellSize,
        IsoLevel,
        TArrayView<const float>(LatticeDensity),
        MeshData);

    MarchingMesh->ClearAllMeshSections();

    if (bBuilt)
    {
        MarchingMesh->CreateMeshSection(
            0,
            MeshData.Vertices,
            MeshData.Triangles,
            MeshData.Normals,
            MeshData.UV0,
            MeshData.VertexColors,
            MeshData.Tangents,
            /*bCreateCollision=*/true);
        MarchingMesh->SetVisibility(true);
        FilledCellCount = MeshData.GetTriangleCount();
    }
    else
    {
        MarchingMesh->SetVisibility(false);
        FilledCellCount = 0;
    }

    LastRebuildStatus = FString::Printf(
        TEXT("F2_MarchingCubes Dim=%dx%dx%d Tris=%d IsoLevel=%.2f"),
        GridDimensions.X, GridDimensions.Y, GridDimensions.Z,
        FilledCellCount, IsoLevel);
    UE_LOG(LogRTPSVoxelAuthoring, Log, TEXT("RebuildAuthoringMesh: %s"), *LastRebuildStatus);
}
```

- [ ] **Step 5: ClearAuthoringMesh에서 MarchingMesh도 클리어**

`ClearAuthoringMesh()` 함수를:

```cpp
void ARuntimeAuthoringVolume::ClearAuthoringMesh()
{
    if (BlockHISM)
    {
        BlockHISM->ClearInstances();
    }
    if (MarchingMesh)
    {
        MarchingMesh->ClearAllMeshSections();
        MarchingMesh->SetVisibility(false);
    }
    FilledCellCount = 0;
}
```

- [ ] **Step 6: 빌드 및 에디터 확인**

빌드 후 UE 에디터에서:
1. 레벨에 `ARuntimeAuthoringVolume` 배치
2. Details 패널 → Authoring|Debug → `ApplyDebugSphereFill` 실행
3. Representation = `F2_MarchingCubes_Reserved` 로 변경
4. `RebuildAuthoringMesh` 실행
5. 예상: 구형 Marching Cubes 메시가 레벨에 표시됨

- [ ] **Step 7: 커밋**

```bash
git add Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h
git add Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp
git commit -m "feat(voxel): implement F2_MarchingCubes rendering in RuntimeAuthoringVolume"
```

---

## Task 3: UVoxelDebugVisualizer 컴포넌트 생성

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelDebugVisualizer.h`
- Create: `Source/RTPS/Private/VoxelAuthoring/VoxelDebugVisualizer.cpp`

### 배경

각 격자점(lattice vertex, Dim+1³)에 HISM 구 메시를 배치하고, PerInstanceCustomData[0]에 밀도 값을 저장한다.
머티리얼 `M_VoxelDebugSphere`(사용자 셰이더 작업)가 이 값을 읽어 density 값에 따른 색상 + threshold 경계 하이라이트를 표시한다.

- [ ] **Step 1: 헤더 파일 생성**

`Source/RTPS/Public/VoxelAuthoring/VoxelDebugVisualizer.h` 를 아래 내용으로 생성:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelDebugVisualizer.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

RTPS_API DECLARE_LOG_CATEGORY_EXTERN(LogRTPSVoxelDebug, Log, All);

/**
 * 밀도 격자 시각화 컴포넌트.
 * 각 래티스 정점에 HISM 구 메시를 배치하고, PerInstanceCustomData[0] = density 값을 전달.
 * M_VoxelDebugSphere 머티리얼이 density → color gradient + threshold 경계 하이라이트를 출력.
 *
 * 사용법:
 *   1. RuntimeAuthoringVolume에 컴포넌트로 추가
 *   2. SetDebugMaterial(M_VoxelDebugSphere 인스턴스)
 *   3. RebuildVisualizer() 호출 → 구 메시 갱신
 *   4. SetThreshold() 로 실시간 threshold 변경
 */
UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelDebugVisualizer : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoxelDebugVisualizer();

    // ---- 파라미터 ----

    /** 시각화 표시 여부 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bVisualizerEnabled = false;

    /** 구 메시 스케일 (CellSize 대비 비율, 0.1 = 10%) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float SphereScale = 0.12f;

    /** M_VoxelDebugSphere 머티리얼 (에디터에서 할당) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    TObjectPtr<UMaterialInterface> DebugSphereMaterial;

    /** IsoLevel 슬라이더 (0~1). 변경 시 자동으로 DynamicMI에 전달 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Threshold = 0.5f;

    // ---- API ----

    /**
     * 밀도 그리드로 시각화 재구축.
     * @param DensityGrid   RuntimeAuthoringVolume.DensityGrid (GridDim.X*Y*Z 크기)
     * @param GridDimensions 셀 격자 크기 (래티스는 +1)
     * @param CellSize      셀 하나의 크기 (cm)
     */
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void RebuildVisualizer(
        const TArray<float>& DensityGrid,
        FIntVector GridDimensions,
        float CellSize);

    /** Threshold 변경 — 머티리얼 파라미터만 업데이트 (재구축 없음) */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Debug")
    void ApplyThreshold();

    /** 모든 인스턴스 삭제 */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Debug")
    void ClearVisualizer();

    virtual void OnRegister() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SphereHISM;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> DynamicMI;

    void EnsureHISMCreated();
    void EnsureDynamicMI();

    // 래티스 인덱스: X + SampleDim.X * (Y + SampleDim.Y * Z)
    static int32 LatticeIndex(const FIntVector& SampleDim, int32 X, int32 Y, int32 Z);

    // 셀 중심이 아닌 래티스 정점 위치 (World Local space)
    static FVector LatticeVertexPosition(int32 SX, int32 SY, int32 SZ, float CellSize);
};
```

- [ ] **Step 2: 구현 파일 생성**

`Source/RTPS/Private/VoxelAuthoring/VoxelDebugVisualizer.cpp` 를 아래 내용으로 생성:

```cpp
#include "VoxelAuthoring/VoxelDebugVisualizer.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogRTPSVoxelDebug);

UVoxelDebugVisualizer::UVoxelDebugVisualizer()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UVoxelDebugVisualizer::OnRegister()
{
    Super::OnRegister();
    EnsureHISMCreated();
}

void UVoxelDebugVisualizer::EnsureHISMCreated()
{
    if (SphereHISM)
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return;
    }

    SphereHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(
        Owner, TEXT("VoxelDebugSphereHISM"));
    SphereHISM->RegisterComponent();
    SphereHISM->AttachToComponent(
        Owner->GetRootComponent(),
        FAttachmentTransformRules::KeepRelativeTransform);

    // 구 메시 — 엔진 기본 제공
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshRef(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMeshRef.Succeeded())
    {
        SphereHISM->SetStaticMesh(SphereMeshRef.Object);
    }

    // PerInstanceCustomData: 슬롯 0 = density
    SphereHISM->NumCustomDataFloats = 1;
    SphereHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SphereHISM->SetCastShadow(false);
    SphereHISM->SetVisibility(false);
}

void UVoxelDebugVisualizer::EnsureDynamicMI()
{
    if (DynamicMI)
    {
        return;
    }
    if (!IsValid(DebugSphereMaterial))
    {
        UE_LOG(LogRTPSVoxelDebug, Warning,
            TEXT("UVoxelDebugVisualizer: DebugSphereMaterial이 할당되지 않았습니다."));
        return;
    }
    DynamicMI = UMaterialInstanceDynamic::Create(DebugSphereMaterial, this);
    if (SphereHISM)
    {
        SphereHISM->SetMaterial(0, DynamicMI);
    }
}

void UVoxelDebugVisualizer::RebuildVisualizer(
    const TArray<float>& DensityGrid,
    FIntVector GridDimensions,
    float CellSize)
{
    EnsureHISMCreated();
    if (!SphereHISM)
    {
        return;
    }

    SphereHISM->ClearInstances();

    if (!bVisualizerEnabled)
    {
        SphereHISM->SetVisibility(false);
        return;
    }

    const int32 TotalCells = GridDimensions.X * GridDimensions.Y * GridDimensions.Z;
    if (DensityGrid.Num() < TotalCells)
    {
        UE_LOG(LogRTPSVoxelDebug, Warning,
            TEXT("RebuildVisualizer: DensityGrid.Num()=%d < expected %d"),
            DensityGrid.Num(), TotalCells);
        return;
    }

    EnsureDynamicMI();

    // 래티스 정점은 (GridDimensions + 1)³
    const FIntVector SampleDim(
        GridDimensions.X + 1,
        GridDimensions.Y + 1,
        GridDimensions.Z + 1);

    // 엔진 기본 Sphere 메시는 반지름 50cm → Scale 보정
    const float SphereMeshRadius = 50.f;
    const float DesiredRadius = CellSize * SphereScale * 0.5f;
    const float UniformScale = DesiredRadius / SphereMeshRadius;

    SphereHISM->PreAllocateInstanceMemory(SampleDim.X * SampleDim.Y * SampleDim.Z);

    TArray<FTransform> Transforms;
    TArray<float> CustomDataValues;
    Transforms.Reserve(SampleDim.X * SampleDim.Y * SampleDim.Z);
    CustomDataValues.Reserve(SampleDim.X * SampleDim.Y * SampleDim.Z);

    for (int32 SZ = 0; SZ < SampleDim.Z; ++SZ)
    {
        for (int32 SY = 0; SY < SampleDim.Y; ++SY)
        {
            for (int32 SX = 0; SX < SampleDim.X; ++SX)
            {
                // 래티스 정점 밀도: 인접 최대 8개 셀 평균
                float Sum = 0.f;
                int32 Count = 0;
                for (int32 DZ = -1; DZ <= 0; ++DZ)
                {
                    for (int32 DY = -1; DY <= 0; ++DY)
                    {
                        for (int32 DX = -1; DX <= 0; ++DX)
                        {
                            const int32 CX = SX + DX;
                            const int32 CY = SY + DY;
                            const int32 CZ = SZ + DZ;
                            if (CX >= 0 && CX < GridDimensions.X &&
                                CY >= 0 && CY < GridDimensions.Y &&
                                CZ >= 0 && CZ < GridDimensions.Z)
                            {
                                Sum += DensityGrid[CX + GridDimensions.X * (CY + GridDimensions.Y * CZ)];
                                ++Count;
                            }
                        }
                    }
                }
                const float LatticeDensity = (Count > 0) ? (Sum / static_cast<float>(Count)) : 0.f;

                const FVector LocalPos(
                    static_cast<float>(SX) * CellSize,
                    static_cast<float>(SY) * CellSize,
                    static_cast<float>(SZ) * CellSize);

                Transforms.Add(FTransform(
                    FQuat::Identity,
                    LocalPos,
                    FVector(UniformScale)));
                CustomDataValues.Add(LatticeDensity);
            }
        }
    }

    // 인스턴스 일괄 추가
    SphereHISM->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/false);

    // PerInstanceCustomData 일괄 설정
    for (int32 i = 0; i < CustomDataValues.Num(); ++i)
    {
        SphereHISM->SetCustomDataValue(i, 0, CustomDataValues[i], /*bMarkRenderStateDirty=*/false);
    }
    SphereHISM->MarkRenderStateDirty();

    SphereHISM->SetVisibility(true);

    ApplyThreshold();

    UE_LOG(LogRTPSVoxelDebug, Log,
        TEXT("RebuildVisualizer: %d 래티스 정점 시각화 완료 (Threshold=%.2f)"),
        CustomDataValues.Num(), Threshold);
}

void UVoxelDebugVisualizer::ApplyThreshold()
{
    EnsureDynamicMI();
    if (DynamicMI)
    {
        DynamicMI->SetScalarParameterValue(TEXT("Threshold"), Threshold);
    }
}

void UVoxelDebugVisualizer::ClearVisualizer()
{
    if (SphereHISM)
    {
        SphereHISM->ClearInstances();
        SphereHISM->SetVisibility(false);
    }
}

int32 UVoxelDebugVisualizer::LatticeIndex(
    const FIntVector& SampleDim, int32 X, int32 Y, int32 Z)
{
    return X + SampleDim.X * (Y + SampleDim.Y * Z);
}

FVector UVoxelDebugVisualizer::LatticeVertexPosition(
    int32 SX, int32 SY, int32 SZ, float CellSize)
{
    return FVector(
        static_cast<float>(SX) * CellSize,
        static_cast<float>(SY) * CellSize,
        static_cast<float>(SZ) * CellSize);
}
```

- [ ] **Step 3: 빌드**

```powershell
& "C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" `
  RTPS Win64 Development `
  -project="E:\perforce\Project\RTPS\RTPS.uproject" -waitmutex
```
예상: 빌드 성공

- [ ] **Step 4: 커밋**

```bash
git add Source/RTPS/Public/VoxelAuthoring/VoxelDebugVisualizer.h
git add Source/RTPS/Private/VoxelAuthoring/VoxelDebugVisualizer.cpp
git commit -m "feat(voxel): add UVoxelDebugVisualizer HISM density sphere component"
```

---

## Task 4: RuntimeAuthoringVolume에 DebugVisualizer 통합

**Files:**
- Modify: `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h`
- Modify: `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp`

- [ ] **Step 1: 헤더에 DebugVisualizer 포함**

`RuntimeAuthoringVolume.h` 상단 forward declarations에 추가:
```cpp
class UVoxelDebugVisualizer;
```

public UPROPERTY 섹션 (`FilledCellCount` 아래)에 추가:
```cpp
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Authoring|Debug")
TObjectPtr<UVoxelDebugVisualizer> DebugVisualizer;
```

public UFUNCTION 섹션에 추가:
```cpp
/** DebugVisualizer 재구축 (에디터 Details 패널 버튼) */
UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|Debug")
void RebuildDebugVisualizer();

/** Threshold 적용 (에디터 슬라이더 변경 후 누르는 버튼) */
UFUNCTION(CallInEditor, BlueprintCallable, Category = "Authoring|Debug")
void ApplyDebugVisualizerThreshold();
```

- [ ] **Step 2: 생성자에서 DebugVisualizer 생성**

`RuntimeAuthoringVolume.cpp` include 상단에 추가:
```cpp
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
```

생성자 내부, `MarchingMesh` 초기화 블록 아래에 추가:
```cpp
DebugVisualizer = CreateDefaultSubobject<UVoxelDebugVisualizer>(TEXT("DebugVisualizer"));
```

- [ ] **Step 3: RebuildDebugVisualizer / ApplyDebugVisualizerThreshold 구현**

`RuntimeAuthoringVolume.cpp` 하단에 추가:

```cpp
void ARuntimeAuthoringVolume::RebuildDebugVisualizer()
{
    if (!DebugVisualizer)
    {
        UE_LOG(LogRTPSVoxelAuthoring, Warning, TEXT("RebuildDebugVisualizer: DebugVisualizer component is null"));
        return;
    }
    EnsureGridAllocated();
    DebugVisualizer->RebuildVisualizer(DensityGrid, GridDimensions, CellSize);
}

void ARuntimeAuthoringVolume::ApplyDebugVisualizerThreshold()
{
    if (DebugVisualizer)
    {
        DebugVisualizer->ApplyThreshold();
    }
}
```

- [ ] **Step 4: 빌드 및 에디터 동작 확인**

빌드 후 에디터:
1. 레벨에 `ARuntimeAuthoringVolume` 배치
2. Details → Authoring|Debug → `ApplyDebugSphereFill` 실행 (구형 밀도 채우기)
3. Details → Authoring|Debug → `DebugVisualizer` → `bVisualizerEnabled = true`
4. `RebuildDebugVisualizer` 클릭
5. 예상: 격자점마다 구가 생성됨 (머티리얼은 다음 Task에서 연결)

- [ ] **Step 5: 커밋**

```bash
git add Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h
git add Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp
git commit -m "feat(voxel): integrate VoxelDebugVisualizer into RuntimeAuthoringVolume"
```

---

## Task 5: 셰이더 작업 — M_VoxelDebugSphere (사용자 직접 작업)

> **⚠️ 이 Task는 Unreal Editor 머티리얼 에디터에서 직접 수행해야 합니다. C++ 코드 작업이 없습니다.**

**목표:** HISM의 PerInstanceCustomData[0](density 0~1)을 읽어 색상 그라디언트와 Threshold 경계 하이라이트를 표시하는 머티리얼.

- [ ] **Step 1: 머티리얼 생성**

Content Browser → `Content/Materials/Debug/` 폴더 생성 →  
우클릭 → Create Material → 이름: `M_VoxelDebugSphere`

- [ ] **Step 2: 머티리얼 기본 설정**

Details 패널:
- **Blend Mode:** `Translucent`
- **Shading Model:** `Unlit`
- **Two Sided:** `true`
- **Used with Instanced Static Meshes:** `true` ← 필수 (HISM 사용 시 요구됨)

- [ ] **Step 3: Density 값 읽기 노드**

노드 배치 순서:
1. 빈 공간 우클릭 → `PerInstanceCustomData` 노드 추가
   - DataIndex = `0`
   - DefaultValue = `0.0`
2. 이 노드를 `Density` 라고 레이블링 (주석 박스로 감싸기)

- [ ] **Step 4: 색상 그라디언트 노드**

```
[Vector Parameter "EmptyColor"] (기본값: 0.0, 0.2, 1.0) ─┐
                                                           ├─[Lerp]─ A
[Vector Parameter "SolidColor"] (기본값: 1.0, 0.1, 0.0) ─┘    │
                                                                │
[Density(위 노드)] ─────────────────────────────────────── Alpha

→ Lerp 출력 = "GradientColor"
```

- [ ] **Step 5: Threshold 경계 하이라이트**

```
[Density] ─────────────────────────┐
                                    ├─[Subtract]─[Abs]─[Multiply(×20)]─[OneMinus]─[Clamp(0,1)]
[Scalar Parameter "Threshold"]  ───┘                                              = "BoundaryMask"

[Vector Parameter "BoundaryColor"] (기본값: 1.0, 0.9, 0.0)
[Multiply(BoundaryColor × 3.0)] = "BoundaryEmissive"
```

- [ ] **Step 6: 최종 연결**

```
GradientColor ─[Lerp(Alpha=BoundaryMask)]──► Emissive Color 핀
              └─ A 핀

[Scalar Parameter "Opacity"] (기본값: 0.75) ──► Opacity 핀
```

BaseColor 핀은 연결하지 않음 (Unlit이므로 Emissive만 사용).

- [ ] **Step 7: 파라미터 목록 확인**

머티리얼 에디터 왼쪽 파라미터 패널에서 다음이 모두 존재하는지 확인:
- `Threshold` (Scalar, 기본 0.5)
- `EmptyColor` (Vector)
- `SolidColor` (Vector)
- `BoundaryColor` (Vector)
- `Opacity` (Scalar, 기본 0.75)

- [ ] **Step 8: 컴파일 및 저장**

머티리얼 에디터 → Apply → Save.

- [ ] **Step 9: RuntimeAuthoringVolume에 머티리얼 연결**

레벨에서 `ARuntimeAuthoringVolume` 선택 → Details →  
`DebugVisualizer` → `Debug Sphere Material` →  
`M_VoxelDebugSphere` 할당 → `RebuildDebugVisualizer` 실행

예상: 파란색(empty) ~ 빨간색(solid) 구 메시 + threshold 경계에서 노란색 링

---

## Task 6: Automation Test — DebugVisualizer 동작 검증

**Files:**
- Modify: `Source/RTPS/Private/Tests/RTPSVoxelImportAutomation.cpp`

- [ ] **Step 1: 테스트 추가**

`RTPSVoxelImportAutomation.cpp` 의 `#include` 섹션에 추가:
```cpp
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
```

기존 테스트들 아래에 추가:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRTPSVoxelDebugVisualizerTest,
    "RTPS.VoxelAuthoring.DebugVisualizer.RebuildAndThreshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDebugVisualizerTest::RunTest(const FString& Parameters)
{
    // 에디터 월드에서 임시 액터 생성
    UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!EditorWorld)
    {
        AddError(TEXT("EditorWorld is null"));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.ObjectFlags |= RF_Transient;
    SpawnParams.bTemporaryEditorActor = true;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ARuntimeAuthoringVolume* Volume = EditorWorld->SpawnActor<ARuntimeAuthoringVolume>(
        ARuntimeAuthoringVolume::StaticClass(),
        FTransform::Identity,
        SpawnParams);

    if (!Volume)
    {
        AddError(TEXT("ARuntimeAuthoringVolume 생성 실패"));
        return false;
    }

    TestNotNull(TEXT("DebugVisualizer 컴포넌트 존재"), Volume->DebugVisualizer);

    if (Volume->DebugVisualizer)
    {
        // 구형 밀도 채우기
        Volume->FillSphereDensity(
            FVector(800.f, 800.f, 800.f), 500.f, 1.f);

        Volume->DebugVisualizer->bVisualizerEnabled = true;

        // 크래시 없이 실행되어야 함
        Volume->RebuildDebugVisualizer();

        // Threshold 변경 — 크래시 없이
        Volume->DebugVisualizer->Threshold = 0.3f;
        Volume->ApplyDebugVisualizerThreshold();

        Volume->DebugVisualizer->Threshold = 0.7f;
        Volume->ApplyDebugVisualizerThreshold();

        // Clear — 크래시 없이
        Volume->DebugVisualizer->ClearVisualizer();

        AddInfo(TEXT("DebugVisualizer Rebuild / ApplyThreshold / Clear 모두 정상 실행"));
    }

    EditorWorld->DestroyActor(Volume, false, false);
    return true;
}
```

- [ ] **Step 2: 빌드 및 테스트 실행**

빌드 후 Session Frontend → Automation → `RTPS.VoxelAuthoring.DebugVisualizer.*`  
예상: PASS (머티리얼 없이도 크래시 없이 실행)

- [ ] **Step 3: 커밋**

```bash
git add Source/RTPS/Private/Tests/RTPSVoxelImportAutomation.cpp
git commit -m "test(voxel): add DebugVisualizer rebuild and threshold automation test"
```

---

## Task 7: 셰이더 작업 — M_VoxelTerrain_Master (사용자 직접 작업)

> **⚠️ Unreal Editor 머티리얼 에디터에서 직접 수행. C++ 없음.**

**목표:** ProceduralMesh의 지형 머티리얼. 경사도·높이 기반 레이어 블렌딩.

- [ ] **Step 1: MF_TriplanarBlend Material Function 생성**

Content Browser → `Content/Materials/Functions/` →  
우클릭 → New Material Function → 이름: `MF_TriplanarBlend`

```
[WorldPosition] → Divide(100) → [ComponentMask XZ] → TexSample(Param:"Tex_XZ") ─┐
[WorldPosition] → Divide(100) → [ComponentMask XY] → TexSample(Param:"Tex_XY") ─┤
[WorldPosition] → Divide(100) → [ComponentMask YZ] → TexSample(Param:"Tex_YZ") ─┘

[VertexNormalWS] → Abs → Power(4)
               → Divide(합산) = BlendWeights(X,Y,Z)

Lerp(XZ_Result, XY_Result, W.Y) → Lerp(결과, YZ_Result, W.Z) → [FunctionOutput "Color"]
```

별도 Scalar Parameter `"Tiling"` (기본값 0.01)을 Divide 분자에 곱해 줌.

- [ ] **Step 2: M_VoxelTerrain_Master 머티리얼 생성**

Content Browser → `Content/Materials/` → 새 머티리얼 `M_VoxelTerrain_Master`

```
[MF_TriplanarBlend 함수 호출] (Tex_XZ="T_Rock", Tex_XY="T_Rock", Tex_YZ="T_Rock")
      → RockColor

[MF_TriplanarBlend 함수 호출] (Tex_XZ="T_Grass", Tex_XY="T_Grass", Tex_YZ="T_Grass")
      → GrassColor

[VertexNormalWS].Z → SmoothStep(0.6, 0.9) → SlopeWeight
Lerp(RockColor, GrassColor, SlopeWeight) → BaseColor
```

- [ ] **Step 3: RuntimeAuthoringVolume에 머티리얼 적용**

`RebuildF2_MarchingCubes()` 에서 `CreateMeshSection` 호출 후:
```cpp
// 기존 CreateMeshSection 코드 아래에 추가
static ConstructorHelpers::FObjectFinder<UMaterialInterface> TerrainMatRef(
    TEXT("/Game/Materials/M_VoxelTerrain_Master.M_VoxelTerrain_Master"));
// 위 경로는 에디터에서 우클릭 → Copy Reference 로 확인
if (TerrainMatRef.Succeeded())
{
    MarchingMesh->SetMaterial(0, TerrainMatRef.Object);
}
```

> **경로 확인 방법:** Content Browser에서 `M_VoxelTerrain_Master` 우클릭 → "Copy Reference" → 따옴표 안 경로를 사용.

- [ ] **Step 4: 빌드 및 확인**

빌드 후 에디터에서 F2 메시에 지형 머티리얼 적용 확인.

- [ ] **Step 5: 커밋**

```bash
git add Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp
git commit -m "feat(voxel): apply M_VoxelTerrain_Master to F2 MarchingCubes mesh"
```

---

## Self-Review

**스펙 커버리지 확인:**
- [x] float density 기반 MC → Task 1
- [x] F2 MarchingCubes 렌더링 → Task 2
- [x] 디버그 구 시각화 (HISM + PerInstanceCustomData) → Task 3, 4
- [x] Threshold 실시간 조정 → Task 3 (`ApplyThreshold`), Task 5 (셰이더)
- [x] 경계 구 하이라이트 → Task 5 (Step 5 BoundaryMask)
- [x] 오류 관리 (null 체크, 크기 검증) → Task 1 Step 4, Task 3 Step 2
- [x] 경계 케이스 (빈/완전 채움 그리드) → Task 1 Step 1 테스트
- [x] 머티리얼 → Task 5, 7
- [x] 자동화 테스트 → Task 1, 6

**타입 일관성:**
- `BuildMarchingCubesChunkMeshDensity` 시그니처: Task 1 Step 3(헤더) = Task 1 Step 4(구현) = Task 1 Step 1(테스트) ✓
- `RebuildVisualizer` 시그니처: Task 3 Step 1(헤더) = Task 3 Step 2(구현) = Task 4 Step 3(호출) ✓
- `DensityGrid` 타입: `TArray<float>` — RuntimeAuthoringVolume.h 기존 정의와 일치 ✓

---

**Plan complete and saved to `docs/superpowers/plans/2026-04-16-voxel-terrain-runtime-editor.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** — Task마다 신선한 서브에이전트 파견, 태스크 간 리뷰 체크포인트

**2. Inline Execution** — 현재 세션에서 executing-plans로 일괄 실행, 체크포인트에서 리뷰

**어떤 방식으로 진행할까요?**
