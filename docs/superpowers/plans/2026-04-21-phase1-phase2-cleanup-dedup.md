# RTPS Phase 1+2: 기반 정리 및 중복 제거 구현 플랜

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 동작 변경 없이 빈 클래스 제거, 레거시 코드 삭제, FBM 노이즈 로직 중복 제거

**Architecture:** 빈 캐릭터 클래스를 삭제하고 ARTPSEnemy 상속을 직접 연결. FBM 노이즈 생성 로직을 VoxelNoiseUtils 네임스페이스로 추출하여 RuntimeAuthoringVolume과 VoxelChunk가 공유. RuntimeAuthoringVolume의 개별 노이즈 멤버 변수를 FVoxelNoiseParams 단일 구조체로 통합.

**Tech Stack:** Unreal Engine 5.5, C++, Perforce

---

## 파일 변경 목록

### 생성
- `Source/RTPS/Public/VoxelAuthoring/VoxelNoiseConstants.h` — 시드 오프셋 상수
- `Source/RTPS/Public/VoxelAuthoring/VoxelNoiseUtils.h` — FBM 유틸리티 선언
- `Source/RTPS/Private/VoxelAuthoring/VoxelNoiseUtils.cpp` — FBM 유틸리티 구현

### 삭제
- `Source/RTPS/Public/Character/RTPSCharacterAI.h`
- `Source/RTPS/Private/Character/RTPSCharacterAI.cpp`
- `Source/RTPS/Public/Character/RTPSCharacterNonPlayer.h`
- `Source/RTPS/Private/Character/RTPSCharacterNonPlayer.cpp`

### 수정
- `Source/RTPS/Public/Character/RTPSEnemy.h` — 상속 대상 변경
- `Source/RTPS/Private/Character/RTPSEnemy.cpp` — include 변경
- `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp` — #if 0 제거, 노이즈 로직 교체
- `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h` — 노이즈 멤버 변수 → FVoxelNoiseParams
- `Source/RTPS/Private/VoxelAuthoring/VoxelChunk.cpp` — FBM 로직 교체
- `Source/RTPS/Private/VoxelAuthoring/VoxelChunkManager.cpp` — 인라인 좌표 계산 교체

---

## Task 1: ARTPSEnemy 상속 변경

`ARTPSCharacterAI`를 먼저 우회해야 삭제가 가능.

**Files:**
- Modify: `Source/RTPS/Public/Character/RTPSEnemy.h`
- Modify: `Source/RTPS/Private/Character/RTPSEnemy.cpp`

- [ ] **Step 1: RTPSEnemy.h 상속 및 include 변경**

`Source/RTPS/Public/Character/RTPSEnemy.h`를 다음과 같이 수정:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Character/RTPSCharacterBase.h"
#include "RTPSEnemy.generated.h"

class UBossComponent;

UCLASS(Blueprintable)
class RTPS_API ARTPSEnemy : public ARTPSCharacterBase
{
    GENERATED_BODY()

public:
    ARTPSEnemy();

protected:
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
};
```

- [ ] **Step 2: RTPSEnemy.cpp include 확인**

`Source/RTPS/Private/Character/RTPSEnemy.cpp` 상단에 `#include "Character/RTPSCharacterAI.h"` 가 있으면 `#include "Character/RTPSCharacterBase.h"` 로 교체. 없으면 그대로.

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 2: 빈 캐릭터 클래스 삭제

**Files:**
- Delete: `Source/RTPS/Public/Character/RTPSCharacterAI.h`
- Delete: `Source/RTPS/Private/Character/RTPSCharacterAI.cpp`
- Delete: `Source/RTPS/Public/Character/RTPSCharacterNonPlayer.h`
- Delete: `Source/RTPS/Private/Character/RTPSCharacterNonPlayer.cpp`

- [ ] **Step 1: 참조 여부 확인**

삭제 전 다른 파일에서 참조하는지 검색:

```
Source 디렉토리에서 "RTPSCharacterAI" 문자열 검색
Source 디렉토리에서 "RTPSCharacterNonPlayer" 문자열 검색
```

Task 1 완료 후 참조가 0개여야 함. 남아 있으면 해당 파일도 수정 후 진행.

- [ ] **Step 2: 파일 4개 삭제**

```
삭제: Source/RTPS/Public/Character/RTPSCharacterAI.h
삭제: Source/RTPS/Private/Character/RTPSCharacterAI.cpp
삭제: Source/RTPS/Public/Character/RTPSCharacterNonPlayer.h
삭제: Source/RTPS/Private/Character/RTPSCharacterNonPlayer.cpp
```

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

- [ ] **Step 4: Perforce 체크인**

변경 목록: RTPSEnemy.h, RTPSEnemy.cpp 수정 + 4개 파일 삭제  
설명: `refactor: remove empty ARTPSCharacterAI and ARTPSCharacterNonPlayer, connect ARTPSEnemy directly to ARTPSCharacterBase`

---

## Task 3: RuntimeAuthoringVolume.cpp #if 0 블록 제거

**Files:**
- Modify: `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp`

- [ ] **Step 1: #if 0 블록 제거**

`RuntimeAuthoringVolume.cpp` 244~279번 줄의 다음 블록 전체 삭제:

```cpp
#if 0
void ARuntimeAuthoringVolume::ClearAuthoringMesh()
{
    /* legacy - commented out */
}

void ARuntimeAuthoringVolume::RebuildAuthoringMesh()
{
    /* legacy - commented out */
}

void ARuntimeAuthoringVolume::RebuildF1_Block()
{
    /* legacy - commented out */
}

void ARuntimeAuthoringVolume::RebuildF2_MarchingCubes()
{
    /* legacy - commented out */
}

void ARuntimeAuthoringVolume::RebuildDebugVisualizer()
{
    /* legacy - commented out */
}

void ARuntimeAuthoringVolume::ApplyDebugVisualizerThreshold()
{
    /* legacy - commented out */
}

void ARuntimeAuthoringVolume::ClearDebugVisualizer()
{
    /* legacy - commented out */
}
#endif
```

- [ ] **Step 2: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

- [ ] **Step 3: Perforce 체크인**

설명: `cleanup: remove legacy #if 0 dead code from RuntimeAuthoringVolume`

---

## Task 4: VoxelNoiseConstants.h 생성

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelNoiseConstants.h`

- [ ] **Step 1: 파일 생성**

`Source/RTPS/Public/VoxelAuthoring/VoxelNoiseConstants.h`:

```cpp
#pragma once

namespace VoxelNoiseConstants
{
    // Seed offset multipliers — shift Perlin sample positions per NoiseSeed value.
    // Values chosen to be irrational-like to avoid grid artifacts.
    constexpr float SeedOffsetX = 3.9812f;
    constexpr float SeedOffsetY = 7.1543f;
    constexpr float SeedOffsetZ = 5.4321f;
}
```

- [ ] **Step 2: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 5: VoxelNoiseUtils 생성

FBM 로직을 두 파일이 공유할 수 있는 유틸리티로 추출.

**Files:**
- Create: `Source/RTPS/Public/VoxelAuthoring/VoxelNoiseUtils.h`
- Create: `Source/RTPS/Private/VoxelAuthoring/VoxelNoiseUtils.cpp`

- [ ] **Step 1: VoxelNoiseUtils.h 생성**

`Source/RTPS/Public/VoxelAuthoring/VoxelNoiseUtils.h`:

```cpp
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
```

- [ ] **Step 2: VoxelNoiseUtils.cpp 생성**

`Source/RTPS/Private/VoxelAuthoring/VoxelNoiseUtils.cpp`:

```cpp
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
```

- [ ] **Step 3: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

---

## Task 6: RuntimeAuthoringVolume 노이즈 로직 교체

현재 개별 멤버 변수(`NoiseOctaves`, `NoisePersistence` 등)를 `FVoxelNoiseParams NoiseParams` 단일 멤버로 통합하고 FBM 로직을 VoxelNoiseUtils로 교체.

> **주의:** 이 변경은 블루프린트 에디터에서 노이즈 프로퍼티 표시 방식을 바꿔. 기존에 펼쳐진 개별 항목들이 `NoiseParams` 구조체 내부로 들어감. 에디터에서 확인 필요.

**Files:**
- Modify: `Source/RTPS/Public/VoxelAuthoring/RuntimeAuthoringVolume.h`
- Modify: `Source/RTPS/Private/VoxelAuthoring/RuntimeAuthoringVolume.cpp`

- [ ] **Step 1: RuntimeAuthoringVolume.h에서 개별 노이즈 멤버 → FVoxelNoiseParams 교체**

헤더에서 다음 개별 UPROPERTY들을 찾아:

```cpp
UPROPERTY(EditAnywhere, ...)
float NoiseScale;

UPROPERTY(EditAnywhere, ...)
float NoiseWeight;

UPROPERTY(EditAnywhere, ...)
int32 NoiseOctaves;

UPROPERTY(EditAnywhere, ...)
float NoiseLacunarity;

UPROPERTY(EditAnywhere, ...)
float NoisePersistence;

UPROPERTY(EditAnywhere, ...)
float NoiseFloorOffset;

UPROPERTY(EditAnywhere, ...)
int32 NoiseSeed;
```

전부 삭제하고 다음으로 교체:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Noise")
FVoxelNoiseParams NoiseParams;
```

헤더 상단에 include 추가:

```cpp
#include "VoxelAuthoring/VoxelNoiseParams.h"
#include "VoxelAuthoring/VoxelNoiseUtils.h"
```

- [ ] **Step 2: RuntimeAuthoringVolume.cpp FillNoiseDensity() 교체**

`FillNoiseDensity()` 함수 본문을 다음으로 교체:

```cpp
void ARuntimeAuthoringVolume::FillNoiseDensity()
{
    EnsureGridAllocated();

    const FVector SeedOffset = VoxelNoiseUtils::ComputeSeedOffset(NoiseParams.NoiseSeed);

    for (int32 Z = 0; Z < GridDimensions.Z; ++Z)
    {
        for (int32 Y = 0; Y < GridDimensions.Y; ++Y)
        {
            for (int32 X = 0; X < GridDimensions.X; ++X)
            {
                const FVector SamplePos(
                    (static_cast<float>(X) + 0.5f) * NoiseParams.NoiseScale + SeedOffset.X,
                    (static_cast<float>(Y) + 0.5f) * NoiseParams.NoiseScale + SeedOffset.Y,
                    (static_cast<float>(Z) + 0.5f) * NoiseParams.NoiseScale + SeedOffset.Z);

                const float NormalizedNoise = VoxelNoiseUtils::SampleFBM(SamplePos, NoiseParams);

                const float NormalizedY = static_cast<float>(Z) / FMath::Max(static_cast<float>(GridDimensions.Z - 1), 1.f);
                const float HeightGradient = 1.0f - NormalizedY;
                const float RawDensity = HeightGradient - NoiseParams.NoiseFloorOffset + NormalizedNoise * NoiseParams.NoiseWeight;
                DensityGrid[LinearIndex(X, Y, Z)] = FMath::Clamp(RawDensity, 0.f, 1.f);
            }
        }
    }
}
```

- [ ] **Step 3: cpp 파일에서 더 이상 필요 없는 개별 변수 참조 수정**

`RuntimeAuthoringVolume.cpp` 전체에서 `NoiseOctaves`, `NoisePersistence`, `NoiseLacunarity`, `NoiseFloorOffset`, `NoiseWeight`, `NoiseScale`, `NoiseSeed`를 직접 참조하는 곳을 `NoiseParams.NoiseOctaves` 등으로 변경.

`GET_MEMBER_NAME_CHECKED`를 사용하는 코드가 있으면 구조체 내 멤버명도 확인:
```cpp
// 변경 전
GET_MEMBER_NAME_CHECKED(ARuntimeAuthoringVolume, NoiseLacunarity)
// 변경 후
GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseLacunarity)
```

- [ ] **Step 4: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

- [ ] **Step 5: 에디터 확인**

UE5 에디터 실행 → `RuntimeAuthoringVolume` 액터 선택 → 디테일 패널에서 `Noise Params` 구조체 항목이 보이는지, 값 변경 후 노이즈가 정상 적용되는지 확인.

---

## Task 7: VoxelChunk.cpp FBM 로직 교체

**Files:**
- Modify: `Source/RTPS/Private/VoxelAuthoring/VoxelChunk.cpp`

- [ ] **Step 1: VoxelChunk.cpp include 추가**

파일 상단에 추가:

```cpp
#include "VoxelAuthoring/VoxelNoiseUtils.h"
```

- [ ] **Step 2: FBM 루프 교체**

VoxelChunk.cpp에서 FBM 루프 블록을 찾아:

```cpp
float Amplitude = 1.0f;
float Frequency = 1.0f;
float NoiseSum = 0.0f;
float MaxAmplitude = 0.0f;

for (int32 OctaveIndex = 0; OctaveIndex < NoiseParams.NoiseOctaves; ++OctaveIndex)
{
    NoiseSum += FMath::PerlinNoise3D(Base * Frequency) * Amplitude;
    MaxAmplitude += Amplitude;
    Amplitude *= NoiseParams.NoisePersistence;
    Frequency *= NoiseParams.NoiseLacunarity;
}

const float NormalizedNoise = (MaxAmplitude > 0.0f) ? (NoiseSum / MaxAmplitude) : 0.0f;
```

다음으로 교체:

```cpp
const float NormalizedNoise = VoxelNoiseUtils::SampleFBM(Base, NoiseParams);
```

- [ ] **Step 3: SeedOffset 계산 교체**

VoxelChunk.cpp에서 SeedOffset 계산 블록을 찾아:

```cpp
const FVector SeedOffset(
    static_cast<float>(NoiseParams.NoiseSeed) * 3.9812f,
    static_cast<float>(NoiseParams.NoiseSeed) * 7.1543f,
    static_cast<float>(NoiseParams.NoiseSeed) * 5.4321f);
```

다음으로 교체:

```cpp
const FVector SeedOffset = VoxelNoiseUtils::ComputeSeedOffset(NoiseParams.NoiseSeed);
```

- [ ] **Step 4: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

- [ ] **Step 5: 에디터 확인**

UE5 에디터에서 VoxelTestMap 열기 → 청크 노이즈 생성이 이전과 동일하게 작동하는지 확인.

- [ ] **Step 6: Perforce 체크인**

변경 목록: Task 4~7 전체  
설명: `refactor: extract FBM noise into VoxelNoiseUtils, unify RuntimeAuthoringVolume noise params into FVoxelNoiseParams`

---

## Task 8: VoxelChunkManager 인라인 좌표 계산 교체

`VoxelChunkManager.cpp` 53~60번 줄에 `ChunkCoordToWorldCenter()`와 동일한 계산이 인라인으로 중복되어 있음. 이미 존재하는 메서드 호출로 교체.

**Files:**
- Modify: `Source/RTPS/Private/VoxelAuthoring/VoxelChunkManager.cpp`

- [ ] **Step 1: 인라인 계산 교체**

53~60번 줄 근처에서 다음 패턴을 찾아:

```cpp
ChunkDimensions.X * CellSize * 0.5f,
ChunkDimensions.Y * CellSize * 0.5f,
ChunkDimensions.Z * CellSize * 0.5f
```

및

```cpp
(Pair.Key.X + 0.5f) * ChunkDimensions.X * CellSize,
(Pair.Key.Y + 0.5f) * ChunkDimensions.Y * CellSize,
(Pair.Key.Z + 0.5f) * ChunkDimensions.Z * CellSize
```

컨텍스트를 파악한 뒤 `ChunkCoordToWorldCenter(Pair.Key)` 호출로 교체 가능한지 확인. 교체 가능하면 변경.

- [ ] **Step 2: 빌드 확인**

Visual Studio에서 `RTPS Editor` 타겟 빌드. 에러 없이 통과해야 함.

- [ ] **Step 3: Perforce 체크인**

설명: `refactor: replace inline coordinate calculations with ChunkCoordToWorldCenter()`

---

## 완료 기준

- [ ] 모든 Task 빌드 통과
- [ ] 에디터에서 VoxelTestMap 노이즈 지형 정상 생성
- [ ] `Source/RTPS/Public/Character/` 디렉토리에 `RTPSCharacterAI.h`, `RTPSCharacterNonPlayer.h` 없음
- [ ] `RuntimeAuthoringVolume.cpp`에 `#if 0` 블록 없음
- [ ] FBM 루프가 `RuntimeAuthoringVolume.cpp`와 `VoxelChunk.cpp` 어디에도 직접 존재하지 않음
