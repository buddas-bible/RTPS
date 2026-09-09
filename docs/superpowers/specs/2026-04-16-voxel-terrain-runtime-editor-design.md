# Voxel Terrain Runtime Editor — Design Spec
**Date:** 2026-04-16  
**Project:** RTPS (UE 5.5.4, Multiplayer RPG)  
**Status:** Approved for implementation

---

## 1. 목표 (Goals)

- Marching Cubes 기반 지형을 **런타임에 실시간으로 수정**하며 플레이할 수 있다
- 편집은 **서버 권위적(Server-Authoritative)** 으로 모든 플레이어에게 동기화된다
- 에디터에서 **밀도 필드 시각 디버거**로 density 값을 구 메시로 확인하고 threshold를 실시간 조정할 수 있다
- MVP 범위는 **단일 볼륨(RuntimeAuthoringVolume 확장)** — 무한 청크 시스템은 이후 단계

---

## 2. 아키텍처 개요

```
┌──────────────────────────────────────────────────────────────┐
│ AVoxelWorld (Manager Actor, Replicated)                      │
│  ├── TMap<FIntVector, AVoxelChunk*> Chunks                   │
│  ├── Server_ApplyBrush(Brush) — RPC                          │
│  └── Multicast_ChunkUpdated(ChunkIdx, DeltaPayload)          │
│                                                              │
│ AVoxelChunk (Replicated, 16³ cells per chunk)                │
│  ├── float[] DensityGrid  (17³ sample points)                │
│  ├── UProceduralMeshComponent  — render + collision          │
│  ├── UVoxelDebugVisualizer     — density sphere HISM         │
│  └── AsyncTask rebuild pipeline                              │
│                                                              │
│ UVoxelEditComponent (on PlayerController)                    │
│  ├── LineTrace → FVoxelBrush                                 │
│  ├── Preview decal (M_BrushPreview)                          │
│  └── Server_ApplyBrush RPC                                   │
│                                                              │
│ UVoxelDebugVisualizer (Editor + Runtime)                     │
│  ├── HISM — sphere mesh per lattice vertex                   │
│  ├── Per-Instance Custom Data [0] = density                  │
│  ├── M_VoxelDebugSphere — density→color gradient            │
│  └── ThresholdParam — 실시간 threshold 라인 표시             │
└──────────────────────────────────────────────────────────────┘
```

---

## 3. 컴포넌트 구조

### 3-A. 핵심 데이터 타입

```cpp
// 편집 브러시
USTRUCT(BlueprintType)
struct FVoxelBrush {
    GENERATED_BODY()
    FVector   WorldCenter;
    float     Radius      = 200.f;
    float     Strength    = 1.f;       // delta per second
    EVoxelOp  Operation;               // Add / Remove / Smooth
};

// 청크 밀도 델타 패킷 (복제용)
USTRUCT()
struct FChunkDeltaPacket {
    GENERATED_BODY()
    FIntVector          ChunkIndex;
    TArray<uint8>       RLEPayload;    // RLE-encoded float[] delta
    uint32              SequenceID;   // 패킷 순서 보장
};
```

### 3-B. 클래스 목록

| 클래스 | 역할 | 파일 위치 |
|---|---|---|
| `AVoxelWorld` | 청크 생성/제거/RPC 허브 | Public/Voxel/ |
| `AVoxelChunk` | 밀도+메시+콜리전 | Public/Voxel/ |
| `UVoxelEditComponent` | 플레이어 편집 도구 | Public/Voxel/ |
| `UVoxelDebugVisualizer` | 밀도 구 시각화 | Public/Voxel/Debug/ |
| `FVoxelMeshBuilder` | 비동기 MC 빌드 | Private/Voxel/ |
| `FDensityKernel` | 브러시→밀도 연산 | Private/Voxel/ |
| `FRLECoder` | 네트워크 압축 | Private/Voxel/ |

---

## 4. 데이터 흐름

### 편집 흐름

```
[Client]
  Input(Hold LMB)
  └─ UVoxelEditComponent::Tick()
       ├─ LineTrace vs ECC_Voxel
       ├─ UpdatePreviewDecal(HitPoint)
       └─ Server_ApplyBrush(FVoxelBrush) ──────────────────►

[Server AVoxelWorld]
  ├─ ValidateBrush() → 범위 클램프, 권한 체크
  ├─ GetAffectedChunks(Brush) → TArray<AVoxelChunk*>
  └─ For each Chunk:
       ├─ FDensityKernel::ApplySphere(Brush, DensityGrid)
       ├─ Chunk.MarkDirty()
       └─ EnqueueAsyncRebuild()

[AsyncTask (BackgroundThread)]
  FVoxelMeshBuilder::Build(DensityGrid, ChunkOrigin, IsoLevel)
  └─ Returns FMarchingCubesMeshData

[GameThread Callback]
  ├─ ProceduralMeshComponent::CreateMeshSection_Lod0()
  ├─ ProceduralMeshComponent::SetCollisionEnabled()
  └─ Multicast_ChunkUpdated(DeltaPacket) ──────────────────►

[All Clients]
  ├─ FRLECoder::Decode() → DensityGrid
  └─ Local async MC rebuild (visual only, no authority)
```

### 디버그 시각화 흐름

```
Editor or Runtime:
  DebugVisualizer.SetVisible(true)
  └─ For each lattice vertex (17³):
       ├─ HISM.AddInstance(Position)
       └─ HISM.SetCustomDataValue(idx, 0, DensityValue)

  Threshold Slider 변경:
  └─ DynamicMaterialInstance.SetScalarParam("Threshold", Val)
       → M_VoxelDebugSphere shader: density > threshold ? SolidColor : EmptyColor
       → 경계 근처(|density - threshold| < 0.05) → 하이라이트 링
```

---

## 5. MC 알고리즘 확장

기존 `RTPSMarchingCubes` 는 `TSet<FIntVector>` 이진 점유를 float density로 변환.  
**변경 방향:** float[] 직접 입력을 받는 오버로드 추가 (기존 함수 유지).

```cpp
// 추가할 시그니처
bool BuildMarchingCubesChunkMeshDensity(
    const FIntVector& ChunkDimensions,   // 16,16,16
    const FIntVector& ChunkOriginGrid,
    float VoxelSizeCm,
    float IsoLevel,
    const float* DensityData,            // 17³ float 배열
    int32 DensitySampleCount,
    FMarchingCubesMeshData& OutMeshData
);
```

---

## 6. 셰이더 작업 (사용자 직접 작업 — 순서 엄수)

### [Step 1] MF_TriplanarBlend — Material Function
`Content/Materials/Functions/MF_TriplanarBlend.uasset`

```
목적: UV좌표 없이 월드 노멀로 텍스처 3방향 프로젝션
노드 연결:
  WorldPosition → Divide(CellSize) → ComponentMask
  WorldVertexNormal → Abs → Power(4) → 합산 나누기 → BlendWeights

  Texture2D Param "Tex_XZ" → TexSample(UV=WorldPos.XZ * Tiling)
  Texture2D Param "Tex_XY" → TexSample(UV=WorldPos.XY * Tiling)  
  Texture2D Param "Tex_YZ" → TexSample(UV=WorldPos.YZ * Tiling)

  Lerp(XZ, XY, W.Y) → Lerp(결과, YZ, W.Z) → Output Color/Normal/Roughness
```

### [Step 2] M_VoxelDebugSphere — Debug Sphere Material
`Content/Materials/Debug/M_VoxelDebugSphere.uasset`

```
목적: HISM Custom Data [0](density) 읽어서 색상 결정
노드 연결:
  PerInstanceCustomData[0] → "Density" (0~1)
  ScalarParam "Threshold"  → 슬라이더 (0~1)

  Density → Lerp(EmptyColor=파란색, SolidColor=빨간색, Density)
             = 기본 그라디언트

  Abs(Density - Threshold) < 0.05
  → 경계 구 하이라이트: Emissive Yellow 오버레이

  BaseColor = 그라디언트 결과
  Emissive  = 경계 하이라이트 * 3.0
  Opacity   = 0.8 (Translucent)
```

### [Step 3] M_VoxelTerrain_Master — Terrain Material
`Content/Materials/M_VoxelTerrain_Master.uasset`

```
목적: 경사도/높이 기반 지형 레이어 블렌딩
노드 연결:
  [MF_TriplanarBlend] × 2 (암석, 잔디)
  WorldVertexNormal.Z → SmoothStep(0.6, 0.9) → SlopeWeight
  Lerp(암석, 잔디, SlopeWeight) → 최종 BaseColor/Normal/Roughness
  
  WorldPosition.Z → Normalize(SnowMinAlt, SnowMaxAlt) → SnowMask
  Lerp(이전결과, 눈텍스처, SnowMask) → Output

  VertexColor.R → EmissiveHighlight (편집 영역 표시)
```

### [Step 4] M_BrushPreview — Decal Preview
`Content/Materials/M_BrushPreview.uasset`

```
목적: 편집 브러시 위치/반경 데칼 오버레이
노드 연결:
  DecalTexCoord.XY → Length → "DistFromCenter"
  Abs(DistFromCenter - 1.0) → 링 마스크 (테두리만 표시)
  SmoothStep(0.9, 1.0, 링마스크) → Opacity
  Emissive = Cyan × 2.0
  BlendMode = Translucent, DeferredDecal
  
  ScalarParam "BrushRadius" → 런타임 C++에서 업데이트
```

---

## 7. 오류 관리

| 오류 상황 | 처리 방법 |
|---|---|
| 밀도 배열 크기 불일치 | `ensure()` + 로그, 리빌드 스킵 |
| 비동기 빌드 중 편집 요청 | 큐에 병합(merge dirty regions) |
| 네트워크 패킷 드롭 | SequenceID 기반 full-resync 요청 |
| ProceduralMesh 섹션 없음 | CreateMeshSection 전 Clear 보장 |
| 청크 경계 MC 아티팩트 | 인접 청크 1셀 overlap 샘플링 |
| RPC 플러드 (편집 스팸) | 100ms 쿨다운 + 서버 레이트 리밋 |
| HISM Custom Data 오버플로우 | 최대 17³=4913 인스턴스 상한 검증 |

---

## 8. 경계 사례 처리

| 케이스 | 처리 |
|---|---|
| 청크 경계면에 걸친 브러시 | 두 청크 모두 dirty 마킹 |
| density=0인 완전 빈 청크 | MC 스킵, 메시 섹션 삭제 |
| density=1인 완전 채워진 청크 | MC 스킵(모두 내부), 빈 메시 |
| IsoLevel = density (정확히 일치) | InterpolateVertex epsilon 처리 (기존 코드) |
| GridDimensions 변경 시 | DensityGrid 재할당, HISM 전체 갱신 |
| 플레이어가 지형 내부 편집 | 서버: 최소 이탈 경로 보장 후 편집 |

---

## 9. MVP 구현 범위 (Phase 별)

### Phase 0 — 기반 전환 (기존 코드 확장)
- `RTPSMarchingCubes`: float density 오버로드 추가
- `RuntimeAuthoringVolume`: F2_MarchingCubes 구현 (ProceduralMeshComponent 추가)
- 셰이더 Step 2: M_VoxelDebugSphere 제작

### Phase 1 — 디버그 시각화
- `UVoxelDebugVisualizer` 컴포넌트 (HISM + custom data)
- RuntimeAuthoringVolume에 통합
- 에디터 슬라이더로 Threshold 실시간 조정

### Phase 2 — 런타임 편집 (단일 볼륨)
- `FDensityKernel` (Sphere Add/Remove)
- `UVoxelEditComponent` (LineTrace + RPC skeleton)
- 셰이더 Step 3,4: M_VoxelTerrain_Master, M_BrushPreview

### Phase 3 — 멀티플레이어 복제
- `AVoxelWorld` / `AVoxelChunk` 리팩토링
- `FRLECoder` 압축
- Multicast 동기화

### Phase 4 — 청크 시스템
- 동적 로드/언로드
- 셰이더 Step 1: MF_TriplanarBlend 완성

---

## 10. 모듈 의존성 추가 (RTPS.Build.cs)

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "ProceduralMeshComponent",
    "RenderCore",       // FRHICommandList (향후 GPU 빌드용)
});
```
