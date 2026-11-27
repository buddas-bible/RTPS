# RTPS — Voxel Arena

RTPS는 UE 5.8 C++로 만든 1대1 복셀 아레나입니다. 상대 발밑을 부수면 아웃시킬 수 있고, 지형을 생성하면 새 발판에 착지할 수 있습니다. 서버가 지형 편집과 낙하를 판정합니다. 결과 화면을 거쳐 로비로 돌아오면 두 사람이 다시 준비하고 초기 지형에서 재경기할 수 있습니다.

## Win64 빌드로 플레이

Windows 패키지의 파일을 같은 폴더 구조로 유지한 채 `RTPS.exe`를 실행합니다. 호스트는 방을 만들고 참가자는 세션에 참가합니다. 로비에서 호스트가 `Q` 또는 `E`로 **Voxel Arena 1v1**을 고릅니다. 두 플레이어가 각각 `R`로 준비하고 호스트가 `G`로 시작합니다. 경기 결과가 표시되고 5초 후 같은 로비로 돌아옵니다.

| 경기 조작 | 키 |
|---|---|
| 이동 및 점프 | WASD, Space |
| 복셀 파괴 | 마우스 왼쪽 버튼 |
| 복셀 생성 | 마우스 오른쪽 버튼 |

### 한 PC에서 두 프로세스로 재현

Win64 Development 패키지의 루트에서 PowerShell 두 개를 열고 아래 명령을 각각 실행합니다. 첫 프로세스가 로비를 열면 두 번째 프로세스를 연결합니다.

```powershell
# 호스트
.\RTPS.exe -windowed -ResX=960 -ResY=540 -nosteam '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL' -port=17845 -RTPSAutoCreateSession -RTPSAutoReadyInLobby -RTPSAutoStartQuest -RTPSAutoQuestId=voxel_arena -RTPSAutoExpectedPlayers=2 -RTPSAutoArenaDemo

# 참가자
.\RTPS.exe 127.0.0.1:17845 -windowed -ResX=960 -ResY=540 -nosteam '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL' -RTPSAutoReadyInLobby
```

자동 재현에서는 호스트가 중앙 발판을 생성하고 착지한 뒤 참가자 발밑을 제거합니다. 양쪽 결과를 확인하면 로비로 돌아가 같은 순서를 반복합니다. 직접 플레이하려면 `-RTPSAutoReadyInLobby`, `-RTPSAutoStartQuest`, `-RTPSAutoArenaDemo`를 빼고 로비 조작을 사용합니다.

## 기술 사례

[동시 복셀 편집에서 최신 밀도와 충돌 유지하기](TECHNICAL_CASE.md)

- [서버 편집 승인](Source/RTPS/Private/Controller/RTPSPlayerController.cpp)
- [청크 메시 생성과 오래된 결과 폐기](Source/RTPS/Private/VoxelAuthoring/VoxelChunk.cpp)
- [편집 연산과 청크 스냅샷 동기화](Source/RTPS/Private/VoxelAuthoring/VoxelChunkManager.cpp)
- [서버 아웃 판정과 재경기](Source/RTPS/Private/Arena/ArenaGameMode_Steal.cpp)

## 소스에서 빌드

이 프로젝트에서 검증한 환경은 UE 5.8.4, Visual Studio 2026의 MSVC 14.51, Windows x64입니다. 저장소를 받은 뒤 Git LFS 콘텐츠를 받아야 합니다.

```powershell
git lfs pull
$ue = 'E:/Game/UE_5.8'  # 자신의 엔진 경로로 변경
$project = (Resolve-Path .\RTPS.uproject).Path
& "$ue/Engine/Build/BatchFiles/Build.bat" RTPSEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
& "$ue/Engine/Build/BatchFiles/RunUAT.bat" BuildCookRun "-project=$project" -target=RTPS -platform=Win64 -clientconfig=Development -build -cook -stage -package -pak -iostore -archive "-archivedirectory=$PWD/Artifacts" '-AdditionalCookerOptions=-noshaderworker' '-ubtargs=-NoHotReloadFromIDE -MaxParallelActions=4' -unattended -nop4
```
## 패키지 검증 기록

패키지 실행 파일 두 개로 메인 메뉴에서 방 생성, 루프백 연결, 두 명 준비, 아레나 진입, 생성 발판 착지, 상대 발판 파괴와 아웃, 결과 HUD, 로비 복귀, 초기 지형에서 재경기까지 확인했습니다. 검증은 Development 빌드에서 스크립트로 조작했으며 Steam 서비스에서의 검색·접속은 포함하지 않았습니다.

유효한 Unreal 패키지 서명이 없는 매니퀸 파일 다섯 개를 제거했습니다. 그중 네 에셋의 기존 참조는 Core Redirect로 프로젝트 안의 유효한 동명 에셋에 연결했습니다. 기존 참조가 발견되지 않은 물리 에셋에는 리디렉션을 추가하지 않았습니다.

UE 5.8.4의 Win64 Development 빌드에서 이전 출력을 지우고 735개 패키지를 전체 쿠킹했습니다. 쿠커는 오류 0건과 경고 27건을 보고했고, 같은 BuildCookRun 실행에서 패키징과 아카이브가 종료 코드 0으로 완료됐습니다. 새 실행 파일 두 개로 경기와 로비 복귀를 각각 두 번 확인했습니다.
