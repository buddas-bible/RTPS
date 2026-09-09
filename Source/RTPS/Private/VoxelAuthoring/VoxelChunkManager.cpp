#include "VoxelAuthoring/VoxelChunkManager.h"
#include "Controller/RTPSPlayerController.h"
#include "VoxelAuthoring/VoxelChunk.h"
#include "Character/RTPSCharacterPlayer.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"

namespace
{
	FString FormatChunkCoordList(const TArray<FIntVector>& ChunkCoords)
	{
		if (ChunkCoords.IsEmpty())
		{
			return TEXT("None");
		}

		TArray<FString> CoordStrings;
		CoordStrings.Reserve(ChunkCoords.Num());
		for (const FIntVector& ChunkCoord : ChunkCoords)
		{
			CoordStrings.Add(FString::Printf(TEXT("(%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));
		}

		return FString::Join(CoordStrings, TEXT(","));
	}

	const TCHAR* GetVoxelBrushShapeDebugName(EVoxelBrushShape Shape)
	{
		switch (Shape)
		{
		case EVoxelBrushShape::Sphere:
			return TEXT("Sphere");
		case EVoxelBrushShape::Box:
			return TEXT("Box");
		case EVoxelBrushShape::Flatten:
			return TEXT("Flatten");
		case EVoxelBrushShape::Smooth:
			return TEXT("Smooth");
		case EVoxelBrushShape::SurfaceBlob:
			return TEXT("SurfaceBlob");
		case EVoxelBrushShape::TerrainMudBlob:
			return TEXT("TerrainMudBlob");
		default:
			return TEXT("Invalid");
		}
	}

	const TCHAR* GetVoxelBrushBlendModeDebugName(EVoxelBrushBlendMode BlendMode)
	{
		switch (BlendMode)
		{
		case EVoxelBrushBlendMode::Additive:
			return TEXT("Additive");
		case EVoxelBrushBlendMode::TargetMax:
			return TEXT("TargetMax");
		case EVoxelBrushBlendMode::TargetLerp:
			return TEXT("TargetLerp");
		default:
			return TEXT("Invalid");
		}
	}

	const TCHAR* GetVoxelBrushFalloffDebugName(EVoxelBrushFalloff Falloff)
	{
		switch (Falloff)
		{
		case EVoxelBrushFalloff::Linear:
			return TEXT("Linear");
		case EVoxelBrushFalloff::Smooth:
			return TEXT("Smooth");
		case EVoxelBrushFalloff::Spherical:
			return TEXT("Spherical");
		case EVoxelBrushFalloff::Plateau:
			return TEXT("Plateau");
		default:
			return TEXT("Invalid");
		}
	}

	FVector GetSafeSurfaceBlobNormal(const FVoxelBrush& Brush)
	{
		const FVector SurfaceNormal = Brush.SurfaceNormal.GetSafeNormal();
		return SurfaceNormal.IsNearlyZero() ? FVector::UpVector : SurfaceNormal;
	}

	FBox BuildOrientedBlobBounds(const FVoxelBrush& Brush)
	{
		const float Radius = FMath::Max(Brush.Radius, 0.f);
		const float FrontDepth = FMath::Max(Brush.SurfaceDepth, 0.f);
		const float BackDepth = Brush.Shape == EVoxelBrushShape::TerrainMudBlob ? FMath::Max(Brush.BackDepth, 0.f) : 0.f;
		const FVector SurfaceNormal = GetSafeSurfaceBlobNormal(Brush);
		const FVector TangentExtent(
			Radius * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(SurfaceNormal.X))),
			Radius * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(SurfaceNormal.Y))),
			Radius * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(SurfaceNormal.Z))));
		const FVector FrontExtent = SurfaceNormal * FrontDepth;
		const FVector BackExtent = -SurfaceNormal * BackDepth;
		const FVector MinNormalOffset(
			FMath::Min(BackExtent.X, FrontExtent.X),
			FMath::Min(BackExtent.Y, FrontExtent.Y),
			FMath::Min(BackExtent.Z, FrontExtent.Z));
		const FVector MaxNormalOffset(
			FMath::Max(BackExtent.X, FrontExtent.X),
			FMath::Max(BackExtent.Y, FrontExtent.Y),
			FMath::Max(BackExtent.Z, FrontExtent.Z));
		return FBox(
			Brush.WorldPosition + MinNormalOffset - TangentExtent,
			Brush.WorldPosition + MaxNormalOffset + TangentExtent);
	}

	FBox BuildBrushBounds(const FVoxelBrush& Brush)
	{
		if (Brush.Shape == EVoxelBrushShape::SurfaceBlob || Brush.Shape == EVoxelBrushShape::TerrainMudBlob)
		{
			return BuildOrientedBlobBounds(Brush);
		}

		const float BrushRadius = FMath::Max(Brush.Radius, 0.f);
		const FVector BrushExtent(BrushRadius);
		return FBox(Brush.WorldPosition - BrushExtent, Brush.WorldPosition + BrushExtent);
	}
}

AVoxelChunkManager::AVoxelChunkManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Global voxel edit coordinator for multicast brush application; revisit relevancy if large worlds use multiple managers.
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VoxelChunkManagerRoot"));
	SetRootComponent(SceneRoot);
}

void AVoxelChunkManager::BeginPlay()
{
	Super::BeginPlay();

	if (bUseFixedArenaBounds)
	{
		LoadFixedArenaBounds();
	}

	if (ShouldUseCameraStreaming())
	{
		UpdateStreaming(GetCameraWorldLocation(), GetCameraForwardVector());
		LoadedChunkCount = LoadedChunks.Num();
	}
}

void AVoxelChunkManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ShouldUseCameraStreaming())
	{
		StreamingAccumulator += DeltaTime;
		if (StreamingAccumulator >= StreamingTickInterval)
		{
			StreamingAccumulator = 0.f;
			UpdateStreaming(GetCameraWorldLocation(), GetCameraForwardVector());
			LoadedChunkCount = LoadedChunks.Num();
		}
	}

	RefreshReadyChunkStateMirrors(TEXT("TickReadyChunkMirror"));
	if (HasAuthority())
	{
		FlushQueuedChunkStatePayloads(DeltaTime);
	}

	if (bDebugShowChunkWireframes)
	{
		const UWorld* DebugWorld = GetWorld();
		for (const TPair<FIntVector, TObjectPtr<AVoxelChunk>>& Pair : LoadedChunks)
		{
			const AVoxelChunk* Chunk = Pair.Value.Get();
			if (!IsValid(Chunk))
			{
				continue;
			}

			const FVector HalfExtent(
				ChunkDimensions.X * CellSize * 0.5f,
				ChunkDimensions.Y * CellSize * 0.5f,
				ChunkDimensions.Z * CellSize * 0.5f);

			const FVector Center = ChunkCoordToWorldCenter(Pair.Key);

			FColor WireColor = FColor::Silver;
			if (Chunk->State == EVoxelChunkState::Ready)
			{
				WireColor = Chunk->IsRendered() ? FColor::Green : FColor::Red;
			}

			DrawDebugBox(DebugWorld, Center, HalfExtent, WireColor, false, -1.f, 0, 3.f);
		}
	}
}

#if WITH_EDITOR
void AVoxelChunkManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName != GET_MEMBER_NAME_CHECKED(AVoxelChunkManager, ChunkMaterial))
	{
		return;
	}

	for (TPair<FIntVector, TObjectPtr<AVoxelChunk>>& Pair : LoadedChunks)
	{
		AVoxelChunk* Chunk = Pair.Value;
		if (!IsValid(Chunk))
		{
			continue;
		}

		Chunk->ChunkMaterial = ChunkMaterial;
		if (Chunk->State != EVoxelChunkState::Ready)
		{
			continue;
		}

		if (UProceduralMeshComponent* ProcMesh = Chunk->FindComponentByClass<UProceduralMeshComponent>())
		{
			ProcMesh->SetMaterial(0, ChunkMaterial);
		}
	}
}
#endif

FVector AVoxelChunkManager::GetCameraWorldLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
			{
				return CameraManager->GetCameraLocation();
			}

			if (APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->GetActorLocation();
			}
		}
	}

	return FVector::ZeroVector;
}

FVector AVoxelChunkManager::GetCameraForwardVector() const
{
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
			{
				return CameraManager->GetCameraRotation().Vector();
			}

			if (APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->GetActorForwardVector();
			}
		}
	}

	return FVector::ForwardVector;
}

FIntVector AVoxelChunkManager::WorldToChunkCoord(FVector WorldPos) const
{
	const float ChunkWorldSizeX = FMath::Max(ChunkDimensions.X * CellSize, 1.f);
	const float ChunkWorldSizeY = FMath::Max(ChunkDimensions.Y * CellSize, 1.f);
	const float ChunkWorldSizeZ = FMath::Max(ChunkDimensions.Z * CellSize, 1.f);

	return FIntVector(
		FMath::FloorToInt(WorldPos.X / ChunkWorldSizeX),
		FMath::FloorToInt(WorldPos.Y / ChunkWorldSizeY),
		FMath::FloorToInt(WorldPos.Z / ChunkWorldSizeZ));
}

FVector AVoxelChunkManager::ChunkCoordToWorldCenter(FIntVector ChunkCoord) const
{
	return FVector(
		(ChunkCoord.X + 0.5f) * ChunkDimensions.X * CellSize,
		(ChunkCoord.Y + 0.5f) * ChunkDimensions.Y * CellSize,
		(ChunkCoord.Z + 0.5f) * ChunkDimensions.Z * CellSize);
}

float AVoxelChunkManager::ChunkDistanceFromCamera(FIntVector ChunkCoord, FVector CameraPos) const
{
	return FVector::Dist(ChunkCoordToWorldCenter(ChunkCoord), CameraPos);
}

bool AVoxelChunkManager::ShouldUseCameraStreaming() const
{
	return StreamingMode == EVoxelStreamingMode::Streaming;
}

TArray<FIntVector> AVoxelChunkManager::BuildExpectedBoundaryNeighborChunkCoords(
	const FIntVector& HitChunkCoord,
	bool bNearMinX,
	bool bNearMaxX,
	bool bNearMinY,
	bool bNearMaxY,
	bool bNearMinZ,
	bool bNearMaxZ)
{
	TArray<int32> XOffsets { 0 };
	TArray<int32> YOffsets { 0 };
	TArray<int32> ZOffsets { 0 };

	if (bNearMinX)
	{
		XOffsets.Add(-1);
	}
	if (bNearMaxX)
	{
		XOffsets.Add(1);
	}
	if (bNearMinY)
	{
		YOffsets.Add(-1);
	}
	if (bNearMaxY)
	{
		YOffsets.Add(1);
	}
	if (bNearMinZ)
	{
		ZOffsets.Add(-1);
	}
	if (bNearMaxZ)
	{
		ZOffsets.Add(1);
	}

	TArray<FIntVector> NeighborChunkCoords;
	for (const int32 ZOffset : ZOffsets)
	{
		for (const int32 YOffset : YOffsets)
		{
			for (const int32 XOffset : XOffsets)
			{
				if (XOffset == 0 && YOffset == 0 && ZOffset == 0)
				{
					continue;
				}

				NeighborChunkCoords.AddUnique(HitChunkCoord + FIntVector(XOffset, YOffset, ZOffset));
			}
		}
	}

	return NeighborChunkCoords;
}

void AVoxelChunkManager::UpdateStreaming(FVector CameraPos, FVector CameraForward)
{
	const FIntVector CenterChunk = WorldToChunkCoord(CameraPos);
	const float ChunkWorldSizeX = ChunkDimensions.X * CellSize;
	const float ChunkWorldSizeY = ChunkDimensions.Y * CellSize;
	const float ChunkWorldSizeZ = ChunkDimensions.Z * CellSize;
	const float MaxChunkWorldSize = FMath::Max(ChunkWorldSizeX, FMath::Max(ChunkWorldSizeY, ChunkWorldSizeZ));

	// Use the larger front/back radius as the candidate iteration range.
	const int32 LoopRange = FMath::Max(ViewDistanceFront, ViewDistanceBack);

	for (int32 OffsetZ = -LoopRange; OffsetZ <= LoopRange; ++OffsetZ)
	{
		for (int32 OffsetY = -LoopRange; OffsetY <= LoopRange; ++OffsetY)
		{
			for (int32 OffsetX = -LoopRange; OffsetX <= LoopRange; ++OffsetX)
			{
				const FIntVector CandidateChunk = CenterChunk + FIntVector(OffsetX, OffsetY, OffsetZ);
				if (LoadedChunks.Contains(CandidateChunk))
				{
					continue;
				}

				const FVector ChunkCenter = ChunkCoordToWorldCenter(CandidateChunk);

				const FVector ToChunk = ChunkCenter - CameraPos;
				const float Dot = FVector::DotProduct(ToChunk.GetSafeNormal(), CameraForward);
				const bool bInFront = Dot >= 0.f;
				const float LoadRadius = (bInFront ? ViewDistanceFront : ViewDistanceBack) * MaxChunkWorldSize;

				if (FVector::Dist(ChunkCenter, CameraPos) <= LoadRadius)
				{
					SpawnChunk(CandidateChunk);
				}
			}
		}
	}

	TArray<FIntVector> ChunksToDestroy;
	for (const TPair<FIntVector, TObjectPtr<AVoxelChunk>>& Pair : LoadedChunks)
	{
		const FVector ChunkCenter = ChunkCoordToWorldCenter(Pair.Key);

		const FVector ToChunk = ChunkCenter - CameraPos;
		const float Dot = FVector::DotProduct(ToChunk.GetSafeNormal(), CameraForward);
		const bool bInFront = Dot >= 0.f;
		const int32 EffectiveUnload = FMath::Max(
			bInFront ? UnloadDistanceFront : UnloadDistanceBack,
			bInFront ? ViewDistanceFront : ViewDistanceBack);
		const float UnloadRadius = EffectiveUnload * MaxChunkWorldSize;

		if (FVector::Dist(ChunkCenter, CameraPos) > UnloadRadius)
		{
			ChunksToDestroy.Add(Pair.Key);
		}
	}

	for (const FIntVector& ChunkCoord : ChunksToDestroy)
	{
		DestroyChunk(ChunkCoord);
	}

	LoadedChunkCount = LoadedChunks.Num();
}

void AVoxelChunkManager::SpawnChunk(FIntVector ChunkCoord)
{
	if (LoadedChunks.Contains(ChunkCoord))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector WorldLocation(
		ChunkCoord.X * ChunkDimensions.X * CellSize,
		ChunkCoord.Y * ChunkDimensions.Y * CellSize,
		ChunkCoord.Z * ChunkDimensions.Z * CellSize);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;

	AVoxelChunk* Chunk = World->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		WorldLocation,
		FRotator::ZeroRotator,
		SpawnParameters);

	if (!Chunk)
	{
		return;
	}

	Chunk->ChunkCoord = ChunkCoord;
	Chunk->ChunkDimensions = ChunkDimensions;
	Chunk->CellSize = CellSize;
	Chunk->IsoLevel = IsoLevel;
	Chunk->NoiseParams = NoiseParams;
	Chunk->Source = GenerationSource;
	Chunk->SaveDir = ChunkSaveDir;
	Chunk->ChunkMaterial = ChunkMaterial;
	Chunk->bDebugVoxelMeshRebuilds = bDebugVoxelMeshRebuilds;
	Chunk->BeginAsyncLoad();

	LoadedChunks.Add(ChunkCoord, Chunk);
}

void AVoxelChunkManager::DestroyChunk(FIntVector ChunkCoord)
{
	TObjectPtr<AVoxelChunk>* ChunkPtr = LoadedChunks.Find(ChunkCoord);
	if (!ChunkPtr)
	{
		return;
	}

	AVoxelChunk* Chunk = ChunkPtr->Get();
	if (IsValid(Chunk))
	{
		if (Chunk->bModified)
		{
			Chunk->SaveToDisk();
		}

		Chunk->Destroy();
	}

	if (!HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			if (ARTPSPlayerController* PlayerController = Cast<ARTPSPlayerController>(World->GetFirstPlayerController()))
			{
				if (PlayerController->IsLocalController())
				{
					PlayerController->ServerUnsubscribeVoxelChunk(ChunkCoord);
				}
			}
		}
	}

	LoadedChunks.Remove(ChunkCoord);
	LastSubscribedKnownRevisionByCoord.Remove(ChunkCoord);
	LastChunkSubscribeRequestTimeByCoord.Remove(ChunkCoord);
	if (!HasAuthority())
	{
		AppliedEditSequencesByChunk.Remove(ChunkCoord);
		PendingIncomingChunkPayloads.Remove(ChunkCoord);
		LastAppliedRemoteRevisionByCoord.Remove(ChunkCoord);
		ChunkStates.Remove(ChunkCoord);
		ChunksNeedingFullSnapshotResync.Remove(ChunkCoord);
	}
}

void AVoxelChunkManager::LoadChunksAroundCamera()
{
	UpdateStreaming(GetCameraWorldLocation(), GetCameraForwardVector());
}

void AVoxelChunkManager::LoadFixedArenaBounds()
{
	const FIntVector MinCoord(
		FMath::Min(FixedArenaMinChunkCoord.X, FixedArenaMaxChunkCoord.X),
		FMath::Min(FixedArenaMinChunkCoord.Y, FixedArenaMaxChunkCoord.Y),
		FMath::Min(FixedArenaMinChunkCoord.Z, FixedArenaMaxChunkCoord.Z));
	const FIntVector MaxCoord(
		FMath::Max(FixedArenaMinChunkCoord.X, FixedArenaMaxChunkCoord.X),
		FMath::Max(FixedArenaMinChunkCoord.Y, FixedArenaMaxChunkCoord.Y),
		FMath::Max(FixedArenaMinChunkCoord.Z, FixedArenaMaxChunkCoord.Z));

	const int32 RequestedChunkCount =
		(MaxCoord.X - MinCoord.X + 1) *
		(MaxCoord.Y - MinCoord.Y + 1) *
		(MaxCoord.Z - MinCoord.Z + 1);

	for (int32 ChunkZ = MinCoord.Z; ChunkZ <= MaxCoord.Z; ++ChunkZ)
	{
		for (int32 ChunkY = MinCoord.Y; ChunkY <= MaxCoord.Y; ++ChunkY)
		{
			for (int32 ChunkX = MinCoord.X; ChunkX <= MaxCoord.X; ++ChunkX)
			{
				SpawnChunk(FIntVector(ChunkX, ChunkY, ChunkZ));
			}
		}
	}

	LoadedChunkCount = LoadedChunks.Num();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Fixed arena chunk bounds loaded. ChunkManager=%s Min=(%d,%d,%d) Max=(%d,%d,%d) RequestedChunkCount=%d LoadedChunkCount=%d CameraStreamingActive=%d"),
		*GetNameSafe(this),
		MinCoord.X,
		MinCoord.Y,
		MinCoord.Z,
		MaxCoord.X,
		MaxCoord.Y,
		MaxCoord.Z,
		RequestedChunkCount,
		LoadedChunkCount,
		ShouldUseCameraStreaming() ? 1 : 0);
}

void AVoxelChunkManager::UnloadAllChunks()
{
	TArray<FIntVector> ChunkCoords;
	LoadedChunks.GetKeys(ChunkCoords);

	for (const FIntVector& ChunkCoord : ChunkCoords)
	{
		DestroyChunk(ChunkCoord);
	}

	LoadedChunks.Empty();
	LoadedChunkCount = 0;
}

void AVoxelChunkManager::SaveAllModifiedChunks()
{
	for (const TPair<FIntVector, TObjectPtr<AVoxelChunk>>& Pair : LoadedChunks)
	{
		AVoxelChunk* Chunk = Pair.Value;
		if (IsValid(Chunk) && Chunk->bModified)
		{
			MirrorChunkDensityToState(Pair.Key, *Chunk, TEXT("SaveAllModifiedChunks"));
			Chunk->SaveToDisk();
		}
	}
}

FRTPSVoxelChunkBoundaryDebugInfo AVoxelChunkManager::BuildChunkBoundaryDebugInfo(
	const AVoxelChunk& HitChunk,
	const FVector& HitWorldPosition) const
{
	FRTPSVoxelChunkBoundaryDebugInfo BoundaryInfo;
	BoundaryInfo.HitChunkCoord = HitChunk.ChunkCoord;
	BoundaryInfo.ChunkWorldOrigin = HitChunk.GetActorLocation();
	BoundaryInfo.ChunkWorldSize = FVector(
		HitChunk.ChunkDimensions.X * HitChunk.CellSize,
		HitChunk.ChunkDimensions.Y * HitChunk.CellSize,
		HitChunk.ChunkDimensions.Z * HitChunk.CellSize);
	BoundaryInfo.LocalPosition = HitWorldPosition - BoundaryInfo.ChunkWorldOrigin;
	BoundaryInfo.DistanceToMinBoundary = BoundaryInfo.LocalPosition;
	BoundaryInfo.DistanceToMaxBoundary = BoundaryInfo.ChunkWorldSize - BoundaryInfo.LocalPosition;

	const float Threshold = FMath::Max(BoundaryDebugThresholdWorldUnits, 0.f);
	const bool bNearMinX = BoundaryInfo.DistanceToMinBoundary.X <= Threshold;
	const bool bNearMaxX = BoundaryInfo.DistanceToMaxBoundary.X <= Threshold;
	const bool bNearMinY = BoundaryInfo.DistanceToMinBoundary.Y <= Threshold;
	const bool bNearMaxY = BoundaryInfo.DistanceToMaxBoundary.Y <= Threshold;
	const bool bNearMinZ = BoundaryInfo.DistanceToMinBoundary.Z <= Threshold;
	const bool bNearMaxZ = BoundaryInfo.DistanceToMaxBoundary.Z <= Threshold;

	if (bNearMinX)
	{
		BoundaryInfo.BoundaryAxes.Add(TEXT("-X"));
	}
	if (bNearMaxX)
	{
		BoundaryInfo.BoundaryAxes.Add(TEXT("+X"));
	}
	if (bNearMinY)
	{
		BoundaryInfo.BoundaryAxes.Add(TEXT("-Y"));
	}
	if (bNearMaxY)
	{
		BoundaryInfo.BoundaryAxes.Add(TEXT("+Y"));
	}
	if (bNearMinZ)
	{
		BoundaryInfo.BoundaryAxes.Add(TEXT("-Z"));
	}
	if (bNearMaxZ)
	{
		BoundaryInfo.BoundaryAxes.Add(TEXT("+Z"));
	}

	BoundaryInfo.ExpectedNeighborChunkCoords = BuildExpectedBoundaryNeighborChunkCoords(
		BoundaryInfo.HitChunkCoord,
		bNearMinX,
		bNearMaxX,
		bNearMinY,
		bNearMaxY,
		bNearMinZ,
		bNearMaxZ);
	return BoundaryInfo;
}

void AVoxelChunkManager::DrawChunkBoundaryDebug(
	const FRTPSVoxelChunkBoundaryDebugInfo& BoundaryInfo,
	float LifetimeSeconds) const
{
	UWorld* DebugWorld = GetWorld();
	if (DebugWorld == nullptr)
	{
		return;
	}

	const float Lifetime = LifetimeSeconds >= 0.f
		? LifetimeSeconds
		: FMath::Max(VoxelChunkBoundaryDebugDrawLifetimeSeconds, 0.1f);
	const FVector HalfExtent = BoundaryInfo.ChunkWorldSize * 0.5f;
	const FVector HitChunkCenter = BoundaryInfo.ChunkWorldOrigin + HalfExtent;
	DrawDebugBox(DebugWorld, HitChunkCenter, HalfExtent, FColor::Blue, false, Lifetime, 0, 2.f);

	for (const FIntVector& NeighborCoord : BoundaryInfo.ExpectedNeighborChunkCoords)
	{
		const FIntVector DeltaCoord = NeighborCoord - BoundaryInfo.HitChunkCoord;
		const FVector NeighborOrigin =
			BoundaryInfo.ChunkWorldOrigin +
			FVector(
				DeltaCoord.X * BoundaryInfo.ChunkWorldSize.X,
				DeltaCoord.Y * BoundaryInfo.ChunkWorldSize.Y,
				DeltaCoord.Z * BoundaryInfo.ChunkWorldSize.Z);
		const FVector NeighborCenter = NeighborOrigin + HalfExtent;
		DrawDebugBox(DebugWorld, NeighborCenter, HalfExtent, FColor::Yellow, false, Lifetime, 0, 2.f);
		DrawDebugString(
			DebugWorld,
			NeighborCenter,
			FString::Printf(TEXT("Expected (%d,%d,%d)"), NeighborCoord.X, NeighborCoord.Y, NeighborCoord.Z),
			nullptr,
			FColor::Yellow,
			Lifetime,
			false);
	}
}

void AVoxelChunkManager::GetBrushDebugChunkCoverage(
	const FVoxelBrush& Brush,
	TArray<FIntVector>& OutDensityAffectedChunkCoords,
	TArray<FIntVector>& OutMeshDirtyChunkCoords) const
{
	FRTPSVoxelEditOp DebugEditOp;
	DebugEditOp.Brush = Brush;
	OutDensityAffectedChunkCoords = GetDensityAffectedChunkCoordsForEditOp(DebugEditOp);
	OutMeshDirtyChunkCoords = GetMeshDirtyChunkCoordsForEditOp(DebugEditOp);
}

void AVoxelChunkManager::ApplyBrushAuthoritative(const FVoxelBrush& Brush)
{
	if (!HasAuthority())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ApplyBrushAuthoritative ignored on non-authority ChunkManager=%s"),
			*GetNameSafe(this));
		return;
	}

	FRTPSVoxelEditOp EditOp;
	EditOp.ServerSequence = NextVoxelEditSequence++;
	EditOp.Brush = Brush;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] VoxelEditOp created. ChunkManager=%s ServerSequence=%lld Map=%s Position=%s Radius=%.2f Strength=%.2f Mode=%d Shape=%s Falloff=%s SurfaceNormal=%s SurfaceDepth=%.2f BackDepth=%.2f EmbedDepth=%.2f RoundnessPower=%.2f BlendMode=%s ConvergenceAlpha=%.3f IsoLevel=%.3f"),
		*GetNameSafe(this),
		EditOp.ServerSequence,
		GetWorld() != nullptr ? *GetWorld()->GetMapName() : TEXT("None"),
		*EditOp.Brush.WorldPosition.ToCompactString(),
		EditOp.Brush.Radius,
		EditOp.Brush.Strength,
		static_cast<int32>(EditOp.Brush.Mode),
		GetVoxelBrushShapeDebugName(EditOp.Brush.Shape),
		GetVoxelBrushFalloffDebugName(EditOp.Brush.Falloff),
		*EditOp.Brush.SurfaceNormal.ToCompactString(),
		EditOp.Brush.SurfaceDepth,
		EditOp.Brush.BackDepth,
		EditOp.Brush.EmbedDepth,
		EditOp.Brush.ClumpRoundnessPower,
		GetVoxelBrushBlendModeDebugName(EditOp.Brush.BlendMode),
		EditOp.Brush.ConvergenceAlpha,
		IsoLevel);

	ApplyEditOpAuthoritative(EditOp);
}

void AVoxelChunkManager::Server_ApplyBrush_Implementation(FVoxelBrush Brush)
{
	ApplyBrushAuthoritative(Brush);
}

void AVoxelChunkManager::ApplyEditOpAuthoritative(const FRTPSVoxelEditOp& EditOp)
{
	if (!HasAuthority())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ApplyEditOpAuthoritative ignored on non-authority ChunkManager=%s ServerSequence=%lld"),
			*GetNameSafe(this),
			EditOp.ServerSequence);
		return;
	}

	const TArray<FIntVector> DensityAffectedChunkCoords = GetDensityAffectedChunkCoordsForEditOp(EditOp);
	const TArray<FIntVector> MeshDirtyChunkCoords = GetMeshDirtyChunkCoordsForEditOp(EditOp);
	const FRTPSVoxelDensityApplyResult ApplyResult =
		ApplyEditOpToDensityAffectedChunksAuthoritative(EditOp, DensityAffectedChunkCoords);
	const int32 AccountedChunkCount =
		ApplyResult.AppliedChunks +
		ApplyResult.QueuedChunks +
		ApplyResult.DuplicateChunks +
		ApplyResult.DroppedChunks;

	if (bDebugVoxelBrushApplication)
	{
		UE_LOG(
			LogRTPSVoxelDebug,
			Log,
			TEXT("[VoxelBrushDebug] ServerSequence=%lld Mode=%d Shape=%s Falloff=%s Position=%s Radius=%.2f Strength=%.3f SurfaceNormal=%s SurfaceDepth=%.2f BackDepth=%.2f EmbedDepth=%.2f RoundnessPower=%.2f BlendMode=%s ConvergenceAlpha=%.3f IsoLevel=%.3f DensityAffectedCount=%d DensityAffectedCoords=%s MeshDirtyCount=%d MeshDirtyCoords=%s AppliedReadyChunks=%d QueuedChunks=%d DuplicateChunks=%d DroppedChunks=%d PendingChunkCoordCount=%d"),
			EditOp.ServerSequence,
			static_cast<int32>(EditOp.Brush.Mode),
			GetVoxelBrushShapeDebugName(EditOp.Brush.Shape),
			GetVoxelBrushFalloffDebugName(EditOp.Brush.Falloff),
			*EditOp.Brush.WorldPosition.ToCompactString(),
			EditOp.Brush.Radius,
			EditOp.Brush.Strength,
			*EditOp.Brush.SurfaceNormal.ToCompactString(),
			EditOp.Brush.SurfaceDepth,
			EditOp.Brush.BackDepth,
			EditOp.Brush.EmbedDepth,
			EditOp.Brush.ClumpRoundnessPower,
			GetVoxelBrushBlendModeDebugName(EditOp.Brush.BlendMode),
			EditOp.Brush.ConvergenceAlpha,
			IsoLevel,
			DensityAffectedChunkCoords.Num(),
			*FormatChunkCoordList(DensityAffectedChunkCoords),
			MeshDirtyChunkCoords.Num(),
			*FormatChunkCoordList(MeshDirtyChunkCoords),
			ApplyResult.AppliedChunks,
			ApplyResult.QueuedChunks,
			ApplyResult.DuplicateChunks,
			ApplyResult.DroppedChunks,
			PendingOpsByChunk.Num());
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] VoxelEditOp authoritative density accounting. ChunkManager=%s ServerSequence=%lld Map=%s DensityAffectedCount=%d MeshDirtyCount=%d AppliedChunks=%d QueuedChunks=%d DuplicateChunks=%d DroppedChunks=%d LoadedChunks=%d"),
		*GetNameSafe(this),
		EditOp.ServerSequence,
		GetWorld() != nullptr ? *GetWorld()->GetMapName() : TEXT("None"),
		DensityAffectedChunkCoords.Num(),
		MeshDirtyChunkCoords.Num(),
		ApplyResult.AppliedChunks,
		ApplyResult.QueuedChunks,
		ApplyResult.DuplicateChunks,
		ApplyResult.DroppedChunks,
		LoadedChunks.Num());

	if (AccountedChunkCount != DensityAffectedChunkCoords.Num())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] VoxelEditOp density accounting mismatch. ChunkManager=%s ServerSequence=%lld DensityAffectedCount=%d AccountedChunks=%d AppliedChunks=%d QueuedChunks=%d DuplicateChunks=%d DroppedChunks=%d Reason=%s"),
			*GetNameSafe(this),
			EditOp.ServerSequence,
			DensityAffectedChunkCoords.Num(),
			AccountedChunkCount,
			ApplyResult.AppliedChunks,
			ApplyResult.QueuedChunks,
			ApplyResult.DuplicateChunks,
			ApplyResult.DroppedChunks,
			TEXT("ApplyEditOpAuthoritative"));
	}

	RebuildMeshDirtyChunksForEditOp(EditOp, ApplyResult.AppliedChunkCoords, TEXT("ApplyEditOpAuthoritative"));
	SendEditOpToChunkSubscribers(EditOp, DensityAffectedChunkCoords, MeshDirtyChunkCoords.Num(), TEXT("ApplyEditOpAuthoritative"));
}

int32 AVoxelChunkManager::ApplyEditOpLocal(const FRTPSVoxelEditOp& EditOp)
{
	const FVoxelBrush& Brush = EditOp.Brush;
	int32 AppliedChunkCount = 0;
	const int32 LoadedChunksConsidered = LoadedChunks.Num();
	const TArray<FIntVector> DensityAffectedChunkCoords = GetDensityAffectedChunkCoordsForEditOp(EditOp);
	TSet<FIntVector> DensityAppliedChunkCoords;

	for (const FIntVector& ChunkCoord : DensityAffectedChunkCoords)
	{
		AVoxelChunk* Chunk = nullptr;
		if (!IsChunkReadyForEdit(ChunkCoord, Chunk))
		{
			continue;
		}

		if (ApplyEditOpToReadyChunk(ChunkCoord, *Chunk, EditOp, TEXT("ApplyEditOpLocal")))
		{
			++AppliedChunkCount;
			DensityAppliedChunkCoords.Add(ChunkCoord);
		}
	}

	const int32 NeighborRebuildCount = RebuildMeshDirtyChunksForEditOp(EditOp, DensityAppliedChunkCoords, TEXT("ApplyEditOpLocal"));

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] VoxelEditOp applied locally. ChunkManager=%s ServerSequence=%lld Map=%s DensityAffectedCount=%d LoadedChunksConsidered=%d AppliedChunks=%d NeighborMeshRebuilds=%d Position=%s Radius=%.2f Mode=%d Shape=%d"),
		*GetNameSafe(this),
		EditOp.ServerSequence,
		GetWorld() != nullptr ? *GetWorld()->GetMapName() : TEXT("None"),
		DensityAffectedChunkCoords.Num(),
		LoadedChunksConsidered,
		AppliedChunkCount,
		NeighborRebuildCount,
		*Brush.WorldPosition.ToCompactString(),
		Brush.Radius,
		static_cast<int32>(Brush.Mode),
		static_cast<int32>(Brush.Shape));

	if (AppliedChunkCount == 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] VoxelEditOp was not applicable to any local ready density chunk. ChunkManager=%s ServerSequence=%lld DensityAffectedCount=%d LoadedChunksConsidered=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			EditOp.ServerSequence,
			DensityAffectedChunkCoords.Num(),
			LoadedChunksConsidered,
			TEXT("ApplyEditOpLocal"),
			HasAuthority() ? 1 : 0);
	}

	return AppliedChunkCount;
}

bool AVoxelChunkManager::BuildChunkStatePayload(FIntVector ChunkCoord, FRTPSVoxelChunkStatePayload& OutPayload) const
{
	OutPayload = FRTPSVoxelChunkStatePayload();
	OutPayload.ChunkCoord = ChunkCoord;

	if (!HasAuthority())
	{
		OutPayload.FailureReason = TEXT("Chunk state payload can only be built on authority.");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Server had no chunk state payload authority. ChunkManager=%s ChunkCoord=(%d,%d,%d) Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			*OutPayload.FailureReason,
			HasAuthority() ? 1 : 0);
		return false;
	}

	const FRTPSVoxelChunkState* ChunkState = ChunkStates.Find(ChunkCoord);
	if (ChunkState == nullptr)
	{
		OutPayload.FailureReason = TEXT("Authoritative ChunkState is missing.");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Server had no chunk state. ChunkManager=%s ChunkCoord=(%d,%d,%d) Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			*OutPayload.FailureReason,
			HasAuthority() ? 1 : 0);
		return false;
	}

	if (!ChunkState->bHasDensity || ChunkState->LatticeDensity.IsEmpty())
	{
		OutPayload.Revision = ChunkState->Revision;
		OutPayload.bHasDensity = ChunkState->bHasDensity;
		OutPayload.FailureReason = TEXT("Authoritative ChunkState has no density.");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Server chunk state has no density. ChunkManager=%s ChunkCoord=(%d,%d,%d) Revision=%d DensityCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ChunkState->Revision,
			ChunkState->LatticeDensity.Num(),
			*OutPayload.FailureReason,
			HasAuthority() ? 1 : 0);
		return false;
	}

	OutPayload.Revision = ChunkState->Revision;
	OutPayload.SnapshotRevision = ChunkState->SnapshotRevision;
	OutPayload.SnapshotServerSequence = ChunkState->SnapshotServerSequence;
	OutPayload.LatticeDensity = ChunkState->LatticeDensity;
	OutPayload.bHasDensity = true;
	OutPayload.bSuccess = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Server built chunk state payload. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d SnapshotRevision=%d SnapshotServerSequence=%lld DensityCount=%d Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		OutPayload.Revision,
		OutPayload.SnapshotRevision,
		OutPayload.SnapshotServerSequence,
		OutPayload.LatticeDensity.Num(),
		HasAuthority() ? 1 : 0);

	return true;
}

bool AVoxelChunkManager::CanBuildCompleteDeltaFromRecentOps(const FRTPSVoxelChunkState& ChunkState, int32 FromRevision, int32 ToRevision, FString& OutFailureReason) const
{
	OutFailureReason.Reset();

	if (!ChunkState.bHasDensity || ChunkState.LatticeDensity.IsEmpty())
	{
		OutFailureReason = TEXT("ChunkState has no density.");
		return false;
	}

	if (FromRevision < ChunkState.SnapshotRevision)
	{
		OutFailureReason = FString::Printf(
			TEXT("Client revision %d is older than snapshot baseline %d."),
			FromRevision,
			ChunkState.SnapshotRevision);
		return false;
	}

	if (FromRevision > ToRevision)
	{
		OutFailureReason = FString::Printf(
			TEXT("Invalid delta revision range %d -> %d."),
			FromRevision,
			ToRevision);
		return false;
	}

	if (FromRevision == ToRevision)
	{
		OutFailureReason = FString::Printf(
			TEXT("No delta needed because revisions already match at %d."),
			FromRevision);
		return false;
	}

	if (ToRevision != ChunkState.Revision)
	{
		OutFailureReason = FString::Printf(
			TEXT("Requested ToRevision %d does not match server revision %d."),
			ToRevision,
			ChunkState.Revision);
		return false;
	}

	int32 ExpectedRevisionBeforeApply = FromRevision;
	for (int32 ExpectedRevisionAfterApply = FromRevision + 1; ExpectedRevisionAfterApply <= ToRevision; ++ExpectedRevisionAfterApply)
	{
		const FRTPSVoxelRecentEditOp* MatchingRecentOp = nullptr;
		for (const FRTPSVoxelRecentEditOp& RecentOp : ChunkState.RecentOps)
		{
			if (RecentOp.RevisionBeforeApply == ExpectedRevisionBeforeApply
				&& RecentOp.RevisionAfterApply == ExpectedRevisionAfterApply)
			{
				MatchingRecentOp = &RecentOp;
				break;
			}
		}

		if (MatchingRecentOp == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("RecentOps coverage gap for revision %d -> %d."),
				ExpectedRevisionBeforeApply,
				ExpectedRevisionAfterApply);
			return false;
		}

		if (MatchingRecentOp->ServerSequence == INDEX_NONE || MatchingRecentOp->EditOp.ServerSequence == INDEX_NONE)
		{
			OutFailureReason = FString::Printf(
				TEXT("RecentOp for revision %d -> %d has invalid ServerSequence."),
				ExpectedRevisionBeforeApply,
				ExpectedRevisionAfterApply);
			return false;
		}

		if (MatchingRecentOp->ServerSequence != MatchingRecentOp->EditOp.ServerSequence)
		{
			OutFailureReason = FString::Printf(
				TEXT("RecentOp ServerSequence mismatch for revision %d -> %d."),
				ExpectedRevisionBeforeApply,
				ExpectedRevisionAfterApply);
			return false;
		}

		ExpectedRevisionBeforeApply = ExpectedRevisionAfterApply;
	}

	return true;
}

ERTPSVoxelChunkSyncMode AVoxelChunkManager::ChooseChunkSyncMode(
	const FRTPSVoxelChunkState& ChunkState,
	int32 ClientKnownRevision,
	FString& OutReason) const
{
	OutReason.Reset();

	ERTPSVoxelChunkSyncMode ChosenMode = ERTPSVoxelChunkSyncMode::FullSnapshot;

	if (!ChunkState.bHasDensity || ChunkState.LatticeDensity.Num() == 0)
	{
		OutReason = TEXT("no authoritative density available for chunk");
		ChosenMode = ERTPSVoxelChunkSyncMode::Skip;
	}
	else if (ClientKnownRevision == ChunkState.Revision)
	{
		OutReason = TEXT("client is already up to date");
		ChosenMode = ERTPSVoxelChunkSyncMode::Skip;
	}
	else if (ClientKnownRevision > ChunkState.Revision)
	{
		OutReason = TEXT("client reports future/newer revision than server");
		ChosenMode = ERTPSVoxelChunkSyncMode::FullSnapshot;
	}
	else if (ClientKnownRevision < ChunkState.SnapshotRevision)
	{
		OutReason = TEXT("client revision older than snapshot baseline");
		ChosenMode = ERTPSVoxelChunkSyncMode::FullSnapshot;
	}
	else if (ClientKnownRevision < ChunkState.Revision)
	{
		FString CoverageFailure;
		if (CanBuildCompleteDeltaFromRecentOps(ChunkState, ClientKnownRevision, ChunkState.Revision, CoverageFailure))
		{
			OutReason = TEXT("client within RecentOps window with complete coverage");
			ChosenMode = ERTPSVoxelChunkSyncMode::Delta;
		}
		else
		{
			OutReason = FString::Printf(TEXT("delta coverage incomplete: %s"), *CoverageFailure);
			ChosenMode = ERTPSVoxelChunkSyncMode::FullSnapshot;
		}
	}
	else
	{
		OutReason = TEXT("fallback: defaulting to FullSnapshot due to unhandled case");
		ChosenMode = ERTPSVoxelChunkSyncMode::FullSnapshot;
	}

	const TCHAR* ModeName = TEXT("FullSnapshot");
	switch (ChosenMode)
	{
	case ERTPSVoxelChunkSyncMode::Skip:
		ModeName = TEXT("Skip");
		break;
	case ERTPSVoxelChunkSyncMode::Delta:
		ModeName = TEXT("Delta");
		break;
	case ERTPSVoxelChunkSyncMode::FullSnapshot:
	default:
		ModeName = TEXT("FullSnapshot");
		break;
	}

	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("[RTPSValidation] Chunk sync mode chosen. ChunkManager=%s ChunkCoord=(%d,%d,%d) Mode=%s ClientKnownRevision=%d ServerRevision=%d SnapshotRevision=%d RecentOpsCount=%d Reason=%s"),
		*GetNameSafe(this),
		ChunkState.ChunkCoord.X,
		ChunkState.ChunkCoord.Y,
		ChunkState.ChunkCoord.Z,
		ModeName,
		ClientKnownRevision,
		ChunkState.Revision,
		ChunkState.SnapshotRevision,
		ChunkState.RecentOps.Num(),
		*OutReason);

	return ChosenMode;
}

bool AVoxelChunkManager::BuildChunkDeltaPayload(FIntVector ChunkCoord, int32 ClientKnownRevision, FRTPSVoxelChunkDeltaPayload& OutPayload) const
{
	OutPayload = FRTPSVoxelChunkDeltaPayload();
	OutPayload.ChunkCoord = ChunkCoord;
	OutPayload.FromRevision = ClientKnownRevision;

	if (!HasAuthority())
	{
		OutPayload.FailureReason = TEXT("Chunk delta payload can only be built on authority.");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chunk delta payload build failed. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			*OutPayload.FailureReason,
			HasAuthority() ? 1 : 0);
		return false;
	}

	const FRTPSVoxelChunkState* ChunkState = ChunkStates.Find(ChunkCoord);
	if (ChunkState == nullptr)
	{
		OutPayload.FailureReason = TEXT("Authoritative ChunkState is missing.");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chunk delta payload build failed. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			*OutPayload.FailureReason,
			HasAuthority() ? 1 : 0);
		return false;
	}

	OutPayload.ToRevision = ChunkState->Revision;
	OutPayload.SnapshotRevision = ChunkState->SnapshotRevision;
	OutPayload.SnapshotServerSequence = ChunkState->SnapshotServerSequence;

	if (!ChunkState->bHasDensity || ChunkState->LatticeDensity.IsEmpty())
	{
		OutPayload.FailureReason = TEXT("Authoritative ChunkState has no density.");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chunk delta payload build failed. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d SnapshotRevision=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			ChunkState->SnapshotRevision,
			*OutPayload.FailureReason,
			HasAuthority() ? 1 : 0);
		return false;
	}

	if (ClientKnownRevision < ChunkState->SnapshotRevision)
	{
		OutPayload.bRequiresFullSnapshot = true;
		OutPayload.FailureReason = FString::Printf(
			TEXT("Client revision %d is older than snapshot baseline %d."),
			ClientKnownRevision,
			ChunkState->SnapshotRevision);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Chunk delta payload requires full snapshot. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d SnapshotRevision=%d RecentOpsCount=%d Reason=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			ChunkState->SnapshotRevision,
			ChunkState->RecentOps.Num(),
			*OutPayload.FailureReason);
		return false;
	}

	if (ClientKnownRevision == ChunkState->Revision)
	{
		OutPayload.FailureReason = FString::Printf(
			TEXT("No delta needed because client revision %d matches server revision."),
			ClientKnownRevision);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Chunk delta payload skipped as up to date. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d SnapshotRevision=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			ChunkState->SnapshotRevision);
		return false;
	}

	if (ClientKnownRevision > ChunkState->Revision)
	{
		OutPayload.bRequiresFullSnapshot = true;
		OutPayload.FailureReason = FString::Printf(
			TEXT("Client claims future revision %d while server revision is %d."),
			ClientKnownRevision,
			ChunkState->Revision);
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chunk delta payload requires full snapshot because client is newer than server. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d SnapshotRevision=%d RecentOpsCount=%d Reason=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			ChunkState->SnapshotRevision,
			ChunkState->RecentOps.Num(),
			*OutPayload.FailureReason);
		return false;
	}

	FString CoverageFailureReason;
	if (!CanBuildCompleteDeltaFromRecentOps(*ChunkState, ClientKnownRevision, ChunkState->Revision, CoverageFailureReason))
	{
		OutPayload.bRequiresFullSnapshot = true;
		OutPayload.FailureReason = CoverageFailureReason;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Chunk delta payload requires full snapshot because RecentOps coverage failed. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d SnapshotRevision=%d RecentOpsCount=%d Reason=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			ChunkState->SnapshotRevision,
			ChunkState->RecentOps.Num(),
			*OutPayload.FailureReason);
		return false;
	}

	TArray<FRTPSVoxelEditOp> DeltaEditOps;
	DeltaEditOps.Reserve(ChunkState->Revision - ClientKnownRevision);
	int32 ExpectedRevisionBeforeApply = ClientKnownRevision;
	for (int32 ExpectedRevisionAfterApply = ClientKnownRevision + 1; ExpectedRevisionAfterApply <= ChunkState->Revision; ++ExpectedRevisionAfterApply)
	{
		const FRTPSVoxelRecentEditOp* MatchingRecentOp = nullptr;
		for (const FRTPSVoxelRecentEditOp& RecentOp : ChunkState->RecentOps)
		{
			if (RecentOp.RevisionBeforeApply == ExpectedRevisionBeforeApply
				&& RecentOp.RevisionAfterApply == ExpectedRevisionAfterApply)
			{
				MatchingRecentOp = &RecentOp;
				break;
			}
		}

		if (MatchingRecentOp == nullptr)
		{
			OutPayload.bRequiresFullSnapshot = true;
			OutPayload.FailureReason = FString::Printf(
				TEXT("RecentOps coverage gap while building delta for revision %d -> %d."),
				ExpectedRevisionBeforeApply,
				ExpectedRevisionAfterApply);
			return false;
		}

		DeltaEditOps.Add(MatchingRecentOp->EditOp);
		ExpectedRevisionBeforeApply = ExpectedRevisionAfterApply;
	}

	OutPayload.EditOps = MoveTemp(DeltaEditOps);
	OutPayload.bSuccess = true;
	OutPayload.bRequiresFullSnapshot = false;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chunk delta payload built. ChunkManager=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d SnapshotRevision=%d SnapshotServerSequence=%lld DeltaOpCount=%d RecentOpsCount=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		OutPayload.FromRevision,
		OutPayload.ToRevision,
		OutPayload.SnapshotRevision,
		OutPayload.SnapshotServerSequence,
		OutPayload.EditOps.Num(),
		ChunkState->RecentOps.Num());

	return true;
}

void AVoxelChunkManager::SubscribePlayerToChunk(ARTPSPlayerController* PlayerController, const FIntVector& ChunkCoord, int32 ClientKnownRevision, const TCHAR* Reason)
{
	if (!HasAuthority() || !IsValid(PlayerController))
	{
		return;
	}

	PruneStaleVoxelChunkSubscriptions(TEXT("SubscribePlayerToChunk"));

	const TWeakObjectPtr<ARTPSPlayerController> PlayerKey(PlayerController);
	TSet<TWeakObjectPtr<ARTPSPlayerController>>& Subscribers = ChunkSubscribers.FindOrAdd(ChunkCoord);
	const bool bWasAlreadySubscribed = Subscribers.Contains(PlayerKey);
	Subscribers.Add(PlayerKey);
	ClientSubscribedChunks.FindOrAdd(PlayerKey).Add(ChunkCoord);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Voxel chunk subscriber %s. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d SubscriberCount=%d Reason=%s"),
		bWasAlreadySubscribed ? TEXT("refreshed") : TEXT("added"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerController),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ClientKnownRevision,
		Subscribers.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"));

	bool bMaterializedPendingOnlyChunk = false;
	const FRTPSVoxelChunkState* ChunkState = ChunkStates.Find(ChunkCoord);
	if (ChunkState == nullptr || !ChunkState->bHasDensity || ChunkState->LatticeDensity.IsEmpty())
	{
		const TArray<FRTPSVoxelEditOp>* PendingOps = PendingOpsByChunk.Find(ChunkCoord);
		const int32 PendingOpCount = PendingOps != nullptr ? PendingOps->Num() : 0;
		if (PendingOpCount > 0)
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Subscribe found ChunkState missing but PendingOps exist. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d PendingOpCount=%d Reason=%s"),
				*GetNameSafe(this),
				*GetNameSafe(PlayerController),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				ClientKnownRevision,
				PendingOpCount,
				Reason != nullptr ? Reason : TEXT("Unknown"));

			if (!MaterializePendingChunkStateForSubscribe(ChunkCoord, TEXT("SubscribePlayerToChunk")))
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[RTPSValidation] Initial chunk payload skipped because pending-only materialization failed. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d PendingOpCount=%d Reason=%s"),
					*GetNameSafe(this),
					*GetNameSafe(PlayerController),
					ChunkCoord.X,
					ChunkCoord.Y,
					ChunkCoord.Z,
					ClientKnownRevision,
					PendingOpCount,
					Reason != nullptr ? Reason : TEXT("Unknown"));
				return;
			}

			ChunkState = ChunkStates.Find(ChunkCoord);
			bMaterializedPendingOnlyChunk = true;
		}
		else
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Materialization skipped because no ChunkState and no PendingOps. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Reason=%s"),
				*GetNameSafe(this),
				*GetNameSafe(PlayerController),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				ClientKnownRevision,
				Reason != nullptr ? Reason : TEXT("Unknown"));
		}
	}

	if (ChunkState == nullptr || !ChunkState->bHasDensity || ChunkState->LatticeDensity.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Initial chunk payload skipped because authoritative state is missing. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Reason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(PlayerController),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			Reason != nullptr ? Reason : TEXT("Unknown"));
		return;
	}

	const TCHAR* SubscribeReasonText = Reason != nullptr ? Reason : TEXT("Unknown");

	FString SyncDecisionReason;
	const ERTPSVoxelChunkSyncMode SyncMode = ChooseChunkSyncMode(*ChunkState, ClientKnownRevision, SyncDecisionReason);

	if (SyncMode == ERTPSVoxelChunkSyncMode::Skip)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Subscribe skipped sending payload. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d SyncReason=%s Reason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(PlayerController),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			*SyncDecisionReason,
			SubscribeReasonText);
		return;
	}

	auto QueueFullSnapshot = [&](const TCHAR* QueueReasonContext) -> bool
	{
		FRTPSVoxelChunkStatePayload FullPayload;
		if (!BuildChunkStatePayload(ChunkCoord, FullPayload))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[RTPSValidation] Subscribe failed to build full chunk state payload. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d ContextReason=%s SubscribeReason=%s"),
				*GetNameSafe(this),
				*GetNameSafe(PlayerController),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				ClientKnownRevision,
				ChunkState->Revision,
				QueueReasonContext,
				SubscribeReasonText);
			return false;
		}

		QueueChunkStatePayloadForClient(PlayerController, FullPayload, SubscribeReasonText);

		if (bMaterializedPendingOnlyChunk)
		{
			const TWeakObjectPtr<ARTPSPlayerController> PendingPayloadPlayerKey(PlayerController);
			const TArray<FRTPSVoxelChunkStatePayload>* PendingPayloads = PendingChunkStatePayloadsByClient.Find(PendingPayloadPlayerKey);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Pending-only ChunkState payload queued. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d DensityCount=%d QueueSize=%d Reason=%s"),
				*GetNameSafe(this),
				*GetNameSafe(PlayerController),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				ClientKnownRevision,
				FullPayload.Revision,
				FullPayload.LatticeDensity.Num(),
				PendingPayloads != nullptr ? PendingPayloads->Num() : 0,
				SubscribeReasonText);
		}
		return true;
	};

	if (SyncMode == ERTPSVoxelChunkSyncMode::Delta)
	{
		FRTPSVoxelChunkDeltaPayload DeltaPayload;
		if (BuildChunkDeltaPayload(ChunkCoord, ClientKnownRevision, DeltaPayload)
			&& DeltaPayload.bSuccess
			&& !DeltaPayload.bRequiresFullSnapshot
			&& DeltaPayload.EditOps.Num() > 0)
		{
			QueueChunkDeltaPayloadForClient(PlayerController, DeltaPayload, SubscribeReasonText);
			return;
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Subscribe delta build failed; falling back to full snapshot. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d ServerRevision=%d DeltaSuccess=%d DeltaRequiresFullSnapshot=%d EditOpCount=%d FailureReason=%s SubscribeReason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(PlayerController),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			ChunkState->Revision,
			DeltaPayload.bSuccess ? 1 : 0,
			DeltaPayload.bRequiresFullSnapshot ? 1 : 0,
			DeltaPayload.EditOps.Num(),
			*DeltaPayload.FailureReason,
			SubscribeReasonText);

		QueueFullSnapshot(TEXT("DeltaBuildFallback"));
		return;
	}

	// FullSnapshot
	QueueFullSnapshot(TEXT("FullSnapshotMode"));
}

void AVoxelChunkManager::UnsubscribePlayerFromChunk(ARTPSPlayerController* PlayerController, const FIntVector& ChunkCoord, const TCHAR* Reason)
{
	if (!HasAuthority() || !IsValid(PlayerController))
	{
		return;
	}

	const TWeakObjectPtr<ARTPSPlayerController> PlayerKey(PlayerController);
	int32 SubscriberCount = 0;
	if (TSet<TWeakObjectPtr<ARTPSPlayerController>>* Subscribers = ChunkSubscribers.Find(ChunkCoord))
	{
		Subscribers->Remove(PlayerKey);
		SubscriberCount = Subscribers->Num();
		if (Subscribers->IsEmpty())
		{
			ChunkSubscribers.Remove(ChunkCoord);
		}
	}

	if (TSet<FIntVector>* SubscribedChunks = ClientSubscribedChunks.Find(PlayerKey))
	{
		SubscribedChunks->Remove(ChunkCoord);
		if (SubscribedChunks->IsEmpty())
		{
			ClientSubscribedChunks.Remove(PlayerKey);
		}
	}

	int32 RemovedPayloads = 0;
	if (TArray<FRTPSVoxelChunkStatePayload>* PendingPayloads = PendingChunkStatePayloadsByClient.Find(PlayerKey))
	{
		RemovedPayloads = PendingPayloads->RemoveAll([&ChunkCoord](const FRTPSVoxelChunkStatePayload& Payload)
		{
			return Payload.ChunkCoord == ChunkCoord;
		});
		if (PendingPayloads->IsEmpty())
		{
			PendingChunkStatePayloadsByClient.Remove(PlayerKey);
		}
	}

	int32 RemovedDeltaPayloads = 0;
	if (TArray<FRTPSVoxelChunkDeltaPayload>* PendingDeltas = PendingChunkDeltaPayloadsByClient.Find(PlayerKey))
	{
		RemovedDeltaPayloads = PendingDeltas->RemoveAll([&ChunkCoord](const FRTPSVoxelChunkDeltaPayload& Payload)
		{
			return Payload.ChunkCoord == ChunkCoord;
		});
		if (PendingDeltas->IsEmpty())
		{
			PendingChunkDeltaPayloadsByClient.Remove(PlayerKey);
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Voxel chunk subscriber removed. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) SubscriberCount=%d RemovedFullPayloads=%d RemovedDeltaPayloads=%d Reason=%s"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerController),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		SubscriberCount,
		RemovedPayloads,
		RemovedDeltaPayloads,
		Reason != nullptr ? Reason : TEXT("Unknown"));
}

void AVoxelChunkManager::UnsubscribePlayerFromAllChunks(ARTPSPlayerController* PlayerController, const TCHAR* Reason)
{
	if (!HasAuthority() || !IsValid(PlayerController))
	{
		return;
	}

	const TWeakObjectPtr<ARTPSPlayerController> PlayerKey(PlayerController);
	TArray<FIntVector> SubscribedChunks;
	if (const TSet<FIntVector>* ExistingChunks = ClientSubscribedChunks.Find(PlayerKey))
	{
		SubscribedChunks = ExistingChunks->Array();
	}

	for (const FIntVector& ChunkCoord : SubscribedChunks)
	{
		UnsubscribePlayerFromChunk(PlayerController, ChunkCoord, Reason);
	}

	const int32 RemovedQueuedPayloads = PendingChunkStatePayloadsByClient.Remove(PlayerKey);
	const int32 RemovedQueuedDeltaPayloads = PendingChunkDeltaPayloadsByClient.Remove(PlayerKey);
	LastChunkPayloadFlushTimeByClient.Remove(PlayerKey);
	ClientSubscribedChunks.Remove(PlayerKey);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Voxel chunk subscriptions cleaned up. ChunkManager=%s PlayerController=%s RemovedChunks=%d RemovedClientFullQueue=%d RemovedClientDeltaQueue=%d Reason=%s"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerController),
		SubscribedChunks.Num(),
		RemovedQueuedPayloads,
		RemovedQueuedDeltaPayloads,
		Reason != nullptr ? Reason : TEXT("Unknown"));
}

void AVoxelChunkManager::QueueChunkStatePayloadForClient(ARTPSPlayerController* PlayerController, const FRTPSVoxelChunkStatePayload& Payload, const TCHAR* Reason)
{
	if (!HasAuthority() || !IsValid(PlayerController) || !Payload.bSuccess || !Payload.bHasDensity)
	{
		return;
	}

	const TWeakObjectPtr<ARTPSPlayerController> PlayerKey(PlayerController);
	int32 SupersededDeltaCount = 0;
	if (TArray<FRTPSVoxelChunkDeltaPayload>* PendingDeltas = PendingChunkDeltaPayloadsByClient.Find(PlayerKey))
	{
		for (int32 DeltaIndex = PendingDeltas->Num() - 1; DeltaIndex >= 0; --DeltaIndex)
		{
			if ((*PendingDeltas)[DeltaIndex].ChunkCoord == Payload.ChunkCoord)
			{
				PendingDeltas->RemoveAt(DeltaIndex, 1, EAllowShrinking::No);
				++SupersededDeltaCount;
			}
		}

		if (PendingDeltas->IsEmpty())
		{
			PendingChunkDeltaPayloadsByClient.Remove(PlayerKey);
		}
	}

	if (SupersededDeltaCount > 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Full snapshot superseded queued delta. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d SupersededDeltaCount=%d Reason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(PlayerController),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			SupersededDeltaCount,
			Reason != nullptr ? Reason : TEXT("Unknown"));
	}

	TArray<FRTPSVoxelChunkStatePayload>& PendingPayloads = PendingChunkStatePayloadsByClient.FindOrAdd(PlayerKey);
	for (FRTPSVoxelChunkStatePayload& ExistingPayload : PendingPayloads)
	{
		if (ExistingPayload.ChunkCoord == Payload.ChunkCoord)
		{
			if (Payload.Revision >= ExistingPayload.Revision)
			{
				ExistingPayload = Payload;
			}

			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Chunk state payload queue refreshed. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d QueueSize=%d Reason=%s"),
				*GetNameSafe(this),
				*GetNameSafe(PlayerController),
				Payload.ChunkCoord.X,
				Payload.ChunkCoord.Y,
				Payload.ChunkCoord.Z,
				Payload.Revision,
				Payload.LatticeDensity.Num(),
				PendingPayloads.Num(),
				Reason != nullptr ? Reason : TEXT("Unknown"));
			return;
		}
	}

	PendingPayloads.Add(Payload);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chunk state payload queued. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d QueueSize=%d Reason=%s"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerController),
		Payload.ChunkCoord.X,
		Payload.ChunkCoord.Y,
		Payload.ChunkCoord.Z,
		Payload.Revision,
		Payload.LatticeDensity.Num(),
		PendingPayloads.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"));
}

void AVoxelChunkManager::QueueChunkDeltaPayloadForClient(ARTPSPlayerController* PlayerController, const FRTPSVoxelChunkDeltaPayload& Payload, const TCHAR* Reason)
{
	const TCHAR* ReasonText = Reason != nullptr ? Reason : TEXT("Unknown");

	if (!HasAuthority() || !IsValid(PlayerController))
	{
		return;
	}

	if (!Payload.bSuccess || Payload.bRequiresFullSnapshot || Payload.EditOps.Num() == 0)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Chunk delta payload queue rejected invalid payload. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d bSuccess=%d bRequiresFullSnapshot=%d Reason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(PlayerController),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.FromRevision,
			Payload.ToRevision,
			Payload.EditOps.Num(),
			Payload.bSuccess ? 1 : 0,
			Payload.bRequiresFullSnapshot ? 1 : 0,
			ReasonText);
		return;
	}

	const TWeakObjectPtr<ARTPSPlayerController> PlayerKey(PlayerController);

	if (const TArray<FRTPSVoxelChunkStatePayload>* PendingFullPayloads = PendingChunkStatePayloadsByClient.Find(PlayerKey))
	{
		for (const FRTPSVoxelChunkStatePayload& FullPayload : *PendingFullPayloads)
		{
			if (FullPayload.ChunkCoord == Payload.ChunkCoord)
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[RTPSValidation] Chunk delta payload dropped because full snapshot already queued. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d Reason=%s"),
					*GetNameSafe(this),
					*GetNameSafe(PlayerController),
					Payload.ChunkCoord.X,
					Payload.ChunkCoord.Y,
					Payload.ChunkCoord.Z,
					Payload.FromRevision,
					Payload.ToRevision,
					ReasonText);
				return;
			}
		}
	}

	TArray<FRTPSVoxelChunkDeltaPayload>& PendingDeltas = PendingChunkDeltaPayloadsByClient.FindOrAdd(PlayerKey);
	for (FRTPSVoxelChunkDeltaPayload& ExistingDelta : PendingDeltas)
	{
		if (ExistingDelta.ChunkCoord == Payload.ChunkCoord)
		{
			if (Payload.ToRevision > ExistingDelta.ToRevision)
			{
				ExistingDelta = Payload;
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[RTPSValidation] Chunk delta payload queue replaced with newer ToRevision. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d QueueSize=%d Reason=%s"),
					*GetNameSafe(this),
					*GetNameSafe(PlayerController),
					Payload.ChunkCoord.X,
					Payload.ChunkCoord.Y,
					Payload.ChunkCoord.Z,
					Payload.FromRevision,
					Payload.ToRevision,
					Payload.EditOps.Num(),
					PendingDeltas.Num(),
					ReasonText);
			}
			else
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("[RTPSValidation] Chunk delta payload queue kept existing newer-or-equal entry. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) IncomingToRevision=%d ExistingToRevision=%d Reason=%s"),
					*GetNameSafe(this),
					*GetNameSafe(PlayerController),
					Payload.ChunkCoord.X,
					Payload.ChunkCoord.Y,
					Payload.ChunkCoord.Z,
					Payload.ToRevision,
					ExistingDelta.ToRevision,
					ReasonText);
			}
			return;
		}
	}

	PendingDeltas.Add(Payload);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chunk delta payload queued. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d QueueSize=%d Reason=%s"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerController),
		Payload.ChunkCoord.X,
		Payload.ChunkCoord.Y,
		Payload.ChunkCoord.Z,
		Payload.FromRevision,
		Payload.ToRevision,
		Payload.EditOps.Num(),
		PendingDeltas.Num(),
		ReasonText);
}

int32 AVoxelChunkManager::FlushQueuedChunkStatePayloads(float DeltaSeconds)
{
	if (!HasAuthority())
	{
		return 0;
	}

	PruneStaleVoxelChunkSubscriptions(TEXT("FlushQueuedChunkStatePayloads"));

	const int32 MaxPayloadsPerClient = FMath::Max(MaxChunkStatePayloadsPerClientPerTick, 1);

	TSet<TWeakObjectPtr<ARTPSPlayerController>> CombinedClientKeys;
	{
		TArray<TWeakObjectPtr<ARTPSPlayerController>> FullClientKeys;
		PendingChunkStatePayloadsByClient.GetKeys(FullClientKeys);
		for (const TWeakObjectPtr<ARTPSPlayerController>& Key : FullClientKeys)
		{
			CombinedClientKeys.Add(Key);
		}

		TArray<TWeakObjectPtr<ARTPSPlayerController>> DeltaClientKeys;
		PendingChunkDeltaPayloadsByClient.GetKeys(DeltaClientKeys);
		for (const TWeakObjectPtr<ARTPSPlayerController>& Key : DeltaClientKeys)
		{
			CombinedClientKeys.Add(Key);
		}
	}

	const double Now = FPlatformTime::Seconds();

	int32 SentPayloadCount = 0;
	for (const TWeakObjectPtr<ARTPSPlayerController>& ClientKey : CombinedClientKeys)
	{
		ARTPSPlayerController* PlayerController = ClientKey.Get();
		if (!IsValid(PlayerController))
		{
			PendingChunkStatePayloadsByClient.Remove(ClientKey);
			PendingChunkDeltaPayloadsByClient.Remove(ClientKey);
			LastChunkPayloadFlushTimeByClient.Remove(ClientKey);
			continue;
		}

		const double* LastFlushTime = LastChunkPayloadFlushTimeByClient.Find(ClientKey);
		if (MinChunkStateFlushIntervalSecondsPerClient > 0.0f
			&& LastFlushTime != nullptr
			&& (Now - *LastFlushTime) < static_cast<double>(MinChunkStateFlushIntervalSecondsPerClient))
		{
			const TArray<FRTPSVoxelChunkStatePayload>* DeferredFullQueue = PendingChunkStatePayloadsByClient.Find(ClientKey);
			const TArray<FRTPSVoxelChunkDeltaPayload>* DeferredDeltaQueue = PendingChunkDeltaPayloadsByClient.Find(ClientKey);

			UE_LOG(
				LogTemp,
				Verbose,
				TEXT("[RTPSValidation] Chunk payload flush deferred by min interval. ChunkManager=%s PlayerController=%s LastFlushElapsedSeconds=%.4f MinFlushIntervalSeconds=%.4f FullQueueSize=%d DeltaQueueSize=%d"),
				*GetNameSafe(this),
				*GetNameSafe(PlayerController),
				static_cast<float>(Now - *LastFlushTime),
				MinChunkStateFlushIntervalSecondsPerClient,
				DeferredFullQueue != nullptr ? DeferredFullQueue->Num() : 0,
				DeferredDeltaQueue != nullptr ? DeferredDeltaQueue->Num() : 0);
			continue;
		}

		int32 RemainingBudget = MaxPayloadsPerClient;
		const int32 SentBeforeForClient = SentPayloadCount;

		if (TArray<FRTPSVoxelChunkStatePayload>* PendingFullPayloads = PendingChunkStatePayloadsByClient.Find(ClientKey))
		{
			const int32 FullToSend = FMath::Min(RemainingBudget, PendingFullPayloads->Num());
			for (int32 FullIndex = 0; FullIndex < FullToSend; ++FullIndex)
			{
				const FRTPSVoxelChunkStatePayload Payload = (*PendingFullPayloads)[0];
				PendingFullPayloads->RemoveAt(0, 1, EAllowShrinking::No);
				PlayerController->ClientReceiveVoxelChunkState(Payload);
				++SentPayloadCount;
				--RemainingBudget;

				UE_LOG(
					LogTemp,
					Log,
					TEXT("[RTPSValidation] Chunk state payload flushed. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d RemainingQueueSize=%d RemainingBudget=%d MaxPerTick=%d DeltaSeconds=%.3f"),
					*GetNameSafe(this),
					*GetNameSafe(PlayerController),
					Payload.ChunkCoord.X,
					Payload.ChunkCoord.Y,
					Payload.ChunkCoord.Z,
					Payload.Revision,
					Payload.LatticeDensity.Num(),
					PendingFullPayloads->Num(),
					RemainingBudget,
					MaxPayloadsPerClient,
					DeltaSeconds);
			}

			if (PendingFullPayloads->IsEmpty())
			{
				PendingChunkStatePayloadsByClient.Remove(ClientKey);
			}
		}

		if (RemainingBudget <= 0)
		{
			if (SentPayloadCount > SentBeforeForClient)
			{
				LastChunkPayloadFlushTimeByClient.Add(ClientKey, Now);
			}
			continue;
		}

		if (TArray<FRTPSVoxelChunkDeltaPayload>* PendingDeltaPayloads = PendingChunkDeltaPayloadsByClient.Find(ClientKey))
		{
			const int32 DeltaToSend = FMath::Min(RemainingBudget, PendingDeltaPayloads->Num());
			for (int32 DeltaIndex = 0; DeltaIndex < DeltaToSend; ++DeltaIndex)
			{
				const FRTPSVoxelChunkDeltaPayload Payload = (*PendingDeltaPayloads)[0];
				PendingDeltaPayloads->RemoveAt(0, 1, EAllowShrinking::No);
				PlayerController->ClientReceiveVoxelChunkDelta(Payload);
				++SentPayloadCount;
				--RemainingBudget;

				UE_LOG(
					LogTemp,
					Log,
					TEXT("[RTPSValidation] Chunk delta payload flushed. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d RemainingQueueSize=%d RemainingBudget=%d MaxPerTick=%d DeltaSeconds=%.3f"),
					*GetNameSafe(this),
					*GetNameSafe(PlayerController),
					Payload.ChunkCoord.X,
					Payload.ChunkCoord.Y,
					Payload.ChunkCoord.Z,
					Payload.FromRevision,
					Payload.ToRevision,
					Payload.EditOps.Num(),
					PendingDeltaPayloads->Num(),
					RemainingBudget,
					MaxPayloadsPerClient,
					DeltaSeconds);
			}

			if (PendingDeltaPayloads->IsEmpty())
			{
				PendingChunkDeltaPayloadsByClient.Remove(ClientKey);
			}
		}

		if (SentPayloadCount > SentBeforeForClient)
		{
			LastChunkPayloadFlushTimeByClient.Add(ClientKey, Now);
		}
	}

	return SentPayloadCount;
}

void AVoxelChunkManager::PruneStaleVoxelChunkSubscriptions(const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<TWeakObjectPtr<ARTPSPlayerController>> StaleClients;
	for (const TPair<TWeakObjectPtr<ARTPSPlayerController>, TSet<FIntVector>>& Pair : ClientSubscribedChunks)
	{
		ARTPSPlayerController* PlayerController = Pair.Key.Get();
		if (!IsValid(PlayerController) || PlayerController->IsPendingKillPending())
		{
			StaleClients.Add(Pair.Key);
		}
	}

	for (const TWeakObjectPtr<ARTPSPlayerController>& StaleClient : StaleClients)
	{
		if (const TSet<FIntVector>* SubscribedChunks = ClientSubscribedChunks.Find(StaleClient))
		{
			for (const FIntVector& ChunkCoord : *SubscribedChunks)
			{
				if (TSet<TWeakObjectPtr<ARTPSPlayerController>>* Subscribers = ChunkSubscribers.Find(ChunkCoord))
				{
					Subscribers->Remove(StaleClient);
				}
			}
		}

		ClientSubscribedChunks.Remove(StaleClient);
		PendingChunkStatePayloadsByClient.Remove(StaleClient);
		PendingChunkDeltaPayloadsByClient.Remove(StaleClient);
		LastChunkPayloadFlushTimeByClient.Remove(StaleClient);
	}

	TArray<FIntVector> EmptySubscriberChunks;
	for (TPair<FIntVector, TSet<TWeakObjectPtr<ARTPSPlayerController>>>& Pair : ChunkSubscribers)
	{
		for (auto SubscriberIt = Pair.Value.CreateIterator(); SubscriberIt; ++SubscriberIt)
		{
			ARTPSPlayerController* PlayerController = SubscriberIt->Get();
			if (!IsValid(PlayerController) || PlayerController->IsPendingKillPending())
			{
				SubscriberIt.RemoveCurrent();
			}
		}

		if (Pair.Value.IsEmpty())
		{
			EmptySubscriberChunks.Add(Pair.Key);
		}
	}

	for (const FIntVector& ChunkCoord : EmptySubscriberChunks)
	{
		ChunkSubscribers.Remove(ChunkCoord);
	}

	if (!StaleClients.IsEmpty() || !EmptySubscriberChunks.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Stale voxel chunk subscriptions pruned. ChunkManager=%s StaleClients=%d EmptyChunks=%d Reason=%s"),
			*GetNameSafe(this),
			StaleClients.Num(),
			EmptySubscriberChunks.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"));
	}
}

int32 AVoxelChunkManager::SelectClientSubscribeKnownRevision(const FIntVector& ChunkCoord, int32 LocalKnownRevision) const
{
	if (ChunksNeedingFullSnapshotResync.Contains(ChunkCoord))
	{
		return INDEX_NONE;
	}
	if (LastAppliedRemoteRevisionByCoord.Find(ChunkCoord) == nullptr)
	{
		return INDEX_NONE;
	}
	return LocalKnownRevision;
}

bool AVoxelChunkManager::ShouldBypassClientSubscribeRevisionThrottle(const FIntVector& ChunkCoord) const
{
	if (ChunksNeedingFullSnapshotResync.Contains(ChunkCoord))
	{
		return true;
	}
	if (LastAppliedRemoteRevisionByCoord.Find(ChunkCoord) == nullptr)
	{
		return true;
	}
	return false;
}

void AVoxelChunkManager::SubscribeToAuthoritativeChunkStateIfClient(const FIntVector& ChunkCoord, int32 LocalKnownRevision, const TCHAR* Reason)
{
	if (HasAuthority())
	{
		return;
	}

	const bool bForceFullSnapshotResync = ChunksNeedingFullSnapshotResync.Contains(ChunkCoord);
	const bool bNoBaseline = LastAppliedRemoteRevisionByCoord.Find(ChunkCoord) == nullptr;
	const bool bBypassRevisionThrottle = ShouldBypassClientSubscribeRevisionThrottle(ChunkCoord);
	const int32 EffectiveKnownRevision = SelectClientSubscribeKnownRevision(ChunkCoord, LocalKnownRevision);

	if (!bBypassRevisionThrottle)
	{
		const int32* LastSubscribedRevision = LastSubscribedKnownRevisionByCoord.Find(ChunkCoord);
		if (LastSubscribedRevision != nullptr && *LastSubscribedRevision >= LocalKnownRevision)
		{
			return;
		}
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const double* LastRequestTime = LastChunkSubscribeRequestTimeByCoord.Find(ChunkCoord);
	const float EffectiveRetryInterval = FMath::Max(ChunkSubscribeRetryInterval, 0.25f);
	if (LastRequestTime != nullptr && CurrentTime - *LastRequestTime < static_cast<double>(EffectiveRetryInterval))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client chunk state request skipped because World is invalid. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d EffectiveKnownRevision=%d ForceFullResync=%d NoBaseline=%d Reason=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			LocalKnownRevision,
			EffectiveKnownRevision,
			bForceFullSnapshotResync ? 1 : 0,
			bNoBaseline ? 1 : 0,
			Reason != nullptr ? Reason : TEXT("Unknown"));
		return;
	}

	ARTPSPlayerController* PlayerController = Cast<ARTPSPlayerController>(World->GetFirstPlayerController());
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client chunk state request skipped because local ARTPSPlayerController is missing. ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d EffectiveKnownRevision=%d ForceFullResync=%d NoBaseline=%d Reason=%s Map=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			LocalKnownRevision,
			EffectiveKnownRevision,
			bForceFullSnapshotResync ? 1 : 0,
			bNoBaseline ? 1 : 0,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			*World->GetMapName());
		return;
	}

	LastSubscribedKnownRevisionByCoord.Add(ChunkCoord, EffectiveKnownRevision);
	LastChunkSubscribeRequestTimeByCoord.Add(ChunkCoord, FPlatformTime::Seconds());

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Client requested voxel chunk subscription. ChunkManager=%s PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d EffectiveKnownRevision=%d ForceFullResync=%d NoBaseline=%d Reason=%s Map=%s"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerController),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		LocalKnownRevision,
		EffectiveKnownRevision,
		bForceFullSnapshotResync ? 1 : 0,
		bNoBaseline ? 1 : 0,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		*World->GetMapName());

	PlayerController->ServerSubscribeVoxelChunk(ChunkCoord, EffectiveKnownRevision);
}

bool AVoxelChunkManager::ApplyChunkStatePayloadLocal(const FRTPSVoxelChunkStatePayload& Payload, const TCHAR* Reason)
{
	if (!Payload.bSuccess || !Payload.bHasDensity)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client received failed chunk state payload. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d FailureReason=%s Reason=%s Authority=%d"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			Payload.LatticeDensity.Num(),
			*Payload.FailureReason,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	const int32* LastAppliedRevision = LastAppliedRemoteRevisionByCoord.Find(Payload.ChunkCoord);
	if (LastAppliedRevision != nullptr && Payload.Revision < *LastAppliedRevision)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client skipped stale chunk state payload. ChunkManager=%s ChunkCoord=(%d,%d,%d) PayloadRevision=%d LastAppliedRevision=%d DensityCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			*LastAppliedRevision,
			Payload.LatticeDensity.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	TObjectPtr<AVoxelChunk>* ChunkPtr = LoadedChunks.Find(Payload.ChunkCoord);
	AVoxelChunk* Chunk = ChunkPtr != nullptr ? ChunkPtr->Get() : nullptr;
	if (!IsValid(Chunk) || Chunk->State != EVoxelChunkState::Ready)
	{
		PendingIncomingChunkPayloads.Add(Payload.ChunkCoord, Payload);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Client queued incoming chunk payload because local chunk is not ready. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d PendingPayloads=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			Payload.LatticeDensity.Num(),
			PendingIncomingChunkPayloads.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	if (!Chunk->ApplyAuthoritativeDensitySnapshot(Payload.LatticeDensity, Payload.Revision, Reason))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client failed to apply chunk state payload. ChunkManager=%s Chunk=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			*GetNameSafe(Chunk),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			Payload.LatticeDensity.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	FRTPSVoxelChunkState& LocalState = FindOrCreateChunkState(Payload.ChunkCoord);
	LocalState.ChunkCoord = Payload.ChunkCoord;
	LocalState.Revision = Payload.Revision;
	LocalState.SnapshotRevision = Payload.SnapshotRevision;
	LocalState.SnapshotServerSequence = Payload.SnapshotServerSequence;
	LocalState.LatticeDensity = Payload.LatticeDensity;
	LocalState.bHasDensity = true;
	LocalState.bDirty = false;
	LocalState.RecentOps.Reset();

	LastAppliedRemoteRevisionByCoord.Add(Payload.ChunkCoord, Payload.Revision);
	LastSubscribedKnownRevisionByCoord.Add(Payload.ChunkCoord, Payload.Revision);
	LastChunkSubscribeRequestTimeByCoord.Add(Payload.ChunkCoord, FPlatformTime::Seconds());
	PendingIncomingChunkPayloads.Remove(Payload.ChunkCoord);
	ChunksNeedingFullSnapshotResync.Remove(Payload.ChunkCoord);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Client applied authoritative chunk state payload. ChunkManager=%s Chunk=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d SnapshotRevision=%d SnapshotServerSequence=%lld DensityCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		*GetNameSafe(Chunk),
		Payload.ChunkCoord.X,
		Payload.ChunkCoord.Y,
		Payload.ChunkCoord.Z,
		Payload.Revision,
		Payload.SnapshotRevision,
		Payload.SnapshotServerSequence,
		Payload.LatticeDensity.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	return true;
}

bool AVoxelChunkManager::ApplyChunkDeltaPayloadLocal(const FRTPSVoxelChunkDeltaPayload& Payload, const TCHAR* Reason)
{
	const TCHAR* ReasonText = Reason != nullptr ? Reason : TEXT("Unknown");

	auto LogReject = [&](const TCHAR* RejectReason)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client rejected chunk delta payload. ChunkManager=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d RejectReason=%s Reason=%s Authority=%d"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.FromRevision,
			Payload.ToRevision,
			Payload.EditOps.Num(),
			RejectReason,
			ReasonText,
			HasAuthority() ? 1 : 0);
	};

	if (!Payload.bSuccess)
	{
		LogReject(TEXT("payload reports bSuccess=false"));
		return false;
	}

	if (Payload.bRequiresFullSnapshot)
	{
		LogReject(TEXT("payload demands full snapshot fallback"));
		return false;
	}

	if (Payload.EditOps.Num() == 0)
	{
		LogReject(TEXT("payload has no edit ops"));
		return false;
	}

	if (Payload.FromRevision >= Payload.ToRevision)
	{
		LogReject(TEXT("payload FromRevision >= ToRevision"));
		return false;
	}

	TObjectPtr<AVoxelChunk>* ChunkPtr = LoadedChunks.Find(Payload.ChunkCoord);
	AVoxelChunk* Chunk = ChunkPtr != nullptr ? ChunkPtr->Get() : nullptr;
	if (!IsValid(Chunk) || Chunk->State != EVoxelChunkState::Ready)
	{
		LogReject(TEXT("local chunk missing or not Ready"));
		return false;
	}

	const int32* LastAppliedRevision = LastAppliedRemoteRevisionByCoord.Find(Payload.ChunkCoord);
	if (LastAppliedRevision == nullptr || *LastAppliedRevision != Payload.FromRevision)
	{
		LogReject(TEXT("LastAppliedRemoteRevision does not match Payload.FromRevision"));
		return false;
	}

	const FRTPSVoxelChunkState* ExistingState = ChunkStates.Find(Payload.ChunkCoord);
	if (ExistingState != nullptr && ExistingState->Revision != Payload.FromRevision)
	{
		LogReject(TEXT("existing local ChunkState revision does not match Payload.FromRevision"));
		return false;
	}

	for (const FRTPSVoxelEditOp& EditOp : Payload.EditOps)
	{
		if (EditOp.ServerSequence == INDEX_NONE)
		{
			LogReject(TEXT("edit op has invalid ServerSequence"));
			return false;
		}
	}

	const TSet<int64>* AppliedSequences = AppliedEditSequencesByChunk.Find(Payload.ChunkCoord);
	int32 DuplicateOpCount = 0;
	if (AppliedSequences != nullptr)
	{
		for (const FRTPSVoxelEditOp& EditOp : Payload.EditOps)
		{
			if (AppliedSequences->Contains(EditOp.ServerSequence))
			{
				++DuplicateOpCount;
			}
		}
	}

	if (DuplicateOpCount > 0)
	{
		LogReject(DuplicateOpCount == Payload.EditOps.Num()
			? TEXT("all edit ops already applied; full snapshot resync required")
			: TEXT("partial duplicate edit ops; full snapshot resync required"));
		return false;
	}

	TArray<float> CandidateLatticeDensity = Chunk->GetLatticeDensity();
	for (const FRTPSVoxelEditOp& EditOp : Payload.EditOps)
	{
		if (!AVoxelChunk::ApplyBrushToLatticeDensity(
			CandidateLatticeDensity,
			Chunk->ChunkCoord,
			Chunk->ChunkDimensions,
			Chunk->CellSize,
			EditOp.Brush,
			Chunk->IsoLevel))
		{
			LogReject(TEXT("brush could not be applied to candidate density"));
			return false;
		}
	}

	if (!Chunk->ApplyAuthoritativeDensitySnapshot(CandidateLatticeDensity, Payload.ToRevision, ReasonText))
	{
		LogReject(TEXT("ApplyAuthoritativeDensitySnapshot failed"));
		return false;
	}

	FRTPSVoxelChunkState& LocalState = FindOrCreateChunkState(Payload.ChunkCoord);
	LocalState.ChunkCoord = Payload.ChunkCoord;
	LocalState.Revision = Payload.ToRevision;
	LocalState.SnapshotRevision = Payload.SnapshotRevision;
	LocalState.SnapshotServerSequence = Payload.SnapshotServerSequence;
	LocalState.LatticeDensity = Chunk->GetLatticeDensity();
	LocalState.bHasDensity = true;
	LocalState.bDirty = false;

	LastAppliedRemoteRevisionByCoord.Add(Payload.ChunkCoord, Payload.ToRevision);
	LastSubscribedKnownRevisionByCoord.Add(Payload.ChunkCoord, Payload.ToRevision);

	for (const FRTPSVoxelEditOp& EditOp : Payload.EditOps)
	{
		MarkSequenceAppliedToChunk(Payload.ChunkCoord, EditOp.ServerSequence, ReasonText);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Client applied chunk delta payload. ChunkManager=%s Chunk=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d AppliedOpCount=%d SnapshotRevision=%d SnapshotServerSequence=%lld Reason=%s Authority=%d"),
		*GetNameSafe(this),
		*GetNameSafe(Chunk),
		Payload.ChunkCoord.X,
		Payload.ChunkCoord.Y,
		Payload.ChunkCoord.Z,
		Payload.FromRevision,
		Payload.ToRevision,
		Payload.EditOps.Num(),
		Payload.SnapshotRevision,
		Payload.SnapshotServerSequence,
		ReasonText,
		HasAuthority() ? 1 : 0);

	return true;
}

void AVoxelChunkManager::MarkChunkForFullSnapshotResync(FIntVector ChunkCoord, const TCHAR* Reason)
{
	const TCHAR* ReasonText = Reason != nullptr ? Reason : TEXT("Unknown");
	if (HasAuthority())
	{
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("[RTPSValidation] MarkChunkForFullSnapshotResync ignored on authority. ChunkManager=%s ChunkCoord=(%d,%d,%d) Reason=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ReasonText);
		return;
	}

	ChunksNeedingFullSnapshotResync.Add(ChunkCoord);
	LastSubscribedKnownRevisionByCoord.Remove(ChunkCoord);
	LastChunkSubscribeRequestTimeByCoord.Remove(ChunkCoord);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[RTPSValidation] Chunk marked for full snapshot resync. ChunkManager=%s ChunkCoord=(%d,%d,%d) PendingCount=%d Reason=%s"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ChunksNeedingFullSnapshotResync.Num(),
		ReasonText);
}

void AVoxelChunkManager::Multicast_ApplyVoxelEditOp_Implementation(FRTPSVoxelEditOp EditOp)
{
	ApplyEditOpLocal(EditOp);
}

void AVoxelChunkManager::Multicast_ApplyBrush_Implementation(FVoxelBrush Brush)
{
	FRTPSVoxelEditOp CompatibilityEditOp;
	CompatibilityEditOp.Brush = Brush;
	ApplyEditOpLocal(CompatibilityEditOp);
}

TArray<FIntVector> AVoxelChunkManager::GetDensityAffectedChunkCoordsForEditOp(const FRTPSVoxelEditOp& EditOp) const
{
	TArray<FIntVector> DensityAffectedChunkCoords;
	const FVoxelBrush& Brush = EditOp.Brush;
	const FBox BrushBounds = BuildBrushBounds(Brush);
	const FIntVector MinChunkCoord = WorldToChunkCoord(BrushBounds.Min);
	const FIntVector MaxChunkCoord = WorldToChunkCoord(BrushBounds.Max);

	for (int32 Z = MinChunkCoord.Z; Z <= MaxChunkCoord.Z; ++Z)
	{
		for (int32 Y = MinChunkCoord.Y; Y <= MaxChunkCoord.Y; ++Y)
		{
			for (int32 X = MinChunkCoord.X; X <= MaxChunkCoord.X; ++X)
			{
				const FIntVector ChunkCoord(X, Y, Z);
				if (DoesEditOpOverlapChunk(EditOp, ChunkCoord))
				{
					DensityAffectedChunkCoords.Add(ChunkCoord);
				}
			}
		}
	}

	return DensityAffectedChunkCoords;
}

TArray<FIntVector> AVoxelChunkManager::GetAffectedChunkCoords(const FRTPSVoxelEditOp& EditOp) const
{
	return GetDensityAffectedChunkCoordsForEditOp(EditOp);
}

TArray<FIntVector> AVoxelChunkManager::GetMeshDirtyChunkCoordsForEditOp(const FRTPSVoxelEditOp& EditOp) const
{
	TArray<FIntVector> MeshDirtyChunkCoords = GetDensityAffectedChunkCoordsForEditOp(EditOp);
	const FVoxelBrush& Brush = EditOp.Brush;
	const float BoundaryHalo = FMath::Max(CellSize, 1.f);
	const FBox BrushBounds = BuildBrushBounds(Brush).ExpandBy(BoundaryHalo);
	const FIntVector MinChunkCoord = WorldToChunkCoord(BrushBounds.Min);
	const FIntVector MaxChunkCoord = WorldToChunkCoord(BrushBounds.Max);

	for (int32 Z = MinChunkCoord.Z; Z <= MaxChunkCoord.Z; ++Z)
	{
		for (int32 Y = MinChunkCoord.Y; Y <= MaxChunkCoord.Y; ++Y)
		{
			for (int32 X = MinChunkCoord.X; X <= MaxChunkCoord.X; ++X)
			{
				MeshDirtyChunkCoords.AddUnique(FIntVector(X, Y, Z));
			}
		}
	}

	const int32 DensityAffectedCount = GetDensityAffectedChunkCoordsForEditOp(EditOp).Num();
	if (MeshDirtyChunkCoords.Num() > DensityAffectedCount)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] VoxelEditOp boundary halo included neighbor mesh chunks. ChunkManager=%s ServerSequence=%lld DensityAffectedChunks=%d MeshDirtyChunks=%d BoundaryHaloCm=%.2f Position=%s Radius=%.2f"),
			*GetNameSafe(this),
			EditOp.ServerSequence,
			DensityAffectedCount,
			MeshDirtyChunkCoords.Num(),
			BoundaryHalo,
			*Brush.WorldPosition.ToCompactString(),
			Brush.Radius);
	}

	return MeshDirtyChunkCoords;
}

TArray<ARTPSPlayerController*> AVoxelChunkManager::GetSubscribersForAffectedChunks(const TArray<FIntVector>& AffectedChunkCoords)
{
	TArray<ARTPSPlayerController*> Targets;
	if (!HasAuthority())
	{
		return Targets;
	}

	PruneStaleVoxelChunkSubscriptions(TEXT("GetSubscribersForAffectedChunks"));

	TSet<TWeakObjectPtr<ARTPSPlayerController>> UniqueTargets;
	for (const FIntVector& ChunkCoord : AffectedChunkCoords)
	{
		TSet<TWeakObjectPtr<ARTPSPlayerController>>* Subscribers = ChunkSubscribers.Find(ChunkCoord);
		if (Subscribers == nullptr)
		{
			continue;
		}

		for (auto SubscriberIt = Subscribers->CreateIterator(); SubscriberIt; ++SubscriberIt)
		{
			ARTPSPlayerController* PlayerController = SubscriberIt->Get();
			if (!IsValid(PlayerController) || PlayerController->IsPendingKillPending())
			{
				SubscriberIt.RemoveCurrent();
				continue;
			}

			UniqueTargets.Add(*SubscriberIt);
		}
	}

	for (const TWeakObjectPtr<ARTPSPlayerController>& Target : UniqueTargets)
	{
		if (ARTPSPlayerController* PlayerController = Target.Get())
		{
			Targets.Add(PlayerController);
		}
	}

	return Targets;
}

int32 AVoxelChunkManager::RebuildMeshDirtyChunksForEditOp(const FRTPSVoxelEditOp& EditOp, const TSet<FIntVector>& DensityAppliedChunkCoords, const TCHAR* Reason)
{
	const TArray<FIntVector> MeshDirtyChunkCoords = GetMeshDirtyChunkCoordsForEditOp(EditOp);
	int32 RebuiltChunkCount = 0;
	for (const FIntVector& ChunkCoord : MeshDirtyChunkCoords)
	{
		if (DensityAppliedChunkCoords.Contains(ChunkCoord))
		{
			continue;
		}

		TObjectPtr<AVoxelChunk>* ChunkPtr = LoadedChunks.Find(ChunkCoord);
		AVoxelChunk* Chunk = ChunkPtr != nullptr ? ChunkPtr->Get() : nullptr;
		if (!IsValid(Chunk) || Chunk->State != EVoxelChunkState::Ready)
		{
			continue;
		}

		Chunk->bDebugVoxelMeshRebuilds = bDebugVoxelMeshRebuilds;
		Chunk->RebuildMeshAsync(TEXT("MeshDirtyNeighbor"));
		++RebuiltChunkCount;

		if (bDebugVoxelMeshRebuilds)
		{
			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelMeshRebuildDebug] Queue ChunkCoord=(%d,%d,%d) ServerSequence=%lld Trigger=MeshDirtyNeighbor DirectDensityAffected=0 NeighborMeshDirty=1 Reason=%s"),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				EditOp.ServerSequence,
				Reason != nullptr ? Reason : TEXT("Unknown"));
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] VoxelEditOp rebuilt mesh-dirty neighbor chunk. ChunkManager=%s Chunk=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
			*GetNameSafe(this),
			*GetNameSafe(Chunk),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			EditOp.ServerSequence,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
	}

	return RebuiltChunkCount;
}

void AVoxelChunkManager::SendEditOpToChunkSubscribers(
	const FRTPSVoxelEditOp& EditOp,
	const TArray<FIntVector>& DensityAffectedChunkCoords,
	int32 MeshDirtyChunkCount,
	const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	if (MeshDirtyChunkCount > DensityAffectedChunkCoords.Num())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Live VoxelEditOp target selection ignored mesh-dirty halo subscribers. ChunkManager=%s ServerSequence=%lld TargetSelectionSource=DensityAffected DensityAffectedCount=%d MeshDirtyCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			EditOp.ServerSequence,
			DensityAffectedChunkCoords.Num(),
			MeshDirtyChunkCount,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
	}

	const TArray<ARTPSPlayerController*> Targets = GetSubscribersForAffectedChunks(DensityAffectedChunkCoords);
	for (ARTPSPlayerController* PlayerController : Targets)
	{
		if (IsValid(PlayerController))
		{
			PlayerController->ClientReceiveVoxelEditOp(EditOp);
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Targeted VoxelEditOp delivery completed. ChunkManager=%s ServerSequence=%lld TargetSelectionSource=DensityAffected DensityAffectedCount=%d MeshDirtyCount=%d TargetCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		EditOp.ServerSequence,
		DensityAffectedChunkCoords.Num(),
		MeshDirtyChunkCount,
		Targets.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

bool AVoxelChunkManager::DoesEditOpOverlapChunk(const FRTPSVoxelEditOp& EditOp, const FIntVector& ChunkCoord) const
{
	const FVoxelBrush& Brush = EditOp.Brush;
	const float BrushRadius = FMath::Max(Brush.Radius, 0.f);
	const FVector ChunkMin(
		ChunkCoord.X * ChunkDimensions.X * CellSize,
		ChunkCoord.Y * ChunkDimensions.Y * CellSize,
		ChunkCoord.Z * ChunkDimensions.Z * CellSize);
	const FVector ChunkMax = ChunkMin + FVector(
		ChunkDimensions.X * CellSize,
		ChunkDimensions.Y * CellSize,
		ChunkDimensions.Z * CellSize);

	const FBox ChunkBox(ChunkMin, ChunkMax);
	if (Brush.Shape == EVoxelBrushShape::Box)
	{
		const FVector BrushExtent(BrushRadius);
		const FBox BrushBox(Brush.WorldPosition - BrushExtent, Brush.WorldPosition + BrushExtent);
		return ChunkBox.Intersect(BrushBox);
	}

	if (Brush.Shape == EVoxelBrushShape::Flatten)
	{
		const float ClosestX = FMath::Clamp(Brush.WorldPosition.X, ChunkMin.X, ChunkMax.X);
		const float ClosestY = FMath::Clamp(Brush.WorldPosition.Y, ChunkMin.Y, ChunkMax.Y);
		const float DistanceSquared2D =
			FMath::Square(ClosestX - Brush.WorldPosition.X) +
			FMath::Square(ClosestY - Brush.WorldPosition.Y);
		return DistanceSquared2D <= FMath::Square(BrushRadius);
	}

	if (Brush.Shape == EVoxelBrushShape::SurfaceBlob || Brush.Shape == EVoxelBrushShape::TerrainMudBlob)
	{
		return ChunkBox.Intersect(BuildOrientedBlobBounds(Brush));
	}

	return ChunkBox.ComputeSquaredDistanceToPoint(Brush.WorldPosition) <= FMath::Square(BrushRadius);
}

bool AVoxelChunkManager::IsChunkReadyForEdit(const FIntVector& ChunkCoord, AVoxelChunk*& OutChunk) const
{
	OutChunk = nullptr;

	const TObjectPtr<AVoxelChunk>* ChunkPtr = LoadedChunks.Find(ChunkCoord);
	if (ChunkPtr == nullptr)
	{
		return false;
	}

	AVoxelChunk* Chunk = ChunkPtr->Get();
	if (!IsValid(Chunk) || Chunk->State != EVoxelChunkState::Ready)
	{
		return false;
	}

	OutChunk = Chunk;
	return true;
}

bool AVoxelChunkManager::HasAppliedSequenceToChunk(const FIntVector& ChunkCoord, int64 ServerSequence) const
{
	if (ServerSequence == INDEX_NONE)
	{
		return false;
	}

	const TSet<int64>* AppliedSequences = AppliedEditSequencesByChunk.Find(ChunkCoord);
	return AppliedSequences != nullptr && AppliedSequences->Contains(ServerSequence);
}

void AVoxelChunkManager::MarkSequenceAppliedToChunk(const FIntVector& ChunkCoord, int64 ServerSequence, const TCHAR* Reason)
{
	if (ServerSequence == INDEX_NONE)
	{
		return;
	}

	AppliedEditSequencesByChunk.FindOrAdd(ChunkCoord).Add(ServerSequence);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] VoxelEditOp sequence marked applied. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ServerSequence,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

bool AVoxelChunkManager::IsPendingSequenceForChunk(const FIntVector& ChunkCoord, int64 ServerSequence) const
{
	if (ServerSequence == INDEX_NONE)
	{
		return false;
	}

	const TArray<FRTPSVoxelEditOp>* PendingOps = PendingOpsByChunk.Find(ChunkCoord);
	if (PendingOps == nullptr)
	{
		return false;
	}

	for (const FRTPSVoxelEditOp& PendingOp : *PendingOps)
	{
		if (PendingOp.ServerSequence == ServerSequence)
		{
			return true;
		}
	}

	return false;
}

void AVoxelChunkManager::QueuePendingOpForChunk(const FIntVector& ChunkCoord, const FRTPSVoxelEditOp& EditOp, const TCHAR* Reason)
{
	if (HasAppliedSequenceToChunk(ChunkCoord, EditOp.ServerSequence))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Pending voxel edit skipped because sequence is already applied. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			EditOp.ServerSequence,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return;
	}

	if (IsPendingSequenceForChunk(ChunkCoord, EditOp.ServerSequence))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Pending voxel edit skipped because sequence is already queued. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			EditOp.ServerSequence,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return;
	}

	TArray<FRTPSVoxelEditOp>& PendingOps = PendingOpsByChunk.FindOrAdd(ChunkCoord);
	PendingOps.Add(EditOp);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] VoxelEditOp queued for pending chunk. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld PendingCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		EditOp.ServerSequence,
		PendingOps.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

int32 AVoxelChunkManager::QueuePendingOpsForMissingOrNotReadyChunks(const FRTPSVoxelEditOp& EditOp, const TArray<FIntVector>& AffectedChunkCoords)
{
	if (!HasAuthority())
	{
		return 0;
	}

	int32 QueuedChunkCount = 0;
	for (const FIntVector& ChunkCoord : AffectedChunkCoords)
	{
		AVoxelChunk* ReadyChunk = nullptr;
		if (IsChunkReadyForEdit(ChunkCoord, ReadyChunk))
		{
			continue;
		}

		const int32 PreviousPendingCount = PendingOpsByChunk.Find(ChunkCoord) != nullptr
			? PendingOpsByChunk.Find(ChunkCoord)->Num()
			: 0;
		QueuePendingOpForChunk(ChunkCoord, EditOp, TEXT("ApplyEditOpAuthoritative"));

		const int32 CurrentPendingCount = PendingOpsByChunk.Find(ChunkCoord) != nullptr
			? PendingOpsByChunk.Find(ChunkCoord)->Num()
			: 0;
		if (CurrentPendingCount > PreviousPendingCount)
		{
			++QueuedChunkCount;
		}
	}

	return QueuedChunkCount;
}

AVoxelChunkManager::FRTPSVoxelDensityApplyResult AVoxelChunkManager::ApplyEditOpToDensityAffectedChunksAuthoritative(
	const FRTPSVoxelEditOp& EditOp,
	const TArray<FIntVector>& DensityAffectedChunkCoords)
{
	FRTPSVoxelDensityApplyResult Result;
	if (!HasAuthority())
	{
		return Result;
	}

	for (const FIntVector& ChunkCoord : DensityAffectedChunkCoords)
	{
		if (HasAppliedSequenceToChunk(ChunkCoord, EditOp.ServerSequence))
		{
			++Result.DuplicateChunks;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Density affected chunk skipped because sequence is already applied. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
				*GetNameSafe(this),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				EditOp.ServerSequence,
				TEXT("ApplyEditOpToDensityAffectedChunksAuthoritative"),
				HasAuthority() ? 1 : 0);
			continue;
		}

		if (IsPendingSequenceForChunk(ChunkCoord, EditOp.ServerSequence))
		{
			++Result.DuplicateChunks;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Density affected chunk skipped because sequence is already pending. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
				*GetNameSafe(this),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				EditOp.ServerSequence,
				TEXT("ApplyEditOpToDensityAffectedChunksAuthoritative"),
				HasAuthority() ? 1 : 0);
			continue;
		}

		AVoxelChunk* ReadyChunk = nullptr;
		if (IsChunkReadyForEdit(ChunkCoord, ReadyChunk))
		{
			if (ApplyEditOpToReadyChunk(ChunkCoord, *ReadyChunk, EditOp, TEXT("ApplyEditOpToDensityAffectedChunksAuthoritative")))
			{
				++Result.AppliedChunks;
				Result.AppliedChunkCoords.Add(ChunkCoord);
			}
			else
			{
				++Result.DroppedChunks;
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[RTPSValidation] Ready density affected chunk was not applied or queued. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
					*GetNameSafe(this),
					ChunkCoord.X,
					ChunkCoord.Y,
					ChunkCoord.Z,
					EditOp.ServerSequence,
					TEXT("ApplyEditOpToDensityAffectedChunksAuthoritative"),
					HasAuthority() ? 1 : 0);
			}
			continue;
		}

		QueuePendingOpForChunk(ChunkCoord, EditOp, TEXT("ApplyEditOpToDensityAffectedChunksAuthoritative"));
		++Result.QueuedChunks;
	}

	return Result;
}

bool AVoxelChunkManager::ApplyEditOpToReadyChunk(const FIntVector& ChunkCoord, AVoxelChunk& Chunk, const FRTPSVoxelEditOp& EditOp, const TCHAR* Reason)
{
	if (Chunk.State != EVoxelChunkState::Ready)
	{
		return false;
	}

	if (!DoesEditOpOverlapChunk(EditOp, ChunkCoord))
	{
		return false;
	}

	if (HasAppliedSequenceToChunk(ChunkCoord, EditOp.ServerSequence))
	{
		const FRTPSVoxelChunkState* ChunkState = FindChunkState(ChunkCoord);
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] VoxelEditOp skipped duplicate chunk apply without revision change. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Revision=%d RecentOpsCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			EditOp.ServerSequence,
			ChunkState != nullptr ? ChunkState->Revision : INDEX_NONE,
			ChunkState != nullptr ? ChunkState->RecentOps.Num() : 0,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	Chunk.bDebugVoxelMeshRebuilds = bDebugVoxelMeshRebuilds;
	Chunk.ApplyBrush(EditOp.Brush);
	MirrorChunkDensityToState(ChunkCoord, Chunk, Reason);
	MarkChunkStateDirty(ChunkCoord, Reason);
	if (HasAuthority())
	{
		FRTPSVoxelChunkState& ChunkState = FindOrCreateChunkState(ChunkCoord);
		RecordAppliedEditOpForChunk(ChunkState, EditOp, Reason);
	}
	else
	{
		MarkSequenceAppliedToChunk(ChunkCoord, EditOp.ServerSequence, Reason);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] VoxelEditOp applied to ready chunk. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		EditOp.ServerSequence,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	return true;
}

bool AVoxelChunkManager::MaterializePendingChunkStateForSubscribe(const FIntVector& ChunkCoord, const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (const FRTPSVoxelChunkState* ExistingState = ChunkStates.Find(ChunkCoord))
	{
		if (ExistingState->bHasDensity && !ExistingState->LatticeDensity.IsEmpty())
		{
			return true;
		}
	}

	const TArray<FRTPSVoxelEditOp>* PendingOps = PendingOpsByChunk.Find(ChunkCoord);
	const int32 PendingOpCount = PendingOps != nullptr ? PendingOps->Num() : 0;
	if (PendingOpCount <= 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Pending-only chunk materialization skipped because no pending ops exist. ChunkManager=%s ChunkCoord=(%d,%d,%d) Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Materializing pending-only chunk state. ChunkManager=%s ChunkCoord=(%d,%d,%d) PendingOpCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		PendingOpCount,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	TArray<float> BaseDensity;
	if (!AVoxelChunk::BuildInitialLatticeDensity(
		ChunkCoord,
		ChunkDimensions,
		NoiseParams,
		GenerationSource,
		TArray<float>(),
		ChunkSaveDir,
		BaseDensity))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Materialization failed; pending ops preserved. ChunkManager=%s ChunkCoord=(%d,%d,%d) PendingOpCount=%d ChunkDimensions=(%d,%d,%d) Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			PendingOpCount,
			ChunkDimensions.X,
			ChunkDimensions.Y,
			ChunkDimensions.Z,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return false;
	}

	FRTPSVoxelChunkState& ChunkState = FindOrCreateChunkState(ChunkCoord);
	ChunkState.ChunkCoord = ChunkCoord;
	ChunkState.LatticeDensity = MoveTemp(BaseDensity);
	ChunkState.bHasDensity = true;
	ChunkState.bDirty = false;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Base density initialized for pending-only chunk. ChunkManager=%s ChunkCoord=(%d,%d,%d) DensityCount=%d Revision=%d PendingOpCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ChunkState.LatticeDensity.Num(),
		ChunkState.Revision,
		PendingOpCount,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	TArray<FRTPSVoxelEditOp> OpsToReplay = *PendingOps;
	OpsToReplay.Sort([](const FRTPSVoxelEditOp& Left, const FRTPSVoxelEditOp& Right)
	{
		return Left.ServerSequence < Right.ServerSequence;
	});

	int32 AppliedPendingOpCount = 0;
	int32 DuplicateOpCount = 0;
	int32 SkippedOpCount = 0;
	for (const FRTPSVoxelEditOp& PendingOp : OpsToReplay)
	{
		if (HasAppliedSequenceToChunk(ChunkCoord, PendingOp.ServerSequence))
		{
			++DuplicateOpCount;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Duplicate ServerSequence skipped during materialization. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Revision=%d RecentOpsCount=%d Reason=%s Authority=%d"),
				*GetNameSafe(this),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				PendingOp.ServerSequence,
				ChunkState.Revision,
				ChunkState.RecentOps.Num(),
				Reason != nullptr ? Reason : TEXT("Unknown"),
				HasAuthority() ? 1 : 0);
			continue;
		}

		if (!DoesEditOpOverlapChunk(PendingOp, ChunkCoord))
		{
			++SkippedOpCount;
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[RTPSValidation] Pending op skipped during materialization because it no longer overlaps chunk. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
				*GetNameSafe(this),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				PendingOp.ServerSequence,
				Reason != nullptr ? Reason : TEXT("Unknown"),
				HasAuthority() ? 1 : 0);
			continue;
		}

		AVoxelChunk::ApplyBrushToLatticeDensity(
			ChunkState.LatticeDensity,
			ChunkCoord,
			ChunkDimensions,
			CellSize,
			PendingOp.Brush,
			IsoLevel);
		MarkChunkStateDirty(ChunkCoord, TEXT("MaterializePendingChunkStateForSubscribe"));
		RecordAppliedEditOpForChunk(ChunkState, PendingOp, TEXT("MaterializePendingChunkStateForSubscribe"));
		++AppliedPendingOpCount;
	}

	const int32 EffectiveMaxRecentOps = FMath::Max(MaxRecentOpsPerChunk, 1);
	if (AppliedPendingOpCount >= EffectiveMaxRecentOps
		&& ChunkState.SnapshotRevision < ChunkState.Revision
		&& !ChunkState.RecentOps.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Pending materialization triggered snapshot compaction. ChunkManager=%s ChunkCoord=(%d,%d,%d) AppliedPendingOpCount=%d RecentOpsCount=%d Revision=%d SnapshotRevision=%d MaxRecentOps=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			AppliedPendingOpCount,
			ChunkState.RecentOps.Num(),
			ChunkState.Revision,
			ChunkState.SnapshotRevision,
			EffectiveMaxRecentOps,
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		CompactChunkStateSnapshot(ChunkState, TEXT("MaterializePendingChunkStateForSubscribe"));
	}

	PendingOpsByChunk.Remove(ChunkCoord);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Pending ops replayed into materialized ChunkState. ChunkManager=%s ChunkCoord=(%d,%d,%d) PendingOpCount=%d AppliedPendingOpCount=%d DuplicateOpCount=%d SkippedOpCount=%d ServerRevision=%d RecentOpsCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		PendingOpCount,
		AppliedPendingOpCount,
		DuplicateOpCount,
		SkippedOpCount,
		ChunkState.Revision,
		ChunkState.RecentOps.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	return ChunkState.bHasDensity && !ChunkState.LatticeDensity.IsEmpty();
}

void AVoxelChunkManager::ReplayPendingOpsForChunk(const FIntVector& ChunkCoord, AVoxelChunk& Chunk, const TCHAR* Reason)
{
	if (!HasAuthority() || Chunk.State != EVoxelChunkState::Ready)
	{
		return;
	}

	TArray<FRTPSVoxelEditOp>* PendingOps = PendingOpsByChunk.Find(ChunkCoord);
	if (PendingOps == nullptr || PendingOps->IsEmpty())
	{
		return;
	}

	TArray<FRTPSVoxelEditOp> OpsToReplay = *PendingOps;
	PendingOpsByChunk.Remove(ChunkCoord);
	OpsToReplay.Sort([](const FRTPSVoxelEditOp& Left, const FRTPSVoxelEditOp& Right)
	{
		return Left.ServerSequence < Right.ServerSequence;
	});

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Pending voxel edit replay started. ChunkManager=%s ChunkCoord=(%d,%d,%d) PendingCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		OpsToReplay.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	int32 ReplayedCount = 0;
	int32 SkippedCount = 0;
	for (const FRTPSVoxelEditOp& PendingOp : OpsToReplay)
	{
		if (HasAppliedSequenceToChunk(ChunkCoord, PendingOp.ServerSequence))
		{
			++SkippedCount;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("[RTPSValidation] Pending voxel edit replay skipped duplicate. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Reason=%s Authority=%d"),
				*GetNameSafe(this),
				ChunkCoord.X,
				ChunkCoord.Y,
				ChunkCoord.Z,
				PendingOp.ServerSequence,
				Reason != nullptr ? Reason : TEXT("Unknown"),
				HasAuthority() ? 1 : 0);
			continue;
		}

		if (ApplyEditOpToReadyChunk(ChunkCoord, Chunk, PendingOp, TEXT("ReplayPendingOpsForChunk")))
		{
			++ReplayedCount;
			TSet<FIntVector> DensityAppliedChunkCoords;
			DensityAppliedChunkCoords.Add(ChunkCoord);
			RebuildMeshDirtyChunksForEditOp(PendingOp, DensityAppliedChunkCoords, TEXT("ReplayPendingOpsForChunk"));
			const TArray<FIntVector> PendingDensityAffectedChunkCoords = GetDensityAffectedChunkCoordsForEditOp(PendingOp);
			const TArray<FIntVector> PendingMeshDirtyChunkCoords = GetMeshDirtyChunkCoordsForEditOp(PendingOp);
			SendEditOpToChunkSubscribers(
				PendingOp,
				PendingDensityAffectedChunkCoords,
				PendingMeshDirtyChunkCoords.Num(),
				TEXT("ReplayPendingOpsForChunk"));
		}
		else
		{
			++SkippedCount;
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Pending voxel edit replay completed. ChunkManager=%s ChunkCoord=(%d,%d,%d) Replayed=%d Skipped=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ReplayedCount,
		SkippedCount,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

FRTPSVoxelChunkState& AVoxelChunkManager::FindOrCreateChunkState(const FIntVector& ChunkCoord)
{
	FRTPSVoxelChunkState* ExistingState = ChunkStates.Find(ChunkCoord);
	if (ExistingState != nullptr)
	{
		return *ExistingState;
	}

	FRTPSVoxelChunkState& NewState = ChunkStates.Add(ChunkCoord);
	NewState.ChunkCoord = ChunkCoord;
	NewState.SnapshotRevision = NewState.Revision;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ChunkState created. ChunkManager=%s ChunkCoord=(%d,%d,%d) DensityCount=%d Revision=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		NewState.LatticeDensity.Num(),
		NewState.Revision,
		TEXT("FindOrCreateChunkState"),
		HasAuthority() ? 1 : 0);

	return NewState;
}

const FRTPSVoxelChunkState* AVoxelChunkManager::FindChunkState(const FIntVector& ChunkCoord) const
{
	return ChunkStates.Find(ChunkCoord);
}

void AVoxelChunkManager::MirrorChunkDensityToState(const FIntVector& ChunkCoord, const AVoxelChunk& Chunk, const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	if (Chunk.State != EVoxelChunkState::Ready || !Chunk.HasValidLatticeDensity())
	{
		return;
	}

	FRTPSVoxelChunkState& ChunkState = FindOrCreateChunkState(ChunkCoord);
	const TArray<float>& ChunkDensity = Chunk.GetLatticeDensity();
	const bool bNeedsMirror = !ChunkState.bHasDensity
		|| ChunkState.LatticeDensity.Num() != ChunkDensity.Num()
		|| ChunkState.LatticeDensity != ChunkDensity;

	if (!bNeedsMirror)
	{
		return;
	}

	ChunkState.ChunkCoord = ChunkCoord;
	ChunkState.LatticeDensity = ChunkDensity;
	ChunkState.bHasDensity = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chunk density mirrored. ChunkManager=%s ChunkCoord=(%d,%d,%d) DensityCount=%d Revision=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ChunkState.LatticeDensity.Num(),
		ChunkState.Revision,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

void AVoxelChunkManager::MarkChunkStateDirty(const FIntVector& ChunkCoord, const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	FRTPSVoxelChunkState& ChunkState = FindOrCreateChunkState(ChunkCoord);
	ChunkState.bDirty = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ChunkState marked dirty. ChunkManager=%s ChunkCoord=(%d,%d,%d) DensityCount=%d Revision=%d RecentOpsCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ChunkState.LatticeDensity.Num(),
		ChunkState.Revision,
		ChunkState.RecentOps.Num(),
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

void AVoxelChunkManager::RecordAppliedEditOpForChunk(FRTPSVoxelChunkState& ChunkState, const FRTPSVoxelEditOp& EditOp, const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	if (EditOp.ServerSequence == INDEX_NONE)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] ChunkRevision not advanced for unsequenced edit op. ChunkManager=%s ChunkCoord=(%d,%d,%d) Revision=%d RecentOpsCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkState.ChunkCoord.X,
			ChunkState.ChunkCoord.Y,
			ChunkState.ChunkCoord.Z,
			ChunkState.Revision,
			ChunkState.RecentOps.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return;
	}

	if (HasAppliedSequenceToChunk(ChunkState.ChunkCoord, EditOp.ServerSequence))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] ChunkRevision duplicate skipped without revision change. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Revision=%d RecentOpsCount=%d Reason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkState.ChunkCoord.X,
			ChunkState.ChunkCoord.Y,
			ChunkState.ChunkCoord.Z,
			EditOp.ServerSequence,
			ChunkState.Revision,
			ChunkState.RecentOps.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			HasAuthority() ? 1 : 0);
		return;
	}

	const int32 RevisionBeforeApply = ChunkState.Revision;
	++ChunkState.Revision;
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(RevisionBeforeApply, ChunkState.Revision, EditOp));
	MarkSequenceAppliedToChunk(ChunkState.ChunkCoord, EditOp.ServerSequence, Reason);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ChunkRevision incremented and RecentOp appended. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerSequence=%lld Revision=%d SnapshotRevision=%d RecentOpsCount=%d MaxRecentOps=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkState.ChunkCoord.X,
		ChunkState.ChunkCoord.Y,
		ChunkState.ChunkCoord.Z,
		EditOp.ServerSequence,
		ChunkState.Revision,
		ChunkState.SnapshotRevision,
		ChunkState.RecentOps.Num(),
		MaxRecentOpsPerChunk,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	if (!ShouldCompactChunkStateSnapshot(ChunkState))
	{
		TrimRecentOps(ChunkState, Reason);
		return;
	}

	CompactChunkStateSnapshot(ChunkState, Reason);
}

bool AVoxelChunkManager::ShouldCompactChunkStateSnapshot(const FRTPSVoxelChunkState& ChunkState) const
{
	if (!ChunkState.bHasDensity || ChunkState.LatticeDensity.IsEmpty())
	{
		return false;
	}

	const int32 EffectiveMaxRecentOps = FMath::Max(MaxRecentOpsPerChunk, 1);
	return ChunkState.RecentOps.Num() >= EffectiveMaxRecentOps;
}

bool AVoxelChunkManager::CompactChunkStateSnapshot(FRTPSVoxelChunkState& ChunkState, const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!ChunkState.bHasDensity || ChunkState.LatticeDensity.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Chunk snapshot compaction skipped. ChunkManager=%s ChunkCoord=(%d,%d,%d) Revision=%d SnapshotRevision=%d RecentOpsCount=%d Reason=%s SkipReason=%s Authority=%d"),
			*GetNameSafe(this),
			ChunkState.ChunkCoord.X,
			ChunkState.ChunkCoord.Y,
			ChunkState.ChunkCoord.Z,
			ChunkState.Revision,
			ChunkState.SnapshotRevision,
			ChunkState.RecentOps.Num(),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			TEXT("ChunkState has no density."),
			HasAuthority() ? 1 : 0);
		return false;
	}

	const int32 PreviousSnapshotRevision = ChunkState.SnapshotRevision;
	const int64 PreviousSnapshotServerSequence = ChunkState.SnapshotServerSequence;
	const int32 RecentOpsBefore = ChunkState.RecentOps.Num();
	int64 LatestSnapshotServerSequence = ChunkState.SnapshotServerSequence;
	for (const FRTPSVoxelRecentEditOp& RecentOp : ChunkState.RecentOps)
	{
		if (RecentOp.ServerSequence != INDEX_NONE)
		{
			LatestSnapshotServerSequence = FMath::Max(LatestSnapshotServerSequence, RecentOp.ServerSequence);
		}
	}

	ChunkState.SnapshotRevision = ChunkState.Revision;
	ChunkState.SnapshotServerSequence = LatestSnapshotServerSequence;
	ChunkState.RecentOps.Reset();
	++ChunkState.TotalCompactionCount;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Chunk snapshot compacted. ChunkManager=%s ChunkCoord=(%d,%d,%d) PreviousSnapshotRevision=%d SnapshotRevision=%d Revision=%d PreviousSnapshotServerSequence=%lld SnapshotServerSequence=%lld RecentOpsBefore=%d RecentOpsAfter=%d TotalCompactionCount=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkState.ChunkCoord.X,
		ChunkState.ChunkCoord.Y,
		ChunkState.ChunkCoord.Z,
		PreviousSnapshotRevision,
		ChunkState.SnapshotRevision,
		ChunkState.Revision,
		PreviousSnapshotServerSequence,
		ChunkState.SnapshotServerSequence,
		RecentOpsBefore,
		ChunkState.RecentOps.Num(),
		ChunkState.TotalCompactionCount,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);

	return true;
}

void AVoxelChunkManager::TrimRecentOps(FRTPSVoxelChunkState& ChunkState, const TCHAR* Reason)
{
	const int32 EffectiveMaxRecentOps = FMath::Max(MaxRecentOpsPerChunk, 1);
	const int32 TrimCount = ChunkState.RecentOps.Num() - EffectiveMaxRecentOps;
	if (TrimCount <= 0)
	{
		return;
	}

	ChunkState.RecentOps.RemoveAt(0, TrimCount, EAllowShrinking::No);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] RecentOps trimmed. ChunkManager=%s ChunkCoord=(%d,%d,%d) Trimmed=%d Revision=%d RecentOpsCount=%d MaxRecentOps=%d Reason=%s Authority=%d"),
		*GetNameSafe(this),
		ChunkState.ChunkCoord.X,
		ChunkState.ChunkCoord.Y,
		ChunkState.ChunkCoord.Z,
		TrimCount,
		ChunkState.Revision,
		ChunkState.RecentOps.Num(),
		EffectiveMaxRecentOps,
		Reason != nullptr ? Reason : TEXT("Unknown"),
		HasAuthority() ? 1 : 0);
}

bool AVoxelChunkManager::ApplyPendingIncomingChunkPayloadIfReady(const FIntVector& ChunkCoord, AVoxelChunk& Chunk, const TCHAR* Reason)
{
	if (HasAuthority() || Chunk.State != EVoxelChunkState::Ready)
	{
		return false;
	}

	const FRTPSVoxelChunkStatePayload* PendingPayload = PendingIncomingChunkPayloads.Find(ChunkCoord);
	if (PendingPayload == nullptr)
	{
		return false;
	}

	const FRTPSVoxelChunkStatePayload PayloadCopy = *PendingPayload;
	return ApplyChunkStatePayloadLocal(PayloadCopy, Reason);
}

void AVoxelChunkManager::RefreshReadyChunkStateMirrors(const TCHAR* Reason)
{
	for (const TPair<FIntVector, TObjectPtr<AVoxelChunk>>& Pair : LoadedChunks)
	{
		AVoxelChunk* Chunk = Pair.Value.Get();
		if (!IsValid(Chunk))
		{
			continue;
		}

		if (HasAuthority())
		{
			if (Chunk->State == EVoxelChunkState::Ready)
			{
				const FRTPSVoxelChunkState* ExistingState = FindChunkState(Pair.Key);
				const bool bHasAppliedAuthoritativeState = ExistingState != nullptr
					&& ExistingState->bHasDensity
					&& !ExistingState->LatticeDensity.IsEmpty()
					&& (ExistingState->Revision > 0 || ExistingState->bDirty || !ExistingState->RecentOps.IsEmpty());
				if (bHasAppliedAuthoritativeState
					&& ExistingState->LatticeDensity.Num() == Chunk->GetExpectedLatticeSampleCount()
					&& Chunk->HasValidLatticeDensity()
					&& ExistingState->LatticeDensity != Chunk->GetLatticeDensity())
				{
					Chunk->ApplyAuthoritativeDensitySnapshot(
						ExistingState->LatticeDensity,
						ExistingState->Revision,
						TEXT("RefreshReadyChunkStateMirrorsAuthoritativeState"));
					UE_LOG(
						LogTemp,
						Log,
						TEXT("[RTPSValidation] Ready chunk received existing authoritative ChunkState instead of stale actor mirror. ChunkManager=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d DensityCount=%d Reason=%s Authority=%d"),
						*GetNameSafe(this),
						Pair.Key.X,
						Pair.Key.Y,
						Pair.Key.Z,
						ExistingState->Revision,
						ExistingState->LatticeDensity.Num(),
						Reason != nullptr ? Reason : TEXT("Unknown"),
						HasAuthority() ? 1 : 0);
					continue;
				}
			}

			MirrorChunkDensityToState(Pair.Key, *Chunk, Reason);
			ReplayPendingOpsForChunk(Pair.Key, *Chunk, Reason);
			continue;
		}

		if (Chunk->State != EVoxelChunkState::Ready)
		{
			continue;
		}

		if (ApplyPendingIncomingChunkPayloadIfReady(Pair.Key, *Chunk, Reason))
		{
			continue;
		}

		int32 LocalKnownRevision = 0;
		if (const int32* LastAppliedRemoteRevision = LastAppliedRemoteRevisionByCoord.Find(Pair.Key))
		{
			LocalKnownRevision = *LastAppliedRemoteRevision;
		}
		else if (const FRTPSVoxelChunkState* LocalState = FindChunkState(Pair.Key))
		{
			LocalKnownRevision = LocalState->Revision;
		}

		SubscribeToAuthoritativeChunkStateIfClient(Pair.Key, LocalKnownRevision, Reason);
	}
}
