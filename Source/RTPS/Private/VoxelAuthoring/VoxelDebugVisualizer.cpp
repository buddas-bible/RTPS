#include "VoxelAuthoring/VoxelDebugVisualizer.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogRTPSVoxelDebug);

UVoxelDebugVisualizer::UVoxelDebugVisualizer()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVoxelDebugVisualizer::OnRegister()
{
	Super::OnRegister();
	EnsureHISMCreated();
}

void UVoxelDebugVisualizer::EnsureHISMCreated()
{
	if (SphereHISM)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}

	SphereHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(
		Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(),
		MakeUniqueObjectName(Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(), TEXT("VoxelDebugSphereHISM")));
	SphereHISM->RegisterComponent();
	SphereHISM->AttachToComponent(
		Owner->GetRootComponent(),
		FAttachmentTransformRules::KeepRelativeTransform);

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (IsValid(SphereMesh))
	{
		SphereHISM->SetStaticMesh(SphereMesh);
	}

	SphereHISM->NumCustomDataFloats = 1;
	SphereHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SphereHISM->SetCastShadow(false);
	SphereHISM->SetVisibility(false);
}

void UVoxelDebugVisualizer::EnsureDynamicMI()
{
	if (DynamicMI)
	{
		return;
	}
	if (!IsValid(DebugSphereMaterial))
	{
		UE_LOG(LogRTPSVoxelDebug, Warning,
			TEXT("UVoxelDebugVisualizer: DebugSphereMaterial이 할당되지 않았습니다. "
			     "Details 패널에서 M_VoxelDebugSphere를 할당하세요."));
		return;
	}
	DynamicMI = UMaterialInstanceDynamic::Create(DebugSphereMaterial, this);
	if (SphereHISM)
	{
		SphereHISM->SetMaterial(0, DynamicMI);
	}
}

void UVoxelDebugVisualizer::RebuildVisualizer(
	const TArray<float>& DensityGrid,
	FIntVector GridDimensions,
	float CellSize)
{
	EnsureHISMCreated();
	if (!SphereHISM)
	{
		UE_LOG(LogRTPSVoxelDebug, Warning, TEXT("RebuildVisualizer: SphereHISM이 null입니다."));
		return;
	}

	SphereHISM->ClearInstances();

	if (!bVisualizerEnabled)
	{
		SphereHISM->SetVisibility(false);
		return;
	}

	const int32 TotalCells = GridDimensions.X * GridDimensions.Y * GridDimensions.Z;
	if (DensityGrid.Num() < TotalCells)
	{
		UE_LOG(LogRTPSVoxelDebug, Warning,
			TEXT("RebuildVisualizer: DensityGrid.Num()=%d < expected %d"),
			DensityGrid.Num(), TotalCells);
		return;
	}

	EnsureDynamicMI();

	// 래티스 정점: (GridDimensions + 1)^3
	const FIntVector SampleDim(
		GridDimensions.X + 1,
		GridDimensions.Y + 1,
		GridDimensions.Z + 1);

	// 엔진 기본 Sphere 메시 반지름 = 50cm
	const float SphereMeshRadius = 50.f;
	const float DesiredRadius = CellSize * SphereScale * 0.5f;
	const float UniformScale = DesiredRadius / SphereMeshRadius;

	const int32 TotalLatticePoints = SampleDim.X * SampleDim.Y * SampleDim.Z;

	TArray<FTransform> Transforms;
	TArray<float> CustomDataValues;
	Transforms.Reserve(TotalLatticePoints);
	CustomDataValues.Reserve(TotalLatticePoints);

	for (int32 SZ = 0; SZ < SampleDim.Z; ++SZ)
	{
		for (int32 SY = 0; SY < SampleDim.Y; ++SY)
		{
			for (int32 SX = 0; SX < SampleDim.X; ++SX)
			{
				// 래티스 정점 밀도: 인접 최대 8개 셀 평균
				float Sum = 0.f;
				int32 Count = 0;
				for (int32 DZ = -1; DZ <= 0; ++DZ)
				{
					for (int32 DY = -1; DY <= 0; ++DY)
					{
						for (int32 DX = -1; DX <= 0; ++DX)
						{
							const int32 CX = SX + DX;
							const int32 CY = SY + DY;
							const int32 CZ = SZ + DZ;
							if (CX >= 0 && CX < GridDimensions.X &&
								CY >= 0 && CY < GridDimensions.Y &&
								CZ >= 0 && CZ < GridDimensions.Z)
							{
								Sum += DensityGrid[CX + GridDimensions.X * (CY + GridDimensions.Y * CZ)];
								++Count;
							}
						}
					}
				}
				const float LatticeDensity = (Count > 0) ? (Sum / static_cast<float>(Count)) : 0.f;

				const FVector LocalPos(
					static_cast<float>(SX) * CellSize,
					static_cast<float>(SY) * CellSize,
					static_cast<float>(SZ) * CellSize);

				Transforms.Add(FTransform(
					FQuat::Identity,
					LocalPos,
					FVector(UniformScale)));
				CustomDataValues.Add(LatticeDensity);
			}
		}
	}

	SphereHISM->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/false);

	for (int32 i = 0; i < CustomDataValues.Num(); ++i)
	{
		SphereHISM->SetCustomDataValue(i, 0, CustomDataValues[i], /*bMarkRenderStateDirty=*/false);
	}
	SphereHISM->MarkRenderStateDirty();
	SphereHISM->SetVisibility(true);

	ApplyThreshold();

	UE_LOG(LogRTPSVoxelDebug, Log,
		TEXT("RebuildVisualizer: %d 래티스 정점 시각화 완료 (Threshold=%.2f)"),
		CustomDataValues.Num(), Threshold);
}

void UVoxelDebugVisualizer::ApplyThreshold()
{
	EnsureDynamicMI();
	if (DynamicMI)
	{
		DynamicMI->SetScalarParameterValue(TEXT("Threshold"), Threshold);
	}
}

void UVoxelDebugVisualizer::ClearVisualizer()
{
	if (SphereHISM)
	{
		SphereHISM->ClearInstances();
		SphereHISM->SetVisibility(false);
	}
}
