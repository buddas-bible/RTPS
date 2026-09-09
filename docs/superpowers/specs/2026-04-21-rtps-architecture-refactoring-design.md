# RTPS 아키텍처 리팩토링 설계

**작성일:** 2026-04-21  
**목적:** 포트폴리오 공개 및 향후 확장을 위한 전체 시스템 구조 재설계  
**방향:** 파트별 단계적 리팩토링 — 각 단계는 독립적으로 검증 가능

---

## 프로젝트 방향

- **단기 프로토타입:** 3인칭 사격 캐릭터, 거대 보스 전투, 파괴 가능한 Marching Cubes 지형
- **장기 목표:** 코옵 던전 게임, 직업 시스템 (Warrior / Mage / Rogue)
- **멀티플레이:** 서버 권위(Server-Authoritative) 모델

---

## Phase 1 — 기반 정리

**제거:**
- `ARTPSCharacterNonPlayer` — 현재 빈 껍데기. 삭제 후 Phase 4에서 AI Controller 연결 로직을 담은 클래스로 재작성
- `ARTPSCharacterAI` — 내용 없는 빈 클래스, 완전 삭제 (`ARTPSCharacterNonPlayer`로 역할 통합)
- `RuntimeAuthoringVolume.cpp`의 `#if 0` 레거시 블록

**상수화:**
```cpp
// Source/RTPS/Public/VoxelAuthoring/VoxelNoiseConstants.h
namespace VoxelNoiseConstants
{
    constexpr float SeedOffsetX = 3.9812f;
    constexpr float SeedOffsetY = 7.1543f;
    constexpr float SeedOffsetZ = 13.4271f;
    constexpr float GridCoordOffset = 0.5f;
}
```

**영향 없음:** 블루프린트 참조, 기존 동작, 공개 API

---

## Phase 2 — 중복 제거

### FBM 노이즈 생성 유틸리티 추출

현재 `RuntimeAuthoringVolume.cpp`와 `VoxelChunk.cpp` 양쪽에 동일한 Fractal Brownian Motion 구현이 존재.

```cpp
// Source/RTPS/Public/VoxelAuthoring/VoxelNoiseUtils.h
namespace VoxelNoiseUtils
{
    float GenerateFBMNoise(const FVector& SamplePos, const FVoxelNoiseParams& Params);
}
```

두 파일 모두 이 함수를 호출하도록 교체.

### 복셀 좌표 변환 유틸리티 추출

`VoxelChunkManager.cpp`와 `VoxelChunk.h` 등 여러 곳에 분산된 청크 좌표 ↔ 월드 좌표 변환 로직 통합.

```cpp
// Source/RTPS/Public/VoxelAuthoring/VoxelCoordinateSystem.h
namespace VoxelCoordinateSystem
{
    FVector ChunkCoordToWorldCenter(const FIntVector& Coord, float CellSize, const FIntVector& Dimensions);
    FIntVector WorldToChunkCoord(const FVector& WorldPos, float CellSize);
}
```

---

## Phase 3 — 복셀 시스템 구조화

### 현재 문제

`ARuntimeAuthoringVolume` (726줄)이 다음을 모두 담당:
- 밀도 필드 데이터 관리
- 시각화 4가지 모드 (와이어프레임, 밀도 구체, Marching Cubes, 인터랙티브)
- 노이즈 생성
- JSON 파일 I/O
- 입력 처리

### 분리 방향

```
ARuntimeAuthoringVolume (얇은 오케스트레이터)
├── UVoxelDensityGrid        — 밀도 필드 데이터 + 노이즈 생성
├── UVoxelVisualizationManager — 시각화 모드 전환 + 렌더링
└── UVoxelAuthoringPersistence — JSON 저장/불러오기
```

각 컴포넌트는 독립적으로 교체/테스트 가능.

---

## Phase 4 — 게임 프레임워크 분리

### RTPSGameInstance 분리

현재 701줄에 세션 관리 + 퀘스트 정의 + 자동화 플래그가 혼재.

```
RTPSGameInstance (세션 관리 + 생명주기만)
USessionManager          — 세션 생성/탐색/참가 (OnlineSubsystem 연동)
UQuestDefinitionRegistry — 퀘스트 데이터 정의 (하드코딩 → 데이터 에셋으로)
```

### RTPSPlayerController 분리

현재 714줄에 채팅 + 입력 + 로비 UI + 자동화 테스트가 혼재.

```
RTPSPlayerController (입력 수신 + RPC 포워딩만)
UChatController      — 채팅 활성화, 전송, 표시
```

---

## Phase 5 — 문서화

각 시스템 설계 의도를 문서로 작성 (GitHub 공개용).

---

## 캐릭터 클래스 구조

### 설계 원칙

> **상속 = 타입 구분**, **컴포넌트 = 여러 타입이 공유하는 행동**

컴포넌트는 독립적인 데이터와 여러 타입에서 재사용되는 경우에만 사용.  
직업별 완전히 다른 로직은 상속으로 분리.

### 계층 구조

```
ARTPSCharacterBase
├── ARTPSCharacterPlayer          — 플레이어 고유: 입력, 카메라
│   ├── ARTPSCharacterWarrior     — 전사 고유 로직/스탯
│   ├── ARTPSCharacterMage        — 마법사 고유 로직/스탯
│   └── ARTPSCharacterRogue       — 로그 고유 로직/스탯
└── ARTPSCharacterNonPlayer       — AI Controller 연결 공통
    ├── ARTPSCharacterEnemy        — 적 공통 데이터: 드롭, XP, 진영
    │   ├── ARTPSEnemyNormal
    │   └── ARTPSEnemyBoss         — 페이즈 데이터
    └── ARTPSCharacterNPC          — 상호작용, 대화 트리
```

### 공통 컴포넌트 (진짜 공유되는 것만)

```
UHealthComponent    — 체력/힐/데미지 처리 (모든 캐릭터 공통)
UCombatComponent    — 기본 데미지 계산/피격 반응 (Enemy, Player 공통)
UInteractionComponent — 대화/상점 상호작용 (NPC 전용)
```

AI 행동(순찰, 어그로)은 컴포넌트가 아닌 **Behavior Tree Task**로 구현.

---

## 보스 AI 구조

### 설계 원칙

- **서버 독점 소유:** 모든 보스 상태 판단과 실행은 서버에서만 수행
- **클라이언트:** Replicate된 결과만 받아서 시각적으로 표현

### 구조

```
ARTPSEnemyBoss (서버 소유)
└── ARTPSBossAIController
    ├── UBossPhaseStateMachine    — 페이즈 판단 (복합 조건 평가)
    └── UBehaviorTree             — 페이즈 내 행동 실행
```

### 페이즈 스테이트 머신

복합 조건(AND/OR 조합)을 C++ 데이터로 정의:

```cpp
struct FBossPhaseTransition
{
    EBossPhase From;
    EBossPhase To;
    TArray<FBossCondition> Conditions; // AND 평가
};

// 조건 예시
FBossCondition::HPBelow(0.5f)
FBossCondition::TimeInPhaseAbove(30.f)
FBossCondition::PlayerDistanceAbove(1000.f)
FBossCondition::AllPlayersDistanceAbove(2000.f)  // 멀티보스 대응
```

### BT 역할 분리

```
페이즈 스테이트 머신 → "지금 어떤 페이즈인가" 결정
Behavior Tree       → "이 페이즈에서 어떻게 행동하는가" 실행

Phase1 → BTTree_Phase1 (기본 공격)
Phase2 → BTTree_Phase2 (범위 공격 추가)
Phase3 → BTTree_Enraged (전체 패턴)
```

---

## 아이템 시스템

### 상태별 표현

| 상태 | 표현 | 서버 소유 |
|------|------|----------|
| 월드에 드롭 | World Actor (스폰) | ✓ 서버 스폰, 클라 복제 |
| 소지 중 | 인벤토리 ID 배열 (데이터) | ✓ PlayerState 복제 |
| 장착 중 | 캐릭터 소켓에 메시 붙임 | ✓ 복제 |

### 손 역할

- **오른손:** 장착 아이템이 사용 가능한 액션 결정 (공격, 사격 등)
- **왼손:** 시각 표현 + 패시브 효과만 (빛, 방어력 보너스 등)

### 아이템 조합

소규모(수십 가지) → 데이터 에셋 룩업 테이블로 관리:

```cpp
// 오른손 아이템 타입 → 가능한 액션 세트
TMap<EItemType, FItemActionSet> RightHandActionTable;
```

---

## 지형 파괴 시스템

### 단일 진입점 원칙

플레이어와 몬스터 양쪽에서 지형 파괴가 발생하지만, 모든 요청은 서버의 `VoxelChunkManager`를 통해 처리:

```
플레이어 → Server_ApplyBrush RPC ──┐
                                    ├──→ VoxelChunkManager::ApplyBrush() → 청크 업데이트 → Multicast
보스/몬스터 → 서버 직접 호출    ──┘
```

서버에서 순차 처리되므로 동시 편집 충돌은 자연스럽게 해결.

---

## 어빌리티 시스템

### 결정: 커스텀 직접 구현

**이유:** 신입 포트폴리오에서 "왜 이렇게 설계했는가"를 설명하려면 직접 구현한 코드여야 함.  
GAS는 내부를 직접 구현해본 뒤 도입 시 진정한 가치가 있음.

**프로토타입 단계:** 직업 시스템 없이 사격 + 보스 + 지형 먼저 완성.  
**직업 시스템 단계:** 커스텀 어빌리티 시스템 설계 — 스킬 실행, 쿨다운, 멀티플레이 복제 직접 구현.

---

## 지금 결정된 것 vs 나중에 결정할 것

### 지금 결정됨
- 캐릭터 계층 구조와 원칙
- 보스 AI 아키텍처 (페이즈 머신 + BT)
- 아이템 상태별 표현 방식
- 지형 파괴 단일 진입점
- 어빌리티 시스템 방향 (커스텀)

### 나중에 결정
- 직업별 구체적인 스킬 목록
- 보스별 페이즈 조건 수치
- 아이템 종류와 무게 수치
- NPC 대화 시스템 구체 구현
- GAS 마이그레이션 시점

---

## 시스템 간 의존 방향

```
Character ──→ HealthComponent, CombatComponent
BossAIController ──→ BossPhaseStateMachine ──→ BehaviorTree
PlayerController ──→ VoxelChunkManager (RPC)
BossEnemy ──→ VoxelChunkManager (직접)
PlayerController ──→ ChatController
GameInstance ──→ SessionManager, QuestDefinitionRegistry
```

순환 의존 없음. 모든 의존은 단방향.
