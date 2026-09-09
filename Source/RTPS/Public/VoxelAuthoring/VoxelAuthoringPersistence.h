#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelAuthoring/VoxelDensitySnapshot.h"
#include "VoxelAuthoringPersistence.generated.h"

UCLASS(ClassGroup = "Voxel", meta = (BlueprintSpawnableComponent))
class RTPS_API UVoxelAuthoringPersistence : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel|IO")
	FString LastIOStatus;

	bool ExportToFile(const FVoxelDensitySnapshot& Snapshot, const FString& FilePath);
	bool ImportFromFile(const FString& FilePath, FVoxelDensitySnapshot& OutSnapshot);
};
