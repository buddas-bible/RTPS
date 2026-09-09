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

	WireframeLines->DrawLine(W(FVector(Min.X, Min.Y, Min.Z)), W(FVector(Max.X, Min.Y, Min.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Max.X, Min.Y, Min.Z)), W(FVector(Max.X, Max.Y, Min.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Max.X, Max.Y, Min.Z)), W(FVector(Min.X, Max.Y, Min.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Min.X, Max.Y, Min.Z)), W(FVector(Min.X, Min.Y, Min.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Min.X, Min.Y, Max.Z)), W(FVector(Max.X, Min.Y, Max.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Max.X, Min.Y, Max.Z)), W(FVector(Max.X, Max.Y, Max.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Max.X, Max.Y, Max.Z)), W(FVector(Min.X, Max.Y, Max.Z)), Color, SDPG_World);
	WireframeLines->DrawLine(W(FVector(Min.X, Max.Y, Max.Z)), W(FVector(Min.X, Min.Y, Max.Z)), Color, SDPG_World);
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
