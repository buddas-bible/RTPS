#include "Arena/ArenaGameMode_Steal.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerStart.h"
#include "State/RTPSPlayerState.h"
#include "UObject/SoftObjectPath.h"

namespace
{
constexpr int32 ArenaTeamCount = 2;
}

AArenaGameMode_Steal::AArenaGameMode_Steal()
	: bBuildTemporaryArenaAtRuntime(false)
	, FloorSize(3000.f, 3000.f)
	, FloorThickness(50.f)
	, WallHeight(300.f)
	, WallThickness(100.f)
	, CentralPlatformExtent(300.f, 300.f, 100.f)
	, NextSpawnIndex(0)
	, NextTeamAssignmentIndex(0)
	, bTemporaryArenaBuilt(false)
{
}

void AArenaGameMode_Steal::BeginPlay()
{
	// Keep the shared gameplay class setup from ARTPSGameMode, but bypass quest BeginPlay behavior for this arena scaffold.
	AGameMode::BeginPlay();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ArenaGameMode_Steal initialized. Authority=%d BuildTemporaryArena=%d"),
		HasAuthority() ? 1 : 0,
		bBuildTemporaryArenaAtRuntime ? 1 : 0);

	if (HasAuthority() && bBuildTemporaryArenaAtRuntime)
	{
		EnsureTemporaryArenaBuilt();
	}
}

void AArenaGameMode_Steal::PostLogin(APlayerController* NewPlayer)
{
	AssignTeamForPlayer(NewPlayer);

	Super::PostLogin(NewPlayer);
}

AActor* AArenaGameMode_Steal::ChoosePlayerStart_Implementation(AController* Player)
{
	if (APlayerController* PlayerController = Cast<APlayerController>(Player))
	{
		AssignTeamForPlayer(PlayerController);
	}

	const int32 TeamId = GetTeamIdForController(Player);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Arena PlayerStart requested. Player=%s TeamId=%d"),
		Player ? *Player->GetName() : TEXT("None"),
		TeamId);

	if (APlayerStart* TeamStart = FindTaggedPlayerStartForTeam(TeamId))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Arena team PlayerStart selected. Player=%s TeamId=%d PlayerStart=%s PlayerStartTag=%s"),
			Player ? *Player->GetName() : TEXT("None"),
			TeamId,
			*TeamStart->GetName(),
			*TeamStart->PlayerStartTag.ToString());
		return TeamStart;
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[RTPSValidation] Arena team PlayerStart fallback. Player=%s TeamId=%d ExpectedTag=%s Reason=No matching map-authored PlayerStart found"),
		Player ? *Player->GetName() : TEXT("None"),
		TeamId,
		*GetPlayerStartTagForTeam(TeamId).ToString());

	if (!bBuildTemporaryArenaAtRuntime)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	EnsureTemporaryArenaBuilt();

	if (ArenaPlayerStarts.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Arena spawn transform unavailable. Player=%s"), Player ? *Player->GetName() : TEXT("None"));
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	const int32 SpawnIndex = NextSpawnIndex % ArenaPlayerStarts.Num();
	NextSpawnIndex = (NextSpawnIndex + 1) % ArenaPlayerStarts.Num();

	APlayerStart* SelectedStart = ArenaPlayerStarts[SpawnIndex].Get();
	const FVector SpawnLocation = SelectedStart ? SelectedStart->GetActorLocation() : FVector::ZeroVector;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Arena spawn transform selected. Player=%s SpawnIndex=%d Location=%s"),
		Player ? *Player->GetName() : TEXT("None"),
		SpawnIndex,
		*SpawnLocation.ToString());

	return SelectedStart ? SelectedStart : Super::ChoosePlayerStart_Implementation(Player);
}

void AArenaGameMode_Steal::AssignTeamForPlayer(APlayerController* PlayerController)
{
	ARTPSPlayerState* RTPSPlayerState = PlayerController ? PlayerController->GetPlayerState<ARTPSPlayerState>() : nullptr;
	if (RTPSPlayerState == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Arena team assignment skipped. PlayerController=%s Reason=Missing RTPSPlayerState"),
			PlayerController ? *PlayerController->GetName() : TEXT("None"));
		return;
	}

	if (RTPSPlayerState->GetTeamId() != INDEX_NONE)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Arena team assignment preserved. PlayerController=%s PlayerState=%s TeamId=%d"),
			*PlayerController->GetName(),
			*RTPSPlayerState->GetName(),
			RTPSPlayerState->GetTeamId());
		return;
	}

	const int32 AssignedTeamId = NextTeamAssignmentIndex % ArenaTeamCount;
	++NextTeamAssignmentIndex;
	RTPSPlayerState->SetTeamId(AssignedTeamId);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Arena team assigned. PlayerController=%s PlayerState=%s TeamId=%d AssignmentIndex=%d"),
		*PlayerController->GetName(),
		*RTPSPlayerState->GetName(),
		AssignedTeamId,
		NextTeamAssignmentIndex - 1);
}

int32 AArenaGameMode_Steal::GetTeamIdForController(const AController* Player) const
{
	const ARTPSPlayerState* RTPSPlayerState = Player ? Player->GetPlayerState<ARTPSPlayerState>() : nullptr;
	return RTPSPlayerState ? RTPSPlayerState->GetTeamId() : INDEX_NONE;
}

APlayerStart* AArenaGameMode_Steal::FindTaggedPlayerStartForTeam(int32 TeamId)
{
	UWorld* World = GetWorld();
	if (World == nullptr || TeamId == INDEX_NONE)
	{
		return nullptr;
	}

	const FName DesiredTag = GetPlayerStartTagForTeam(TeamId);
	TArray<APlayerStart*> MatchingStarts;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		APlayerStart* PlayerStart = *It;
		if (PlayerStart != nullptr && PlayerStart->PlayerStartTag == DesiredTag)
		{
			MatchingStarts.Add(PlayerStart);
		}
	}

	if (MatchingStarts.Num() == 0)
	{
		return nullptr;
	}

	int32& NextTeamSpawnIndex = NextTeamSpawnIndexByTeam.FindOrAdd(TeamId);
	const int32 SelectedIndex = NextTeamSpawnIndex % MatchingStarts.Num();
	NextTeamSpawnIndex = (NextTeamSpawnIndex + 1) % MatchingStarts.Num();
	return MatchingStarts[SelectedIndex];
}

FName AArenaGameMode_Steal::GetPlayerStartTagForTeam(int32 TeamId) const
{
	if (TeamId == 0)
	{
		return FName(TEXT("Team0"));
	}

	if (TeamId == 1)
	{
		return FName(TEXT("Team1"));
	}

	return NAME_None;
}

void AArenaGameMode_Steal::EnsureTemporaryArenaBuilt()
{
	if (bTemporaryArenaBuilt)
	{
		return;
	}

	bTemporaryArenaBuilt = true;
	BuildSpawnPoints();

	UStaticMesh* CubeMesh = LoadArenaCubeMesh();
	if (CubeMesh == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Temporary arena cube mesh load failed. Path=/Engine/BasicShapes/Cube.Cube"));
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Temporary arena spawned. FloorScale=%s WallCount=%d PlatformAt=%s"),
			*FVector::ZeroVector.ToString(),
			0,
			*FVector::ZeroVector.ToString());
		return;
	}

	const FVector FloorScale(FloorSize.X / 100.f, FloorSize.Y / 100.f, FloorThickness / 100.f);
	SpawnArenaCube(CubeMesh, FVector(0.f, 0.f, -FloorThickness * 0.5f), FloorScale, TEXT("TemporaryArena_Floor"));

	int32 WallCount = 0;
	const float HalfFloorX = FloorSize.X * 0.5f;
	const float HalfFloorY = FloorSize.Y * 0.5f;
	const float WallZ = WallHeight * 0.5f;
	const FVector LongWallScale((FloorSize.X + (WallThickness * 2.f)) / 100.f, WallThickness / 100.f, WallHeight / 100.f);
	const FVector ShortWallScale(WallThickness / 100.f, FloorSize.Y / 100.f, WallHeight / 100.f);

	WallCount += SpawnArenaCube(CubeMesh, FVector(0.f, HalfFloorY + (WallThickness * 0.5f), WallZ), LongWallScale, TEXT("TemporaryArena_Wall_North")) ? 1 : 0;
	WallCount += SpawnArenaCube(CubeMesh, FVector(0.f, -HalfFloorY - (WallThickness * 0.5f), WallZ), LongWallScale, TEXT("TemporaryArena_Wall_South")) ? 1 : 0;
	WallCount += SpawnArenaCube(CubeMesh, FVector(HalfFloorX + (WallThickness * 0.5f), 0.f, WallZ), ShortWallScale, TEXT("TemporaryArena_Wall_East")) ? 1 : 0;
	WallCount += SpawnArenaCube(CubeMesh, FVector(-HalfFloorX - (WallThickness * 0.5f), 0.f, WallZ), ShortWallScale, TEXT("TemporaryArena_Wall_West")) ? 1 : 0;

	const FVector PlatformLocation(0.f, 0.f, CentralPlatformExtent.Z);
	const FVector PlatformScale(
		(CentralPlatformExtent.X * 2.f) / 100.f,
		(CentralPlatformExtent.Y * 2.f) / 100.f,
		(CentralPlatformExtent.Z * 2.f) / 100.f);
	SpawnArenaCube(CubeMesh, PlatformLocation, PlatformScale, TEXT("TemporaryArena_CentralPlatform"));

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Temporary arena spawned. FloorScale=%s WallCount=%d PlatformAt=%s"),
		*FloorScale.ToString(),
		WallCount,
		*PlatformLocation.ToString());
}

UStaticMesh* AArenaGameMode_Steal::LoadArenaCubeMesh() const
{
	const FSoftObjectPath CubeMeshPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	return Cast<UStaticMesh>(CubeMeshPath.TryLoad());
}

AStaticMeshActor* AArenaGameMode_Steal::SpawnArenaCube(UStaticMesh* CubeMesh, const FVector& Location, const FVector& Scale, const TCHAR* DebugName)
{
	UWorld* World = GetWorld();
	if (World == nullptr || CubeMesh == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* ArenaActor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, SpawnParameters);
	if (ArenaActor == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] Failed to spawn temporary arena actor. Name=%s"), DebugName);
		return nullptr;
	}

	ArenaActor->SetActorScale3D(Scale);
	ArenaActor->SetReplicates(true);
	ArenaActor->SetReplicateMovement(false);
	ArenaActor->bAlwaysRelevant = true;
	ArenaActor->SetNetDormancy(DORM_Initial);

	if (UStaticMeshComponent* MeshComponent = ArenaActor->GetStaticMeshComponent())
	{
		MeshComponent->SetStaticMesh(CubeMesh);
		MeshComponent->SetMobility(EComponentMobility::Static);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetIsReplicated(true);
	}

	SpawnedArenaActors.Add(ArenaActor);
	return ArenaActor;
}

void AArenaGameMode_Steal::BuildSpawnPoints()
{
	if (ArenaPlayerStarts.Num() > 0)
	{
		return;
	}

	static const FVector SpawnLocations[] =
	{
		FVector(-1000.f, -800.f, 150.f),
		FVector(-1000.f, 800.f, 150.f),
		FVector(1000.f, -800.f, 150.f),
		FVector(1000.f, 800.f, 150.f)
	};

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (const FVector& SpawnLocation : SpawnLocations)
	{
		const FTransform SpawnTransform = MakeSpawnTransform(SpawnLocation);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), SpawnTransform, SpawnParameters);
		if (PlayerStart)
		{
			ArenaPlayerStarts.Add(PlayerStart);
		}
	}
}

FTransform AArenaGameMode_Steal::MakeSpawnTransform(const FVector& Location) const
{
	const FVector CenterAtSpawnHeight(0.f, 0.f, Location.Z);
	const FRotator Rotation = (CenterAtSpawnHeight - Location).Rotation();
	return FTransform(Rotation, Location);
}
