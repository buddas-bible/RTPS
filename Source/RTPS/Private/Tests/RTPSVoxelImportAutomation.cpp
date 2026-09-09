// Fill out your copyright notice in the Description page of Project Settings.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include <limits>
#include "Async/TaskGraphInterfaces.h"
#include "Components/SceneComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "Controller/RTPSPlayerController.h"
#include "GameFramework/Pawn.h"
#include "VoxelAuthoring/RuntimeAuthoringVolume.h"
#include "VoxelAuthoring/VoxelChunk.h"
#include "VoxelAuthoring/VoxelChunkManager.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
#include "VoxelAuthoring/VoxelEditOp.h"
#include "VoxelImport/RTPSMarchingCubes.h"

namespace
{
	AVoxelChunk* SpawnReadyVoxelChunkForAutomation(
		UWorld* World,
		const FActorSpawnParameters& SpawnParams,
		const FIntVector& ChunkCoord,
		const FIntVector& ChunkDimensions,
		float CellSize,
		float IsoLevel)
	{
		if (!World)
		{
			return nullptr;
		}

		const FVector ChunkLocation(
			ChunkCoord.X * ChunkDimensions.X * CellSize,
			ChunkCoord.Y * ChunkDimensions.Y * CellSize,
			ChunkCoord.Z * ChunkDimensions.Z * CellSize);

		AVoxelChunk* Chunk = World->SpawnActor<AVoxelChunk>(
			AVoxelChunk::StaticClass(),
			ChunkLocation,
			FRotator::ZeroRotator,
			SpawnParams);

		if (!Chunk)
		{
			return nullptr;
		}

		Chunk->ChunkCoord = ChunkCoord;
		Chunk->ChunkDimensions = ChunkDimensions;
		Chunk->CellSize = CellSize;
		Chunk->IsoLevel = IsoLevel;

		const int32 SampleCount =
			(ChunkDimensions.X + 1) *
			(ChunkDimensions.Y + 1) *
			(ChunkDimensions.Z + 1);

		TArray<float> InitialLattice;
		InitialLattice.SetNumZeroed(SampleCount);

		RTPSVoxelImport::FMarchingCubesMeshData InitialMeshData;
		Chunk->ApplyMeshData(MoveTemp(InitialMeshData), MoveTemp(InitialLattice), true);
		return Chunk;
	}

	FRTPSVoxelEditOp MakeVoxelAutomationEditOp(int64 ServerSequence, const FVector& WorldPosition)
	{
		FVoxelBrush Brush;
		Brush.WorldPosition = WorldPosition;
		Brush.Radius = 200.f;
		Brush.Strength = 0.25f;
		Brush.Mode = EVoxelBrushMode::Add;
		Brush.Shape = EVoxelBrushShape::Sphere;

		FRTPSVoxelEditOp EditOp;
		EditOp.ServerSequence = ServerSequence;
		EditOp.Brush = Brush;
		return EditOp;
	}

	ARTPSPlayerController* SpawnVoxelAutomationPlayerController(UWorld* World, const FActorSpawnParameters& SpawnParams)
	{
		if (!World)
		{
			return nullptr;
		}

		return World->SpawnActor<ARTPSPlayerController>(
			ARTPSPlayerController::StaticClass(),
			FTransform::Identity,
			SpawnParams);
	}

	void AddAutomationTriangle(
		RTPSVoxelImport::FMarchingCubesMeshData& MeshData,
		const FVector& A,
		const FVector& B,
		const FVector& C)
	{
		const int32 BaseIndex = MeshData.Vertices.Num();
		MeshData.Vertices.Add(A);
		MeshData.Vertices.Add(B);
		MeshData.Vertices.Add(C);
		MeshData.Triangles.Add(BaseIndex);
		MeshData.Triangles.Add(BaseIndex + 1);
		MeshData.Triangles.Add(BaseIndex + 2);

		FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			Normal = FVector::UpVector;
		}
		MeshData.Normals.Add(Normal);
		MeshData.Normals.Add(Normal);
		MeshData.Normals.Add(Normal);
		MeshData.UV0.Add(FVector2D::ZeroVector);
		MeshData.UV0.Add(FVector2D::UnitVector);
		MeshData.UV0.Add(FVector2D(0.f, 1.f));
		MeshData.VertexColors.Add(FColor::White);
		MeshData.VertexColors.Add(FColor::White);
		MeshData.VertexColors.Add(FColor::White);
		const FProcMeshTangent Tangent(FVector::ForwardVector, false);
		MeshData.Tangents.Add(Tangent);
		MeshData.Tangents.Add(Tangent);
		MeshData.Tangents.Add(Tangent);
	}

	bool HasAnyDensityChanged(const TArray<float>& Before, const TArray<float>& After)
	{
		if (Before.Num() != After.Num())
		{
			return true;
		}

		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			if (!FMath::IsNearlyEqual(Before[Index], After[Index]))
			{
				return true;
			}
		}

		return false;
	}

	bool PendingOpsContainSequence(const TArray<FRTPSVoxelEditOp>* PendingOps, int64 ServerSequence)
	{
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

	void ConfigureAuthoritativeChunkStateForSyncMode(
		FRTPSVoxelChunkState& ChunkState,
		const FIntVector& ChunkCoord,
		int32 Revision,
		int32 SnapshotRevision)
	{
		ChunkState = FRTPSVoxelChunkState();
		ChunkState.ChunkCoord = ChunkCoord;
		ChunkState.bHasDensity = true;
		ChunkState.LatticeDensity.Init(0.5f, 8);
		ChunkState.Revision = Revision;
		ChunkState.SnapshotRevision = SnapshotRevision;
		ChunkState.SnapshotServerSequence = SnapshotRevision > 0 ? static_cast<int64>(SnapshotRevision * 100) : INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelDegenerateTrianglesAreFilteredTest,
	"RTPS.VoxelAuthoring.Mesh.DegenerateTrianglesAreFiltered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDegenerateTrianglesAreFilteredTest::RunTest(const FString& Parameters)
{
	RTPSVoxelImport::FMarchingCubesMeshData MeshData;
	AddAutomationTriangle(
		MeshData,
		FVector::ZeroVector,
		FVector(100.f, 0.f, 0.f),
		FVector(0.f, 100.f, 0.f));
	AddAutomationTriangle(
		MeshData,
		FVector(200.f, 0.f, 0.f),
		FVector(200.f, 0.f, 0.f),
		FVector(200.f, 100.f, 0.f));
	AddAutomationTriangle(
		MeshData,
		FVector(300.f, 0.f, 0.f),
		FVector(300.001f, 0.f, 0.f),
		FVector(300.f, 0.001f, 0.f));

	const RTPSVoxelImport::FMarchingCubesMeshFilterStats FilterStats =
		RTPSVoxelImport::FilterDegenerateTriangles(MeshData);

	TestEqual(TEXT("Original triangle count includes valid and degenerate triangles."), FilterStats.OriginalTriangleCount, 3);
	TestEqual(TEXT("Degenerate triangle count was removed."), FilterStats.RemovedDegenerateTriangleCount, 2);
	TestEqual(TEXT("Only one valid triangle remains."), FilterStats.FinalTriangleCount, 1);
	TestEqual(TEXT("Filtered mesh triangle index count."), MeshData.Triangles.Num(), 3);
	TestEqual(TEXT("Filtered mesh vertex count."), MeshData.Vertices.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelValidTrianglesArePreservedTest,
	"RTPS.VoxelAuthoring.Mesh.ValidTrianglesArePreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelValidTrianglesArePreservedTest::RunTest(const FString& Parameters)
{
	RTPSVoxelImport::FMarchingCubesMeshData MeshData;
	AddAutomationTriangle(
		MeshData,
		FVector::ZeroVector,
		FVector(100.f, 0.f, 0.f),
		FVector(0.f, 100.f, 0.f));
	AddAutomationTriangle(
		MeshData,
		FVector(100.f, 0.f, 100.f),
		FVector(200.f, 0.f, 100.f),
		FVector(100.f, 100.f, 100.f));

	const RTPSVoxelImport::FMarchingCubesMeshFilterStats FilterStats =
		RTPSVoxelImport::FilterDegenerateTriangles(MeshData);

	TestEqual(TEXT("Original valid triangle count."), FilterStats.OriginalTriangleCount, 2);
	TestEqual(TEXT("No valid triangles are removed."), FilterStats.RemovedDegenerateTriangleCount, 0);
	TestEqual(TEXT("All valid triangles remain."), FilterStats.FinalTriangleCount, 2);
	TestEqual(TEXT("Filtered valid mesh triangle index count."), MeshData.Triangles.Num(), 6);
	TestEqual(TEXT("Filtered valid mesh vertex count."), MeshData.Vertices.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSDensityMCBasicTest,
	"RTPS.VoxelAuthoring.DensityMC.BasicOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSDensityMCEmptyTest::RunTest(const FString& Parameters)
{
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
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSDensityMCFullTest::RunTest(const FString& Parameters)
{
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelDebugVisualizerTest,
	"RTPS.VoxelAuthoring.DebugVisualizer.RebuildAndThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDebugVisualizerTest::RunTest(const FString& Parameters)
{
	// 에디터 월드에서 임시 ARuntimeAuthoringVolume 액터 생성
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null - 에디터 컨텍스트에서만 실행 가능"));
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

	TestNotNull(TEXT("DebugVisualizer 컴포넌트가 생성되어 있어야 함"), Volume->DebugVisualizer.Get());

	if (Volume->DebugVisualizer)
	{
		// 구형 밀도 채우기 (중심 800,800,800 반경 500 → 구형 솔리드 영역)
		Volume->FillSphereDensity(FVector(800.f, 800.f, 800.f), 500.f, 1.f);

		Volume->DebugVisualizer->bVisualizerEnabled = true;

		// RebuildDebugVisualizer: 크래시 없이 실행
		Volume->RebuildDebugVisualizer();
		AddInfo(TEXT("RebuildDebugVisualizer 정상 실행"));

		// Threshold 변경: 크래시 없이 실행
		Volume->DebugVisualizer->Threshold = 0.3f;
		Volume->ApplyDebugVisualizerThreshold();

		Volume->DebugVisualizer->Threshold = 0.7f;
		Volume->ApplyDebugVisualizerThreshold();
		AddInfo(TEXT("ApplyDebugVisualizerThreshold 정상 실행"));

		// ClearVisualizer: 크래시 없이 실행
		Volume->DebugVisualizer->ClearVisualizer();
		AddInfo(TEXT("ClearVisualizer 정상 실행"));
	}

	EditorWorld->DestroyActor(Volume, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChunkModifiedStatePersistenceTest,
	"RTPS.VoxelAuthoring.Chunk.ModifiedStatePersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChunkModifiedStatePersistenceTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunk* Chunk = EditorWorld->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!Chunk)
	{
		AddError(TEXT("AVoxelChunk spawn failed."));
		return false;
	}

	Chunk->ChunkCoord = FIntVector::ZeroValue;
	Chunk->ChunkDimensions = FIntVector(1, 1, 1);
	Chunk->CellSize = 100.f;
	Chunk->IsoLevel = 0.5f;
	Chunk->SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/VoxelChunkModifiedState"));

	TArray<float> InitialLattice;
	InitialLattice.SetNumZeroed(8);

	RTPSVoxelImport::FMarchingCubesMeshData InitialMeshData;
	Chunk->bModified = true;
	Chunk->ApplyMeshData(MoveTemp(InitialMeshData), MoveTemp(InitialLattice), true);

	TestTrue(TEXT("Initial mesh application leaves the chunk ready."), Chunk->State == EVoxelChunkState::Ready);
	TestFalse(TEXT("Initial mesh application marks the chunk clean."), Chunk->bModified);

	Chunk->State = EVoxelChunkState::Loading;

	FVoxelBrush Brush;
	Brush.WorldPosition = FVector(50.f, 50.f, 50.f);
	Brush.Radius = 200.f;
	Brush.Strength = 0.25f;
	Brush.Mode = EVoxelBrushMode::Add;
	Brush.Shape = EVoxelBrushShape::Sphere;

	Chunk->ApplyBrush(Brush);
	TestTrue(TEXT("ApplyBrush marks the chunk modified before rebuild completion."), Chunk->bModified);

	const double DeadlineSeconds = FPlatformTime::Seconds() + 5.0;
	while (Chunk->State != EVoxelChunkState::Ready && FPlatformTime::Seconds() < DeadlineSeconds)
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.01f);
	}
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

	TestTrue(TEXT("Brush rebuild callback applied mesh data."), Chunk->State == EVoxelChunkState::Ready);
	TestTrue(TEXT("Brush rebuild keeps the chunk modified."), Chunk->bModified);
	TestTrue(TEXT("SaveAllModifiedChunks/DestroyChunk would see the modified chunk."), Chunk->bModified);

	const bool bSaved = Chunk->SaveToDisk();
	TestTrue(TEXT("Modified chunk density can be saved."), bSaved);
	TestTrue(TEXT("Modified chunk save file exists."), FPaths::FileExists(Chunk->GetSaveFilePath()));

	IFileManager::Get().Delete(*Chunk->GetSaveFilePath(), false, true);
	EditorWorld->DestroyActor(Chunk, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelEditOpLocalApplyTest,
	"RTPS.VoxelAuthoring.EditOp.LocalApplyMatchesBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelEditOpLocalApplyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	AVoxelChunk* EditOpChunk = EditorWorld->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	AVoxelChunk* DirectBrushChunk = EditorWorld->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!ChunkManager || !EditOpChunk || !DirectBrushChunk)
	{
		AddError(TEXT("Voxel edit op test actor spawn failed."));
		if (EditOpChunk) { EditorWorld->DestroyActor(EditOpChunk, false, false); }
		if (DirectBrushChunk) { EditorWorld->DestroyActor(DirectBrushChunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	auto InitializeReadyChunk = [](AVoxelChunk* Chunk)
	{
		Chunk->ChunkCoord = FIntVector::ZeroValue;
		Chunk->ChunkDimensions = FIntVector(1, 1, 1);
		Chunk->CellSize = 100.f;
		Chunk->IsoLevel = 0.5f;

		TArray<float> InitialLattice;
		InitialLattice.SetNumZeroed(8);

		RTPSVoxelImport::FMarchingCubesMeshData InitialMeshData;
		Chunk->ApplyMeshData(MoveTemp(InitialMeshData), MoveTemp(InitialLattice), true);
	};

	InitializeReadyChunk(EditOpChunk);
	InitializeReadyChunk(DirectBrushChunk);

	ChunkManager->LoadedChunks.Add(FIntVector::ZeroValue, EditOpChunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	FVoxelBrush Brush;
	Brush.WorldPosition = FVector(50.f, 50.f, 50.f);
	Brush.Radius = 200.f;
	Brush.Strength = 0.25f;
	Brush.Mode = EVoxelBrushMode::Add;
	Brush.Shape = EVoxelBrushShape::Sphere;

	FRTPSVoxelEditOp EditOp;
	EditOp.ServerSequence = 42;
	EditOp.Brush = Brush;

	ChunkManager->ApplyEditOpLocal(EditOp);
	DirectBrushChunk->ApplyBrush(Brush);

	TestTrue(TEXT("EditOp local apply marks the chunk modified."), EditOpChunk->bModified);
	TestTrue(TEXT("Direct brush marks the comparison chunk modified."), DirectBrushChunk->bModified);
	TestEqual(TEXT("EditOp and direct brush produce the same lattice sample count."), EditOpChunk->LatticeDensity.Num(), DirectBrushChunk->LatticeDensity.Num());

	const int32 SampleCount = FMath::Min(EditOpChunk->LatticeDensity.Num(), DirectBrushChunk->LatticeDensity.Num());
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		if (!TestEqual(
			FString::Printf(TEXT("EditOp density matches direct brush at sample %d"), SampleIndex),
			EditOpChunk->LatticeDensity[SampleIndex],
			DirectBrushChunk->LatticeDensity[SampleIndex]))
		{
			break;
		}
	}

	EditorWorld->DestroyActor(EditOpChunk, false, false);
	EditorWorld->DestroyActor(DirectBrushChunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChunkStateMirrorTracksLoadedEditTest,
	"RTPS.VoxelAuthoring.Chunk.StateMirrorTracksLoadedEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChunkStateMirrorTracksLoadedEditTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	AVoxelChunk* Chunk = EditorWorld->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Chunk state mirror test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	Chunk->ChunkCoord = TestChunkCoord;
	Chunk->ChunkDimensions = FIntVector(1, 1, 1);
	Chunk->CellSize = 100.f;
	Chunk->IsoLevel = 0.5f;

	TArray<float> InitialLattice;
	InitialLattice.SetNumZeroed(8);

	RTPSVoxelImport::FMarchingCubesMeshData InitialMeshData;
	Chunk->ApplyMeshData(MoveTemp(InitialMeshData), MoveTemp(InitialLattice), true);

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->RefreshReadyChunkStateMirrors(TEXT("AutomationInitialMirror"));

	const FRTPSVoxelChunkState* InitialState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("Initial ChunkState mirror exists."), InitialState);
	if (InitialState != nullptr)
	{
		TestTrue(TEXT("Initial ChunkState has density."), InitialState->bHasDensity);
		TestFalse(TEXT("Initial ChunkState starts clean."), InitialState->bDirty);
		TestEqual(TEXT("Initial ChunkState revision starts at zero."), InitialState->Revision, 0);
		TestEqual(TEXT("Initial ChunkState density count matches chunk."), InitialState->LatticeDensity.Num(), Chunk->GetLatticeDensity().Num());
	}

	FVoxelBrush Brush;
	Brush.WorldPosition = FVector(50.f, 50.f, 50.f);
	Brush.Radius = 200.f;
	Brush.Strength = 0.25f;
	Brush.Mode = EVoxelBrushMode::Add;
	Brush.Shape = EVoxelBrushShape::Sphere;

	FRTPSVoxelEditOp EditOp;
	EditOp.ServerSequence = 7;
	EditOp.Brush = Brush;

	ChunkManager->ApplyEditOpLocal(EditOp);

	const FRTPSVoxelChunkState* EditedState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("Edited ChunkState mirror exists."), EditedState);
	if (EditedState != nullptr)
	{
		TestTrue(TEXT("Edited ChunkState keeps density."), EditedState->bHasDensity);
		TestTrue(TEXT("Edited ChunkState is dirty."), EditedState->bDirty);
		TestEqual(TEXT("Edited ChunkState revision increments once."), EditedState->Revision, 1);
		TestEqual(TEXT("Edited ChunkState density count matches chunk."), EditedState->LatticeDensity.Num(), Chunk->GetLatticeDensity().Num());
		TestTrue(TEXT("Edited ChunkState density mirrors chunk density."), EditedState->LatticeDensity == Chunk->GetLatticeDensity());
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelPendingOpsReplayWhenChunkBecomesReadyTest,
	"RTPS.VoxelAuthoring.Chunk.PendingOpsReplayWhenChunkBecomesReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelPendingOpsReplayWhenChunkBecomesReadyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!ChunkManager)
	{
		AddError(TEXT("AVoxelChunkManager spawn failed."));
		return false;
	}

	const FIntVector TestChunkCoord(1, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(4, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	FVoxelBrush Brush;
	Brush.WorldPosition = FVector(600.f, 200.f, 200.f);
	Brush.Radius = 125.f;
	Brush.Strength = 0.25f;
	Brush.Mode = EVoxelBrushMode::Add;
	Brush.Shape = EVoxelBrushShape::Sphere;

	FRTPSVoxelEditOp EditOp;
	EditOp.ServerSequence = 101;
	EditOp.Brush = Brush;

	const TArray<FIntVector> AffectedChunkCoords = ChunkManager->GetAffectedChunkCoords(EditOp);
	TestEqual(TEXT("Brush affects one chunk in this test setup."), AffectedChunkCoords.Num(), 1);
	TestTrue(TEXT("Affected chunk coords include the not-yet-loaded chunk."), AffectedChunkCoords.Contains(TestChunkCoord));

	ChunkManager->ApplyEditOpAuthoritative(EditOp);

	const TArray<FRTPSVoxelEditOp>* PendingBeforeLoad = ChunkManager->PendingOpsByChunk.Find(TestChunkCoord);
	TestNotNull(TEXT("EditOp is queued while the affected chunk is missing."), PendingBeforeLoad);
	if (PendingBeforeLoad != nullptr)
	{
		TestEqual(TEXT("Exactly one pending op is queued for the missing chunk."), PendingBeforeLoad->Num(), 1);
		TestEqual(TEXT("Pending op keeps the authoritative server sequence."), (*PendingBeforeLoad)[0].ServerSequence, EditOp.ServerSequence);
	}
	TestFalse(TEXT("Missing chunk is not marked applied before replay."), ChunkManager->HasAppliedSequenceToChunk(TestChunkCoord, EditOp.ServerSequence));

	AVoxelChunk* Chunk = EditorWorld->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		FVector(400.f, 0.f, 0.f),
		FRotator::ZeroRotator,
		SpawnParams);

	if (!Chunk)
	{
		AddError(TEXT("AVoxelChunk spawn failed."));
		EditorWorld->DestroyActor(ChunkManager, false, false);
		return false;
	}

	Chunk->ChunkCoord = TestChunkCoord;
	Chunk->ChunkDimensions = FIntVector(4, 4, 4);
	Chunk->CellSize = 100.f;
	Chunk->IsoLevel = 0.5f;

	TArray<float> InitialLattice;
	InitialLattice.SetNumZeroed(125);

	RTPSVoxelImport::FMarchingCubesMeshData InitialMeshData;
	Chunk->ApplyMeshData(MoveTemp(InitialMeshData), MoveTemp(InitialLattice), true);

	const TArray<float> DensityBeforeReplay = Chunk->GetLatticeDensity();

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->RefreshReadyChunkStateMirrors(TEXT("AutomationPendingReplay"));

	const TArray<FRTPSVoxelEditOp>* PendingAfterReplay = ChunkManager->PendingOpsByChunk.Find(TestChunkCoord);
	TestTrue(TEXT("Pending op is removed after replay."), PendingAfterReplay == nullptr || PendingAfterReplay->Num() == 0);
	TestTrue(TEXT("Replay marks the server sequence applied for this chunk."), ChunkManager->HasAppliedSequenceToChunk(TestChunkCoord, EditOp.ServerSequence));

	bool bDensityChanged = false;
	const TArray<float>& DensityAfterReplay = Chunk->GetLatticeDensity();
	for (int32 SampleIndex = 0; SampleIndex < FMath::Min(DensityBeforeReplay.Num(), DensityAfterReplay.Num()); ++SampleIndex)
	{
		if (!FMath::IsNearlyEqual(DensityBeforeReplay[SampleIndex], DensityAfterReplay[SampleIndex]))
		{
			bDensityChanged = true;
			break;
		}
	}
	TestTrue(TEXT("Replayed pending op changes the chunk density."), bDensityChanged);
	TestTrue(TEXT("Replayed pending op marks the chunk modified."), Chunk->bModified);

	const FRTPSVoxelChunkState* StateAfterReplay = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists after pending replay."), StateAfterReplay);
	if (StateAfterReplay != nullptr)
	{
		TestTrue(TEXT("ChunkState is dirty after pending replay."), StateAfterReplay->bDirty);
		TestEqual(TEXT("ChunkState revision increments once after replay."), StateAfterReplay->Revision, 1);
		TestTrue(TEXT("ChunkState mirrors replayed density."), StateAfterReplay->LatticeDensity == DensityAfterReplay);
	}

	const TArray<float> DensityAfterFirstReplay = Chunk->GetLatticeDensity();
	const int32 RevisionAfterFirstReplay = StateAfterReplay != nullptr ? StateAfterReplay->Revision : INDEX_NONE;

	ChunkManager->PendingOpsByChunk.FindOrAdd(TestChunkCoord).Add(EditOp);
	ChunkManager->ReplayPendingOpsForChunk(TestChunkCoord, *Chunk, TEXT("AutomationDuplicateReplay"));

	const TArray<FRTPSVoxelEditOp>* PendingAfterDuplicateReplay = ChunkManager->PendingOpsByChunk.Find(TestChunkCoord);
	TestTrue(TEXT("Duplicate pending op is removed after duplicate replay attempt."), PendingAfterDuplicateReplay == nullptr || PendingAfterDuplicateReplay->Num() == 0);
	TestTrue(TEXT("Duplicate replay does not change density."), Chunk->GetLatticeDensity() == DensityAfterFirstReplay);

	const FRTPSVoxelChunkState* StateAfterDuplicateReplay = ChunkManager->FindChunkState(TestChunkCoord);
	if (StateAfterDuplicateReplay != nullptr)
	{
		TestEqual(TEXT("Duplicate replay does not increment revision."), StateAfterDuplicateReplay->Revision, RevisionAfterFirstReplay);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelRevisionAndRecentOpsTrackUniqueEditsTest,
	"RTPS.VoxelAuthoring.Chunk.RevisionAndRecentOpsTrackUniqueEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelRevisionAndRecentOpsTrackUniqueEditsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Revision/recent-op test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	for (int64 Sequence = 1; Sequence <= 3; ++Sequence)
	{
		ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(Sequence, FVector(50.f, 50.f, 50.f)));
	}

	const FRTPSVoxelChunkState* ChunkState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists after unique edits."), ChunkState);
	if (ChunkState != nullptr)
	{
		TestTrue(TEXT("ChunkState is dirty after unique edits."), ChunkState->bDirty);
		TestEqual(TEXT("Revision increments once per unique edit."), ChunkState->Revision, 3);
		TestEqual(TEXT("RecentOps contains every unique edit."), ChunkState->RecentOps.Num(), 3);
		TestEqual(TEXT("RecentOps[0] sequence."), ChunkState->RecentOps[0].ServerSequence, static_cast<int64>(1));
		TestEqual(TEXT("RecentOps[1] sequence."), ChunkState->RecentOps[1].ServerSequence, static_cast<int64>(2));
		TestEqual(TEXT("RecentOps[2] sequence."), ChunkState->RecentOps[2].ServerSequence, static_cast<int64>(3));
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelRecentOpsCapTrimsOldestTest,
	"RTPS.VoxelAuthoring.Chunk.RecentOpsCapTrimsOldest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelRecentOpsCapTrimsOldestTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("RecentOps cap test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->MaxRecentOpsPerChunk = 3;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	for (int64 Sequence = 10; Sequence <= 14; ++Sequence)
	{
		ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(Sequence, FVector(50.f, 50.f, 50.f)));
	}

	const FRTPSVoxelChunkState* ChunkState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists after capped edits."), ChunkState);
	if (ChunkState != nullptr)
	{
		TestEqual(TEXT("Revision tracks all unique edits even when RecentOps is capped."), ChunkState->Revision, 5);
		TestEqual(TEXT("Snapshot compaction runs when RecentOps reaches MaxRecentOpsPerChunk."), ChunkState->SnapshotRevision, 3);
		TestEqual(TEXT("Snapshot sequence records the last op compacted into the snapshot."), ChunkState->SnapshotServerSequence, static_cast<int64>(12));
		TestEqual(TEXT("One snapshot compaction is recorded."), ChunkState->TotalCompactionCount, 1);
		TestEqual(TEXT("RecentOps keeps only ops newer than the compacted snapshot."), ChunkState->RecentOps.Num(), 2);
		TestEqual(TEXT("RecentOps keeps post-snapshot sequence 13."), ChunkState->RecentOps[0].ServerSequence, static_cast<int64>(13));
		TestEqual(TEXT("RecentOps keeps post-snapshot sequence 14."), ChunkState->RecentOps[1].ServerSequence, static_cast<int64>(14));
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelDuplicateEditDoesNotAdvanceRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.DuplicateEditDoesNotAdvanceRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDuplicateEditDoesNotAdvanceRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Duplicate edit test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	const FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(77, FVector(50.f, 50.f, 50.f));
	ChunkManager->ApplyEditOpLocal(EditOp);
	ChunkManager->ApplyEditOpLocal(EditOp);

	const FRTPSVoxelChunkState* ChunkState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists after duplicate edit."), ChunkState);
	if (ChunkState != nullptr)
	{
		TestEqual(TEXT("Duplicate edit increments Revision only once."), ChunkState->Revision, 1);
		TestEqual(TEXT("Duplicate edit records one RecentOp."), ChunkState->RecentOps.Num(), 1);
		TestEqual(TEXT("RecentOps stores the duplicate sequence once."), ChunkState->RecentOps[0].ServerSequence, static_cast<int64>(77));
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSnapshotCompactionUpdatesSnapshotRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.SnapshotCompactionUpdatesSnapshotRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelRecentOpEntryStoresRevisionBeforeAfterAndServerSequenceTest,
	"RTPS.VoxelAuthoring.Chunk.RecentOpEntryStoresRevisionBeforeAfterAndServerSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelRecentOpEntryStoresRevisionBeforeAfterAndServerSequenceTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("RecentOp metadata test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	const FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(88, FVector(50.f, 50.f, 50.f));
	ChunkManager->ApplyEditOpLocal(EditOp);

	const FRTPSVoxelChunkState* ChunkState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists after wrapped recent-op edit."), ChunkState);
	if (ChunkState != nullptr)
	{
		TestEqual(TEXT("One RecentOp entry is stored."), ChunkState->RecentOps.Num(), 1);
		if (ChunkState->RecentOps.Num() == 1)
		{
			const FRTPSVoxelRecentEditOp& RecentOp = ChunkState->RecentOps[0];
			TestEqual(TEXT("RecentOp stores RevisionBeforeApply."), RecentOp.RevisionBeforeApply, 0);
			TestEqual(TEXT("RecentOp stores RevisionAfterApply."), RecentOp.RevisionAfterApply, 1);
			TestEqual(TEXT("RecentOp stores ServerSequence."), RecentOp.ServerSequence, static_cast<int64>(88));
			TestEqual(TEXT("Wrapped EditOp stores ServerSequence."), RecentOp.EditOp.ServerSequence, EditOp.ServerSequence);
			TestEqual(TEXT("Wrapped EditOp stores brush position."), RecentOp.EditOp.Brush.WorldPosition, EditOp.Brush.WorldPosition);
			TestEqual(TEXT("Wrapped EditOp stores brush radius."), RecentOp.EditOp.Brush.Radius, EditOp.Brush.Radius);
			TestEqual(TEXT("Wrapped EditOp stores brush strength."), RecentOp.EditOp.Brush.Strength, EditOp.Brush.Strength);
			TestEqual(TEXT("Wrapped EditOp stores brush mode."), RecentOp.EditOp.Brush.Mode, EditOp.Brush.Mode);
			TestEqual(TEXT("Wrapped EditOp stores brush shape."), RecentOp.EditOp.Brush.Shape, EditOp.Brush.Shape);
		}
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelCompactionUsesWrappedRecentOpsToFindLatestServerSequenceTest,
	"RTPS.VoxelAuthoring.Chunk.CompactionUsesWrappedRecentOpsToFindLatestServerSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelCompactionUsesWrappedRecentOpsToFindLatestServerSequenceTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Wrapped RecentOps compaction test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	for (int64 Sequence = 101; Sequence <= 103; ++Sequence)
	{
		ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(Sequence, FVector(50.f, 50.f, 50.f)));
	}

	FRTPSVoxelChunkState* ChunkState = ChunkManager->ChunkStates.Find(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists before wrapped RecentOps compaction."), ChunkState);
	if (ChunkState != nullptr)
	{
		const int32 RevisionBeforeCompaction = ChunkState->Revision;
		const bool bCompacted = ChunkManager->CompactChunkStateSnapshot(*ChunkState, TEXT("AutomationWrappedRecentOpsCompaction"));
		TestTrue(TEXT("Snapshot compaction succeeds with wrapped RecentOps."), bCompacted);
		TestEqual(TEXT("SnapshotRevision records current Revision."), ChunkState->SnapshotRevision, RevisionBeforeCompaction);
		TestEqual(TEXT("Compaction does not advance Revision."), ChunkState->Revision, RevisionBeforeCompaction);
		TestEqual(TEXT("SnapshotServerSequence uses latest wrapped RecentOp sequence."), ChunkState->SnapshotServerSequence, static_cast<int64>(103));
		TestEqual(TEXT("Clear-on-compaction policy removes wrapped RecentOps."), ChunkState->RecentOps.Num(), 0);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBuildDeltaPayloadFromRecentOpsCoversFullRangeTest,
	"RTPS.VoxelAuthoring.Chunk.BuildDeltaPayloadFromRecentOpsCoversFullRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBuildDeltaPayloadFromRecentOpsCoversFullRangeTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Delta full-range test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	for (int64 Sequence = 1; Sequence <= 3; ++Sequence)
	{
		ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(Sequence, FVector(50.f, 50.f, 50.f)));
	}

	FRTPSVoxelChunkDeltaPayload Payload;
	const bool bBuilt = ChunkManager->BuildChunkDeltaPayload(TestChunkCoord, 0, Payload);
	TestTrue(TEXT("Delta payload builds when RecentOps covers the full revision range."), bBuilt);
	TestTrue(TEXT("Delta payload reports success."), Payload.bSuccess);
	TestFalse(TEXT("Delta payload does not require full snapshot."), Payload.bRequiresFullSnapshot);
	TestEqual(TEXT("Delta payload FromRevision."), Payload.FromRevision, 0);
	TestEqual(TEXT("Delta payload ToRevision."), Payload.ToRevision, 3);
	TestEqual(TEXT("Delta payload includes all three EditOps."), Payload.EditOps.Num(), 3);
	if (Payload.EditOps.Num() == 3)
	{
		TestEqual(TEXT("Delta EditOp[0] sequence."), Payload.EditOps[0].ServerSequence, static_cast<int64>(1));
		TestEqual(TEXT("Delta EditOp[1] sequence."), Payload.EditOps[1].ServerSequence, static_cast<int64>(2));
		TestEqual(TEXT("Delta EditOp[2] sequence."), Payload.EditOps[2].ServerSequence, static_cast<int64>(3));
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBuildDeltaPayloadFailsWhenFromRevisionBeforeSnapshotTest,
	"RTPS.VoxelAuthoring.Chunk.BuildDeltaPayloadFailsWhenFromRevisionBeforeSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBuildDeltaPayloadFailsWhenFromRevisionBeforeSnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Delta old-snapshot test manager spawn failed."));
		return false;
	}

	const FIntVector TestChunkCoord(10, 0, 0);
	FRTPSVoxelChunkState& ChunkState = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	ChunkState.bHasDensity = true;
	ChunkState.LatticeDensity.Init(0.5f, 8);
	ChunkState.Revision = 5;
	ChunkState.SnapshotRevision = 3;
	ChunkState.SnapshotServerSequence = 300;

	FRTPSVoxelChunkDeltaPayload Payload;
	const bool bBuilt = ChunkManager->BuildChunkDeltaPayload(TestChunkCoord, 1, Payload);
	TestFalse(TEXT("Delta payload is not built when client is older than snapshot baseline."), bBuilt);
	TestFalse(TEXT("Failed delta payload does not report success."), Payload.bSuccess);
	TestTrue(TEXT("Old client revision requires full snapshot."), Payload.bRequiresFullSnapshot);
	TestTrue(TEXT("Failure reason mentions snapshot baseline."), Payload.FailureReason.Contains(TEXT("snapshot baseline")));
	TestEqual(TEXT("Failure payload FromRevision."), Payload.FromRevision, 1);
	TestEqual(TEXT("Failure payload ToRevision."), Payload.ToRevision, 5);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBuildDeltaPayloadFailsWhenFromRevisionEqualsToRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.BuildDeltaPayloadFailsWhenFromRevisionEqualsToRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBuildDeltaPayloadFailsWhenFromRevisionEqualsToRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Delta no-op test manager spawn failed."));
		return false;
	}

	const FIntVector TestChunkCoord(11, 0, 0);
	FRTPSVoxelChunkState& ChunkState = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	ChunkState.bHasDensity = true;
	ChunkState.LatticeDensity.Init(0.5f, 8);
	ChunkState.Revision = 3;
	ChunkState.SnapshotRevision = 0;
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(0, 1, MakeVoxelAutomationEditOp(1, FVector::ZeroVector)));
	const int32 RecentOpsBefore = ChunkState.RecentOps.Num();
	const int32 RevisionBefore = ChunkState.Revision;

	FRTPSVoxelChunkDeltaPayload Payload;
	const bool bBuilt = ChunkManager->BuildChunkDeltaPayload(TestChunkCoord, 3, Payload);
	TestFalse(TEXT("Delta payload is not built for an up-to-date client."), bBuilt);
	TestFalse(TEXT("No-op delta payload does not report success."), Payload.bSuccess);
	TestFalse(TEXT("No-op delta payload does not require full snapshot."), Payload.bRequiresFullSnapshot);
	TestEqual(TEXT("No-op delta payload contains no EditOps."), Payload.EditOps.Num(), 0);
	TestEqual(TEXT("No-op delta build does not mutate Revision."), ChunkState.Revision, RevisionBefore);
	TestEqual(TEXT("No-op delta build does not mutate RecentOps."), ChunkState.RecentOps.Num(), RecentOpsBefore);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBuildDeltaPayloadFailsOnRecentOpsGapTest,
	"RTPS.VoxelAuthoring.Chunk.BuildDeltaPayloadFailsOnRecentOpsGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBuildDeltaPayloadFailsOnRecentOpsGapTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Delta gap test manager spawn failed."));
		return false;
	}

	const FIntVector TestChunkCoord(12, 0, 0);
	FRTPSVoxelChunkState& ChunkState = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	ChunkState.bHasDensity = true;
	ChunkState.LatticeDensity.Init(0.5f, 8);
	ChunkState.Revision = 3;
	ChunkState.SnapshotRevision = 0;
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(0, 1, MakeVoxelAutomationEditOp(10, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(2, 3, MakeVoxelAutomationEditOp(12, FVector::ZeroVector)));

	FString CoverageFailureReason;
	TestFalse(
		TEXT("RecentOps coverage helper rejects missing 1->2 revision."),
		ChunkManager->CanBuildCompleteDeltaFromRecentOps(ChunkState, 0, 3, CoverageFailureReason));
	TestTrue(TEXT("Coverage failure reason mentions gap."), CoverageFailureReason.Contains(TEXT("gap")));

	FRTPSVoxelChunkDeltaPayload Payload;
	const bool bBuilt = ChunkManager->BuildChunkDeltaPayload(TestChunkCoord, 0, Payload);
	TestFalse(TEXT("Delta payload is not built with a RecentOps gap."), bBuilt);
	TestFalse(TEXT("Gap payload does not report success."), Payload.bSuccess);
	TestTrue(TEXT("Gap payload requires full snapshot fallback."), Payload.bRequiresFullSnapshot);
	TestEqual(TEXT("Gap payload does not return partial EditOps."), Payload.EditOps.Num(), 0);
	TestTrue(TEXT("Gap failure reason mentions coverage gap."), Payload.FailureReason.Contains(TEXT("gap")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelCanBuildCompleteDeltaRejectsFutureClientRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.CanBuildCompleteDeltaRejectsFutureClientRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelCanBuildCompleteDeltaRejectsFutureClientRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Delta future-revision test manager spawn failed."));
		return false;
	}

	const FIntVector TestChunkCoord(13, 0, 0);
	FRTPSVoxelChunkState& ChunkState = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	ChunkState.bHasDensity = true;
	ChunkState.LatticeDensity.Init(0.5f, 8);
	ChunkState.Revision = 2;
	ChunkState.SnapshotRevision = 0;
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(0, 1, MakeVoxelAutomationEditOp(20, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(1, 2, MakeVoxelAutomationEditOp(21, FVector::ZeroVector)));

	FRTPSVoxelChunkDeltaPayload Payload;
	const bool bBuilt = ChunkManager->BuildChunkDeltaPayload(TestChunkCoord, 5, Payload);
	TestFalse(TEXT("Delta payload is not built for a future client revision."), bBuilt);
	TestFalse(TEXT("Future revision payload does not report success."), Payload.bSuccess);
	TestTrue(TEXT("Future revision requires full snapshot fallback."), Payload.bRequiresFullSnapshot);
	TestTrue(TEXT("Failure reason mentions future revision."), Payload.FailureReason.Contains(TEXT("future")));
	TestEqual(TEXT("Future revision payload contains no EditOps."), Payload.EditOps.Num(), 0);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeSkipsWhenClientUpToDateTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeSkipsWhenClientUpToDate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeSkipsWhenClientUpToDateTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync up-to-date test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(21, 0, 0), 5, 3);

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 5, Reason);
	TestTrue(TEXT("Up-to-date client sync mode is Skip."), Mode == ERTPSVoxelChunkSyncMode::Skip);
	TestTrue(TEXT("Up-to-date reason mentions up to date."), Reason.Contains(TEXT("up to date")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeFullWhenClientOlderThanSnapshotTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeFullWhenClientOlderThanSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeFullWhenClientOlderThanSnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync old-snapshot test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(22, 0, 0), 10, 6);

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 3, Reason);
	TestTrue(TEXT("Older-than-snapshot client sync mode is FullSnapshot."), Mode == ERTPSVoxelChunkSyncMode::FullSnapshot);
	TestTrue(TEXT("Older-than-snapshot reason mentions older than snapshot."), Reason.Contains(TEXT("older than snapshot")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeDeltaWhenClientWithinRecentWindowTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeDeltaWhenClientWithinRecentWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeDeltaWhenClientWithinRecentWindowTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync delta-window test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(23, 0, 0), 6, 3);
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(3, 4, MakeVoxelAutomationEditOp(34, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(4, 5, MakeVoxelAutomationEditOp(45, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(5, 6, MakeVoxelAutomationEditOp(56, FVector::ZeroVector)));

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 3, Reason);
	TestTrue(TEXT("Client within complete RecentOps window sync mode is Delta."), Mode == ERTPSVoxelChunkSyncMode::Delta);
	TestTrue(TEXT("Delta reason mentions complete coverage."), Reason.Contains(TEXT("complete coverage")) || Reason.Contains(TEXT("within RecentOps")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeFullWhenRecentOpsCoverageIncompleteTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeFullWhenRecentOpsCoverageIncomplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeFullWhenRecentOpsCoverageIncompleteTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync incomplete-coverage test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(24, 0, 0), 6, 3);
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(3, 4, MakeVoxelAutomationEditOp(64, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(5, 6, MakeVoxelAutomationEditOp(66, FVector::ZeroVector)));

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 3, Reason);
	TestTrue(TEXT("Incomplete RecentOps coverage sync mode is FullSnapshot."), Mode == ERTPSVoxelChunkSyncMode::FullSnapshot);
	TestTrue(TEXT("Incomplete coverage reason mentions coverage/gap."), Reason.Contains(TEXT("incomplete")) || Reason.Contains(TEXT("gap")) || Reason.Contains(TEXT("coverage")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeFullWhenClientReportsFutureRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeFullWhenClientReportsFutureRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeFullWhenClientReportsFutureRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync future-revision test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(25, 0, 0), 4, 2);

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 8, Reason);
	TestTrue(TEXT("Future client revision sync mode is FullSnapshot."), Mode == ERTPSVoxelChunkSyncMode::FullSnapshot);
	TestTrue(TEXT("Future revision reason mentions future/newer."), Reason.Contains(TEXT("future")) || Reason.Contains(TEXT("newer")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeSkipsWhenNoAuthoritativeDensityTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeSkipsWhenNoAuthoritativeDensity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeSkipsWhenNoAuthoritativeDensityTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync no-density test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(26, 0, 0), 4, 2);
	ChunkState.bHasDensity = false;

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 1, Reason);
	TestTrue(TEXT("No authoritative density sync mode is Skip."), Mode == ERTPSVoxelChunkSyncMode::Skip);
	TestTrue(TEXT("No-density reason mentions no authoritative density."), Reason.Contains(TEXT("no authoritative density")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChooseSyncModeUsesDeltaBuildCoverageHelperTest,
	"RTPS.VoxelAuthoring.Chunk.ChooseSyncModeUsesDeltaBuildCoverageHelper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChooseSyncModeUsesDeltaBuildCoverageHelperTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Choose sync coverage-helper test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState ChunkState;
	ConfigureAuthoritativeChunkStateForSyncMode(ChunkState, FIntVector(27, 0, 0), 6, 3);
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(3, 4, MakeVoxelAutomationEditOp(74, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(4, 5, MakeVoxelAutomationEditOp(75, FVector::ZeroVector)));
	ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(5, 6, MakeVoxelAutomationEditOp(76, FVector::ZeroVector)));
	ChunkState.RecentOps[1].RevisionBeforeApply = 99;

	FString Reason;
	const ERTPSVoxelChunkSyncMode Mode = ChunkManager->ChooseChunkSyncMode(ChunkState, 3, Reason);
	TestTrue(TEXT("Corrupt RecentOps coverage sync mode is FullSnapshot."), Mode == ERTPSVoxelChunkSyncMode::FullSnapshot);
	TestTrue(TEXT("Corrupt coverage reason mentions incomplete/gap/coverage."), Reason.Contains(TEXT("incomplete")) || Reason.Contains(TEXT("gap")) || Reason.Contains(TEXT("coverage")));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

bool FRTPSVoxelSnapshotCompactionUpdatesSnapshotRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Snapshot compaction revision test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	for (int64 Sequence = 1; Sequence <= 3; ++Sequence)
	{
		ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(Sequence, FVector(50.f, 50.f, 50.f)));
	}

	FRTPSVoxelChunkState* ChunkState = ChunkManager->ChunkStates.Find(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists before snapshot compaction."), ChunkState);
	if (ChunkState != nullptr)
	{
		const TArray<float> DensityBeforeCompaction = ChunkState->LatticeDensity;
		const bool bCompacted = ChunkManager->CompactChunkStateSnapshot(*ChunkState, TEXT("AutomationSnapshotCompactionRevision"));
		TestTrue(TEXT("Snapshot compaction succeeds for valid state."), bCompacted);
		TestEqual(TEXT("SnapshotRevision is updated to current Revision."), ChunkState->SnapshotRevision, ChunkState->Revision);
		TestTrue(TEXT("Snapshot compaction does not change lattice density."), ChunkState->LatticeDensity == DensityBeforeCompaction);
		TestEqual(TEXT("SnapshotServerSequence records latest applied sequence."), ChunkState->SnapshotServerSequence, static_cast<int64>(3));
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSnapshotCompactionDoesNotAdvanceRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.SnapshotCompactionDoesNotAdvanceRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSnapshotCompactionDoesNotAdvanceRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Snapshot compaction no-revision test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(31, FVector(50.f, 50.f, 50.f)));

	FRTPSVoxelChunkState* ChunkState = ChunkManager->ChunkStates.Find(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists before compaction no-revision assertion."), ChunkState);
	if (ChunkState != nullptr)
	{
		const int32 RevisionBeforeCompaction = ChunkState->Revision;
		ChunkManager->CompactChunkStateSnapshot(*ChunkState, TEXT("AutomationSnapshotDoesNotAdvanceRevision"));
		TestEqual(TEXT("Snapshot compaction does not advance Revision."), ChunkState->Revision, RevisionBeforeCompaction);
		TestEqual(TEXT("SnapshotRevision records the existing Revision."), ChunkState->SnapshotRevision, RevisionBeforeCompaction);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSnapshotCompactionTrimsRecentOpsTest,
	"RTPS.VoxelAuthoring.Chunk.SnapshotCompactionTrimsRecentOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSnapshotCompactionTrimsRecentOpsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Snapshot compaction trims test manager spawn failed."));
		return false;
	}

	FRTPSVoxelChunkState& ChunkState = ChunkManager->FindOrCreateChunkState(FIntVector(9, 0, 0));
	ChunkState.bHasDensity = true;
	ChunkState.LatticeDensity.Init(0.5f, 8);
	ChunkState.Revision = 5;
	for (int64 Sequence = 1; Sequence <= 5; ++Sequence)
	{
		const int32 RevisionBeforeApply = static_cast<int32>(Sequence - 1);
		const int32 RevisionAfterApply = static_cast<int32>(Sequence);
		ChunkState.RecentOps.Add(FRTPSVoxelRecentEditOp(
			RevisionBeforeApply,
			RevisionAfterApply,
			MakeVoxelAutomationEditOp(Sequence, FVector(0.f, 0.f, 0.f))));
	}

	const bool bCompacted = ChunkManager->CompactChunkStateSnapshot(ChunkState, TEXT("AutomationSnapshotTrimsRecentOps"));
	TestTrue(TEXT("Manual snapshot compaction succeeds."), bCompacted);
	TestEqual(TEXT("Clear-on-compaction policy removes all RecentOps."), ChunkState.RecentOps.Num(), 0);
	TestEqual(TEXT("SnapshotRevision matches current Revision after manual compaction."), ChunkState.SnapshotRevision, 5);
	TestEqual(TEXT("SnapshotServerSequence reflects latest compacted op."), ChunkState.SnapshotServerSequence, static_cast<int64>(5));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelRecentOpsTriggerSnapshotCompactionTest,
	"RTPS.VoxelAuthoring.Chunk.RecentOpsTriggerSnapshotCompaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelRecentOpsTriggerSnapshotCompactionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("RecentOps trigger compaction test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->MaxRecentOpsPerChunk = 3;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	for (int64 Sequence = 1; Sequence <= 3; ++Sequence)
	{
		ChunkManager->ApplyEditOpLocal(MakeVoxelAutomationEditOp(Sequence, FVector(50.f, 50.f, 50.f)));
	}

	const FRTPSVoxelChunkState* ChunkState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists after automatic snapshot compaction."), ChunkState);
	if (ChunkState != nullptr)
	{
		TestEqual(TEXT("Revision tracks all unique edits."), ChunkState->Revision, 3);
		TestEqual(TEXT("SnapshotRevision advances when RecentOps reaches max."), ChunkState->SnapshotRevision, 3);
		TestEqual(TEXT("SnapshotServerSequence records threshold edit sequence."), ChunkState->SnapshotServerSequence, static_cast<int64>(3));
		TestEqual(TEXT("RecentOps is cleared by automatic compaction."), ChunkState->RecentOps.Num(), 0);
		TestEqual(TEXT("Automatic compaction count increments."), ChunkState->TotalCompactionCount, 1);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBuildChunkStatePayloadFromMirrorTest,
	"RTPS.VoxelAuthoring.Chunk.BuildChunkStatePayloadFromMirror",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBuildChunkStatePayloadFromMirrorTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	const FIntVector TestChunkCoord(2, 0, 0);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Chunk state payload mirror test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->RefreshReadyChunkStateMirrors(TEXT("AutomationBuildPayloadMirror"));

	FRTPSVoxelChunkStatePayload Payload;
	const bool bBuilt = ChunkManager->BuildChunkStatePayload(TestChunkCoord, Payload);
	TestTrue(TEXT("BuildChunkStatePayload succeeds for mirrored chunk state."), bBuilt);
	TestTrue(TEXT("Payload reports success."), Payload.bSuccess);
	TestTrue(TEXT("Payload has density."), Payload.bHasDensity);
	TestEqual(TEXT("Payload chunk coord matches request."), Payload.ChunkCoord, TestChunkCoord);
	TestEqual(TEXT("Payload revision matches initial mirror revision."), Payload.Revision, 0);
	TestEqual(TEXT("Payload snapshot revision matches initial mirror revision."), Payload.SnapshotRevision, 0);
	TestEqual(TEXT("Payload snapshot sequence is unset before edits."), Payload.SnapshotServerSequence, static_cast<int64>(INDEX_NONE));
	TestEqual(TEXT("Payload density count matches chunk density count."), Payload.LatticeDensity.Num(), Chunk->GetLatticeDensity().Num());
	TestTrue(TEXT("Payload density matches mirrored chunk density."), Payload.LatticeDensity == Chunk->GetLatticeDensity());

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyChunkStatePayloadRebuildsLocalChunkTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyChunkStatePayloadRebuildsLocalChunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyChunkStatePayloadRebuildsLocalChunkTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Chunk state payload apply test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	FRTPSVoxelChunkStatePayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.Revision = 5;
	Payload.bSuccess = true;
	Payload.bHasDensity = true;
	Payload.LatticeDensity.Init(1.f, Chunk->GetExpectedLatticeSampleCount());

	const bool bApplied = ChunkManager->ApplyChunkStatePayloadLocal(Payload, TEXT("AutomationApplyPayload"));
	TestTrue(TEXT("ApplyChunkStatePayloadLocal succeeds for ready local chunk."), bApplied);
	TestTrue(TEXT("Payload density immediately replaces local density."), Chunk->GetLatticeDensity() == Payload.LatticeDensity);
	TestFalse(TEXT("Authoritative payload apply does not mark chunk locally modified."), Chunk->bModified);

	const double DeadlineSeconds = FPlatformTime::Seconds() + 5.0;
	while (Chunk->State != EVoxelChunkState::Ready && FPlatformTime::Seconds() < DeadlineSeconds)
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.01f);
	}
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

	TestTrue(TEXT("Authoritative payload rebuild returns chunk to Ready."), Chunk->State == EVoxelChunkState::Ready);
	TestFalse(TEXT("Authoritative payload rebuild keeps chunk clean."), Chunk->bModified);

	const FRTPSVoxelChunkState* LocalState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("Local ChunkState mirror exists after payload apply."), LocalState);
	if (LocalState != nullptr)
	{
		TestEqual(TEXT("Local ChunkState revision matches payload."), LocalState->Revision, Payload.Revision);
		TestFalse(TEXT("Local ChunkState is clean after remote payload apply."), LocalState->bDirty);
		TestTrue(TEXT("Local ChunkState density matches payload."), LocalState->LatticeDensity == Payload.LatticeDensity);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelIncomingPayloadQueuesUntilChunkReadyTest,
	"RTPS.VoxelAuthoring.Chunk.IncomingPayloadQueuesUntilChunkReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelIncomingPayloadQueuesUntilChunkReadyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!ChunkManager)
	{
		AddError(TEXT("AVoxelChunkManager spawn failed."));
		return false;
	}

	const FIntVector TestChunkCoord(3, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	FRTPSVoxelChunkStatePayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.Revision = 9;
	Payload.bSuccess = true;
	Payload.bHasDensity = true;
	Payload.LatticeDensity.Init(0.75f, 8);

	const bool bAppliedBeforeChunkReady = ChunkManager->ApplyChunkStatePayloadLocal(Payload, TEXT("AutomationQueuePayload"));
	TestFalse(TEXT("Payload is not applied before local chunk exists."), bAppliedBeforeChunkReady);
	TestTrue(TEXT("Payload is queued while local chunk is missing."), ChunkManager->PendingIncomingChunkPayloads.Contains(TestChunkCoord));

	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!Chunk)
	{
		AddError(TEXT("AVoxelChunk spawn failed."));
		EditorWorld->DestroyActor(ChunkManager, false, false);
		return false;
	}

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	const FRTPSVoxelChunkStatePayload QueuedPayload = ChunkManager->PendingIncomingChunkPayloads[TestChunkCoord];
	const bool bAppliedAfterChunkReady = ChunkManager->ApplyChunkStatePayloadLocal(QueuedPayload, TEXT("AutomationApplyQueuedPayload"));
	TestTrue(TEXT("Queued payload applies once local chunk is ready."), bAppliedAfterChunkReady);
	TestFalse(TEXT("Queued payload is removed after apply."), ChunkManager->PendingIncomingChunkPayloads.Contains(TestChunkCoord));
	TestTrue(TEXT("Queued payload density replaces local density."), Chunk->GetLatticeDensity() == Payload.LatticeDensity);

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelClientChunkDestroyClearsAppliedSequencesAndPendingPayloadsTest,
	"RTPS.VoxelAuthoring.Chunk.ClientChunkDestroyClearsAppliedSequencesAndPendingPayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelClientChunkDestroyClearsAppliedSequencesAndPendingPayloadsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		TestChunkCoord,
		FIntVector(1, 1, 1),
		100.f,
		0.5f);

	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Client chunk destroy cleanup test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	const FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(42, FVector(50.f, 50.f, 50.f));
	ChunkManager->AppliedEditSequencesByChunk.FindOrAdd(TestChunkCoord).Add(EditOp.ServerSequence);

	FRTPSVoxelChunkStatePayload PendingPayload;
	PendingPayload.ChunkCoord = TestChunkCoord;
	PendingPayload.Revision = 1;
	PendingPayload.bSuccess = true;
	PendingPayload.bHasDensity = true;
	PendingPayload.LatticeDensity = Chunk->GetLatticeDensity();
	ChunkManager->PendingIncomingChunkPayloads.Add(TestChunkCoord, PendingPayload);

	FRTPSVoxelChunkState& LocalState = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	LocalState.ChunkCoord = TestChunkCoord;
	LocalState.bHasDensity = true;
	LocalState.LatticeDensity = Chunk->GetLatticeDensity();
	LocalState.Revision = 1;
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 1);

	ChunkManager->DestroyChunk(TestChunkCoord);

	const TSet<int64>* AppliedSequences = ChunkManager->AppliedEditSequencesByChunk.Find(TestChunkCoord);
	TestTrue(
		TEXT("Client DestroyChunk clears applied edit sequence state for unloaded chunk."),
		AppliedSequences == nullptr || !AppliedSequences->Contains(EditOp.ServerSequence));
	TestFalse(TEXT("Client DestroyChunk clears pending incoming full payload."), ChunkManager->PendingIncomingChunkPayloads.Contains(TestChunkCoord));
	TestFalse(TEXT("Client DestroyChunk clears local ChunkState mirror."), ChunkManager->ChunkStates.Contains(TestChunkCoord));
	TestFalse(TEXT("Client DestroyChunk clears last applied remote revision."), ChunkManager->LastAppliedRemoteRevisionByCoord.Contains(TestChunkCoord));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeSkipsPayloadWhenRevisionUpToDateTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeSkipsPayloadWhenRevisionUpToDate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeSkipsPayloadWhenRevisionUpToDateTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	const FIntVector TestChunkCoord(4, 0, 0);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);

	if (!ChunkManager || !Controller || !Chunk)
	{
		AddError(TEXT("Subscribe up-to-date test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->RefreshReadyChunkStateMirrors(TEXT("AutomationSubscribeUpToDateMirror"));

	ChunkManager->SubscribePlayerToChunk(Controller, TestChunkCoord, 0, TEXT("AutomationSubscribeUpToDate"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	TestTrue(TEXT("Chunk subscriber was registered."), ChunkManager->ChunkSubscribers.Contains(TestChunkCoord));
	TestTrue(TEXT("Client subscription reverse map was registered."), ChunkManager->ClientSubscribedChunks.Contains(ControllerKey));
	TestFalse(TEXT("Up-to-date subscription does not queue a full payload."), ChunkManager->PendingChunkStatePayloadsByClient.Contains(ControllerKey));

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeQueuesPayloadWhenClientBehindTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeQueuesPayloadWhenClientBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeQueuesPayloadWhenClientBehindTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	const FIntVector TestChunkCoord(5, 0, 0);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);

	if (!ChunkManager || !Controller || !Chunk)
	{
		AddError(TEXT("Subscribe behind test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->RefreshReadyChunkStateMirrors(TEXT("AutomationSubscribeBehindMirror"));

	FRTPSVoxelChunkState* ChunkState = ChunkManager->ChunkStates.Find(TestChunkCoord);
	TestNotNull(TEXT("ChunkState exists before subscribe behind."), ChunkState);
	if (ChunkState != nullptr)
	{
		ChunkState->Revision = 3;
	}

	ChunkManager->SubscribePlayerToChunk(Controller, TestChunkCoord, 1, TEXT("AutomationSubscribeBehind"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* PendingPayloads = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Behind subscription queues payload."), PendingPayloads);
	if (PendingPayloads != nullptr)
	{
		TestEqual(TEXT("Behind subscription queues one payload."), PendingPayloads->Num(), 1);
		TestEqual(TEXT("Queued payload revision matches server revision."), (*PendingPayloads)[0].Revision, 3);
		TestEqual(TEXT("Queued payload chunk coord matches."), (*PendingPayloads)[0].ChunkCoord, TestChunkCoord);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelPayloadQueueIsThrottledTest,
	"RTPS.VoxelAuthoring.Chunk.PayloadQueueIsThrottled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelPayloadQueueIsThrottledTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Payload throttle test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->MaxChunkStatePayloadsPerClientPerTick = 1;

	FRTPSVoxelChunkStatePayload FirstPayload;
	FirstPayload.ChunkCoord = FIntVector(6, 0, 0);
	FirstPayload.Revision = 1;
	FirstPayload.bSuccess = true;
	FirstPayload.bHasDensity = true;
	FirstPayload.LatticeDensity.Init(0.25f, 8);

	FRTPSVoxelChunkStatePayload SecondPayload = FirstPayload;
	SecondPayload.ChunkCoord = FIntVector(7, 0, 0);
	SecondPayload.Revision = 2;

	ChunkManager->QueueChunkStatePayloadForClient(Controller, FirstPayload, TEXT("AutomationThrottleFirst"));
	ChunkManager->QueueChunkStatePayloadForClient(Controller, SecondPayload, TEXT("AutomationThrottleSecond"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* QueuedPayloads = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Payload queue exists before flush."), QueuedPayloads);
	if (QueuedPayloads != nullptr)
	{
		TestEqual(TEXT("Two payloads are queued before flush."), QueuedPayloads->Num(), 2);
	}

	const int32 SentPayloads = ChunkManager->FlushQueuedChunkStatePayloads(0.016f);
	TestEqual(TEXT("Flush sends one payload due to per-client throttle."), SentPayloads, 1);

	const TArray<FRTPSVoxelChunkStatePayload>* RemainingPayloads = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("One payload remains after throttled flush."), RemainingPayloads);
	if (RemainingPayloads != nullptr)
	{
		TestEqual(TEXT("One payload remains after throttled flush count."), RemainingPayloads->Num(), 1);
		TestEqual(TEXT("Second payload remains queued."), (*RemainingPayloads)[0].ChunkCoord, SecondPayload.ChunkCoord);
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelLiveEditTargetsOnlyChunkSubscribersTest,
	"RTPS.VoxelAuthoring.Chunk.LiveEditTargetsOnlyChunkSubscribers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelLiveEditTargetsOnlyChunkSubscribersTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* SubscribedController = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	ARTPSPlayerController* OtherController = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !SubscribedController || !OtherController)
	{
		AddError(TEXT("Live edit target test actor spawn failed."));
		if (OtherController) { EditorWorld->DestroyActor(OtherController, false, false); }
		if (SubscribedController) { EditorWorld->DestroyActor(SubscribedController, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector SubscribedChunkCoord = FIntVector::ZeroValue;
	const FIntVector OtherChunkCoord(20, 0, 0);
	ChunkManager->SubscribePlayerToChunk(SubscribedController, SubscribedChunkCoord, 0, TEXT("AutomationLiveEditSubscribed"));
	ChunkManager->SubscribePlayerToChunk(OtherController, OtherChunkCoord, 0, TEXT("AutomationLiveEditOther"));

	TArray<FIntVector> AffectedChunks;
	AffectedChunks.Add(SubscribedChunkCoord);
	const TArray<ARTPSPlayerController*> Targets = ChunkManager->GetSubscribersForAffectedChunks(AffectedChunks);

	TestEqual(TEXT("One subscriber target is collected."), Targets.Num(), 1);
	if (Targets.Num() == 1)
	{
		TestTrue(TEXT("Target is the subscribed controller."), Targets[0] == SubscribedController);
		TestFalse(TEXT("Unsubscribed chunk controller is not targeted."), Targets[0] == OtherController);
	}

	EditorWorld->DestroyActor(OtherController, false, false);
	EditorWorld->DestroyActor(SubscribedController, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelLiveEditTargetsDensityAffectedSubscribersOnlyTest,
	"RTPS.VoxelAuthoring.Chunk.LiveEditTargetsDensityAffectedSubscribersOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelLiveEditTargetsDensityAffectedSubscribersOnlyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* DensitySubscriber = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	ARTPSPlayerController* MeshDirtyOnlySubscriber = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !DensitySubscriber || !MeshDirtyOnlySubscriber)
	{
		AddError(TEXT("Density affected live target test actor spawn failed."));
		if (MeshDirtyOnlySubscriber) { EditorWorld->DestroyActor(MeshDirtyOnlySubscriber, false, false); }
		if (DensitySubscriber) { EditorWorld->DestroyActor(DensitySubscriber, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(16, 16, 16);
	ChunkManager->CellSize = 100.f;

	FRTPSVoxelEditOp BoundaryEditOp = MakeVoxelAutomationEditOp(501, FVector(1550.f, 800.f, 800.f));
	BoundaryEditOp.Brush.Radius = 25.f;
	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(BoundaryEditOp);
	const TArray<FIntVector> MeshDirtyCoords = ChunkManager->GetMeshDirtyChunkCoordsForEditOp(BoundaryEditOp);

	const FIntVector DensityAffectedChunkCoord(0, 0, 0);
	const FIntVector MeshDirtyOnlyChunkCoord(1, 0, 0);
	TestTrue(TEXT("Boundary edit density affects source chunk."), DensityAffectedCoords.Contains(DensityAffectedChunkCoord));
	TestFalse(TEXT("Boundary edit does not density-affect halo neighbor."), DensityAffectedCoords.Contains(MeshDirtyOnlyChunkCoord));
	TestTrue(TEXT("Boundary edit marks halo neighbor mesh dirty."), MeshDirtyCoords.Contains(MeshDirtyOnlyChunkCoord));

	ChunkManager->SubscribePlayerToChunk(DensitySubscriber, DensityAffectedChunkCoord, 0, TEXT("AutomationDensityAffectedLiveTarget"));
	ChunkManager->SubscribePlayerToChunk(MeshDirtyOnlySubscriber, MeshDirtyOnlyChunkCoord, 0, TEXT("AutomationMeshDirtyOnlyLiveTarget"));

	const TArray<ARTPSPlayerController*> Targets = ChunkManager->GetSubscribersForAffectedChunks(DensityAffectedCoords);
	TestTrue(TEXT("Density affected subscriber is targeted."), Targets.Contains(DensitySubscriber));
	TestFalse(TEXT("Mesh-dirty-only subscriber is not targeted."), Targets.Contains(MeshDirtyOnlySubscriber));
	TestEqual(TEXT("Only one live EditOp target is selected."), Targets.Num(), 1);

	EditorWorld->DestroyActor(MeshDirtyOnlySubscriber, false, false);
	EditorWorld->DestroyActor(DensitySubscriber, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelMeshDirtyOnlySubscriberDoesNotReceiveLiveEditOpTest,
	"RTPS.VoxelAuthoring.Chunk.MeshDirtyOnlySubscriberDoesNotReceiveLiveEditOp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelMeshDirtyOnlySubscriberDoesNotReceiveLiveEditOpTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* MeshDirtyOnlySubscriber = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !MeshDirtyOnlySubscriber)
	{
		AddError(TEXT("Mesh-dirty-only live target test actor spawn failed."));
		if (MeshDirtyOnlySubscriber) { EditorWorld->DestroyActor(MeshDirtyOnlySubscriber, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(16, 16, 16);
	ChunkManager->CellSize = 100.f;

	FRTPSVoxelEditOp BoundaryEditOp = MakeVoxelAutomationEditOp(502, FVector(1550.f, 800.f, 800.f));
	BoundaryEditOp.Brush.Radius = 25.f;
	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(BoundaryEditOp);
	const TArray<FIntVector> MeshDirtyCoords = ChunkManager->GetMeshDirtyChunkCoordsForEditOp(BoundaryEditOp);
	const FIntVector MeshDirtyOnlyChunkCoord(1, 0, 0);

	TestTrue(TEXT("Mesh-dirty-only chunk is present in halo set."), MeshDirtyCoords.Contains(MeshDirtyOnlyChunkCoord));
	TestFalse(TEXT("Mesh-dirty-only chunk is absent from density set."), DensityAffectedCoords.Contains(MeshDirtyOnlyChunkCoord));

	ChunkManager->SubscribePlayerToChunk(MeshDirtyOnlySubscriber, MeshDirtyOnlyChunkCoord, 0, TEXT("AutomationMeshDirtyOnlySubscriber"));

	const TArray<ARTPSPlayerController*> Targets = ChunkManager->GetSubscribersForAffectedChunks(DensityAffectedCoords);
	TestFalse(TEXT("Mesh-dirty-only subscriber is excluded from density live targets."), Targets.Contains(MeshDirtyOnlySubscriber));
	TestEqual(TEXT("No live EditOp targets are selected when only halo chunks are subscribed."), Targets.Num(), 0);

	EditorWorld->DestroyActor(MeshDirtyOnlySubscriber, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelLiveEditTargetSelectionDoesNotUseMeshDirtyHaloTest,
	"RTPS.VoxelAuthoring.Chunk.LiveEditTargetSelectionDoesNotUseMeshDirtyHalo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelLiveEditTargetSelectionDoesNotUseMeshDirtyHaloTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* MeshDirtyOnlySubscriber = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !MeshDirtyOnlySubscriber)
	{
		AddError(TEXT("Live target halo exclusion test actor spawn failed."));
		if (MeshDirtyOnlySubscriber) { EditorWorld->DestroyActor(MeshDirtyOnlySubscriber, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(16, 16, 16);
	ChunkManager->CellSize = 100.f;

	FRTPSVoxelEditOp BoundaryEditOp = MakeVoxelAutomationEditOp(503, FVector(1550.f, 800.f, 800.f));
	BoundaryEditOp.Brush.Radius = 25.f;
	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(BoundaryEditOp);
	const TArray<FIntVector> MeshDirtyCoords = ChunkManager->GetMeshDirtyChunkCoordsForEditOp(BoundaryEditOp);
	const FIntVector MeshDirtyOnlyChunkCoord(1, 0, 0);

	TestTrue(TEXT("Mesh dirty halo expands beyond density affected chunks."), MeshDirtyCoords.Num() > DensityAffectedCoords.Num());
	TestTrue(TEXT("Halo-only subscriber chunk is in mesh dirty coords."), MeshDirtyCoords.Contains(MeshDirtyOnlyChunkCoord));

	ChunkManager->SubscribePlayerToChunk(MeshDirtyOnlySubscriber, MeshDirtyOnlyChunkCoord, 0, TEXT("AutomationHaloOnlySubscriber"));

	const TArray<ARTPSPlayerController*> DensityTargets = ChunkManager->GetSubscribersForAffectedChunks(DensityAffectedCoords);
	const TArray<ARTPSPlayerController*> MeshDirtyTargets = ChunkManager->GetSubscribersForAffectedChunks(MeshDirtyCoords);
	TestFalse(TEXT("Density live target selection does not include halo-only subscriber."), DensityTargets.Contains(MeshDirtyOnlySubscriber));
	TestTrue(TEXT("Mesh dirty target selection would include halo-only subscriber."), MeshDirtyTargets.Contains(MeshDirtyOnlySubscriber));

	EditorWorld->DestroyActor(MeshDirtyOnlySubscriber, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelClientLiveEditAppliedChunksZeroDoesNotAdvanceRemoteRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.ClientLiveEditAppliedChunksZeroDoesNotAdvanceRemoteRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelClientLiveEditAppliedChunksZeroDoesNotAdvanceRemoteRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Client zero-apply live edit test actor spawn failed."));
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(16, 16, 16);
	ChunkManager->CellSize = 100.f;

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(504, FVector(800.f, 800.f, 800.f));
	EditOp.Brush.Radius = 25.f;
	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(EditOp);
	TestTrue(TEXT("Test edit has at least one density affected chunk."), DensityAffectedCoords.Num() > 0);

	const int32 AppliedChunks = ChunkManager->ApplyEditOpLocal(EditOp);
	TestEqual(TEXT("Live edit applies to zero local chunks when none are loaded."), AppliedChunks, 0);
	for (const FIntVector& ChunkCoord : DensityAffectedCoords)
	{
		TestFalse(TEXT("Zero-apply live edit does not advance LastAppliedRemoteRevision for missing chunks."), ChunkManager->LastAppliedRemoteRevisionByCoord.Contains(ChunkCoord));
	}

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBoundaryEditMarksNeighborChunkDirtyTest,
	"RTPS.VoxelAuthoring.Chunk.BoundaryEditMarksNeighborChunkDirty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBoundaryEditMarksNeighborChunkDirtyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("AVoxelChunkManager spawn failed."));
		return false;
	}

	ChunkManager->ChunkDimensions = FIntVector(16, 16, 16);
	ChunkManager->CellSize = 100.f;

	FRTPSVoxelEditOp BoundaryEditOp = MakeVoxelAutomationEditOp(301, FVector(1550.f, 800.f, 800.f));
	BoundaryEditOp.Brush.Radius = 25.f;
	const TArray<FIntVector> BoundaryDensityAffectedChunks = ChunkManager->GetAffectedChunkCoords(BoundaryEditOp);
	const TArray<FIntVector> BoundaryMeshDirtyChunks = ChunkManager->GetMeshDirtyChunkCoordsForEditOp(BoundaryEditOp);

	TestTrue(TEXT("Boundary density edit includes source chunk."), BoundaryDensityAffectedChunks.Contains(FIntVector(0, 0, 0)));
	TestFalse(TEXT("Boundary density edit does not directly include +X neighbor."), BoundaryDensityAffectedChunks.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Boundary mesh dirty chunks include source chunk."), BoundaryMeshDirtyChunks.Contains(FIntVector(0, 0, 0)));
	TestTrue(TEXT("Boundary mesh dirty chunks include +X neighbor through one-sample halo."), BoundaryMeshDirtyChunks.Contains(FIntVector(1, 0, 0)));

	FRTPSVoxelEditOp InteriorEditOp = MakeVoxelAutomationEditOp(302, FVector(800.f, 800.f, 800.f));
	InteriorEditOp.Brush.Radius = 25.f;
	const TArray<FIntVector> InteriorMeshDirtyChunks = ChunkManager->GetMeshDirtyChunkCoordsForEditOp(InteriorEditOp);
	TestTrue(TEXT("Interior mesh dirty chunks include source chunk."), InteriorMeshDirtyChunks.Contains(FIntVector(0, 0, 0)));
	TestFalse(TEXT("Interior mesh dirty chunks do not include unrelated +X chunk."), InteriorMeshDirtyChunks.Contains(FIntVector(1, 0, 0)));
	TestFalse(TEXT("Interior mesh dirty chunks do not include far chunk."), InteriorMeshDirtyChunks.Contains(FIntVector(2, 0, 0)));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelChunkBoundaryDebugInfoDetectsCornerNeighborsTest,
	"RTPS.VoxelAuthoring.ChunkManager.BoundaryDebugInfoDetectsCornerNeighbors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelChunkBoundaryDebugInfoDetectsCornerNeighborsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		100.f,
		0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Failed to spawn boundary debug automation actors."));
		return false;
	}

	ChunkManager->BoundaryDebugThresholdWorldUnits = 100.f;
	const FRTPSVoxelChunkBoundaryDebugInfo BoundaryInfo =
		ChunkManager->BuildChunkBoundaryDebugInfo(*Chunk, FVector(1550.f, 1550.f, 800.f));

	TestTrue(TEXT("Boundary debug detects +X near hit."), BoundaryInfo.BoundaryAxes.Contains(TEXT("+X")));
	TestTrue(TEXT("Boundary debug detects +Y near hit."), BoundaryInfo.BoundaryAxes.Contains(TEXT("+Y")));
	TestEqual(TEXT("Corner hit expects axis and diagonal neighbors."), BoundaryInfo.ExpectedNeighborChunkCoords.Num(), 3);
	TestTrue(TEXT("Corner hit expects +X neighbor."), BoundaryInfo.ExpectedNeighborChunkCoords.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Corner hit expects +Y neighbor."), BoundaryInfo.ExpectedNeighborChunkCoords.Contains(FIntVector(0, 1, 0)));
	TestTrue(TEXT("Corner hit expects +X/+Y diagonal neighbor."), BoundaryInfo.ExpectedNeighborChunkCoords.Contains(FIntVector(1, 1, 0)));

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelLargeBrushQueuesUnloadedAffectedNeighborTest,
	"RTPS.VoxelAuthoring.Chunk.LargeBrushQueuesUnloadedAffectedNeighbor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelLargeBrushQueuesUnloadedAffectedNeighborTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	const FIntVector ReadyChunkCoord = FIntVector::ZeroValue;
	const FIntVector NeighborChunkCoord(1, 0, 0);
	const FIntVector TestChunkDimensions(4, 4, 4);
	AVoxelChunk* ReadyChunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		ReadyChunkCoord,
		TestChunkDimensions,
		100.f,
		0.5f);

	if (!ChunkManager || !ReadyChunk)
	{
		AddError(TEXT("Large brush queue test actor spawn failed."));
		if (ReadyChunk) { EditorWorld->DestroyActor(ReadyChunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = TestChunkDimensions;
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(ReadyChunkCoord, ReadyChunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(401, FVector(390.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;

	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(EditOp);
	TestTrue(TEXT("Large brush density set includes ready source chunk."), DensityAffectedCoords.Contains(ReadyChunkCoord));
	TestTrue(TEXT("Large brush density set includes unloaded +X neighbor."), DensityAffectedCoords.Contains(NeighborChunkCoord));

	const TArray<float> ReadyDensityBefore = ReadyChunk->GetLatticeDensity();
	ChunkManager->ApplyEditOpAuthoritative(EditOp);

	TestTrue(TEXT("Ready density-affected chunk is applied immediately."), HasAnyDensityChanged(ReadyDensityBefore, ReadyChunk->GetLatticeDensity()));
	TestTrue(TEXT("Ready chunk records applied sequence."), ChunkManager->HasAppliedSequenceToChunk(ReadyChunkCoord, EditOp.ServerSequence));

	const TArray<FRTPSVoxelEditOp>* NeighborPendingOps = ChunkManager->PendingOpsByChunk.Find(NeighborChunkCoord);
	TestNotNull(TEXT("Unloaded density-affected neighbor has pending ops."), NeighborPendingOps);
	if (NeighborPendingOps != nullptr)
	{
		TestEqual(TEXT("Unloaded neighbor receives one pending op."), NeighborPendingOps->Num(), 1);
		TestTrue(TEXT("Pending op preserves server sequence."), PendingOpsContainSequence(NeighborPendingOps, EditOp.ServerSequence));
	}
	TestFalse(TEXT("Unloaded neighbor is not marked applied before it becomes ready."), ChunkManager->HasAppliedSequenceToChunk(NeighborChunkCoord, EditOp.ServerSequence));

	EditorWorld->DestroyActor(ReadyChunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelLargeBrushPendingNeighborReplaysWhenReadyTest,
	"RTPS.VoxelAuthoring.Chunk.LargeBrushPendingNeighborReplaysWhenReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelLargeBrushPendingNeighborReplaysWhenReadyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	const FIntVector ReadyChunkCoord = FIntVector::ZeroValue;
	const FIntVector NeighborChunkCoord(1, 0, 0);
	const FIntVector TestChunkDimensions(4, 4, 4);
	AVoxelChunk* ReadyChunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		ReadyChunkCoord,
		TestChunkDimensions,
		100.f,
		0.5f);

	if (!ChunkManager || !ReadyChunk)
	{
		AddError(TEXT("Large brush replay test initial actor spawn failed."));
		if (ReadyChunk) { EditorWorld->DestroyActor(ReadyChunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = TestChunkDimensions;
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(ReadyChunkCoord, ReadyChunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(402, FVector(390.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;
	ChunkManager->ApplyEditOpAuthoritative(EditOp);

	const TArray<FRTPSVoxelEditOp>* PendingBeforeNeighborLoad = ChunkManager->PendingOpsByChunk.Find(NeighborChunkCoord);
	TestTrue(TEXT("Neighbor has pending op before loading."), PendingOpsContainSequence(PendingBeforeNeighborLoad, EditOp.ServerSequence));

	AVoxelChunk* NeighborChunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		NeighborChunkCoord,
		TestChunkDimensions,
		100.f,
		0.5f);
	if (!NeighborChunk)
	{
		AddError(TEXT("Large brush replay neighbor chunk spawn failed."));
		EditorWorld->DestroyActor(ReadyChunk, false, false);
		EditorWorld->DestroyActor(ChunkManager, false, false);
		return false;
	}

	const TArray<float> NeighborDensityBeforeReplay = NeighborChunk->GetLatticeDensity();
	ChunkManager->LoadedChunks.Add(NeighborChunkCoord, NeighborChunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->RefreshReadyChunkStateMirrors(TEXT("AutomationLargeBrushPendingNeighborReplay"));

	const TArray<FRTPSVoxelEditOp>* PendingAfterReplay = ChunkManager->PendingOpsByChunk.Find(NeighborChunkCoord);
	TestTrue(TEXT("Neighbor pending op is removed after replay."), PendingAfterReplay == nullptr || PendingAfterReplay->Num() == 0);
	TestTrue(TEXT("Neighbor density changes after replay."), HasAnyDensityChanged(NeighborDensityBeforeReplay, NeighborChunk->GetLatticeDensity()));
	TestTrue(TEXT("Neighbor records applied sequence after replay."), ChunkManager->HasAppliedSequenceToChunk(NeighborChunkCoord, EditOp.ServerSequence));

	const FRTPSVoxelChunkState* NeighborStateAfterReplay = ChunkManager->FindChunkState(NeighborChunkCoord);
	TestNotNull(TEXT("Neighbor ChunkState exists after replay."), NeighborStateAfterReplay);
	if (NeighborStateAfterReplay != nullptr)
	{
		TestTrue(TEXT("Neighbor ChunkState is dirty after replay."), NeighborStateAfterReplay->bDirty);
		TestEqual(TEXT("Neighbor ChunkState revision increments once."), NeighborStateAfterReplay->Revision, 1);
		TestEqual(TEXT("Neighbor RecentOps records replayed op once."), NeighborStateAfterReplay->RecentOps.Num(), 1);
		if (NeighborStateAfterReplay->RecentOps.Num() == 1)
		{
			TestEqual(TEXT("Neighbor RecentOps stores replayed sequence."), NeighborStateAfterReplay->RecentOps[0].ServerSequence, EditOp.ServerSequence);
		}
	}

	const TArray<float> NeighborDensityAfterReplay = NeighborChunk->GetLatticeDensity();
	const int32 NeighborRevisionAfterReplay = NeighborStateAfterReplay != nullptr ? NeighborStateAfterReplay->Revision : INDEX_NONE;
	ChunkManager->ReplayPendingOpsForChunk(NeighborChunkCoord, *NeighborChunk, TEXT("AutomationLargeBrushDuplicateReplayNoPending"));

	const FRTPSVoxelChunkState* NeighborStateAfterDuplicateReplay = ChunkManager->FindChunkState(NeighborChunkCoord);
	TestTrue(TEXT("Second replay without pending data does not change density."), NeighborChunk->GetLatticeDensity() == NeighborDensityAfterReplay);
	if (NeighborStateAfterDuplicateReplay != nullptr)
	{
		TestEqual(TEXT("Second replay without pending data does not increment revision."), NeighborStateAfterDuplicateReplay->Revision, NeighborRevisionAfterReplay);
	}

	EditorWorld->DestroyActor(NeighborChunk, false, false);
	EditorWorld->DestroyActor(ReadyChunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelMeshDirtyNeighborDoesNotReplaceDensityAffectedPendingTest,
	"RTPS.VoxelAuthoring.Chunk.MeshDirtyNeighborDoesNotReplaceDensityAffectedPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelMeshDirtyNeighborDoesNotReplaceDensityAffectedPendingTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AVoxelChunk* ReadyChunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		FIntVector::ZeroValue,
		FIntVector(4, 4, 4),
		100.f,
		0.5f);

	if (!ChunkManager || !ReadyChunk)
	{
		AddError(TEXT("Mesh dirty versus density pending test actor spawn failed."));
		if (ReadyChunk) { EditorWorld->DestroyActor(ReadyChunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector ReadyChunkCoord = FIntVector::ZeroValue;
	const FIntVector NeighborChunkCoord(1, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(4, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(ReadyChunkCoord, ReadyChunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(403, FVector(390.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;

	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(EditOp);
	const TArray<FIntVector> MeshDirtyCoords = ChunkManager->GetMeshDirtyChunkCoordsForEditOp(EditOp);
	TestTrue(TEXT("Neighbor is density affected by overlapping large brush."), DensityAffectedCoords.Contains(NeighborChunkCoord));
	TestTrue(TEXT("Neighbor is also mesh dirty."), MeshDirtyCoords.Contains(NeighborChunkCoord));

	ChunkManager->ApplyEditOpAuthoritative(EditOp);

	const TArray<FRTPSVoxelEditOp>* NeighborPendingOps = ChunkManager->PendingOpsByChunk.Find(NeighborChunkCoord);
	TestTrue(TEXT("Unloaded density-affected neighbor is queued, not treated only as mesh dirty."), PendingOpsContainSequence(NeighborPendingOps, EditOp.ServerSequence));

	EditorWorld->DestroyActor(ReadyChunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelDensityAffectedAccountingIsCompleteTest,
	"RTPS.VoxelAuthoring.Chunk.DensityAffectedAccountingIsComplete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDensityAffectedAccountingIsCompleteTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	const FIntVector ReadyChunkCoord = FIntVector::ZeroValue;
	const FIntVector TestChunkDimensions(4, 4, 4);
	AVoxelChunk* ReadyChunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		ReadyChunkCoord,
		TestChunkDimensions,
		100.f,
		0.5f);

	if (!ChunkManager || !ReadyChunk)
	{
		AddError(TEXT("Density affected accounting test actor spawn failed."));
		if (ReadyChunk) { EditorWorld->DestroyActor(ReadyChunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->ChunkDimensions = TestChunkDimensions;
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(ReadyChunkCoord, ReadyChunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(404, FVector(390.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;

	const TArray<FIntVector> DensityAffectedCoords = ChunkManager->GetDensityAffectedChunkCoordsForEditOp(EditOp);
	TestEqual(TEXT("Large brush affects ready chunk and unloaded neighbor."), DensityAffectedCoords.Num(), 2);

	ChunkManager->ApplyEditOpAuthoritative(EditOp);

	int32 AppliedChunks = 0;
	int32 QueuedChunks = 0;
	int32 DuplicateChunks = 0;
	for (const FIntVector& ChunkCoord : DensityAffectedCoords)
	{
		const bool bApplied = ChunkManager->HasAppliedSequenceToChunk(ChunkCoord, EditOp.ServerSequence);
		const bool bQueued = PendingOpsContainSequence(ChunkManager->PendingOpsByChunk.Find(ChunkCoord), EditOp.ServerSequence);
		if (bApplied)
		{
			++AppliedChunks;
		}
		else if (bQueued)
		{
			++QueuedChunks;
		}
		else if (ChunkManager->IsPendingSequenceForChunk(ChunkCoord, EditOp.ServerSequence))
		{
			++DuplicateChunks;
		}
	}

	TestEqual(TEXT("One density-affected ready chunk was applied."), AppliedChunks, 1);
	TestEqual(TEXT("One density-affected unloaded chunk was queued."), QueuedChunks, 1);
	TestEqual(TEXT("No duplicates in first authoritative apply."), DuplicateChunks, 0);
	TestEqual(TEXT("Every density-affected chunk is accounted as applied, queued, or duplicate."), AppliedChunks + QueuedChunks + DuplicateChunks, DensityAffectedCoords.Num());

	EditorWorld->DestroyActor(ReadyChunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeMaterializesPendingOnlyChunkTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeMaterializesPendingOnlyChunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeMaterializesPendingOnlyChunkTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Pending-only materialize test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector PendingOnlyChunkCoord(1, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(4, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->GenerationSource = EVoxelChunkSource::ChunkData;

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(501, FVector(450.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;
	ChunkManager->PendingOpsByChunk.FindOrAdd(PendingOnlyChunkCoord).Add(EditOp);

	TestFalse(TEXT("Pending-only chunk starts without ChunkState."), ChunkManager->ChunkStates.Contains(PendingOnlyChunkCoord));
	// Phase 6-5: ClientKnownRevision=INDEX_NONE forces ChooseChunkSyncMode to FullSnapshot so this materialization-focused
	// test continues to validate the full-payload path. Delta routing is covered by other Phase 6-5 tests.
	ChunkManager->SubscribePlayerToChunk(Controller, PendingOnlyChunkCoord, INDEX_NONE, TEXT("AutomationPendingOnlyMaterialize"));

	const FRTPSVoxelChunkState* MaterializedState = ChunkManager->FindChunkState(PendingOnlyChunkCoord);
	TestNotNull(TEXT("Subscribe materializes missing ChunkState when pending ops exist."), MaterializedState);
	if (MaterializedState != nullptr)
	{
		TArray<float> ZeroDensity;
		ZeroDensity.SetNumZeroed(MaterializedState->LatticeDensity.Num());
		TestTrue(TEXT("Materialized ChunkState has density."), MaterializedState->bHasDensity);
		TestFalse(TEXT("Materialized ChunkState density is non-empty."), MaterializedState->LatticeDensity.IsEmpty());
		TestTrue(TEXT("Pending op changes materialized base density."), HasAnyDensityChanged(ZeroDensity, MaterializedState->LatticeDensity));
		TestTrue(TEXT("Materialized ChunkState is dirty after replayed pending op."), MaterializedState->bDirty);
		TestEqual(TEXT("Materialized ChunkState revision advances once."), MaterializedState->Revision, 1);
		TestEqual(TEXT("Materialized ChunkState records one RecentOp."), MaterializedState->RecentOps.Num(), 1);
	}

	const TArray<FRTPSVoxelEditOp>* PendingAfterSubscribe = ChunkManager->PendingOpsByChunk.Find(PendingOnlyChunkCoord);
	TestTrue(TEXT("Pending op is removed after successful materialization."), PendingAfterSubscribe == nullptr || PendingAfterSubscribe->IsEmpty());
	TestTrue(TEXT("Materialized pending op sequence is marked applied."), ChunkManager->HasAppliedSequenceToChunk(PendingOnlyChunkCoord, EditOp.ServerSequence));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	TestTrue(TEXT("Pending-only subscribe queues an initial payload."), ChunkManager->PendingChunkStatePayloadsByClient.Contains(ControllerKey));

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelPendingOnlySubscribeQueuesPayloadAfterMaterializeTest,
	"RTPS.VoxelAuthoring.Chunk.PendingOnlySubscribeQueuesPayloadAfterMaterialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelPendingOnlySubscribeQueuesPayloadAfterMaterializeTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Pending-only payload test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector PendingOnlyChunkCoord(2, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(4, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->GenerationSource = EVoxelChunkSource::ChunkData;

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(502, FVector(850.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;
	ChunkManager->PendingOpsByChunk.FindOrAdd(PendingOnlyChunkCoord).Add(EditOp);
	// Phase 6-5: ClientKnownRevision=INDEX_NONE forces ChooseChunkSyncMode to FullSnapshot for this payload-shape test.
	ChunkManager->SubscribePlayerToChunk(Controller, PendingOnlyChunkCoord, INDEX_NONE, TEXT("AutomationPendingOnlyPayload"));

	const FRTPSVoxelChunkState* MaterializedState = ChunkManager->FindChunkState(PendingOnlyChunkCoord);
	TestNotNull(TEXT("Materialized state exists before payload assertions."), MaterializedState);

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* PendingPayloads = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Pending-only materialization queues payload for subscriber."), PendingPayloads);
	if (PendingPayloads != nullptr && MaterializedState != nullptr)
	{
		TestEqual(TEXT("Exactly one materialized payload is queued."), PendingPayloads->Num(), 1);
		if (!PendingPayloads->IsEmpty())
		{
			const FRTPSVoxelChunkStatePayload& Payload = (*PendingPayloads)[0];
			TestEqual(TEXT("Payload chunk coord matches materialized chunk."), Payload.ChunkCoord, PendingOnlyChunkCoord);
			TestTrue(TEXT("Payload reports success."), Payload.bSuccess);
			TestTrue(TEXT("Payload reports density."), Payload.bHasDensity);
			TestEqual(TEXT("Payload revision matches materialized state revision."), Payload.Revision, MaterializedState->Revision);
			TestFalse(TEXT("Payload contains full lattice density."), Payload.LatticeDensity.IsEmpty());
			TestTrue(TEXT("Payload density matches materialized state density."), Payload.LatticeDensity == MaterializedState->LatticeDensity);
		}
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelMaterializedPendingChunkDoesNotReplayTwiceTest,
	"RTPS.VoxelAuthoring.Chunk.MaterializedPendingChunkDoesNotReplayTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelMaterializedPendingChunkDoesNotReplayTwiceTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Materialized duplicate replay test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector PendingOnlyChunkCoord(3, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(4, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->GenerationSource = EVoxelChunkSource::ChunkData;

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(503, FVector(1250.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;
	ChunkManager->PendingOpsByChunk.FindOrAdd(PendingOnlyChunkCoord).Add(EditOp);
	ChunkManager->SubscribePlayerToChunk(Controller, PendingOnlyChunkCoord, 0, TEXT("AutomationMaterializedNoDoubleReplay"));

	const FRTPSVoxelChunkState* MaterializedState = ChunkManager->FindChunkState(PendingOnlyChunkCoord);
	TestNotNull(TEXT("Materialized state exists before duplicate replay."), MaterializedState);
	if (MaterializedState == nullptr)
	{
		EditorWorld->DestroyActor(Controller, false, false);
		EditorWorld->DestroyActor(ChunkManager, false, false);
		return false;
	}

	const TArray<float> MaterializedDensity = MaterializedState->LatticeDensity;
	const int32 MaterializedRevision = MaterializedState->Revision;
	const int32 MaterializedRecentOpsCount = MaterializedState->RecentOps.Num();

	AVoxelChunk* LaterReadyChunk = SpawnReadyVoxelChunkForAutomation(
		EditorWorld,
		SpawnParams,
		PendingOnlyChunkCoord,
		ChunkManager->ChunkDimensions,
		ChunkManager->CellSize,
		ChunkManager->IsoLevel);
	if (!LaterReadyChunk)
	{
		AddError(TEXT("Later ready chunk spawn failed."));
		EditorWorld->DestroyActor(Controller, false, false);
		EditorWorld->DestroyActor(ChunkManager, false, false);
		return false;
	}

	ChunkManager->PendingOpsByChunk.FindOrAdd(PendingOnlyChunkCoord).Add(EditOp);
	ChunkManager->ReplayPendingOpsForChunk(PendingOnlyChunkCoord, *LaterReadyChunk, TEXT("AutomationMaterializedDuplicateReplay"));

	const FRTPSVoxelChunkState* StateAfterDuplicateReplay = ChunkManager->FindChunkState(PendingOnlyChunkCoord);
	TestNotNull(TEXT("ChunkState still exists after duplicate replay attempt."), StateAfterDuplicateReplay);
	if (StateAfterDuplicateReplay != nullptr)
	{
		TestEqual(TEXT("Duplicate replay does not advance revision."), StateAfterDuplicateReplay->Revision, MaterializedRevision);
		TestEqual(TEXT("Duplicate replay does not append RecentOps."), StateAfterDuplicateReplay->RecentOps.Num(), MaterializedRecentOpsCount);
		TestTrue(TEXT("Duplicate replay does not apply density twice."), StateAfterDuplicateReplay->LatticeDensity == MaterializedDensity);
	}

	const TArray<FRTPSVoxelEditOp>* PendingAfterDuplicateReplay = ChunkManager->PendingOpsByChunk.Find(PendingOnlyChunkCoord);
	TestTrue(TEXT("Duplicate pending op is cleared by replay path."), PendingAfterDuplicateReplay == nullptr || PendingAfterDuplicateReplay->IsEmpty());

	EditorWorld->DestroyActor(LaterReadyChunk, false, false);
	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeSkipsUnchangedBaseChunkWithoutMaterializeTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeSkipsUnchangedBaseChunkWithoutMaterialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeSkipsUnchangedBaseChunkWithoutMaterializeTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Unchanged base subscribe test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector BaseOnlyChunkCoord(4, 0, 0);
	ChunkManager->SubscribePlayerToChunk(Controller, BaseOnlyChunkCoord, 0, TEXT("AutomationBaseOnlySubscribe"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	TestTrue(TEXT("Base-only subscribe still registers subscriber."), ChunkManager->ChunkSubscribers.Contains(BaseOnlyChunkCoord));
	TestTrue(TEXT("Base-only subscribe registers reverse subscription."), ChunkManager->ClientSubscribedChunks.Contains(ControllerKey));
	TestFalse(TEXT("Base-only subscribe does not materialize unchanged state."), ChunkManager->ChunkStates.Contains(BaseOnlyChunkCoord));
	TestFalse(TEXT("Base-only subscribe does not queue payload."), ChunkManager->PendingChunkStatePayloadsByClient.Contains(ControllerKey));

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelMaterializationFailurePreservesPendingOpsTest,
	"RTPS.VoxelAuthoring.Chunk.MaterializationFailurePreservesPendingOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelMaterializationFailurePreservesPendingOpsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Materialization failure test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector PendingOnlyChunkCoord(5, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(-1, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->GenerationSource = EVoxelChunkSource::ChunkData;

	FRTPSVoxelEditOp EditOp = MakeVoxelAutomationEditOp(504, FVector(0.f, 200.f, 200.f));
	EditOp.Brush.Radius = 125.f;
	ChunkManager->PendingOpsByChunk.FindOrAdd(PendingOnlyChunkCoord).Add(EditOp);
	ChunkManager->SubscribePlayerToChunk(Controller, PendingOnlyChunkCoord, 0, TEXT("AutomationMaterializationFailure"));

	TestFalse(TEXT("Failed materialization does not create ChunkState."), ChunkManager->ChunkStates.Contains(PendingOnlyChunkCoord));
	TestTrue(TEXT("Failed materialization preserves pending op."), PendingOpsContainSequence(ChunkManager->PendingOpsByChunk.Find(PendingOnlyChunkCoord), EditOp.ServerSequence));
	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	TestFalse(TEXT("Failed materialization does not queue success payload."), ChunkManager->PendingChunkStatePayloadsByClient.Contains(ControllerKey));

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelPendingMaterializationCanCompactSafelyTest,
	"RTPS.VoxelAuthoring.Chunk.PendingMaterializationCanCompactSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelPendingMaterializationCanCompactSafelyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Pending materialization compaction test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector PendingOnlyChunkCoord(6, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(4, 4, 4);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->GenerationSource = EVoxelChunkSource::ChunkData;
	ChunkManager->MaxRecentOpsPerChunk = 3;

	for (int64 Sequence = 601; Sequence <= 603; ++Sequence)
	{
		FRTPSVoxelEditOp PendingOp = MakeVoxelAutomationEditOp(Sequence, FVector(2450.f, 200.f, 200.f));
		PendingOp.Brush.Radius = 125.f;
		ChunkManager->PendingOpsByChunk.FindOrAdd(PendingOnlyChunkCoord).Add(PendingOp);
	}

	ChunkManager->SubscribePlayerToChunk(Controller, PendingOnlyChunkCoord, 0, TEXT("AutomationPendingMaterializationCompaction"));

	const FRTPSVoxelChunkState* MaterializedState = ChunkManager->FindChunkState(PendingOnlyChunkCoord);
	TestNotNull(TEXT("Pending-only materialization creates ChunkState."), MaterializedState);
	if (MaterializedState != nullptr)
	{
		TestTrue(TEXT("Materialized ChunkState has density."), MaterializedState->bHasDensity);
		TestFalse(TEXT("Materialized density is non-empty."), MaterializedState->LatticeDensity.IsEmpty());
		TestEqual(TEXT("Materialized Revision advances once per unique pending op."), MaterializedState->Revision, 3);
		TestEqual(TEXT("Pending materialization compacts at threshold."), MaterializedState->SnapshotRevision, 3);
		TestEqual(TEXT("Pending materialization snapshot sequence records latest pending op."), MaterializedState->SnapshotServerSequence, static_cast<int64>(603));
		TestEqual(TEXT("Pending materialization clears RecentOps after compaction."), MaterializedState->RecentOps.Num(), 0);
		TestEqual(TEXT("Pending materialization records one compaction."), MaterializedState->TotalCompactionCount, 1);
	}

	const TArray<FRTPSVoxelEditOp>* PendingAfterMaterialize = ChunkManager->PendingOpsByChunk.Find(PendingOnlyChunkCoord);
	TestTrue(TEXT("Pending ops are cleared after compacted materialization."), PendingAfterMaterialize == nullptr || PendingAfterMaterialize->IsEmpty());
	TestTrue(TEXT("Latest pending sequence is protected from double replay."), ChunkManager->HasAppliedSequenceToChunk(PendingOnlyChunkCoord, 603));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* PendingPayloads = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Compacted materialized chunk still queues payload."), PendingPayloads);
	if (PendingPayloads != nullptr && !PendingPayloads->IsEmpty())
	{
		const FRTPSVoxelChunkStatePayload& Payload = (*PendingPayloads)[0];
		TestTrue(TEXT("Compacted materialization payload succeeds."), Payload.bSuccess);
		TestTrue(TEXT("Compacted materialization payload has density."), Payload.bHasDensity);
		TestEqual(TEXT("Compacted materialization payload revision matches state."), Payload.Revision, 3);
		TestEqual(TEXT("Compacted materialization payload snapshot revision matches state."), Payload.SnapshotRevision, 3);
		TestEqual(TEXT("Compacted materialization payload snapshot sequence matches state."), Payload.SnapshotServerSequence, static_cast<int64>(603));
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelBrushServerValidationTest,
	"RTPS.VoxelAuthoring.Brush.ServerValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelBrushServerValidationTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARTPSPlayerController* Controller = EditorWorld->SpawnActor<ARTPSPlayerController>(
		ARTPSPlayerController::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!Controller)
	{
		AddError(TEXT("ARTPSPlayerController spawn failed."));
		return false;
	}

	FVoxelBrush ValidBrush;
	ValidBrush.WorldPosition = FVector(100.f, 0.f, 0.f);
	ValidBrush.Radius = 200.f;
	ValidBrush.Strength = 0.1f;
	ValidBrush.Mode = EVoxelBrushMode::Add;
	ValidBrush.Shape = EVoxelBrushShape::Sphere;

	FString Reason;
	TestFalse(TEXT("Voxel brush validation rejects a controller with no pawn."), Controller->ValidateVoxelBrushRequest(ValidBrush, Reason));
	TestFalse(TEXT("Missing pawn rejection explains the reason."), Reason.IsEmpty());

	APawn* Pawn = EditorWorld->SpawnActor<APawn>(
		APawn::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!Pawn)
	{
		AddError(TEXT("APawn spawn failed."));
		EditorWorld->DestroyActor(Controller, false, false);
		return false;
	}

	Controller->Possess(Pawn);
	if (Controller->GetPawn() != Pawn)
	{
		AddError(TEXT("ARTPSPlayerController failed to possess test pawn."));
		EditorWorld->DestroyActor(Pawn, false, false);
		EditorWorld->DestroyActor(Controller, false, false);
		return false;
	}

	Reason.Reset();
	TestTrue(TEXT("Valid nearby brush passes validation."), Controller->ValidateVoxelBrushRequest(ValidBrush, Reason));
	TestTrue(TEXT("Valid brush leaves the rejection reason empty."), Reason.IsEmpty());

	auto TestRejectedBrush = [this, Controller](const TCHAR* Label, const FVoxelBrush& Brush)
	{
		FString LocalReason;
		const bool bAccepted = Controller->ValidateVoxelBrushRequest(Brush, LocalReason);
		TestFalse(Label, bAccepted);
		TestFalse(FString::Printf(TEXT("%s reason"), Label), LocalReason.IsEmpty());
	};

	FVoxelBrush FarBrush = ValidBrush;
	FarBrush.WorldPosition = FVector(10000.f, 0.f, 0.f);
	TestRejectedBrush(TEXT("Far brush fails validation."), FarBrush);

	FVoxelBrush ZeroRadiusBrush = ValidBrush;
	ZeroRadiusBrush.Radius = 0.f;
	TestRejectedBrush(TEXT("Zero-radius brush fails validation."), ZeroRadiusBrush);

	FVoxelBrush ExcessiveRadiusBrush = ValidBrush;
	ExcessiveRadiusBrush.Radius = 10000.f;
	TestRejectedBrush(TEXT("Excessive-radius brush fails validation."), ExcessiveRadiusBrush);

	FVoxelBrush InfiniteStrengthBrush = ValidBrush;
	InfiniteStrengthBrush.Strength = std::numeric_limits<float>::infinity();
	TestRejectedBrush(TEXT("Infinite-strength brush fails validation."), InfiniteStrengthBrush);

	FVoxelBrush NaNPositionBrush = ValidBrush;
	NaNPositionBrush.WorldPosition.X = std::numeric_limits<float>::quiet_NaN();
	TestRejectedBrush(TEXT("NaN-position brush fails validation."), NaNPositionBrush);

	FVoxelBrush InvalidSurfaceBlobRemoveBrush = ValidBrush;
	InvalidSurfaceBlobRemoveBrush.Shape = EVoxelBrushShape::SurfaceBlob;
	InvalidSurfaceBlobRemoveBrush.Mode = EVoxelBrushMode::Remove;
	TestRejectedBrush(TEXT("SurfaceBlob remove brush fails validation."), InvalidSurfaceBlobRemoveBrush);

	FVoxelBrush ZeroDepthSurfaceBlobBrush = ValidBrush;
	ZeroDepthSurfaceBlobBrush.Shape = EVoxelBrushShape::SurfaceBlob;
	ZeroDepthSurfaceBlobBrush.SurfaceDepth = 0.f;
	TestRejectedBrush(TEXT("Zero-depth SurfaceBlob brush fails validation."), ZeroDepthSurfaceBlobBrush);

	FVoxelBrush ZeroNormalSurfaceBlobBrush = ValidBrush;
	ZeroNormalSurfaceBlobBrush.Shape = EVoxelBrushShape::SurfaceBlob;
	ZeroNormalSurfaceBlobBrush.SurfaceNormal = FVector::ZeroVector;
	TestRejectedBrush(TEXT("Zero-normal SurfaceBlob brush fails validation."), ZeroNormalSurfaceBlobBrush);

	FVoxelBrush InvalidTerrainMudBlobRemoveBrush = ValidBrush;
	InvalidTerrainMudBlobRemoveBrush.Shape = EVoxelBrushShape::TerrainMudBlob;
	InvalidTerrainMudBlobRemoveBrush.Mode = EVoxelBrushMode::Remove;
	TestRejectedBrush(TEXT("TerrainMudBlob remove brush fails validation."), InvalidTerrainMudBlobRemoveBrush);

	FVoxelBrush InvalidTerrainMudBlobAlphaBrush = ValidBrush;
	InvalidTerrainMudBlobAlphaBrush.Shape = EVoxelBrushShape::TerrainMudBlob;
	InvalidTerrainMudBlobAlphaBrush.BlendMode = EVoxelBrushBlendMode::TargetLerp;
	InvalidTerrainMudBlobAlphaBrush.ConvergenceAlpha = 2.f;
	TestRejectedBrush(TEXT("TerrainMudBlob convergence alpha above one fails validation."), InvalidTerrainMudBlobAlphaBrush);

	FVoxelBrush InvalidSphereAlphaBrush = ValidBrush;
	InvalidSphereAlphaBrush.Shape = EVoxelBrushShape::Sphere;
	InvalidSphereAlphaBrush.BlendMode = EVoxelBrushBlendMode::TargetLerp;
	InvalidSphereAlphaBrush.ConvergenceAlpha = -0.1f;
	TestRejectedBrush(TEXT("Sphere TargetLerp convergence alpha below zero fails validation."), InvalidSphereAlphaBrush);

	FVoxelBrush InvalidFalloffBrush = ValidBrush;
	InvalidFalloffBrush.Falloff = static_cast<EVoxelBrushFalloff>(255);
	TestRejectedBrush(TEXT("Invalid brush falloff enum fails validation."), InvalidFalloffBrush);

	FVoxelBrush InvalidBlendModeBrush = ValidBrush;
	InvalidBlendModeBrush.BlendMode = static_cast<EVoxelBrushBlendMode>(255);
	TestRejectedBrush(TEXT("Invalid brush blend mode enum fails validation."), InvalidBlendModeBrush);

	FVoxelBrush InvalidTerrainMudBlobEmbedBrush = ValidBrush;
	InvalidTerrainMudBlobEmbedBrush.Shape = EVoxelBrushShape::TerrainMudBlob;
	InvalidTerrainMudBlobEmbedBrush.EmbedDepth = -1.f;
	TestRejectedBrush(TEXT("TerrainMudBlob negative embed depth fails validation."), InvalidTerrainMudBlobEmbedBrush);

	FVoxelBrush InvalidTerrainMudBlobRoundnessBrush = ValidBrush;
	InvalidTerrainMudBlobRoundnessBrush.Shape = EVoxelBrushShape::TerrainMudBlob;
	InvalidTerrainMudBlobRoundnessBrush.ClumpRoundnessPower = 0.5f;
	TestRejectedBrush(TEXT("TerrainMudBlob roundness power below one fails validation."), InvalidTerrainMudBlobRoundnessBrush);

	EditorWorld->DestroyActor(Pawn, false, false);
	EditorWorld->DestroyActor(Controller, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSurfaceBlobDensityProfileTest,
	"RTPS.VoxelAuthoring.Brush.SurfaceBlobDensityProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSurfaceBlobDensityProfileTest::RunTest(const FString& Parameters)
{
	const FIntVector ChunkDimensions(4, 4, 4);
	const int32 LatticeSampleCount =
		(ChunkDimensions.X + 1) *
		(ChunkDimensions.Y + 1) *
		(ChunkDimensions.Z + 1);
	TArray<float> SurfaceBlobDensity;
	SurfaceBlobDensity.Init(0.f, LatticeSampleCount);

	FVoxelBrush SurfaceBlobBrush;
	SurfaceBlobBrush.WorldPosition = FVector(200.f, 200.f, 100.f);
	SurfaceBlobBrush.Radius = 200.f;
	SurfaceBlobBrush.Strength = 0.5f;
	SurfaceBlobBrush.Mode = EVoxelBrushMode::Add;
	SurfaceBlobBrush.Shape = EVoxelBrushShape::SurfaceBlob;
	SurfaceBlobBrush.SurfaceNormal = FVector::UpVector;
	SurfaceBlobBrush.SurfaceDepth = 200.f;

	const bool bSurfaceBlobChanged = AVoxelChunk::ApplyBrushToLatticeDensity(
		SurfaceBlobDensity,
		FIntVector::ZeroValue,
		ChunkDimensions,
		100.f,
		SurfaceBlobBrush);
	TestTrue(TEXT("SurfaceBlob brush mutates density."), bSurfaceBlobChanged);

	const int32 SizeX = ChunkDimensions.X + 1;
	const int32 SizeY = ChunkDimensions.Y + 1;
	const auto SampleAt = [&SurfaceBlobDensity, SizeX, SizeY](int32 X, int32 Y, int32 Z)
	{
		return SurfaceBlobDensity[X + SizeX * (Y + SizeY * Z)];
	};

	const float CenterDensity = SampleAt(2, 2, 1);
	const float BehindSurfaceDensity = SampleAt(2, 2, 0);
	const float RadialFalloffDensity = SampleAt(3, 2, 1);
	const float HeightFalloffDensity = SampleAt(2, 2, 2);
	const float OutsideRadiusDensity = SampleAt(4, 2, 1);

	TestTrue(TEXT("SurfaceBlob affects the impact point on the positive side."), CenterDensity > 0.f);
	TestEqual(TEXT("SurfaceBlob does not affect samples behind the surface normal."), BehindSurfaceDensity, 0.f);
	TestTrue(TEXT("SurfaceBlob radial falloff weakens density away from the impact point."), RadialFalloffDensity > 0.f && RadialFalloffDensity < CenterDensity);
	TestTrue(TEXT("SurfaceBlob height falloff weakens density away from the surface."), HeightFalloffDensity > 0.f && HeightFalloffDensity < CenterDensity);
	TestEqual(TEXT("SurfaceBlob does not affect samples outside its surface radius."), OutsideRadiusDensity, 0.f);

	TArray<float> SphereDensity;
	SphereDensity.Init(0.f, LatticeSampleCount);
	FVoxelBrush SphereBrush = SurfaceBlobBrush;
	SphereBrush.Shape = EVoxelBrushShape::Sphere;
	const bool bSphereChanged = AVoxelChunk::ApplyBrushToLatticeDensity(
		SphereDensity,
		FIntVector::ZeroValue,
		ChunkDimensions,
		100.f,
		SphereBrush);
	TestTrue(TEXT("Existing sphere brush behavior remains active."), bSphereChanged);
	const float SphereBehindDensity = SphereDensity[2 + SizeX * (2 + SizeY * 0)];
	TestTrue(TEXT("Sphere brush still affects points behind the build-surface plane."), SphereBehindDensity > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelTerrainMudBlobDensityProfileTest,
	"RTPS.VoxelAuthoring.Brush.TerrainMudBlobDensityProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelTerrainMudBlobDensityProfileTest::RunTest(const FString& Parameters)
{
	const FIntVector ChunkDimensions(5, 5, 5);
	const int32 LatticeSampleCount =
		(ChunkDimensions.X + 1) *
		(ChunkDimensions.Y + 1) *
		(ChunkDimensions.Z + 1);
	TArray<float> MudDensity;
	MudDensity.Init(0.2f, LatticeSampleCount);

	FVoxelBrush MudBrush;
	MudBrush.WorldPosition = FVector(200.f, 200.f, 200.f);
	MudBrush.Radius = 150.f;
	MudBrush.Strength = 0.35f;
	MudBrush.Mode = EVoxelBrushMode::Add;
	MudBrush.Shape = EVoxelBrushShape::TerrainMudBlob;
	MudBrush.SurfaceNormal = FVector::UpVector;
	MudBrush.SurfaceDepth = 200.f;
	MudBrush.BackDepth = 120.f;
	MudBrush.EmbedDepth = 30.f;
	MudBrush.ClumpRoundnessPower = 2.5f;
	MudBrush.ConvergenceAlpha = 0.45f;
	MudBrush.BlendMode = EVoxelBrushBlendMode::TargetLerp;

	const bool bMudChanged = AVoxelChunk::ApplyBrushToLatticeDensity(
		MudDensity,
		FIntVector::ZeroValue,
		ChunkDimensions,
		100.f,
		MudBrush,
		0.5f);
	TestTrue(TEXT("TerrainMudBlob brush mutates density."), bMudChanged);

	const int32 SizeX = ChunkDimensions.X + 1;
	const int32 SizeY = ChunkDimensions.Y + 1;
	const auto SampleAt = [&MudDensity, SizeX, SizeY](int32 X, int32 Y, int32 Z)
	{
		return MudDensity[X + SizeX * (Y + SizeY * Z)];
	};

	const float EmbeddedCenterDensity = SampleAt(2, 2, 2);
	const float BehindWithinBackDepthDensity = SampleAt(2, 2, 1);
	const float FrontWithinDepthDensity = SampleAt(2, 2, 3);
	const float RadialFalloffDensity = SampleAt(3, 2, 2);
	const float OutsideRadiusDensity = SampleAt(4, 2, 2);
	const float BeyondDepthDensity = SampleAt(2, 2, 5);

	TestTrue(TEXT("TerrainMudBlob v2 affects the embedded clump center."), EmbeddedCenterDensity > 0.2f);
	TestTrue(TEXT("TerrainMudBlob affects points slightly behind the surface within BackDepth."), BehindWithinBackDepthDensity > 0.2f);
	TestTrue(TEXT("TerrainMudBlob affects points in front of the stable normal."), FrontWithinDepthDensity > 0.2f);
	TestTrue(TEXT("TerrainMudBlob v2 ellipsoid falloff weakens density away from the clump center."), RadialFalloffDensity > 0.2f && RadialFalloffDensity < EmbeddedCenterDensity);
	TestEqual(TEXT("TerrainMudBlob does not affect samples outside its radius."), OutsideRadiusDensity, 0.2f);
	TestEqual(TEXT("TerrainMudBlob does not affect samples beyond SurfaceDepth."), BeyondDepthDensity, 0.2f);

	TArray<float> RepeatedDensity;
	RepeatedDensity.Init(0.2f, LatticeSampleCount);
	AVoxelChunk::ApplyBrushToLatticeDensity(RepeatedDensity, FIntVector::ZeroValue, ChunkDimensions, 100.f, MudBrush, 0.5f);
	const float AfterOne = RepeatedDensity[2 + SizeX * (2 + SizeY * 2)];
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		AVoxelChunk::ApplyBrushToLatticeDensity(RepeatedDensity, FIntVector::ZeroValue, ChunkDimensions, 100.f, MudBrush, 0.5f);
	}
	const float AfterRepeated = RepeatedDensity[2 + SizeX * (2 + SizeY * 2)];
	TestTrue(TEXT("Repeated TerrainMudBlob application can still converge upward."), AfterRepeated > AfterOne);
	TestTrue(TEXT("Repeated TerrainMudBlob TargetLerp stays bounded by the iso plus strength envelope."), AfterRepeated <= 0.5f + MudBrush.Strength + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Repeated TerrainMudBlob application does not stack to full density."), AfterRepeated < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSphereFalloffTargetBlendTest,
	"RTPS.VoxelAuthoring.Brush.SphereFalloffTargetBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSphereFalloffTargetBlendTest::RunTest(const FString& Parameters)
{
	const FIntVector ChunkDimensions(4, 4, 4);
	const int32 SizeX = ChunkDimensions.X + 1;
	const int32 SizeY = ChunkDimensions.Y + 1;
	const int32 LatticeSampleCount = SizeX * SizeY * (ChunkDimensions.Z + 1);
	constexpr float CellSize = 100.f;
	constexpr float IsoLevel = 0.5f;

	auto ApplySphere = [&](EVoxelBrushFalloff Falloff, EVoxelBrushMode Mode, EVoxelBrushBlendMode BlendMode, float InitialDensity)
	{
		TArray<float> Density;
		Density.Init(InitialDensity, LatticeSampleCount);

		FVoxelBrush Brush;
		Brush.WorldPosition = FVector(200.f, 200.f, 200.f);
		Brush.Radius = 200.f;
		Brush.Strength = 0.4f;
		Brush.Mode = Mode;
		Brush.Shape = EVoxelBrushShape::Sphere;
		Brush.Falloff = Falloff;
		Brush.BlendMode = BlendMode;
		Brush.ConvergenceAlpha = 0.5f;

		AVoxelChunk::ApplyBrushToLatticeDensity(
			Density,
			FIntVector::ZeroValue,
			ChunkDimensions,
			CellSize,
			Brush,
			IsoLevel);
		return Density;
	};

	const auto SampleAt = [SizeX, SizeY](const TArray<float>& Density, int32 X, int32 Y, int32 Z)
	{
		return Density[X + SizeX * (Y + SizeY * Z)];
	};

	const TArray<float> LinearDensity = ApplySphere(EVoxelBrushFalloff::Linear, EVoxelBrushMode::Add, EVoxelBrushBlendMode::TargetMax, 0.f);
	const TArray<float> SmoothDensity = ApplySphere(EVoxelBrushFalloff::Smooth, EVoxelBrushMode::Add, EVoxelBrushBlendMode::TargetMax, 0.f);
	const TArray<float> SphericalDensity = ApplySphere(EVoxelBrushFalloff::Spherical, EVoxelBrushMode::Add, EVoxelBrushBlendMode::TargetMax, 0.f);
	const TArray<float> PlateauDensity = ApplySphere(EVoxelBrushFalloff::Plateau, EVoxelBrushMode::Add, EVoxelBrushBlendMode::TargetMax, 0.f);

	const float LinearMid = SampleAt(LinearDensity, 3, 2, 2);
	const float SmoothMid = SampleAt(SmoothDensity, 3, 2, 2);
	const float SphericalMid = SampleAt(SphericalDensity, 3, 2, 2);
	const float PlateauMid = SampleAt(PlateauDensity, 3, 2, 2);
	TestTrue(TEXT("Sphere falloff Smooth is softer than Linear at mid radius."), SmoothMid < LinearMid);
	TestTrue(TEXT("Sphere falloff Spherical is stronger than Linear at mid radius."), SphericalMid > LinearMid);
	TestTrue(TEXT("Sphere falloff Plateau keeps full strength through its inner region."), PlateauMid > SphericalMid);
	TestEqual(TEXT("Sphere falloff skips samples at or beyond outer radius."), SampleAt(SmoothDensity, 4, 2, 2), 0.f);

	TArray<float> AddLerpDensity;
	AddLerpDensity.Init(0.2f, LatticeSampleCount);
	FVoxelBrush AddLerpBrush;
	AddLerpBrush.WorldPosition = FVector(200.f, 200.f, 200.f);
	AddLerpBrush.Radius = 200.f;
	AddLerpBrush.Strength = 0.35f;
	AddLerpBrush.Mode = EVoxelBrushMode::Add;
	AddLerpBrush.Shape = EVoxelBrushShape::Sphere;
	AddLerpBrush.Falloff = EVoxelBrushFalloff::Smooth;
	AddLerpBrush.BlendMode = EVoxelBrushBlendMode::TargetLerp;
	AddLerpBrush.ConvergenceAlpha = 0.45f;

	AVoxelChunk::ApplyBrushToLatticeDensity(AddLerpDensity, FIntVector::ZeroValue, ChunkDimensions, CellSize, AddLerpBrush, IsoLevel);
	const float AddAfterOne = SampleAt(AddLerpDensity, 2, 2, 2);
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		AVoxelChunk::ApplyBrushToLatticeDensity(AddLerpDensity, FIntVector::ZeroValue, ChunkDimensions, CellSize, AddLerpBrush, IsoLevel);
	}
	const float AddAfterRepeated = SampleAt(AddLerpDensity, 2, 2, 2);
	TestTrue(TEXT("Sphere Add TargetLerp moves density upward."), AddAfterOne > 0.2f);
	TestTrue(TEXT("Sphere Add TargetLerp continues converging upward."), AddAfterRepeated > AddAfterOne);
	TestTrue(TEXT("Sphere Add TargetLerp stays bounded by the target envelope."), AddAfterRepeated <= IsoLevel + AddLerpBrush.Strength + KINDA_SMALL_NUMBER);

	TArray<float> RemoveLerpDensity;
	RemoveLerpDensity.Init(0.8f, LatticeSampleCount);
	FVoxelBrush RemoveLerpBrush = AddLerpBrush;
	RemoveLerpBrush.Mode = EVoxelBrushMode::Remove;
	AVoxelChunk::ApplyBrushToLatticeDensity(RemoveLerpDensity, FIntVector::ZeroValue, ChunkDimensions, CellSize, RemoveLerpBrush, IsoLevel);
	const float RemoveAfterOne = SampleAt(RemoveLerpDensity, 2, 2, 2);
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		AVoxelChunk::ApplyBrushToLatticeDensity(RemoveLerpDensity, FIntVector::ZeroValue, ChunkDimensions, CellSize, RemoveLerpBrush, IsoLevel);
	}
	const float RemoveAfterRepeated = SampleAt(RemoveLerpDensity, 2, 2, 2);
	TestTrue(TEXT("Sphere Remove TargetLerp moves density downward."), RemoveAfterOne < 0.8f);
	TestTrue(TEXT("Sphere Remove TargetLerp continues converging downward."), RemoveAfterRepeated < RemoveAfterOne);
	TestTrue(TEXT("Sphere Remove TargetLerp stays bounded by the target envelope."), RemoveAfterRepeated >= IsoLevel - RemoveLerpBrush.Strength - KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelShotBuildSurfacePolicyTest,
	"RTPS.VoxelAuthoring.Shot.BuildSurfacePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelShotBuildSurfacePolicyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARTPSPlayerController* Controller = EditorWorld->SpawnActor<ARTPSPlayerController>(
		ARTPSPlayerController::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AActor* ActorTaggedSurface = EditorWorld->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AActor* ComponentTaggedSurface = EditorWorld->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AActor* UntaggedSurface = EditorWorld->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AVoxelChunk* VoxelChunk = EditorWorld->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!Controller || !ActorTaggedSurface || !ComponentTaggedSurface || !UntaggedSurface || !VoxelChunk)
	{
		AddError(TEXT("Build surface policy test actor spawn failed."));
		if (VoxelChunk) { EditorWorld->DestroyActor(VoxelChunk, false, false); }
		if (UntaggedSurface) { EditorWorld->DestroyActor(UntaggedSurface, false, false); }
		if (ComponentTaggedSurface) { EditorWorld->DestroyActor(ComponentTaggedSurface, false, false); }
		if (ActorTaggedSurface) { EditorWorld->DestroyActor(ActorTaggedSurface, false, false); }
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		return false;
	}

	TestEqual(TEXT("Default build surface tag remains explicit."), Controller->VoxelBuildSurfaceTag, FName(TEXT("VoxelBuildSurface")));
	TestTrue(TEXT("Unified sphere voxel shots are enabled by default."), Controller->bUseUnifiedSphereVoxelShots);
	TestTrue(TEXT("Default voxel shot falloff is Smooth."), Controller->VoxelShotFalloff == EVoxelBrushFalloff::Smooth);
	TestTrue(TEXT("Default voxel shot blend mode is TargetLerp."), Controller->VoxelShotBlendMode == EVoxelBrushBlendMode::TargetLerp);
	TestEqual(TEXT("Default voxel shot convergence alpha is explicit."), Controller->VoxelShotConvergenceAlpha, 0.45f);
	TestEqual(TEXT("Default Add sphere radius is explicit."), Controller->VoxelAddSphereRadius, 220.f);
	TestEqual(TEXT("Default Add sphere strength is explicit."), Controller->VoxelAddSphereStrength, 0.35f);
	TestEqual(TEXT("Default Remove sphere radius is explicit."), Controller->VoxelRemoveSphereRadius, 220.f);
	TestEqual(TEXT("Default Remove sphere strength is explicit."), Controller->VoxelRemoveSphereStrength, 0.35f);
	TestEqual(TEXT("Default voxel chunk Add sphere embed depth is explicit."), Controller->VoxelChunkAddSphereEmbedDepth, 30.f);
	TestEqual(TEXT("Default build surface Add sphere offset is explicit."), Controller->BuildSurfaceAddSphereOffset, 60.f);
	TestFalse(TEXT("Experimental build-surface SurfaceBlob mode is disabled by default."), Controller->bUseSurfaceBlobForBuildSurfaceAdd);
	TestEqual(TEXT("Default build-surface SurfaceBlob radius is explicit."), Controller->BuildSurfaceBlobRadius, 240.f);
	TestEqual(TEXT("Default build-surface SurfaceBlob depth is explicit."), Controller->BuildSurfaceBlobDepth, 100.f);
	TestEqual(TEXT("Default build-surface SurfaceBlob strength is explicit."), Controller->BuildSurfaceBlobStrength, 0.35f);
	TestFalse(TEXT("Experimental VoxelChunk TerrainMudBlob Add mode is disabled by default."), Controller->bUseTerrainMudBlobForVoxelChunkAdd);
	TestEqual(TEXT("Default TerrainMudBlob radius is explicit."), Controller->TerrainMudBlobRadius, 220.f);
	TestEqual(TEXT("Default TerrainMudBlob depth is explicit."), Controller->TerrainMudBlobDepth, 130.f);
	TestEqual(TEXT("Default TerrainMudBlob back depth is explicit."), Controller->TerrainMudBlobBackDepth, 35.f);
	TestEqual(TEXT("Default TerrainMudBlob embed depth is explicit."), Controller->TerrainMudBlobEmbedDepth, 30.f);
	TestEqual(TEXT("Default TerrainMudBlob roundness power is explicit."), Controller->TerrainMudBlobRoundnessPower, 2.5f);
	TestEqual(TEXT("Default TerrainMudBlob strength is explicit."), Controller->TerrainMudBlobStrength, 0.35f);
	TestEqual(TEXT("Default TerrainMudBlob convergence alpha is explicit."), Controller->TerrainMudBlobConvergenceAlpha, 0.45f);
	TestEqual(TEXT("Default TerrainMudBlob shot direction blend is explicit."), Controller->TerrainMudBlobShotDirectionBlend, 0.25f);
	TestTrue(TEXT("Default TerrainMudBlob blend mode is TargetLerp."), Controller->TerrainMudBlobBlendMode == EVoxelBrushBlendMode::TargetLerp);

	ActorTaggedSurface->Tags.Add(Controller->VoxelBuildSurfaceTag);
	TestTrue(TEXT("Actor tag marks a non-voxel actor as a build surface."), Controller->IsVoxelBuildSurface(ActorTaggedSurface, nullptr));

	FString FailureReason;
	TestTrue(
		TEXT("Add shot accepts an actor-tagged build surface."),
		Controller->ClassifyVoxelShotHit(EVoxelBrushMode::Add, ActorTaggedSurface, nullptr, FailureReason)
			== ERTPSVoxelShotHitKind::BuildSurfaceHit);
	TestTrue(TEXT("Accepted actor-tagged surface leaves no rejection reason."), FailureReason.IsEmpty());

	USceneComponent* TaggedComponent = NewObject<USceneComponent>(ComponentTaggedSurface);
	if (!TaggedComponent)
	{
		AddError(TEXT("Tagged component creation failed."));
		EditorWorld->DestroyActor(VoxelChunk, false, false);
		EditorWorld->DestroyActor(UntaggedSurface, false, false);
		EditorWorld->DestroyActor(ComponentTaggedSurface, false, false);
		EditorWorld->DestroyActor(ActorTaggedSurface, false, false);
		EditorWorld->DestroyActor(Controller, false, false);
		return false;
	}

	TaggedComponent->ComponentTags.Add(Controller->VoxelBuildSurfaceTag);
	TestTrue(TEXT("Component tag marks a non-voxel actor as a build surface."), Controller->IsVoxelBuildSurface(ComponentTaggedSurface, TaggedComponent));

	FailureReason.Reset();
	TestTrue(
		TEXT("Add shot accepts a component-tagged build surface."),
		Controller->ClassifyVoxelShotHit(EVoxelBrushMode::Add, ComponentTaggedSurface, TaggedComponent, FailureReason)
			== ERTPSVoxelShotHitKind::BuildSurfaceHit);
	TestTrue(TEXT("Accepted component-tagged surface leaves no rejection reason."), FailureReason.IsEmpty());

	FHitResult BuildSurfaceHitResult;
	BuildSurfaceHitResult.ImpactPoint = FVector(10.f, 20.f, 30.f);
	BuildSurfaceHitResult.ImpactNormal = FVector::UpVector;
	const FVector TraceStart(10.f, 20.f, 300.f);
	const FVector TraceEnd = BuildSurfaceHitResult.ImpactPoint;
	FVoxelBrush BuildSurfaceBrush;
	Controller->ConfigureVoxelShotBrushForHit(
		EVoxelBrushMode::Add,
		ERTPSVoxelShotHitKind::BuildSurfaceHit,
		BuildSurfaceHitResult,
		TraceStart,
		TraceEnd,
		BuildSurfaceBrush);
	TestTrue(TEXT("Build-surface Add shot configures a Sphere brush by default."), BuildSurfaceBrush.Shape == EVoxelBrushShape::Sphere);
	TestEqual(TEXT("Build-surface Sphere is offset from the impact point."), BuildSurfaceBrush.WorldPosition, BuildSurfaceHitResult.ImpactPoint + (FVector::UpVector * Controller->BuildSurfaceAddSphereOffset));
	TestEqual(TEXT("Build-surface Sphere stores the surface normal for debug."), BuildSurfaceBrush.SurfaceNormal, FVector::UpVector);
	TestEqual(TEXT("Build-surface Sphere uses configured Add radius."), BuildSurfaceBrush.Radius, Controller->VoxelAddSphereRadius);
	TestEqual(TEXT("Build-surface Sphere uses configured Add strength."), BuildSurfaceBrush.Strength, Controller->VoxelAddSphereStrength);
	TestTrue(TEXT("Build-surface Sphere uses configured falloff."), BuildSurfaceBrush.Falloff == Controller->VoxelShotFalloff);
	TestTrue(TEXT("Build-surface Sphere uses configured blend mode."), BuildSurfaceBrush.BlendMode == Controller->VoxelShotBlendMode);
	TestEqual(TEXT("Build-surface Sphere uses configured convergence alpha."), BuildSurfaceBrush.ConvergenceAlpha, Controller->VoxelShotConvergenceAlpha);

	FVoxelBrush VoxelChunkAddBrush;
	Controller->ConfigureVoxelShotBrushForHit(
		EVoxelBrushMode::Add,
		ERTPSVoxelShotHitKind::VoxelChunkHit,
		BuildSurfaceHitResult,
		TraceStart,
		TraceEnd,
		VoxelChunkAddBrush);
	TestTrue(TEXT("VoxelChunk Add shots configure a Sphere brush by default."), VoxelChunkAddBrush.Shape == EVoxelBrushShape::Sphere);
	TestEqual(TEXT("VoxelChunk Add Sphere stores a stable normal for debug/placement."), VoxelChunkAddBrush.SurfaceNormal, FVector::UpVector);
	TestEqual(TEXT("VoxelChunk Add Sphere stores an embedded center."), VoxelChunkAddBrush.WorldPosition, BuildSurfaceHitResult.ImpactPoint - (VoxelChunkAddBrush.SurfaceNormal * Controller->VoxelChunkAddSphereEmbedDepth));
	TestEqual(TEXT("VoxelChunk Add Sphere uses configured Add radius."), VoxelChunkAddBrush.Radius, Controller->VoxelAddSphereRadius);
	TestEqual(TEXT("VoxelChunk Add Sphere uses configured Add strength."), VoxelChunkAddBrush.Strength, Controller->VoxelAddSphereStrength);
	TestTrue(TEXT("VoxelChunk Add Sphere uses configured falloff."), VoxelChunkAddBrush.Falloff == Controller->VoxelShotFalloff);
	TestTrue(TEXT("VoxelChunk Add Sphere uses configured blend mode."), VoxelChunkAddBrush.BlendMode == Controller->VoxelShotBlendMode);
	TestEqual(TEXT("VoxelChunk Add Sphere uses configured convergence alpha."), VoxelChunkAddBrush.ConvergenceAlpha, Controller->VoxelShotConvergenceAlpha);

	Controller->bUseUnifiedSphereVoxelShots = false;
	Controller->bUseTerrainMudBlobForVoxelChunkAdd = true;
	FVoxelBrush ExperimentalVoxelChunkAddBrush;
	Controller->ConfigureVoxelShotBrushForHit(
		EVoxelBrushMode::Add,
		ERTPSVoxelShotHitKind::VoxelChunkHit,
		BuildSurfaceHitResult,
		TraceStart,
		TraceEnd,
		ExperimentalVoxelChunkAddBrush);
	TestTrue(TEXT("Experimental TerrainMudBlob remains available when unified sphere is disabled."), ExperimentalVoxelChunkAddBrush.Shape == EVoxelBrushShape::TerrainMudBlob);
	Controller->bUseTerrainMudBlobForVoxelChunkAdd = false;
	FVoxelBrush DisabledVoxelChunkAddBrush;
	Controller->ConfigureVoxelShotBrushForHit(
		EVoxelBrushMode::Add,
		ERTPSVoxelShotHitKind::VoxelChunkHit,
		BuildSurfaceHitResult,
		TraceStart,
		TraceEnd,
		DisabledVoxelChunkAddBrush);
	TestTrue(TEXT("Legacy VoxelChunk Add shots fall back to Sphere when TerrainMudBlob is disabled."), DisabledVoxelChunkAddBrush.Shape == EVoxelBrushShape::Sphere);
	TestEqual(TEXT("Disabled TerrainMudBlob path keeps the standard shot radius."), DisabledVoxelChunkAddBrush.Radius, Controller->VoxelShotRadius);
	Controller->bUseUnifiedSphereVoxelShots = true;

	FVoxelBrush VoxelChunkRemoveBrush;
	Controller->ConfigureVoxelShotBrushForHit(
		EVoxelBrushMode::Remove,
		ERTPSVoxelShotHitKind::VoxelChunkHit,
		BuildSurfaceHitResult,
		TraceStart,
		TraceEnd,
		VoxelChunkRemoveBrush);
	TestTrue(TEXT("VoxelChunk Remove shots remain Sphere brushes."), VoxelChunkRemoveBrush.Shape == EVoxelBrushShape::Sphere);
	TestEqual(TEXT("VoxelChunk Remove shots use configured Remove radius."), VoxelChunkRemoveBrush.Radius, Controller->VoxelRemoveSphereRadius);
	TestEqual(TEXT("VoxelChunk Remove shots use configured Remove strength."), VoxelChunkRemoveBrush.Strength, Controller->VoxelRemoveSphereStrength);
	TestTrue(TEXT("VoxelChunk Remove shots use configured falloff."), VoxelChunkRemoveBrush.Falloff == Controller->VoxelShotFalloff);
	TestTrue(TEXT("VoxelChunk Remove shots use configured blend mode."), VoxelChunkRemoveBrush.BlendMode == Controller->VoxelShotBlendMode);

	FailureReason.Reset();
	TestTrue(
		TEXT("Remove shot rejects a tagged non-voxel build surface."),
		Controller->ClassifyVoxelShotHit(EVoxelBrushMode::Remove, ActorTaggedSurface, nullptr, FailureReason)
			== ERTPSVoxelShotHitKind::NonVoxelRejected);
	TestEqual(TEXT("Remove rejection names the build-surface rule."), FailureReason, FString(TEXT("BuildSurfaceRemoveRejected")));

	FailureReason.Reset();
	TestTrue(
		TEXT("Add shot rejects an untagged non-voxel actor."),
		Controller->ClassifyVoxelShotHit(EVoxelBrushMode::Add, UntaggedSurface, nullptr, FailureReason)
			== ERTPSVoxelShotHitKind::NonVoxelRejected);
	TestEqual(TEXT("Untagged non-voxel rejection keeps the existing safety reason."), FailureReason, FString(TEXT("NonVoxelHitActor")));

	FailureReason.Reset();
	TestTrue(
		TEXT("Voxel chunks remain valid shot targets."),
		Controller->ClassifyVoxelShotHit(EVoxelBrushMode::Remove, VoxelChunk, nullptr, FailureReason)
			== ERTPSVoxelShotHitKind::VoxelChunkHit);
	TestTrue(TEXT("Voxel chunk hit leaves no rejection reason."), FailureReason.IsEmpty());

	EditorWorld->DestroyActor(VoxelChunk, false, false);
	EditorWorld->DestroyActor(UntaggedSurface, false, false);
	EditorWorld->DestroyActor(ComponentTaggedSurface, false, false);
	EditorWorld->DestroyActor(ActorTaggedSurface, false, false);
	EditorWorld->DestroyActor(Controller, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFixedArenaBoundsPreloadTest,
	"RTPS.VoxelAuthoring.ChunkManager.FixedArenaBoundsPreload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFixedArenaBoundsPreloadTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);

	if (!ChunkManager)
	{
		AddError(TEXT("AVoxelChunkManager spawn failed."));
		return false;
	}

	TestFalse(TEXT("Fixed arena preload is opt-in by default."), ChunkManager->bUseFixedArenaBounds);
	TestFalse(TEXT("Voxel brush debug logging is opt-in by default."), ChunkManager->bDebugVoxelBrushApplication);
	TestFalse(TEXT("Voxel boundary debug logging is opt-in by default."), ChunkManager->bDebugVoxelChunkBoundaries);
	TestFalse(TEXT("Voxel boundary debug drawing is opt-in by default."), ChunkManager->bDrawVoxelChunkBoundaryDebug);
	TestFalse(TEXT("Voxel mesh rebuild debug logging is opt-in by default."), ChunkManager->bDebugVoxelMeshRebuilds);

	ChunkManager->bUseFixedArenaBounds = true;
	ChunkManager->bDebugVoxelBrushApplication = true;
	ChunkManager->bDebugVoxelChunkBoundaries = true;
	ChunkManager->bDrawVoxelChunkBoundaryDebug = true;
	ChunkManager->bDebugVoxelMeshRebuilds = true;
	ChunkManager->StreamingMode = EVoxelStreamingMode::Streaming;
	ChunkManager->FixedArenaMinChunkCoord = FIntVector(-1, -1, 0);
	ChunkManager->FixedArenaMaxChunkCoord = FIntVector(1, 1, 0);

	ChunkManager->LoadFixedArenaBounds();
	TestEqual(TEXT("Fixed arena preload spawns every configured chunk exactly once."), ChunkManager->LoadedChunkCount, 9);

	ChunkManager->LoadFixedArenaBounds();
	TestEqual(TEXT("Reloading the same fixed arena bounds does not duplicate chunks."), ChunkManager->LoadedChunkCount, 9);

	ChunkManager->UnloadAllChunks();
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

namespace
{
	FRTPSVoxelEditOp MakeApplyDeltaTestEditOp(int64 ServerSequence, const FVector& WorldPosition)
	{
		FVoxelBrush Brush;
		Brush.WorldPosition = WorldPosition;
		Brush.Radius = 200.f;
		Brush.Strength = 1.0f;
		Brush.Mode = EVoxelBrushMode::Add;
		Brush.Shape = EVoxelBrushShape::Sphere;

		FRTPSVoxelEditOp EditOp;
		EditOp.ServerSequence = ServerSequence;
		EditOp.Brush = Brush;
		return EditOp;
	}

	AVoxelChunkManager* SpawnApplyDeltaTestChunkManager(
		UWorld* EditorWorld,
		const FActorSpawnParameters& SpawnParams,
		const FIntVector& ChunkDimensions,
		float CellSize,
		float IsoLevel)
	{
		AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
			AVoxelChunkManager::StaticClass(),
			FTransform::Identity,
			SpawnParams);
		if (ChunkManager != nullptr)
		{
			ChunkManager->ChunkDimensions = ChunkDimensions;
			ChunkManager->CellSize = CellSize;
			ChunkManager->IsoLevel = IsoLevel;
		}
		return ChunkManager;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyDeltaRejectsWhenLocalRevisionDoesNotMatchFromRevisionTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyDeltaRejectsWhenLocalRevisionDoesNotMatchFromRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyDeltaRejectsWhenLocalRevisionDoesNotMatchFromRevisionTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Apply delta revision-mismatch test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 2);

	const TArray<float> BaselineDensity = Chunk->GetLatticeDensity();

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.bRequiresFullSnapshot = false;
	Payload.FromRevision = 1;
	Payload.ToRevision = 3;
	Payload.SnapshotRevision = 0;
	Payload.SnapshotServerSequence = INDEX_NONE;
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(101, Chunk->GetActorLocation()));
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(102, Chunk->GetActorLocation()));

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationApplyDeltaRevMismatch"));
	TestFalse(TEXT("ApplyChunkDeltaPayloadLocal rejects payload when local revision does not match FromRevision."), bApplied);

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision still tracked after rejection."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision unchanged after rejection."), *StoredRevision, 2);
	}

	const FRTPSVoxelChunkState* PostState = ChunkManager->FindChunkState(TestChunkCoord);
	if (PostState != nullptr)
	{
		TestNotEqual(TEXT("ChunkState revision did not advance to ToRevision after rejection."), PostState->Revision, 3);
	}

	TestTrue(TEXT("Live chunk lattice density not mutated after rejection."), Chunk->GetLatticeDensity() == BaselineDensity);

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyDeltaRejectsWhenChunkNotReadyTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyDeltaRejectsWhenChunkNotReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyDeltaRejectsWhenChunkNotReadyTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord(7, 0, 0);
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager)
	{
		AddError(TEXT("Apply delta not-ready test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 0);

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.FromRevision = 0;
	Payload.ToRevision = 2;
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(201, FVector(700.f, 0.f, 0.f)));
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(202, FVector(700.f, 0.f, 0.f)));

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationApplyDeltaNoChunk"));
	TestFalse(TEXT("ApplyChunkDeltaPayloadLocal rejects payload when local chunk is missing."), bApplied);

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision still tracked after rejection."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision unchanged when chunk missing."), *StoredRevision, 0);
	}

	TestFalse(TEXT("Applied sequences not recorded when chunk missing."), ChunkManager->AppliedEditSequencesByChunk.Contains(TestChunkCoord));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyDeltaCommitsAtomicallyOnAllOpsSuccessTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyDeltaCommitsAtomicallyOnAllOpsSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyDeltaCommitsAtomicallyOnAllOpsSuccessTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Apply delta success-commit test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 0);

	const TArray<float> BaselineDensity = Chunk->GetLatticeDensity();

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.FromRevision = 0;
	Payload.ToRevision = 2;
	Payload.SnapshotRevision = 0;
	Payload.SnapshotServerSequence = INDEX_NONE;
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(301, Chunk->GetActorLocation()));
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(302, Chunk->GetActorLocation()));

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationApplyDeltaSuccess"));
	TestTrue(TEXT("ApplyChunkDeltaPayloadLocal succeeds when all ops are valid."), bApplied);

	TestFalse(TEXT("Live chunk density changed after successful delta apply."), Chunk->GetLatticeDensity() == BaselineDensity);

	const FRTPSVoxelChunkState* LocalState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("Local ChunkState exists after successful delta apply."), LocalState);
	if (LocalState != nullptr)
	{
		TestEqual(TEXT("ChunkState revision advanced to Payload.ToRevision."), LocalState->Revision, Payload.ToRevision);
		TestTrue(TEXT("ChunkState marks bHasDensity after successful delta."), LocalState->bHasDensity);
	}

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision tracked after successful delta."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision advanced to Payload.ToRevision."), *StoredRevision, Payload.ToRevision);
	}

	const TSet<int64>* AppliedSequences = ChunkManager->AppliedEditSequencesByChunk.Find(TestChunkCoord);
	TestNotNull(TEXT("AppliedEditSequencesByChunk recorded after successful delta."), AppliedSequences);
	if (AppliedSequences != nullptr)
	{
		TestTrue(TEXT("First op's ServerSequence recorded."), AppliedSequences->Contains(301));
		TestTrue(TEXT("Second op's ServerSequence recorded."), AppliedSequences->Contains(302));
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyDeltaRejectsAllDuplicateOpsTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyDeltaRejectsAllDuplicateOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyDeltaRejectsAllDuplicateOpsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Apply delta all-duplicate test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 0);
	ChunkManager->AppliedEditSequencesByChunk.FindOrAdd(TestChunkCoord).Add(42);

	const TArray<float> BaselineDensity = Chunk->GetLatticeDensity();

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.FromRevision = 0;
	Payload.ToRevision = 1;
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(42, Chunk->GetActorLocation()));

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationApplyDeltaAllDuplicate"));
	TestFalse(TEXT("ApplyChunkDeltaPayloadLocal rejects payload when every op is already applied."), bApplied);

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision still tracked after duplicate rejection."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision unchanged after duplicate rejection."), *StoredRevision, 0);
	}

	const FRTPSVoxelChunkState* PostState = ChunkManager->FindChunkState(TestChunkCoord);
	if (PostState != nullptr)
	{
		TestNotEqual(TEXT("ChunkState revision did not advance to ToRevision after duplicate rejection."), PostState->Revision, Payload.ToRevision);
	}

	TestTrue(TEXT("Live chunk lattice density not mutated after duplicate rejection."), Chunk->GetLatticeDensity() == BaselineDensity);

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyDeltaFailureDoesNotRecordPartialSequencesTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyDeltaFailureDoesNotRecordPartialSequences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyDeltaFailureDoesNotRecordPartialSequencesTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Apply delta partial-failure test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 0);

	const TArray<float> BaselineDensity = Chunk->GetLatticeDensity();

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.FromRevision = 0;
	Payload.ToRevision = 2;

	FRTPSVoxelEditOp ValidFirstOp = MakeApplyDeltaTestEditOp(501, Chunk->GetActorLocation());
	FRTPSVoxelEditOp InvalidSecondOp = MakeApplyDeltaTestEditOp(502, Chunk->GetActorLocation());
	InvalidSecondOp.ServerSequence = INDEX_NONE;
	Payload.EditOps.Add(ValidFirstOp);
	Payload.EditOps.Add(InvalidSecondOp);

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationApplyDeltaPartialFailure"));
	TestFalse(TEXT("ApplyChunkDeltaPayloadLocal rejects payload when any op is invalid."), bApplied);

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision still tracked after partial-failure rejection."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision unchanged after partial-failure rejection."), *StoredRevision, 0);
	}

	const TSet<int64>* AppliedSequences = ChunkManager->AppliedEditSequencesByChunk.Find(TestChunkCoord);
	if (AppliedSequences != nullptr)
	{
		TestFalse(TEXT("First op's ServerSequence not recorded after rejection."), AppliedSequences->Contains(501));
		TestFalse(TEXT("Invalid second op's ServerSequence not recorded after rejection."), AppliedSequences->Contains(502));
	}

	TestTrue(TEXT("Live chunk lattice density not partially committed."), Chunk->GetLatticeDensity() == BaselineDensity);

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelApplyDeltaSuccessMirrorsChunkDensityToLocalStateTest,
	"RTPS.VoxelAuthoring.Chunk.ApplyDeltaSuccessMirrorsChunkDensityToLocalState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelApplyDeltaSuccessMirrorsChunkDensityToLocalStateTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Apply delta mirror-density test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 0);

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.FromRevision = 0;
	Payload.ToRevision = 4;
	Payload.SnapshotRevision = 2;
	Payload.SnapshotServerSequence = 600;
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(601, Chunk->GetActorLocation()));
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(602, Chunk->GetActorLocation()));

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationApplyDeltaMirror"));
	TestTrue(TEXT("ApplyChunkDeltaPayloadLocal succeeds for mirror-state test."), bApplied);

	const FRTPSVoxelChunkState* LocalState = ChunkManager->FindChunkState(TestChunkCoord);
	TestNotNull(TEXT("Local ChunkState exists after successful delta apply."), LocalState);
	if (LocalState != nullptr)
	{
		TestTrue(TEXT("ChunkState density mirrors live chunk density."), LocalState->LatticeDensity == Chunk->GetLatticeDensity());
		TestEqual(TEXT("ChunkState revision matches Payload.ToRevision."), LocalState->Revision, Payload.ToRevision);
		TestEqual(TEXT("ChunkState SnapshotRevision copied from payload."), LocalState->SnapshotRevision, Payload.SnapshotRevision);
		TestEqual(TEXT("ChunkState SnapshotServerSequence copied from payload."), LocalState->SnapshotServerSequence, Payload.SnapshotServerSequence);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelMarkFullSnapshotResyncInvalidatesClientSubscribeStateTest,
	"RTPS.VoxelAuthoring.Chunk.MarkFullSnapshotResyncInvalidatesClientSubscribeState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelMarkFullSnapshotResyncInvalidatesClientSubscribeStateTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Mark full snapshot resync test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());

	const FIntVector TestChunkCoord(2, 5, 1);
	ChunkManager->LastSubscribedKnownRevisionByCoord.Add(TestChunkCoord, 7);
	ChunkManager->LastChunkSubscribeRequestTimeByCoord.Add(TestChunkCoord, FPlatformTime::Seconds());

	ChunkManager->MarkChunkForFullSnapshotResync(TestChunkCoord, TEXT("AutomationMarkFullSnapshotResync"));

	TestTrue(TEXT("ChunksNeedingFullSnapshotResync contains chunk after marking."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));
	TestFalse(TEXT("LastSubscribedKnownRevisionByCoord cleared by mark."), ChunkManager->LastSubscribedKnownRevisionByCoord.Contains(TestChunkCoord));
	TestFalse(TEXT("LastChunkSubscribeRequestTimeByCoord cleared by mark."), ChunkManager->LastChunkSubscribeRequestTimeByCoord.Contains(TestChunkCoord));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFullSnapshotApplyClearsFullResyncMarkerTest,
	"RTPS.VoxelAuthoring.Chunk.FullSnapshotApplyClearsFullResyncMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFullSnapshotApplyClearsFullResyncMarkerTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Full snapshot clears marker test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	ChunkManager->MarkChunkForFullSnapshotResync(TestChunkCoord, TEXT("AutomationFullSnapshotClears"));
	TestTrue(TEXT("Marker installed before full snapshot apply."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	FRTPSVoxelChunkStatePayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.Revision = 9;
	Payload.bSuccess = true;
	Payload.bHasDensity = true;
	Payload.LatticeDensity.Init(0.5f, Chunk->GetExpectedLatticeSampleCount());

	const bool bApplied = ChunkManager->ApplyChunkStatePayloadLocal(Payload, TEXT("AutomationFullSnapshotClearsApply"));
	TestTrue(TEXT("Full snapshot apply succeeded."), bApplied);
	TestFalse(TEXT("ChunksNeedingFullSnapshotResync cleared after successful full snapshot apply."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision recorded after full snapshot apply."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision matches Payload.Revision after full snapshot apply."), *StoredRevision, Payload.Revision);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelDestroyChunkClearsFullResyncMarkerTest,
	"RTPS.VoxelAuthoring.Chunk.DestroyChunkClearsFullResyncMarker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDestroyChunkClearsFullResyncMarkerTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(
		AVoxelChunkManager::StaticClass(),
		FTransform::Identity,
		SpawnParams);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, FIntVector(1, 1, 1), 100.f, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("DestroyChunk clears marker test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();

	ChunkManager->AppliedEditSequencesByChunk.FindOrAdd(TestChunkCoord).Add(700);
	FRTPSVoxelChunkStatePayload PendingPayload;
	PendingPayload.ChunkCoord = TestChunkCoord;
	PendingPayload.Revision = 1;
	PendingPayload.bSuccess = true;
	PendingPayload.bHasDensity = true;
	PendingPayload.LatticeDensity = Chunk->GetLatticeDensity();
	ChunkManager->PendingIncomingChunkPayloads.Add(TestChunkCoord, PendingPayload);

	FRTPSVoxelChunkState& LocalState = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	LocalState.ChunkCoord = TestChunkCoord;
	LocalState.bHasDensity = true;
	LocalState.Revision = 1;
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 1);

	ChunkManager->MarkChunkForFullSnapshotResync(TestChunkCoord, TEXT("AutomationDestroyChunkClears"));
	TestTrue(TEXT("Marker installed before DestroyChunk."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	ChunkManager->DestroyChunk(TestChunkCoord);

	TestFalse(TEXT("ChunksNeedingFullSnapshotResync cleared by DestroyChunk."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));
	TestFalse(TEXT("LastAppliedRemoteRevisionByCoord cleared by DestroyChunk."), ChunkManager->LastAppliedRemoteRevisionByCoord.Contains(TestChunkCoord));
	TestFalse(TEXT("ChunkStates cleared by DestroyChunk."), ChunkManager->ChunkStates.Contains(TestChunkCoord));
	TestFalse(TEXT("AppliedEditSequencesByChunk cleared by DestroyChunk."), ChunkManager->AppliedEditSequencesByChunk.Contains(TestChunkCoord));
	TestFalse(TEXT("PendingIncomingChunkPayloads cleared by DestroyChunk."), ChunkManager->PendingIncomingChunkPayloads.Contains(TestChunkCoord));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelDeltaFailureMarksFullSnapshotResyncTest,
	"RTPS.VoxelAuthoring.Chunk.DeltaFailureMarksFullSnapshotResync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelDeltaFailureMarksFullSnapshotResyncTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FIntVector TestChunkCoord = FIntVector::ZeroValue;
	const FIntVector ChunkDimensions(2, 2, 2);
	const float CellSize = 100.f;

	AVoxelChunkManager* ChunkManager = SpawnApplyDeltaTestChunkManager(EditorWorld, SpawnParams, ChunkDimensions, CellSize, 0.5f);
	AVoxelChunk* Chunk = SpawnReadyVoxelChunkForAutomation(EditorWorld, SpawnParams, TestChunkCoord, ChunkDimensions, CellSize, 0.5f);
	if (!ChunkManager || !Chunk)
	{
		AddError(TEXT("Delta failure mark test actor spawn failed."));
		if (Chunk) { EditorWorld->DestroyActor(Chunk, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());
	ChunkManager->LoadedChunks.Add(TestChunkCoord, Chunk);
	ChunkManager->LoadedChunkCount = ChunkManager->LoadedChunks.Num();
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 2);

	FRTPSVoxelChunkDeltaPayload Payload;
	Payload.ChunkCoord = TestChunkCoord;
	Payload.bSuccess = true;
	Payload.bRequiresFullSnapshot = false;
	Payload.FromRevision = 1;
	Payload.ToRevision = 3;
	Payload.EditOps.Add(MakeApplyDeltaTestEditOp(801, Chunk->GetActorLocation()));

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("AutomationDeltaFailureMark"));
	TestFalse(TEXT("Delta apply rejects mismatched FromRevision payload."), bApplied);

	if (!bApplied)
	{
		ChunkManager->MarkChunkForFullSnapshotResync(Payload.ChunkCoord, TEXT("AutomationDeltaFailureMark fallback"));
	}

	TestTrue(TEXT("Delta failure path marks chunk for full snapshot resync."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	const int32* StoredRevision = ChunkManager->LastAppliedRemoteRevisionByCoord.Find(TestChunkCoord);
	TestNotNull(TEXT("LastAppliedRemoteRevision still tracked after delta failure."), StoredRevision);
	if (StoredRevision != nullptr)
	{
		TestEqual(TEXT("LastAppliedRemoteRevision unchanged after delta failure."), *StoredRevision, 2);
	}

	EditorWorld->DestroyActor(Chunk, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

namespace
{
	FRTPSVoxelChunkDeltaPayload MakePhase65DeltaPayload(
		const FIntVector& ChunkCoord,
		int32 FromRevision,
		int32 ToRevision,
		int64 ServerSequenceBase = 1000)
	{
		FRTPSVoxelChunkDeltaPayload Payload;
		Payload.ChunkCoord = ChunkCoord;
		Payload.bSuccess = true;
		Payload.bRequiresFullSnapshot = false;
		Payload.FromRevision = FromRevision;
		Payload.ToRevision = ToRevision;
		Payload.SnapshotRevision = 0;
		Payload.SnapshotServerSequence = INDEX_NONE;
		const int32 OpCount = FMath::Max(1, ToRevision - FromRevision);
		for (int32 OpIndex = 0; OpIndex < OpCount; ++OpIndex)
		{
			Payload.EditOps.Add(MakeApplyDeltaTestEditOp(ServerSequenceBase + OpIndex, FVector::ZeroVector));
		}
		return Payload;
	}

	FRTPSVoxelChunkStatePayload MakePhase65FullPayload(
		const FIntVector& ChunkCoord,
		int32 Revision,
		int32 SampleCount = 8)
	{
		FRTPSVoxelChunkStatePayload Payload;
		Payload.ChunkCoord = ChunkCoord;
		Payload.Revision = Revision;
		Payload.bSuccess = true;
		Payload.bHasDensity = true;
		Payload.LatticeDensity.Init(0.5f, SampleCount);
		return Payload;
	}

	FRTPSVoxelRecentEditOp MakePhase65RecentOp(int32 RevisionBefore, int32 RevisionAfter, int64 ServerSequence)
	{
		FRTPSVoxelEditOp EditOp = MakeApplyDeltaTestEditOp(ServerSequence, FVector::ZeroVector);
		return FRTPSVoxelRecentEditOp(RevisionBefore, RevisionAfter, EditOp);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelQueueDeltaPayloadForClientQueuesValidDeltaTest,
	"RTPS.VoxelAuthoring.Chunk.QueueDeltaPayloadForClientQueuesValidDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelQueueDeltaPayloadForClientQueuesValidDeltaTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("QueueDeltaPayloadForClient test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(1, 1, 1);
	FRTPSVoxelChunkDeltaPayload Payload = MakePhase65DeltaPayload(TestChunkCoord, 2, 5, 1100);

	ChunkManager->QueueChunkDeltaPayloadForClient(Controller, Payload, TEXT("AutomationQueueDeltaValid"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkDeltaPayload>* QueuedDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Delta queue contains entry after queueing valid delta."), QueuedDeltas);
	if (QueuedDeltas != nullptr)
	{
		TestEqual(TEXT("Delta queue size is 1 after queueing one delta."), QueuedDeltas->Num(), 1);
		if (QueuedDeltas->Num() > 0)
		{
			TestEqual(TEXT("Queued delta ChunkCoord matches input."), (*QueuedDeltas)[0].ChunkCoord, Payload.ChunkCoord);
			TestEqual(TEXT("Queued delta FromRevision matches input."), (*QueuedDeltas)[0].FromRevision, Payload.FromRevision);
			TestEqual(TEXT("Queued delta ToRevision matches input."), (*QueuedDeltas)[0].ToRevision, Payload.ToRevision);
		}
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelQueueDeltaDropsWhenFullSnapshotAlreadyQueuedTest,
	"RTPS.VoxelAuthoring.Chunk.QueueDeltaDropsWhenFullSnapshotAlreadyQueued",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelQueueDeltaDropsWhenFullSnapshotAlreadyQueuedTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("QueueDelta drops with full snapshot test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(2, 0, 0);
	FRTPSVoxelChunkStatePayload FullPayload = MakePhase65FullPayload(TestChunkCoord, 5);
	ChunkManager->QueueChunkStatePayloadForClient(Controller, FullPayload, TEXT("AutomationQueueFullFirst"));

	FRTPSVoxelChunkDeltaPayload DeltaPayload = MakePhase65DeltaPayload(TestChunkCoord, 1, 5, 1200);
	ChunkManager->QueueChunkDeltaPayloadForClient(Controller, DeltaPayload, TEXT("AutomationQueueDeltaSecond"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkDeltaPayload>* QueuedDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	if (QueuedDeltas != nullptr)
	{
		const bool bDeltaContainsCoord = QueuedDeltas->ContainsByPredicate([&TestChunkCoord](const FRTPSVoxelChunkDeltaPayload& Existing){ return Existing.ChunkCoord == TestChunkCoord; });
		TestFalse(TEXT("Delta queue does not contain entry when full snapshot already queued."), bDeltaContainsCoord);
	}

	const TArray<FRTPSVoxelChunkStatePayload>* QueuedFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Full payload queue retains entry after dropping delta."), QueuedFulls);
	if (QueuedFulls != nullptr)
	{
		const bool bFullContainsCoord = QueuedFulls->ContainsByPredicate([&TestChunkCoord](const FRTPSVoxelChunkStatePayload& Existing){ return Existing.ChunkCoord == TestChunkCoord; });
		TestTrue(TEXT("Full payload queue still contains entry."), bFullContainsCoord);
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelQueueFullSnapshotSupersedesQueuedDeltaTest,
	"RTPS.VoxelAuthoring.Chunk.QueueFullSnapshotSupersedesQueuedDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelQueueFullSnapshotSupersedesQueuedDeltaTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("QueueFullSnapshot supersedes delta test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(3, 0, 0);
	FRTPSVoxelChunkDeltaPayload DeltaPayload = MakePhase65DeltaPayload(TestChunkCoord, 1, 4, 1300);
	ChunkManager->QueueChunkDeltaPayloadForClient(Controller, DeltaPayload, TEXT("AutomationQueueDeltaFirst"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	{
		const TArray<FRTPSVoxelChunkDeltaPayload>* DeltasBefore = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
		TestNotNull(TEXT("Delta queue contains entry before full supersede."), DeltasBefore);
	}

	FRTPSVoxelChunkStatePayload FullPayload = MakePhase65FullPayload(TestChunkCoord, 6);
	ChunkManager->QueueChunkStatePayloadForClient(Controller, FullPayload, TEXT("AutomationQueueFullSecond"));

	const TArray<FRTPSVoxelChunkDeltaPayload>* DeltasAfter = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	if (DeltasAfter != nullptr)
	{
		const bool bDeltaContainsCoord = DeltasAfter->ContainsByPredicate([&TestChunkCoord](const FRTPSVoxelChunkDeltaPayload& Existing){ return Existing.ChunkCoord == TestChunkCoord; });
		TestFalse(TEXT("Delta queue no longer contains entry after full snapshot supersede."), bDeltaContainsCoord);
	}

	const TArray<FRTPSVoxelChunkStatePayload>* QueuedFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Full payload queue exists after supersede."), QueuedFulls);
	if (QueuedFulls != nullptr)
	{
		const bool bFullContainsCoord = QueuedFulls->ContainsByPredicate([&TestChunkCoord](const FRTPSVoxelChunkStatePayload& Existing){ return Existing.ChunkCoord == TestChunkCoord; });
		TestTrue(TEXT("Full payload queue contains entry after supersede."), bFullContainsCoord);
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFlushQueuedPayloadsHonorsCombinedThrottleTest,
	"RTPS.VoxelAuthoring.Chunk.FlushQueuedPayloadsHonorsCombinedThrottle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFlushQueuedPayloadsHonorsCombinedThrottleTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Flush combined throttle test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->MaxChunkStatePayloadsPerClientPerTick = 2;

	const FIntVector FullCoordA(10, 0, 0);
	const FIntVector FullCoordB(11, 0, 0);
	const FIntVector DeltaCoordA(20, 0, 0);
	const FIntVector DeltaCoordB(21, 0, 0);

	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(FullCoordA, 1), TEXT("AutomationFlushFullA"));
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(FullCoordB, 2), TEXT("AutomationFlushFullB"));
	ChunkManager->QueueChunkDeltaPayloadForClient(Controller, MakePhase65DeltaPayload(DeltaCoordA, 0, 1, 1400), TEXT("AutomationFlushDeltaA"));
	ChunkManager->QueueChunkDeltaPayloadForClient(Controller, MakePhase65DeltaPayload(DeltaCoordB, 0, 1, 1500), TEXT("AutomationFlushDeltaB"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	{
		const TArray<FRTPSVoxelChunkStatePayload>* InitialFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
		const TArray<FRTPSVoxelChunkDeltaPayload>* InitialDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
		TestNotNull(TEXT("Full queue exists pre-flush."), InitialFulls);
		TestNotNull(TEXT("Delta queue exists pre-flush."), InitialDeltas);
		if (InitialFulls != nullptr) { TestEqual(TEXT("Two full payloads queued pre-flush."), InitialFulls->Num(), 2); }
		if (InitialDeltas != nullptr) { TestEqual(TEXT("Two delta payloads queued pre-flush."), InitialDeltas->Num(), 2); }
	}

	const int32 SentPayloads = ChunkManager->FlushQueuedChunkStatePayloads(0.016f);
	TestEqual(TEXT("Combined throttle limits flush to 2 payloads."), SentPayloads, 2);

	const TArray<FRTPSVoxelChunkStatePayload>* RemainingFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestTrue(TEXT("Full payload queue is empty after flush prioritises full first."), RemainingFulls == nullptr || RemainingFulls->Num() == 0);

	const TArray<FRTPSVoxelChunkDeltaPayload>* RemainingDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Delta queue retains entries after flush."), RemainingDeltas);
	if (RemainingDeltas != nullptr)
	{
		TestEqual(TEXT("Both delta payloads remain because budget was consumed by full snapshots."), RemainingDeltas->Num(), 2);
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeQueuesDeltaWhenSyncModeDeltaTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeQueuesDeltaWhenSyncModeDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeQueuesDeltaWhenSyncModeDeltaTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Subscribe delta routing test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(30, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	FRTPSVoxelChunkState& State = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	State.ChunkCoord = TestChunkCoord;
	State.bHasDensity = true;
	State.LatticeDensity.Init(0.5f, 8);
	State.Revision = 3;
	State.SnapshotRevision = 0;
	State.SnapshotServerSequence = INDEX_NONE;
	State.RecentOps.Add(MakePhase65RecentOp(0, 1, 5001));
	State.RecentOps.Add(MakePhase65RecentOp(1, 2, 5002));
	State.RecentOps.Add(MakePhase65RecentOp(2, 3, 5003));

	ChunkManager->SubscribePlayerToChunk(Controller, TestChunkCoord, 0, TEXT("AutomationSubscribeDelta"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkDeltaPayload>* QueuedDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Subscribe queues delta payload when ChooseChunkSyncMode picks Delta."), QueuedDeltas);
	if (QueuedDeltas != nullptr)
	{
		TestEqual(TEXT("One delta payload queued by Subscribe Delta routing."), QueuedDeltas->Num(), 1);
		if (QueuedDeltas->Num() > 0)
		{
			TestEqual(TEXT("Queued delta ChunkCoord matches subscribed chunk."), (*QueuedDeltas)[0].ChunkCoord, TestChunkCoord);
			TestEqual(TEXT("Queued delta FromRevision matches client known revision."), (*QueuedDeltas)[0].FromRevision, 0);
			TestEqual(TEXT("Queued delta ToRevision matches server revision."), (*QueuedDeltas)[0].ToRevision, 3);
		}
	}

	const TArray<FRTPSVoxelChunkStatePayload>* QueuedFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestTrue(TEXT("Subscribe Delta routing does not queue full payload."), QueuedFulls == nullptr || QueuedFulls->Num() == 0);

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeQueuesFullWhenClientOlderThanSnapshotTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeQueuesFullWhenClientOlderThanSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeQueuesFullWhenClientOlderThanSnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Subscribe full older-than-snapshot test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(31, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	FRTPSVoxelChunkState& State = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	State.ChunkCoord = TestChunkCoord;
	State.bHasDensity = true;
	State.LatticeDensity.Init(0.5f, 8);
	State.Revision = 5;
	State.SnapshotRevision = 3;
	State.SnapshotServerSequence = 5500;
	State.RecentOps.Add(MakePhase65RecentOp(3, 4, 5501));
	State.RecentOps.Add(MakePhase65RecentOp(4, 5, 5502));

	ChunkManager->SubscribePlayerToChunk(Controller, TestChunkCoord, 0, TEXT("AutomationSubscribeFullOlder"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* QueuedFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Subscribe queues full payload when client older than snapshot."), QueuedFulls);
	if (QueuedFulls != nullptr)
	{
		TestEqual(TEXT("One full payload queued for older-than-snapshot client."), QueuedFulls->Num(), 1);
		if (QueuedFulls->Num() > 0)
		{
			TestEqual(TEXT("Queued full payload ChunkCoord matches."), (*QueuedFulls)[0].ChunkCoord, TestChunkCoord);
			TestEqual(TEXT("Queued full payload Revision matches server."), (*QueuedFulls)[0].Revision, 5);
		}
	}

	const TArray<FRTPSVoxelChunkDeltaPayload>* QueuedDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestTrue(TEXT("Subscribe older-than-snapshot does not queue delta."), QueuedDeltas == nullptr || QueuedDeltas->Num() == 0);

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeSkipsWhenClientUpToDateTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeSkipsWhenClientUpToDate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeSkipsWhenClientUpToDateTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Subscribe skips up-to-date test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(32, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	FRTPSVoxelChunkState& State = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	State.ChunkCoord = TestChunkCoord;
	State.bHasDensity = true;
	State.LatticeDensity.Init(0.5f, 8);
	State.Revision = 5;
	State.SnapshotRevision = 5;
	State.SnapshotServerSequence = INDEX_NONE;

	ChunkManager->SubscribePlayerToChunk(Controller, TestChunkCoord, 5, TEXT("AutomationSubscribeUpToDate"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* QueuedFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestTrue(TEXT("Up-to-date client does not receive a queued full payload."), QueuedFulls == nullptr || QueuedFulls->Num() == 0);

	const TArray<FRTPSVoxelChunkDeltaPayload>* QueuedDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestTrue(TEXT("Up-to-date client does not receive a queued delta payload."), QueuedDeltas == nullptr || QueuedDeltas->Num() == 0);

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFullResyncMarkerForcesUnknownRevisionOnSubscribeTest,
	"RTPS.VoxelAuthoring.Chunk.FullResyncMarkerForcesUnknownRevisionOnSubscribe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFullResyncMarkerForcesUnknownRevisionOnSubscribeTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("Full resync marker test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());

	const FIntVector TestChunkCoord(40, 0, 0);
	ChunkManager->MarkChunkForFullSnapshotResync(TestChunkCoord, TEXT("AutomationMarkBeforeSelect"));
	TestTrue(TEXT("Marker installed before SelectClientSubscribeKnownRevision."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	const int32 SelectedRevision = ChunkManager->SelectClientSubscribeKnownRevision(TestChunkCoord, 5);
	TestEqual(TEXT("Marker forces ClientKnownRevision to INDEX_NONE."), SelectedRevision, INDEX_NONE);

	TestTrue(TEXT("Marker remains until full snapshot apply succeeds."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	// Phase 6-5B: SelectClientSubscribeKnownRevision now returns INDEX_NONE when
	// LastAppliedRemoteRevisionByCoord has no baseline entry. To prove that an unmarked
	// chunk WITH a known baseline preserves LocalKnownRevision, seed the baseline.
	const FIntVector OtherCoord(41, 0, 0);
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(OtherCoord, 7);
	const int32 OtherSelected = ChunkManager->SelectClientSubscribeKnownRevision(OtherCoord, 7);
	TestEqual(TEXT("Unmarked chunk with known baseline preserves provided LocalKnownRevision."), OtherSelected, 7);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeFallsBackFullWhenDeltaBuildFailsTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeFallsBackFullWhenDeltaBuildFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeFallsBackFullWhenDeltaBuildFailsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Subscribe delta-build-fail fallback test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TestChunkCoord(50, 0, 0);
	ChunkManager->ChunkDimensions = FIntVector(1, 1, 1);
	ChunkManager->CellSize = 100.f;
	ChunkManager->IsoLevel = 0.5f;

	FRTPSVoxelChunkState& State = ChunkManager->FindOrCreateChunkState(TestChunkCoord);
	State.ChunkCoord = TestChunkCoord;
	State.bHasDensity = true;
	State.LatticeDensity.Init(0.5f, 8);
	State.Revision = 4;
	State.SnapshotRevision = 0;
	State.SnapshotServerSequence = INDEX_NONE;
	// Intentional gap to break delta coverage: 0->1 present, 1->2 missing, 2->3 missing, 3->4 present.
	State.RecentOps.Add(MakePhase65RecentOp(0, 1, 6001));
	State.RecentOps.Add(MakePhase65RecentOp(3, 4, 6004));

	ChunkManager->SubscribePlayerToChunk(Controller, TestChunkCoord, 0, TEXT("AutomationSubscribeDeltaBuildFails"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const TArray<FRTPSVoxelChunkStatePayload>* QueuedFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Subscribe falls back to full payload when delta coverage is incomplete."), QueuedFulls);
	if (QueuedFulls != nullptr)
	{
		TestEqual(TEXT("Exactly one full payload queued by fallback."), QueuedFulls->Num(), 1);
		if (QueuedFulls->Num() > 0)
		{
			TestEqual(TEXT("Fallback full payload ChunkCoord matches."), (*QueuedFulls)[0].ChunkCoord, TestChunkCoord);
			TestEqual(TEXT("Fallback full payload Revision matches server."), (*QueuedFulls)[0].Revision, 4);
		}
	}

	const TArray<FRTPSVoxelChunkDeltaPayload>* QueuedDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestTrue(TEXT("No partial delta payload is queued when delta build fails."), QueuedDeltas == nullptr || QueuedDeltas->Num() == 0);

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelUnsubscribePlayerFromChunkRemovesQueuedDeltaForChunkTest,
	"RTPS.VoxelAuthoring.Chunk.UnsubscribePlayerFromChunkRemovesQueuedDeltaForChunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelUnsubscribePlayerFromChunkRemovesQueuedDeltaForChunkTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* TargetController = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	ARTPSPlayerController* OtherController = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !TargetController || !OtherController)
	{
		AddError(TEXT("Unsubscribe-from-chunk delta cleanup test actor spawn failed."));
		if (OtherController) { EditorWorld->DestroyActor(OtherController, false, false); }
		if (TargetController) { EditorWorld->DestroyActor(TargetController, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	// TargetCoord: delta only (will be unsubscribed). OtherCoord: delta only (must remain).
	// FullOnlyCoord: full only (kept on a different chunk to avoid supersede collisions).
	const FIntVector TargetCoord(70, 0, 0);
	const FIntVector OtherCoord(71, 0, 0);
	const FIntVector FullOnlyCoord(72, 0, 0);

	ChunkManager->QueueChunkDeltaPayloadForClient(TargetController, MakePhase65DeltaPayload(TargetCoord, 0, 1, 7000), TEXT("AutomationUnsubDeltaTarget"));
	ChunkManager->QueueChunkDeltaPayloadForClient(TargetController, MakePhase65DeltaPayload(OtherCoord, 0, 1, 7100), TEXT("AutomationUnsubDeltaOther"));
	ChunkManager->QueueChunkStatePayloadForClient(TargetController, MakePhase65FullPayload(FullOnlyCoord, 1), TEXT("AutomationUnsubFullExtra"));

	ChunkManager->QueueChunkDeltaPayloadForClient(OtherController, MakePhase65DeltaPayload(TargetCoord, 0, 1, 7200), TEXT("AutomationUnsubDeltaSibling"));

	const TWeakObjectPtr<ARTPSPlayerController> TargetKey(TargetController);
	ChunkManager->ClientSubscribedChunks.FindOrAdd(TargetKey).Add(TargetCoord);
	ChunkManager->ClientSubscribedChunks.FindOrAdd(TargetKey).Add(OtherCoord);
	ChunkManager->ChunkSubscribers.FindOrAdd(TargetCoord).Add(TargetKey);

	ChunkManager->UnsubscribePlayerFromChunk(TargetController, TargetCoord, TEXT("AutomationUnsubFromChunk"));

	const TArray<FRTPSVoxelChunkDeltaPayload>* TargetDeltasAfter = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(TargetKey);
	TestNotNull(TEXT("Target controller delta queue retains OtherCoord entry after partial unsubscribe."), TargetDeltasAfter);
	if (TargetDeltasAfter != nullptr)
	{
		const bool bContainsTarget = TargetDeltasAfter->ContainsByPredicate([&TargetCoord](const FRTPSVoxelChunkDeltaPayload& P){ return P.ChunkCoord == TargetCoord; });
		TestFalse(TEXT("Unsubscribed-chunk delta payload removed from target queue."), bContainsTarget);
		const bool bContainsOther = TargetDeltasAfter->ContainsByPredicate([&OtherCoord](const FRTPSVoxelChunkDeltaPayload& P){ return P.ChunkCoord == OtherCoord; });
		TestTrue(TEXT("Other-chunk delta payload remains queued for target."), bContainsOther);
	}

	const TArray<FRTPSVoxelChunkStatePayload>* TargetFullsAfter = ChunkManager->PendingChunkStatePayloadsByClient.Find(TargetKey);
	TestNotNull(TEXT("FullOnlyCoord full payload remains queued for target after unrelated unsubscribe."), TargetFullsAfter);
	if (TargetFullsAfter != nullptr)
	{
		const bool bFullStillQueued = TargetFullsAfter->ContainsByPredicate([&FullOnlyCoord](const FRTPSVoxelChunkStatePayload& P){ return P.ChunkCoord == FullOnlyCoord; });
		TestTrue(TEXT("FullOnlyCoord full payload still queued."), bFullStillQueued);
	}

	const TWeakObjectPtr<ARTPSPlayerController> OtherKey(OtherController);
	const TArray<FRTPSVoxelChunkDeltaPayload>* OtherClientDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(OtherKey);
	TestNotNull(TEXT("Sibling client's delta queue is unaffected."), OtherClientDeltas);
	if (OtherClientDeltas != nullptr)
	{
		const bool bSiblingStillHasTarget = OtherClientDeltas->ContainsByPredicate([&TargetCoord](const FRTPSVoxelChunkDeltaPayload& P){ return P.ChunkCoord == TargetCoord; });
		TestTrue(TEXT("Sibling client still has delta payload for TargetCoord."), bSiblingStillHasTarget);
	}

	EditorWorld->DestroyActor(OtherController, false, false);
	EditorWorld->DestroyActor(TargetController, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelUnsubscribePlayerFromAllChunksRemovesQueuedDeltaPayloadsTest,
	"RTPS.VoxelAuthoring.Chunk.UnsubscribePlayerFromAllChunksRemovesQueuedDeltaPayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelUnsubscribePlayerFromAllChunksRemovesQueuedDeltaPayloadsTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* TargetController = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	ARTPSPlayerController* OtherController = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !TargetController || !OtherController)
	{
		AddError(TEXT("Unsubscribe-all delta cleanup test actor spawn failed."));
		if (OtherController) { EditorWorld->DestroyActor(OtherController, false, false); }
		if (TargetController) { EditorWorld->DestroyActor(TargetController, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector CoordA(80, 0, 0);
	const FIntVector CoordB(81, 0, 0);
	const FIntVector CoordC(82, 0, 0);

	ChunkManager->QueueChunkDeltaPayloadForClient(TargetController, MakePhase65DeltaPayload(CoordA, 0, 1, 8000), TEXT("AutomationUnsubAllDeltaA"));
	ChunkManager->QueueChunkDeltaPayloadForClient(TargetController, MakePhase65DeltaPayload(CoordB, 0, 1, 8100), TEXT("AutomationUnsubAllDeltaB"));
	ChunkManager->QueueChunkStatePayloadForClient(TargetController, MakePhase65FullPayload(CoordC, 1), TEXT("AutomationUnsubAllFullC"));

	ChunkManager->QueueChunkDeltaPayloadForClient(OtherController, MakePhase65DeltaPayload(CoordA, 0, 1, 8200), TEXT("AutomationUnsubAllDeltaSibling"));

	const TWeakObjectPtr<ARTPSPlayerController> TargetKey(TargetController);
	ChunkManager->ClientSubscribedChunks.FindOrAdd(TargetKey).Append({CoordA, CoordB, CoordC});
	ChunkManager->ChunkSubscribers.FindOrAdd(CoordA).Add(TargetKey);
	ChunkManager->ChunkSubscribers.FindOrAdd(CoordB).Add(TargetKey);
	ChunkManager->ChunkSubscribers.FindOrAdd(CoordC).Add(TargetKey);

	ChunkManager->UnsubscribePlayerFromAllChunks(TargetController, TEXT("AutomationUnsubAll"));

	TestFalse(TEXT("Target client removed from delta queue map."), ChunkManager->PendingChunkDeltaPayloadsByClient.Contains(TargetKey));
	TestFalse(TEXT("Target client removed from full payload queue map."), ChunkManager->PendingChunkStatePayloadsByClient.Contains(TargetKey));
	TestFalse(TEXT("Target client removed from ClientSubscribedChunks."), ChunkManager->ClientSubscribedChunks.Contains(TargetKey));

	const TWeakObjectPtr<ARTPSPlayerController> OtherKey(OtherController);
	const TArray<FRTPSVoxelChunkDeltaPayload>* SiblingDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(OtherKey);
	TestNotNull(TEXT("Sibling client's delta queue is preserved after unrelated unsubscribe-all."), SiblingDeltas);
	if (SiblingDeltas != nullptr)
	{
		TestEqual(TEXT("Sibling client's delta queue size unchanged."), SiblingDeltas->Num(), 1);
	}

	EditorWorld->DestroyActor(OtherController, false, false);
	EditorWorld->DestroyActor(TargetController, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelUnsubscribePlayerFromChunkRemovesFullAndDeltaQueuesForSameChunkTest,
	"RTPS.VoxelAuthoring.Chunk.UnsubscribePlayerFromChunkRemovesFullAndDeltaQueuesForSameChunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelUnsubscribePlayerFromChunkRemovesFullAndDeltaQueuesForSameChunkTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Unsubscribe combined cleanup test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const FIntVector TargetCoord(90, 0, 0);
	const FIntVector OtherDeltaCoord(91, 0, 0);
	const FIntVector OtherFullCoord(92, 0, 0);

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);

	// TargetCoord ends up with both a full payload (queued normally) and a stale delta entry (force-injected).
	// The delta would normally be dropped by supersede policy when a full is already queued — we inject it directly
	// to validate that unsubscribe purges any residue regardless of how it landed in the queue.
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(TargetCoord, 5), TEXT("AutomationUnsubCombinedFullTarget"));
	ChunkManager->PendingChunkDeltaPayloadsByClient.FindOrAdd(ControllerKey).Add(MakePhase65DeltaPayload(TargetCoord, 0, 1, 9000));

	// Unrelated chunks: one full-only and one delta-only, both must remain after partial unsubscribe.
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(OtherFullCoord, 6), TEXT("AutomationUnsubCombinedFullOther"));
	ChunkManager->QueueChunkDeltaPayloadForClient(Controller, MakePhase65DeltaPayload(OtherDeltaCoord, 0, 1, 9100), TEXT("AutomationUnsubCombinedDeltaOther"));

	ChunkManager->ClientSubscribedChunks.FindOrAdd(ControllerKey).Append({TargetCoord, OtherDeltaCoord, OtherFullCoord});
	ChunkManager->ChunkSubscribers.FindOrAdd(TargetCoord).Add(ControllerKey);
	ChunkManager->ChunkSubscribers.FindOrAdd(OtherDeltaCoord).Add(ControllerKey);
	ChunkManager->ChunkSubscribers.FindOrAdd(OtherFullCoord).Add(ControllerKey);

	ChunkManager->UnsubscribePlayerFromChunk(Controller, TargetCoord, TEXT("AutomationUnsubCombined"));

	const TArray<FRTPSVoxelChunkStatePayload>* PostFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Full payload queue retains entries after partial unsubscribe."), PostFulls);
	if (PostFulls != nullptr)
	{
		const bool bFullTargetGone = !PostFulls->ContainsByPredicate([&TargetCoord](const FRTPSVoxelChunkStatePayload& P){ return P.ChunkCoord == TargetCoord; });
		TestTrue(TEXT("Full payload for TargetCoord removed by unsubscribe."), bFullTargetGone);
		const bool bFullOtherKept = PostFulls->ContainsByPredicate([&OtherFullCoord](const FRTPSVoxelChunkStatePayload& P){ return P.ChunkCoord == OtherFullCoord; });
		TestTrue(TEXT("Full payload for OtherFullCoord retained."), bFullOtherKept);
	}

	const TArray<FRTPSVoxelChunkDeltaPayload>* PostDeltas = ChunkManager->PendingChunkDeltaPayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Delta payload queue retains entries after partial unsubscribe."), PostDeltas);
	if (PostDeltas != nullptr)
	{
		const bool bDeltaTargetGone = !PostDeltas->ContainsByPredicate([&TargetCoord](const FRTPSVoxelChunkDeltaPayload& P){ return P.ChunkCoord == TargetCoord; });
		TestTrue(TEXT("Stale delta payload for TargetCoord removed by unsubscribe."), bDeltaTargetGone);
		const bool bDeltaOtherKept = PostDeltas->ContainsByPredicate([&OtherDeltaCoord](const FRTPSVoxelChunkDeltaPayload& P){ return P.ChunkCoord == OtherDeltaCoord; });
		TestTrue(TEXT("Delta payload for OtherDeltaCoord retained."), bDeltaOtherKept);
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsNoneWhenNoBaselineTest,
	"RTPS.VoxelAuthoring.Chunk.SelectClientSubscribeKnownRevisionReturnsNoneWhenNoBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsNoneWhenNoBaselineTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("SelectKnownRevision no-baseline test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());

	const FIntVector TestChunkCoord(120, 0, 0);
	TestFalse(TEXT("LastAppliedRemoteRevisionByCoord has no baseline before test."), ChunkManager->LastAppliedRemoteRevisionByCoord.Contains(TestChunkCoord));
	TestFalse(TEXT("Marker is not set for this chunk."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	const int32 Selected = ChunkManager->SelectClientSubscribeKnownRevision(TestChunkCoord, 0);
	TestEqual(TEXT("No-baseline subscribe returns INDEX_NONE."), Selected, INDEX_NONE);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsLocalWhenBaselineKnownTest,
	"RTPS.VoxelAuthoring.Chunk.SelectClientSubscribeKnownRevisionReturnsLocalWhenBaselineKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsLocalWhenBaselineKnownTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("SelectKnownRevision baseline-known test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());

	const FIntVector TestChunkCoord(121, 0, 0);
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 5);
	TestFalse(TEXT("Marker not set for baseline-known case."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(TestChunkCoord));

	const int32 Selected = ChunkManager->SelectClientSubscribeKnownRevision(TestChunkCoord, 5);
	TestEqual(TEXT("Baseline-known subscribe returns LocalKnownRevision."), Selected, 5);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsNoneWhenMarkerSetTest,
	"RTPS.VoxelAuthoring.Chunk.SelectClientSubscribeKnownRevisionReturnsNoneWhenMarkerSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSelectClientSubscribeKnownRevisionReturnsNoneWhenMarkerSetTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("SelectKnownRevision marker test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());

	const FIntVector TestChunkCoord(122, 0, 0);
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(TestChunkCoord, 5);
	ChunkManager->ChunksNeedingFullSnapshotResync.Add(TestChunkCoord);

	const int32 Selected = ChunkManager->SelectClientSubscribeKnownRevision(TestChunkCoord, 5);
	TestEqual(TEXT("Marker overrides baseline and returns INDEX_NONE."), Selected, INDEX_NONE);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelSubscribeKnownRevisionBypassesKnownRevisionThrottleWhenNoBaselineTest,
	"RTPS.VoxelAuthoring.Chunk.SubscribeKnownRevisionBypassesKnownRevisionThrottleWhenNoBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelSubscribeKnownRevisionBypassesKnownRevisionThrottleWhenNoBaselineTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	if (!ChunkManager)
	{
		AddError(TEXT("SubscribeKnownRevision throttle bypass test ChunkManager spawn failed."));
		return false;
	}

	ChunkManager->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("ChunkManager fixture simulates a non-authority client."), ChunkManager->HasAuthority());

	const FIntVector NoBaselineCoord(130, 0, 0);
	// LastSubscribedKnownRevisionByCoord populated to simulate a stale throttle entry that
	// would normally short-circuit the subscribe path. With no-baseline detection the
	// throttle must be bypassed so the client can actually request a full snapshot.
	ChunkManager->LastSubscribedKnownRevisionByCoord.Add(NoBaselineCoord, 0);
	TestFalse(TEXT("No baseline entry exists for NoBaselineCoord."), ChunkManager->LastAppliedRemoteRevisionByCoord.Contains(NoBaselineCoord));
	TestFalse(TEXT("Marker is not required to trigger bypass."), ChunkManager->ChunksNeedingFullSnapshotResync.Contains(NoBaselineCoord));

	const bool bBypassNoBaseline = ChunkManager->ShouldBypassClientSubscribeRevisionThrottle(NoBaselineCoord);
	TestTrue(TEXT("Throttle bypass is true for no-baseline chunk regardless of LastSubscribedKnownRevision."), bBypassNoBaseline);

	const int32 EffectiveNoBaseline = ChunkManager->SelectClientSubscribeKnownRevision(NoBaselineCoord, 0);
	TestEqual(TEXT("EffectiveKnownRevision becomes INDEX_NONE for no-baseline chunk."), EffectiveNoBaseline, INDEX_NONE);

	// Sanity: chunk that has a baseline and no marker should NOT bypass throttle.
	const FIntVector BaselineCoord(131, 0, 0);
	ChunkManager->LastAppliedRemoteRevisionByCoord.Add(BaselineCoord, 3);
	const bool bBypassBaseline = ChunkManager->ShouldBypassClientSubscribeRevisionThrottle(BaselineCoord);
	TestFalse(TEXT("Throttle bypass is false when baseline exists and no marker."), bBypassBaseline);

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFlushSkipsClientWhenWithinMinIntervalEvenIfQueuedTest,
	"RTPS.VoxelAuthoring.Chunk.FlushSkipsClientWhenWithinMinIntervalEvenIfQueued",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFlushSkipsClientWhenWithinMinIntervalEvenIfQueuedTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Flush min-interval skip test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->MinChunkStateFlushIntervalSecondsPerClient = 1.0f;
	ChunkManager->MaxChunkStatePayloadsPerClientPerTick = 1;

	const FIntVector CoordA(140, 0, 0);
	const FIntVector CoordB(141, 0, 0);
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(CoordA, 1), TEXT("AutomationFlushMinIntervalA"));
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(CoordB, 2), TEXT("AutomationFlushMinIntervalB"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const double Now = FPlatformTime::Seconds();
	ChunkManager->LastChunkPayloadFlushTimeByClient.Add(ControllerKey, Now);

	const int32 SentPayloads = ChunkManager->FlushQueuedChunkStatePayloads(0.016f);
	TestEqual(TEXT("Flush sends 0 payloads when within min interval."), SentPayloads, 0);

	const TArray<FRTPSVoxelChunkStatePayload>* RemainingFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("Both queued payloads remain after deferred flush."), RemainingFulls);
	if (RemainingFulls != nullptr)
	{
		TestEqual(TEXT("Both payloads still queued."), RemainingFulls->Num(), 2);
	}

	TestTrue(TEXT("LastChunkPayloadFlushTimeByClient still tracks ClientKey."), ChunkManager->LastChunkPayloadFlushTimeByClient.Contains(ControllerKey));

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFlushAllowsClientAfterMinIntervalElapsedTest,
	"RTPS.VoxelAuthoring.Chunk.FlushAllowsClientAfterMinIntervalElapsed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFlushAllowsClientAfterMinIntervalElapsedTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Flush allow-after-elapsed test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->MinChunkStateFlushIntervalSecondsPerClient = 0.05f;
	ChunkManager->MaxChunkStatePayloadsPerClientPerTick = 1;

	const FIntVector CoordA(150, 0, 0);
	const FIntVector CoordB(151, 0, 0);
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(CoordA, 1), TEXT("AutomationFlushElapsedA"));
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(CoordB, 2), TEXT("AutomationFlushElapsedB"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);
	const double NowMinusOneSecond = FPlatformTime::Seconds() - 1.0;
	ChunkManager->LastChunkPayloadFlushTimeByClient.Add(ControllerKey, NowMinusOneSecond);

	const int32 SentPayloads = ChunkManager->FlushQueuedChunkStatePayloads(0.016f);
	TestEqual(TEXT("Flush sends 1 payload after min interval elapsed."), SentPayloads, 1);

	const TArray<FRTPSVoxelChunkStatePayload>* RemainingFulls = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerKey);
	TestNotNull(TEXT("One payload remains after partial flush."), RemainingFulls);
	if (RemainingFulls != nullptr)
	{
		TestEqual(TEXT("One payload remains queued."), RemainingFulls->Num(), 1);
	}

	const double* UpdatedFlushTime = ChunkManager->LastChunkPayloadFlushTimeByClient.Find(ControllerKey);
	TestNotNull(TEXT("LastChunkPayloadFlushTimeByClient still tracks client after flush."), UpdatedFlushTime);
	if (UpdatedFlushTime != nullptr)
	{
		TestTrue(TEXT("LastChunkPayloadFlushTimeByClient updated to a more recent time."), *UpdatedFlushTime > NowMinusOneSecond);
	}

	EditorWorld->DestroyActor(Controller, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelFlushDoesNotPenalizeOtherClientsWhenOneClientThrottledTest,
	"RTPS.VoxelAuthoring.Chunk.FlushDoesNotPenalizeOtherClientsWhenOneClientThrottled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelFlushDoesNotPenalizeOtherClientsWhenOneClientThrottledTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* ControllerA = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	ARTPSPlayerController* ControllerB = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !ControllerA || !ControllerB)
	{
		AddError(TEXT("Per-client throttle isolation test actor spawn failed."));
		if (ControllerB) { EditorWorld->DestroyActor(ControllerB, false, false); }
		if (ControllerA) { EditorWorld->DestroyActor(ControllerA, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	ChunkManager->MinChunkStateFlushIntervalSecondsPerClient = 1.0f;
	ChunkManager->MaxChunkStatePayloadsPerClientPerTick = 1;

	const FIntVector CoordA(160, 0, 0);
	const FIntVector CoordB(161, 0, 0);
	ChunkManager->QueueChunkStatePayloadForClient(ControllerA, MakePhase65FullPayload(CoordA, 1), TEXT("AutomationFlushIsolationA"));
	ChunkManager->QueueChunkStatePayloadForClient(ControllerB, MakePhase65FullPayload(CoordB, 1), TEXT("AutomationFlushIsolationB"));

	const TWeakObjectPtr<ARTPSPlayerController> ControllerAKey(ControllerA);
	const TWeakObjectPtr<ARTPSPlayerController> ControllerBKey(ControllerB);
	ChunkManager->LastChunkPayloadFlushTimeByClient.Add(ControllerAKey, FPlatformTime::Seconds());
	// ControllerB has no last flush timestamp.

	const int32 SentPayloads = ChunkManager->FlushQueuedChunkStatePayloads(0.016f);
	TestEqual(TEXT("Flush sends 1 payload total (ClientA throttled, ClientB allowed)."), SentPayloads, 1);

	const TArray<FRTPSVoxelChunkStatePayload>* RemainingFullsA = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerAKey);
	TestNotNull(TEXT("ClientA payload still queued because of throttle."), RemainingFullsA);
	if (RemainingFullsA != nullptr)
	{
		TestEqual(TEXT("ClientA still has queued payload."), RemainingFullsA->Num(), 1);
	}

	const TArray<FRTPSVoxelChunkStatePayload>* RemainingFullsB = ChunkManager->PendingChunkStatePayloadsByClient.Find(ControllerBKey);
	TestTrue(TEXT("ClientB queue empty after flush."), RemainingFullsB == nullptr || RemainingFullsB->Num() == 0);

	TestTrue(TEXT("ClientB now has a recorded last flush time."), ChunkManager->LastChunkPayloadFlushTimeByClient.Contains(ControllerBKey));

	EditorWorld->DestroyActor(ControllerB, false, false);
	EditorWorld->DestroyActor(ControllerA, false, false);
	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTPSVoxelMinChunkStateFlushIntervalCleanedUpOnStaleClientTest,
	"RTPS.VoxelAuthoring.Chunk.MinChunkStateFlushIntervalCleanedUpOnStaleClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTPSVoxelMinChunkStateFlushIntervalCleanedUpOnStaleClientTest::RunTest(const FString& Parameters)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		AddError(TEXT("EditorWorld is null."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelChunkManager* ChunkManager = EditorWorld->SpawnActor<AVoxelChunkManager>(AVoxelChunkManager::StaticClass(), FTransform::Identity, SpawnParams);
	ARTPSPlayerController* Controller = SpawnVoxelAutomationPlayerController(EditorWorld, SpawnParams);
	if (!ChunkManager || !Controller)
	{
		AddError(TEXT("Stale-client cleanup test actor spawn failed."));
		if (Controller) { EditorWorld->DestroyActor(Controller, false, false); }
		if (ChunkManager) { EditorWorld->DestroyActor(ChunkManager, false, false); }
		return false;
	}

	const TWeakObjectPtr<ARTPSPlayerController> ControllerKey(Controller);

	const FIntVector CoordA(170, 0, 0);
	ChunkManager->QueueChunkStatePayloadForClient(Controller, MakePhase65FullPayload(CoordA, 1), TEXT("AutomationStaleCleanup"));
	ChunkManager->LastChunkPayloadFlushTimeByClient.Add(ControllerKey, FPlatformTime::Seconds());
	ChunkManager->ClientSubscribedChunks.FindOrAdd(ControllerKey).Add(CoordA);

	TestTrue(TEXT("LastChunkPayloadFlushTimeByClient seeded for client."), ChunkManager->LastChunkPayloadFlushTimeByClient.Contains(ControllerKey));

	// Make controller stale.
	EditorWorld->DestroyActor(Controller, false, false);
	Controller = nullptr;

	// Flush invalid-client branch should clean up the timestamp.
	ChunkManager->FlushQueuedChunkStatePayloads(0.016f);

	TestFalse(TEXT("LastChunkPayloadFlushTimeByClient cleared when client became invalid."), ChunkManager->LastChunkPayloadFlushTimeByClient.Contains(ControllerKey));
	TestFalse(TEXT("PendingChunkStatePayloadsByClient cleared for invalid client."), ChunkManager->PendingChunkStatePayloadsByClient.Contains(ControllerKey));
	TestFalse(TEXT("PendingChunkDeltaPayloadsByClient cleared for invalid client."), ChunkManager->PendingChunkDeltaPayloadsByClient.Contains(ControllerKey));

	EditorWorld->DestroyActor(ChunkManager, false, false);
	return true;
}
#endif
