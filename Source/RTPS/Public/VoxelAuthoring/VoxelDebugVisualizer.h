#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelDebugVisualizer.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

RTPS_API DECLARE_LOG_CATEGORY_EXTERN(LogRTPSVoxelDebug, Log, All);

/**
 * 밀도 격자 시각화 컴포넌트.
 * 각 래티스 정점에 HISM 구 메시를 배치하고, PerInstanceCustomData[0] = density 값을 전달.
 * M_VoxelDebugSphere 머티리얼이 density → color gradient + threshold 경계 하이라이트를 출력.
 */
UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelDebugVisualizer : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelDebugVisualizer();

	/** 시각화 표시 여부 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bVisualizerEnabled = false;

	/** 구 메시 스케일 (CellSize 대비 비율, 0.12 = 12%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float SphereScale = 0.12f;

	/** M_VoxelDebugSphere 머티리얼 (에디터에서 할당) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	TObjectPtr<UMaterialInterface> DebugSphereMaterial;

	/** IsoLevel 슬라이더 (0~1). 변경 후 ApplyThreshold 호출 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Threshold = 0.5f;

	/** 
	 * 밀도 그리드로 시각화 재구축.
	 * @param DensityGrid   RuntimeAuthoringVolume.DensityGrid (GridDimensions.X*Y*Z 크기)
	 * @param GridDimensions 셀 격자 크기 (래티스는 +1 씩)
	 * @param CellSize      셀 하나의 크기 (cm)
	 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void RebuildVisualizer(
		const TArray<float>& DensityGrid,
		FIntVector GridDimensions,
		float CellSize);

	/** Threshold 변경 — 머티리얼 파라미터만 업데이트 (재구축 없음) */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Debug")
	void ApplyThreshold();

	/** 모든 인스턴스 삭제 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Debug")
	void ClearVisualizer();

	virtual void OnRegister() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SphereHISM;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMI;

	void EnsureHISMCreated();
	void EnsureDynamicMI();
};
