#include "VoxelAuthoring/RuntimeAuthoringVolume.h"
#include "VoxelAuthoring/VoxelDensityGrid.h"
#include "VoxelAuthoring/VoxelVisualizationManager.h"
#include "VoxelAuthoring/VoxelAuthoringPersistence.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/LineBatchComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogRTPSVoxelAuthoring);

ARuntimeAuthoringVolume::ARuntimeAuthoringVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	BlockHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BlockHISM"));
	BlockHISM->SetupAttachment(SceneRoot);
	BlockHISM->SetMobility(EComponentMobility::Movable);
	BlockHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlockHISM->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshRef(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshRef.Succeeded()) { BlockHISM->SetStaticMesh(CubeMeshRef.Object); }

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialRef(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMaterialRef.Succeeded()) { BlockHISM->SetMaterial(0, BasicMaterialRef.Object); }

	MarchingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MarchingMesh"));
	MarchingMesh->SetupAttachment(SceneRoot);
	MarchingMesh->SetMobility(EComponentMobility::Movable);
	MarchingMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MarchingMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	MarchingMesh->bUseAsyncCooking = true;
	MarchingMesh->SetVisibility(false);

	WireframeLines = CreateDefaultSubobject<ULineBatchComponent>(TEXT("WireframeLines"));
	WireframeLines->SetupAttachment(SceneRoot);
	WireframeLines->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DebugVisualizer = CreateDefaultSubobject<UVoxelDebugVisualizer>(TEXT("DebugVisualizer"));

	DensityGridComp = CreateDefaultSubobject<UVoxelDensityGrid>(TEXT("DensityGrid"));
	VisManagerComp = CreateDefaultSubobject<UVoxelVisualizationManager>(TEXT("VisManager"));
	PersistenceComp = CreateDefaultSubobject<UVoxelAuthoringPersistence>(TEXT("Persistence"));
}

void ARuntimeAuthoringVolume::BeginPlay()
{
	Super::BeginPlay();

	VisManagerComp->InitializeRenderTargets(BlockHISM, MarchingMesh, WireframeLines, DebugVisualizer);

	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		EnableInput(PC);
		if (InputComponent)
		{
			InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode1);
			InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode2);
			InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode3);
			InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ARuntimeAuthoringVolume::SetMode4);
			InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ARuntimeAuthoringVolume::DecreaseSurfaceLevel);
			InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ARuntimeAuthoringVolume::IncreaseSurfaceLevel);
		}
	}
}

#if WITH_EDITOR
void ARuntimeAuthoringVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();

	if (PropName == GET_MEMBER_NAME_CHECKED(ARuntimeAuthoringVolume, IsoLevel))
	{
		if (DebugVisualizer)
		{
			DebugVisualizer->Threshold = IsoLevel;
			DebugVisualizer->ApplyThreshold();
		}
		ApplyCurrentVisMode();
		return;
	}

	static const FName NoiseParamNames[] = {
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseScale),
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseWeight),
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseOctaves),
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseLacunarity),
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoisePersistence),
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseFloorOffset),
		GET_MEMBER_NAME_CHECKED(FVoxelNoiseParams, NoiseSeed),
	};

	for (const FName& NoiseProp : NoiseParamNames)
	{
		if (PropName == NoiseProp)
		{
			FillNoiseDensity();
			ApplyCurrentVisMode();
			return;
		}
	}
}
#endif

void ARuntimeAuthoringVolume::ApplyCurrentVisMode()
{
	if (DensityGridComp && VisManagerComp)
	{
		VisManagerComp->ApplyMode(
			DensityGridComp->GetGrid(),
			DensityGridComp->GridDimensions,
			DensityGridComp->CellSize,
			IsoLevel);
	}
}

void ARuntimeAuthoringVolume::HideAllVisualizations()
{
	if (VisManagerComp) { VisManagerComp->HideAll(); }
}

void ARuntimeAuthoringVolume::FillNoiseDensity()
{
	if (DensityGridComp)
	{
		DensityGridComp->FillNoise(NoiseParams);
		UE_LOG(LogRTPSVoxelAuthoring, Log,
			TEXT("FillNoiseDensity: Dim=%dx%dx%d Scale=%.4f Seed=%d"),
			DensityGridComp->GridDimensions.X,
			DensityGridComp->GridDimensions.Y,
			DensityGridComp->GridDimensions.Z,
			NoiseParams.NoiseScale,
			NoiseParams.NoiseSeed);
	}
}

void ARuntimeAuthoringVolume::ClearDensity()
{
	if (DensityGridComp) { DensityGridComp->Clear(); }
}

void ARuntimeAuthoringVolume::ApplyDebugSphereFill()
{
	FillSphereDensity(DebugSphereCenter, DebugSphereRadius, DebugSphereValue);
}

void ARuntimeAuthoringVolume::FillSphereDensity(FVector LocalCenter, float Radius, float Value)
{
	if (DensityGridComp) { DensityGridComp->FillSphere(LocalCenter, Radius, Value); }
}

void ARuntimeAuthoringVolume::ExportDensityToFile()
{
	if (!DensityGridComp || !PersistenceComp) { return; }
	const FVoxelDensitySnapshot Snap = DensityGridComp->TakeSnapshot(IsoLevel);
	PersistenceComp->ExportToFile(Snap, DensityFilePath);
}

void ARuntimeAuthoringVolume::ImportDensityFromFile()
{
	if (!DensityGridComp || !PersistenceComp) { return; }
	FVoxelDensitySnapshot Snap;
	if (PersistenceComp->ImportFromFile(DensityFilePath, Snap))
	{
		IsoLevel = Snap.IsoLevel;
		DensityGridComp->ApplySnapshot(Snap);
	}
}

void ARuntimeAuthoringVolume::RebuildAuthoringMesh() { ApplyCurrentVisMode(); }

void ARuntimeAuthoringVolume::ClearAuthoringMesh()
{
	HideAllVisualizations();
	if (BlockHISM) { BlockHISM->ClearInstances(); }
}

void ARuntimeAuthoringVolume::RebuildDebugVisualizer()
{
	if (DensityGridComp && VisManagerComp)
	{
		VisManagerComp->HideAll();
		VisManagerComp->ApplyMode(
			DensityGridComp->GetGrid(),
			DensityGridComp->GridDimensions,
			DensityGridComp->CellSize,
			IsoLevel);
	}
}

void ARuntimeAuthoringVolume::ApplyDebugVisualizerThreshold()
{
	if (DebugVisualizer)
	{
		DebugVisualizer->Threshold = IsoLevel;
		DebugVisualizer->ApplyThreshold();
	}
}

void ARuntimeAuthoringVolume::ClearDebugVisualizer()
{
	if (DebugVisualizer) { DebugVisualizer->ClearVisualizer(); }
}

void ARuntimeAuthoringVolume::SetMode1()
{
	if (DensityGridComp && VisManagerComp)
		VisManagerComp->SetMode(EVoxelVisMode::Mode1_WireframeVoxels, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::SetMode2()
{
	if (DensityGridComp && VisManagerComp)
		VisManagerComp->SetMode(EVoxelVisMode::Mode2_DensitySpheres, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::SetMode3()
{
	if (DensityGridComp && VisManagerComp)
		VisManagerComp->SetMode(EVoxelVisMode::Mode3_MarchingCubes, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::SetMode4()
{
	if (DensityGridComp && VisManagerComp)
		VisManagerComp->SetMode(EVoxelVisMode::Mode4_Interactive, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::IncreaseSurfaceLevel()
{
	if (DensityGridComp && VisManagerComp)
		VisManagerComp->AdjustSurfaceLevel(+0.05f, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}

void ARuntimeAuthoringVolume::DecreaseSurfaceLevel()
{
	if (DensityGridComp && VisManagerComp)
		VisManagerComp->AdjustSurfaceLevel(-0.05f, DensityGridComp->GetGrid(), DensityGridComp->GridDimensions, DensityGridComp->CellSize, IsoLevel);
}