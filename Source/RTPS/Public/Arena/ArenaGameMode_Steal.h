#pragma once

#include "CoreMinimal.h"
#include "Game/RTPSGameMode.h"
#include "ArenaGameMode_Steal.generated.h"

class APlayerStart;
class AStaticMeshActor;
class UStaticMesh;

UCLASS()
class RTPS_API AArenaGameMode_Steal : public ARTPSGameMode
{
	GENERATED_BODY()

public:
	AArenaGameMode_Steal();

	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Debug")
	bool bBuildTemporaryArenaAtRuntime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Temporary")
	FVector2D FloorSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Temporary")
	float FloorThickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Temporary")
	float WallHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Temporary")
	float WallThickness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Temporary")
	FVector CentralPlatformExtent;

private:
	void EnsureTemporaryArenaBuilt();
	UStaticMesh* LoadArenaCubeMesh() const;
	AStaticMeshActor* SpawnArenaCube(UStaticMesh* CubeMesh, const FVector& Location, const FVector& Scale, const TCHAR* DebugName);
	void BuildSpawnPoints();
	FTransform MakeSpawnTransform(const FVector& Location) const;
	void AssignTeamForPlayer(APlayerController* PlayerController);
	int32 GetTeamIdForController(const AController* Player) const;
	APlayerStart* FindTaggedPlayerStartForTeam(int32 TeamId);
	FName GetPlayerStartTagForTeam(int32 TeamId) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AStaticMeshActor>> SpawnedArenaActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<APlayerStart>> ArenaPlayerStarts;

	TMap<int32, int32> NextTeamSpawnIndexByTeam;
	int32 NextSpawnIndex;
	int32 NextTeamAssignmentIndex;
	bool bTemporaryArenaBuilt;
};
