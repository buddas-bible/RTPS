#include "VoxelAuthoring/VoxelEditComponent.h"

#include "Controller/RTPSPlayerController.h"
#include "VoxelAuthoring/VoxelChunkManager.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	APawn* ResolveOwningPawn(const UActorComponent* Component)
	{
		if (!IsValid(Component))
		{
			return nullptr;
		}

		if (APawn* Pawn = Cast<APawn>(Component->GetOwner()))
		{
			return Pawn;
		}

		if (const AController* Controller = Cast<AController>(Component->GetOwner()))
		{
			return Controller->GetPawn();
		}

		return nullptr;
	}
}

UVoxelEditComponent::UVoxelEditComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVoxelEditComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(ChunkManager))
	{
		ChunkManager = Cast<AVoxelChunkManager>(
			UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelChunkManager::StaticClass()));
	}
}

void UVoxelEditComponent::RequestBrushAtScreenCenter()
{
	if (!IsValid(ChunkManager))
	{
		return;
	}

	APawn* OwningPawn = ResolveOwningPawn(this);
	if (!IsValid(OwningPawn) || !OwningPawn->IsLocallyControlled())
	{
		return;
	}

	FVector HitWorldPosition = FVector::ZeroVector;
	if (!TryLineTrace(HitWorldPosition))
	{
		return;
	}
	ARTPSPlayerController* RTPSPlayerController = Cast<ARTPSPlayerController>(OwningPawn->GetController());
	if (!IsValid(RTPSPlayerController))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Voxel brush request skipped because the owning controller is not ARTPSPlayerController. Owner=%s"),
			*GetNameSafe(OwningPawn));
		return;
	}

	FVoxelBrush Brush;
	Brush.WorldPosition = HitWorldPosition;
	Brush.Radius = BrushRadius;
	Brush.Strength = BrushStrength;
	Brush.Mode = BrushMode;

	RTPSPlayerController->ServerApplyVoxelBrush(Brush);
}

bool UVoxelEditComponent::TryLineTrace(FVector& OutHitWorldPos)
{
	APawn* OwningPawn = ResolveOwningPawn(this);
	if (!IsValid(OwningPawn))
	{
		return false;
	}

	APlayerController* PlayerController = Cast<APlayerController>(OwningPawn->GetController());
	if (!IsValid(PlayerController) || !IsValid(PlayerController->PlayerCameraManager))
	{
		return false;
	}

	const FVector TraceStart = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FVector TraceDirection = PlayerController->PlayerCameraManager->GetCameraRotation().Vector();
	const FVector TraceEnd = TraceStart + (TraceDirection * TraceDistance);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VoxelEditTrace), false);
	QueryParams.AddIgnoredActor(OwningPawn);

	if (AActor* OwnerActor = GetOwner())
	{
		QueryParams.AddIgnoredActor(OwnerActor);
	}

	FHitResult HitResult;
	if (!GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return false;
	}

	OutHitWorldPos = HitResult.ImpactPoint;
	return true;
}
