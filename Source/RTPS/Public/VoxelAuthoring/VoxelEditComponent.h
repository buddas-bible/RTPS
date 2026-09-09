#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelAuthoring/VoxelBrush.h"
#include "VoxelEditComponent.generated.h"

class AVoxelChunkManager;

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelEditComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelEditComponent();

	UPROPERTY(EditAnywhere, Category = "Voxel|Brush", meta = (ClampMin = "1.0"))
	float BrushRadius = 200.f;

	UPROPERTY(EditAnywhere, Category = "Voxel|Brush", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float BrushStrength = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Voxel|Brush")
	EVoxelBrushMode BrushMode = EVoxelBrushMode::Add;

	UPROPERTY(EditAnywhere, Category = "Voxel|Trace", meta = (ClampMin = "1.0"))
	float TraceDistance = 2000.f;

	UPROPERTY(EditInstanceOnly, Category = "Voxel")
	TObjectPtr<AVoxelChunkManager> ChunkManager = nullptr;

	void RequestBrushAtScreenCenter();

protected:
	virtual void BeginPlay() override;

private:
	bool TryLineTrace(FVector& OutHitWorldPos);
};