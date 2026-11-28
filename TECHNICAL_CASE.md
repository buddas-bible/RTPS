# RTPS 기술 사례

## 동시 복셀 편집에서 최신 지형 유지하기

### 문제와 재현

두 플레이어가 같은 청크 경계에서 동시에 발판을 만들고 부수면, 같은 청크에 여러 Marching Cubes 작업이 큐에 들어간다. 기존 구현은 각 작업이 시작할 때 밀도 배열을 복사했고, 완료 콜백에서 그 복사본을 청크에 다시 썼다. 작업 완료 순서가 바뀌면 오래된 결과가 최신 밀도와 메시를 되돌릴 수 있었다.

실제 로컬 listen host와 원격 역할의 별도 클라이언트를 연결했다. 중앙의 동일 경계에 호스트 생성 12회, 참가자 파괴 12회를 적용했다. 양쪽 비교에 같은 300 ms 지연을 첫 밀도 재생성 작업에 주입해 늦은 완료를 유도했다. 기존 경로에서는 이미 더 최신 작업이 적용된 뒤 다른 밀도를 쓰는 완료가 서버에서 네 번 관측됐다.

### 변경

- 편집과 스냅샷 수신 경로에서 밀도를 적용한다. 메시 작업은 읽기 전용 복사본을 처리하고 결과 메시만 반환한다.
- 청크당 편집 메시 작업은 하나씩 실행한다. 처리 중에 들어온 요청은 최신 세대에 모아 작업이 끝나면 그 상태로 한 번 시작한다.
- 게임 스레드에 도착한 결과의 세대가 현재 요청과 같을 때만 메시와 충돌을 갱신한다. 초기 비동기 로드도 같은 세대 검사로 갱신된 밀도를 보호한다.

변경은 [청크 구현](Source/RTPS/Private/VoxelAuthoring/VoxelChunk.cpp)과 [청크 상태](Source/RTPS/Public/VoxelAuthoring/VoxelChunk.h)에 있다. [수정 커밋](https://github.com/buddas-bible/RTPS/commit/a1aca551e33214c5b85a63dfb063114096759594)

### 측정

UE 5.8.4 Win64 Development에서 렌더링을 끈 로컬 2인 프로세스로 두 경기를 연속 실행했다. 각 경기 시작 후 약 0.5~3.25초 동안 서버의 연속 프레임 사이 경과 시간, 시작된 메시 작업 수, 엔진의 서버 전체 패킷 송신 바이트를 기록했다.

| 서버 지표 | 기존 경로 | 수정 경로 |
|---|---:|---:|
| 프레임 표본 | 2,657 | 2,668 |
| 프레임 간격 p95 | 1.659 ms | 1.688 ms |
| 프레임 간격 p99 | 2.501 ms | 2.414 ms |
| 메시 재생성 시작 수 | 192 | 159 |
| 측정 구간 서버 송신 | 15,151 B | 15,268 B |
| 최신 밀도를 덮은 늦은 완료 | 4 | 0 |

메시 시작 수가 **17.2% 감소**했다. 한 쌍의 짧은 로컬 실행에서 p95와 p99는 서로 다른 방향으로 변했고 송신량도 비슷했다. 이 측정은 서버 전체 패킷을 집계하며, 실서비스 지연이나 손실 조건에서의 성능은 측정하지 않았다.

편집 뒤 상대 발판을 제거해 아웃을 유도했다. 두 구현 모두 종료 시점에 서버와 참가자의 청크 8개 밀도 CRC가 `1770617245`로 같았고, 양쪽 모두 팀 0 승리를 받았다. 원래 버그는 종료 상태만 비교하면 놓칠 수 있는 **경기 중 지형 롤백**이었다.

### 패킷 크기에서 확인한 한계

별도의 초기 2인 검증에서 64³ 셀 청크의 전체 밀도 배열은 274,625개 샘플이었고 Unreal의 복제 배열 한도를 넘었다. 현재 아레나는 16³ 셀과 고정 청크 범위를 사용해 전체 스냅샷을 청크당 4,913개 샘플로 제한한다. 스냅샷 분할이나 압축을 적용한 측정은 이번 결과에 포함되지 않는다.

## Steam 재경기 자동 검증과 JSONL 기록 충돌 해결

### 실패와 원인

Windows 호스트와 VirtualBox의 Windows 클라이언트를 서로 다른 Steam 계정으로 연결했다. VM 클라이언트는 Steam 검색으로 호스트 로비를 찾아 UI 행을 만들고 선택 요청도 처리했다. 게임 로그에는 방 선택 직후 `failed to append row_selected`가 찍혔다. 검증 스크립트는 필수 이벤트를 기다리다 `select` 단계에서 타임아웃됐다.

검증 스크립트는 상태를 읽을 때마다 VirtualBox로 VM의 실시간 `events.jsonl`을 직접 복사했다. Unreal의 기존 기록 경로는 `FILEWRITE_Append`만 지정해 Windows에서 읽기 공유 없이 파일을 열었다. 읽기 핸들을 열어 둔 채 기존 방식으로 추가 기록할 때 Windows 공유 위반 `0x80070020`을 재현했다.

### 수정

- 게임의 JSONL 기록에 `FILEWRITE_AllowRead`를 추가했다.
- VM의 스냅샷 작성기는 실시간 파일을 `FileShare.ReadWrite`로 연다. VirtualBox는 작성이 끝난 스냅샷만 호스트로 복사한다.
- 검증 조건은 호스트가 생성한 로비 ID와 일치하는 `row_selected` 이벤트를 요구한다. 참가 후에는 SteamSockets 연결과 양쪽의 경기 결과 및 로비 복귀를 확인한다.

[게임 기록 코드](Source/RTPS/Private/Game/RTPSRuntimeTest.cpp) · [Steam 검증 스크립트](Tools/Test-RTPSRuntimeSteam.ps1) · [수정 커밋](https://github.com/buddas-bible/RTPS/commit/0e05f891cb5db7852d67978065e0cb460e71740c)

### 재현과 결과

저장소 루트에서 PowerShell로 실행한다. 호스트와 실행 중인 VirtualBox VM에 서로 다른 Steam 계정으로 로그인한다. VM에는 Guest Additions가 필요하다. `$ue`는 UE 5.8 설치 경로이며 `$guestPasswordFile`은 VM의 Windows 계정 암호가 담긴 로컬 파일 경로다.

```powershell
pwsh -NoProfile -File ./Tools/Test-RTPSCommitSteam.ps1 -Commit 3cb9f9f -EngineRoot $ue -VmName 'RTPS Steam Guest' -GuestUser rtpstest -GuestPasswordFile $guestPasswordFile -Repeat 3
```

2026년 11월 28일, UE 5.8.4에서 별도 작업 트리로 Win64 Development 패키지를 한 번 만들었다. 동일한 패키지로 화면 캡처 없이 Steam 방 검색·선택·참가부터 재경기 후 로비 복귀까지 세 번 검증했다. 각 회차에서 호스트의 승리와 참가자의 패배가 기록됐고, 두 클라이언트의 승리 팀 정보가 일치했다. [검증 커밋](https://github.com/buddas-bible/RTPS/commit/3cb9f9fec7fe3ac846640a3feaa92bebab0f8cd7)

| 회차 | `row_selected` 로비 ID | 완료 경기 | JSONL 결과 |
|---:|---|---:|---|
| 1 | `109775241484717007` | 2 | `PASS` |
| 2 | `109775241485402724` | 2 | `PASS` |
| 3 | `109775241485404176` | 2 | `PASS` |

통합 `result.jsonl`에서 세 `attempt_finished`와 마지막 `test_finished`가 모두 `PASS`였고, 프로세스 종료 코드는 0이었다. 실행 ID는 `20261128-021240-82420d76`이다. 기본 출력 폴더의 `BuildCookRun.log`에서 패키징 시작이 한 번 기록됐으며, 회차별 `steam_report` 경로에서 상세 JSONL을 확인할 수 있다.

## Steam 로비 검색 타임아웃 해결

### 로그와 엔진 코드로 확인한 원인

실패한 VM 로그에서 Steam 검색 작업은 후보 로비 ID를 9~10개 받았지만, 종료 시점에 상세 정보가 도착한 로비는 매번 하나씩 적었다. 검색은 약 15초 뒤 `success:false`로 끝났다. UE 5.8의 `FOnlineAsyncTaskSteamFindLobbiesBase`는 후보마다 `RequestLobbyData`를 호출한다. `WaitForRequestLobbyData` 상태에서는 요청한 ID 수와 응답을 받은 ID 수가 같아지기를 기다린다. 제한 시간까지 수가 일치하지 않으면 `bWasSuccessful`이 거짓이 되며, `Finalize`는 도착한 일부 결과도 방 목록에 전달하지 않는다.

RTPS는 공유 개발용 Steam App ID `480`을 사용한다. 프로젝트 필터를 적용하자 후보는 줄었지만, 한 회차에서는 RTPS 후보 2개 중 1개의 상세 정보만 도착해 타임아웃이 재현됐다. 이전 검증 프로세스는 Steam 세션 종료 응답을 기다리지 않고 종료하는 방식이었으며, 어느 로비가 응답하지 않았는지는 당시 로그에서 확인할 수 없었다.

### 수정

- **검색 재시도** — 검색 완료의 `success:false`를 즉시 처리한다. 5초 기다린 뒤 다시 검색하며 최초 요청을 포함해 최대 네 번 시도한다. 재시도마다 `search_retry`를 기록한다.
- **Steam에서 후보 필터링** — 호스트는 `RTPS_PROJECT` 태그를 광고하고 검색은 같은 값을 요구한다. Development 자동 검증에서는 호스트와 VM이 동일한 `RTPS_TEST_RUN` 값도 사용한다. Steam이 현재 회차에 해당하는 후보를 반환하므로, 게임이 로비 상세 정보를 요청하기 전에 범위를 줄일 수 있다.
- **세션 정상 종료** — 재경기에서 양쪽이 로비로 돌아온 뒤 참가자와 호스트에 순서대로 `leave`를 보낸다. 각자의 `session_destroyed.success=true`를 확인한 뒤 프로세스를 정리한다.

[로비 코드](Source/RTPS/Private/Game/RTPSGameInstance.cpp) · [Steam 검증 스크립트](Tools/Test-RTPSRuntimeSteam.ps1) · [수정 및 검증 커밋](https://github.com/buddas-bible/RTPS/commit/71b242561071610c753ac37238f62c288e62c8bd)

### 재현과 결과

앞 절과 같은 Steam 및 VirtualBox 로그인 환경에서 저장소 루트의 PowerShell로 실행한다.

```powershell
pwsh -NoProfile -File ./Tools/Test-RTPSCommitSteam.ps1 -Commit 71b2425 -EngineRoot $ue -VmName 'RTPS Steam Guest' -GuestUser rtpstest -GuestPasswordFile $guestPasswordFile -Repeat 3
```

UE 5.8.4의 기본 경로 `Documents/RTPS-V`에서 Win64 Development 패키징을 한 번 실행했다. 같은 패키지로 화면 캡처 없이 방 검색·선택·SteamSockets 참가, 첫 경기와 재경기 후 양쪽 로비 복귀까지 확인했다.

| 회차 | 완료 경기 | 검색 재시도 | 세션 종료 | JSONL 결과 |
|---:|---:|---:|---|---|
| 1 | 2 | 0 | 호스트·VM 성공 | `PASS` |
| 2 | 2 | 0 | 호스트·VM 성공 | `PASS` |
| 3 | 2 | 0 | 호스트·VM 성공 | `PASS` |

통합 `result.jsonl`의 마지막 `test_finished`는 `PASS`였고 종료 코드는 0이었다. 실행 ID는 `20261128-170605-f3df35bd`다. 세 회차의 결과는 자동 검증용 회차 필터가 활성화된 조건에서 얻었다.
