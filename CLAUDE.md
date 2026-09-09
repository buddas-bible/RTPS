# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

UE 5.5 multiplayer third-person RPG with runtime voxel terrain editing. Single C++ module (`RTPS`) targeting Windows, Linux, LinuxArm64. Steam (primary) and Null online subsystems.

## Build

Open `RTPS.sln` in Visual Studio 2022 with the UE Workspace workload. Build targets:
- `RTPS` — game client
- `RTPSServer` — dedicated server
- `RTPSEditor` — editor build

Module dependencies are declared in `Source/RTPS/RTPS.Build.cs`. Add new dependencies there before using headers.

Maps cooked for packaging: `MainMenuMap`, `ThirdPersonMap`, `VoxelTestMap`, `CharacterTestLevel` (see `Config/DefaultGame.ini`).

## Architecture

### Game Framework
`RTPSGameInstance` is the singleton for session/online subsystem management and quest data. `RTPSGameMode` / `RTPSLobbyGameMode` / `RTPSLoginGameMode` handle match initialization and player spawning per map. `RTPSGameState` holds replicated match data.

### Character System
`ARTPSCharacterBase` owns health, damage, and death montage. `ARTPSCharacterPlayer` adds input handling and voxel edit interaction. AI characters (`ARTPSCharacterAI`, `ARTPSEnemy`) use an AI Controller for patrol/chase/attack. `RTPSPlayerState` replicates Steam identity, stats, and team.

### Voxel Terrain System
The core feature in active development. Architecture:
- `ARuntimeAuthoringVolume` — defines world-space containment bounds and exposes brush application interface
- `AVoxelChunk` — replicated actor; 16³ voxel cells with a 17³ float `DensityGrid`; owns `UProceduralMeshComponent` for render/collision and triggers async Marching Cubes rebuild on density change
- `UVoxelChunkManager` — `TMap<FIntVector, AVoxelChunk*>` lifecycle management; exposes `Server_ApplyBrush(FVoxelBrush)` RPC; multicast `Multicast_ChunkUpdated()` notifies clients
- `UVoxelDebugVisualizer` — HISM of spheres, one per density sample point; per-instance custom data `[0]` = density value fed to `M_VoxelDebugSphere` material for gradient color
- `RTPSMarchingCubes` — isosurface extraction; `FVoxelBrush` supports Add/Remove/Smooth operations

Voxel edits are **server-authoritative**: client calls `Server_ApplyBrush`, server mutates density, multicast triggers mesh rebuild on all clients.

### UI System
`ARTPSHUD` is canvas-based. UMG widgets: `WBP_LoginMenu` (session browser), `WBP_Chatting`/`WBP_ChatMessage` (chat replication), `WBP_NameTag` (distance-based player markers), `WBP_SessionSlot` (server list entries).

## Multiplayer Checklist

When modifying any networked class, verify in order:
1. Is the property/function on the correct authority side (server vs. client)?
2. Is the variable marked `Replicated` and registered in `GetLifetimeReplicatedProps`?
3. Are RPCs annotated `Server`, `Client`, or `NetMulticast` with `_Implementation` suffix?
4. Does `RTPS.Build.cs` include `OnlineSubsystem`, `OnlineSubsystemUtils` if touching sessions?
5. For config-driven networking (Steam, voice), check `Config/DefaultEngine.ini` first.

## Key File Locations

| What | Where |
|------|-------|
| Module build rules | `Source/RTPS/RTPS.Build.cs` |
| Network/Steam config | `Config/DefaultEngine.ini` |
| Cook map list | `Config/DefaultGame.ini` |
| Input bindings | `Config/DefaultInput.ini` |
| Voxel architecture spec | `docs/superpowers/specs/2026-04-16-voxel-terrain-runtime-editor-design.md` |
| Local Claude rules | `.claude/CLAUDE.local.md` |
