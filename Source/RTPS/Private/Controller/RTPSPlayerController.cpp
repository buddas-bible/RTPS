#include "Controller/RTPSPlayerController.h"

#include "Character/RTPSCharacterPlayer.h"
#include "CollisionQueryParams.h"
#include "Components/ActorComponent.h"
#include "EnhancedInputSubsystems.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/RTPSGameMode.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "VoxelAuthoring/VoxelChunk.h"
#include "VoxelAuthoring/VoxelChunkManager.h"
#include "VoxelAuthoring/VoxelDebugVisualizer.h"
#include "VoxelAuthoring/VoxelEditorPawn.h"

namespace
{
	const TCHAR* GetVoxelBrushModeDebugName(EVoxelBrushMode Mode)
	{
		switch (Mode)
		{
		case EVoxelBrushMode::Add:
			return TEXT("Add");
		case EVoxelBrushMode::Remove:
			return TEXT("Remove");
		default:
			return TEXT("Invalid");
		}
	}

	const TCHAR* GetVoxelBrushShapeDebugName(EVoxelBrushShape Shape)
	{
		switch (Shape)
		{
		case EVoxelBrushShape::Sphere:
			return TEXT("Sphere");
		case EVoxelBrushShape::Box:
			return TEXT("Box");
		case EVoxelBrushShape::Flatten:
			return TEXT("Flatten");
		case EVoxelBrushShape::Smooth:
			return TEXT("Smooth");
		case EVoxelBrushShape::SurfaceBlob:
			return TEXT("SurfaceBlob");
		case EVoxelBrushShape::TerrainMudBlob:
			return TEXT("TerrainMudBlob");
		default:
			return TEXT("Invalid");
		}
	}

	const TCHAR* GetVoxelBrushBlendModeDebugName(EVoxelBrushBlendMode BlendMode)
	{
		switch (BlendMode)
		{
		case EVoxelBrushBlendMode::Additive:
			return TEXT("Additive");
		case EVoxelBrushBlendMode::TargetMax:
			return TEXT("TargetMax");
		case EVoxelBrushBlendMode::TargetLerp:
			return TEXT("TargetLerp");
		default:
			return TEXT("Invalid");
		}
	}

	const TCHAR* GetVoxelBrushFalloffDebugName(EVoxelBrushFalloff Falloff)
	{
		switch (Falloff)
		{
		case EVoxelBrushFalloff::Linear:
			return TEXT("Linear");
		case EVoxelBrushFalloff::Smooth:
			return TEXT("Smooth");
		case EVoxelBrushFalloff::Spherical:
			return TEXT("Spherical");
		case EVoxelBrushFalloff::Plateau:
			return TEXT("Plateau");
		default:
			return TEXT("Invalid");
		}
	}

	const TCHAR* GetNetModeDebugName(const UWorld* World)
	{
		if (World == nullptr)
		{
			return TEXT("InvalidWorld");
		}

		switch (World->GetNetMode())
		{
		case NM_Standalone:
			return TEXT("Standalone");
		case NM_DedicatedServer:
			return TEXT("DedicatedServer");
		case NM_ListenServer:
			return TEXT("ListenServer");
		case NM_Client:
			return TEXT("Client");
		default:
			return TEXT("Unknown");
		}
	}

	FString JoinBoundaryAxes(const TArray<FString>& BoundaryAxes)
	{
		return BoundaryAxes.IsEmpty() ? TEXT("None") : FString::Join(BoundaryAxes, TEXT(","));
	}

	const TCHAR* GetVoxelShotHitKindDebugName(ERTPSVoxelShotHitKind HitKind)
	{
		switch (HitKind)
		{
		case ERTPSVoxelShotHitKind::VoxelChunkHit:
			return TEXT("VoxelChunkHit");
		case ERTPSVoxelShotHitKind::BuildSurfaceHit:
			return TEXT("BuildSurfaceHit");
		case ERTPSVoxelShotHitKind::NonVoxelRejected:
			return TEXT("NonVoxelRejected");
		default:
			return TEXT("None");
		}
	}
}

void ARTPSPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController() || !IsQuestOrPlayMap())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RTPSValidation] Local gameplay controller reached in-game map '%s'."), *GetWorld()->GetMapName());
	UE_LOG(LogTemp, Log, TEXT("Quest debug controls: R toggles auto-run, F6 adds a minor kill, F7 adds a target kill. Hold Ctrl for cursor."));
}

void ARTPSPlayerController::PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel)
{
	CleanupVoxelChunkSubscriptions(TEXT("PreClientTravel"));
	CleanupVoxelEditorPawn(TEXT("PreClientTravel"));

	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);
}

void ARTPSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupVoxelChunkSubscriptions(TEXT("EndPlay"));
	CleanupVoxelEditorPawn(TEXT("EndPlay"));

	Super::EndPlay(EndPlayReason);
}

void ARTPSPlayerController::Destroyed()
{
	CleanupVoxelChunkSubscriptions(TEXT("Destroyed"));
	CleanupVoxelEditorPawn(TEXT("Destroyed"));

	Super::Destroyed();
}

void ARTPSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController() || !bAutoRunEnabled || !IsQuestOrPlayMap())
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (ControlledPawn == nullptr)
	{
		return;
	}

	ControlledPawn->AddMovementInput(ControlledPawn->GetActorForwardVector(), 1.0f);
}

void ARTPSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent == nullptr)
	{
		return;
	}

	BindGameplayInput();
}

void ARTPSPlayerController::BindGameplayInput()
{
	if (InputComponent == nullptr)
	{
		return;
	}

	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ARTPSPlayerController::ToggleAutoRun);
	InputComponent->BindKey(EKeys::F6, IE_Pressed, this, &ARTPSPlayerController::RequestDebugMinorKill);
	InputComponent->BindKey(EKeys::F7, IE_Pressed, this, &ARTPSPlayerController::RequestDebugBossKill);
}

void ARTPSPlayerController::CleanupVoxelEditorPawn(const TCHAR* Reason)
{
	if (!HasAuthority())
	{
		ActiveVoxelEditorPawn = nullptr;
		VoxelEditorOriginalPawn = nullptr;
		CachedVoxelEditorCharacterIMC = nullptr;
		ResetVoxelEditorViewTransformState();
		return;
	}

	AVoxelEditorPawn* EditorPawn = ActiveVoxelEditorPawn.Get();
	if (IsValid(EditorPawn))
	{
		UWorld* World = GetWorld();
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] CleanupVoxelEditorPawn Reason=%s Controller=%s EditorPawn=%s OriginalPawn=%s Authority=%d Map=%s"),
			Reason != nullptr ? Reason : TEXT("Unknown"),
			*GetNameSafe(this),
			*GetNameSafe(EditorPawn),
			*GetNameSafe(VoxelEditorOriginalPawn.Get()),
			HasAuthority() ? 1 : 0,
			World != nullptr ? *World->GetMapName() : TEXT("None"));

		if (!EditorPawn->IsPendingKillPending())
		{
			EditorPawn->Destroy();
		}
	}

	ActiveVoxelEditorPawn = nullptr;
	VoxelEditorOriginalPawn = nullptr;
	CachedVoxelEditorCharacterIMC = nullptr;
	ResetVoxelEditorViewTransformState();
}

void ARTPSPlayerController::CleanupVoxelChunkSubscriptions(const TCHAR* Reason)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (HasAuthority())
	{
		if (AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
			UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass())))
		{
			ChunkManager->UnsubscribePlayerFromAllChunks(this, Reason);
		}
		return;
	}

	if (IsLocalController())
	{
		ServerUnsubscribeAllVoxelChunks();
	}
}

void ARTPSPlayerController::ResetVoxelEditorViewTransformState()
{
	bHasApprovedVoxelEditorViewTransform = false;
	ApprovedVoxelEditorViewLocation = FVector::ZeroVector;
	ApprovedVoxelEditorViewRotation = FRotator::ZeroRotator;
	LastApprovedVoxelEditorViewServerTime = -1.0;
	LastAcceptedVoxelEditorViewLogServerTime = -1.0;
	LastRejectedVoxelEditorViewLogServerTime = -1.0;
}

bool ARTPSPlayerController::IsVoxelEditorViewTransformFinite(const FVector& ViewLocation, const FRotator& ViewRotation) const
{
	return FMath::IsFinite(ViewLocation.X)
		&& FMath::IsFinite(ViewLocation.Y)
		&& FMath::IsFinite(ViewLocation.Z)
		&& FMath::IsFinite(ViewRotation.Pitch)
		&& FMath::IsFinite(ViewRotation.Yaw)
		&& FMath::IsFinite(ViewRotation.Roll);
}

bool ARTPSPlayerController::TryGetApprovedVoxelEditorViewLocation(FVector& OutViewLocation, FString& OutReason) const
{
	if (!bHasApprovedVoxelEditorViewTransform)
	{
		OutReason = TEXT("Approved editor view transform is missing.");
		return false;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReason = TEXT("World is invalid while reading editor view transform.");
		return false;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const double AgeSeconds = CurrentTime - LastApprovedVoxelEditorViewServerTime;
	const float EffectiveMaxAge = FMath::Max(MaxVoxelEditorViewTransformAge, 0.05f);
	if (LastApprovedVoxelEditorViewServerTime < 0.0 || AgeSeconds > static_cast<double>(EffectiveMaxAge))
	{
		OutReason = FString::Printf(TEXT("Approved editor view transform is stale. Age=%.3f MaxAge=%.3f"), AgeSeconds, EffectiveMaxAge);
		return false;
	}

	OutViewLocation = ApprovedVoxelEditorViewLocation;
	OutReason = TEXT("Approved editor view transform is fresh.");
	return true;
}

bool ARTPSPlayerController::TryBuildVoxelShotBrush(
	EVoxelBrushMode Mode,
	FVoxelBrush& OutBrush,
	FHitResult& OutHitResult,
	FVector& OutTraceStart,
	FVector& OutTraceEnd,
	FString& OutFailureReason,
	ERTPSVoxelShotHitKind& OutHitKind) const
{
	OutBrush = FVoxelBrush();
	OutHitResult = FHitResult();
	OutTraceStart = FVector::ZeroVector;
	OutTraceEnd = FVector::ZeroVector;
	OutFailureReason.Reset();
	OutHitKind = ERTPSVoxelShotHitKind::None;

	UWorld* World = GetWorld();
	const APawn* ControlledPawn = GetPawn();
	if (World == nullptr || !IsValid(ControlledPawn))
	{
		OutFailureReason = World == nullptr ? TEXT("InvalidWorld") : TEXT("InvalidPawn");
		return false;
	}

	const int32 BrushModeValue = static_cast<int32>(Mode);
	if (BrushModeValue < static_cast<int32>(EVoxelBrushMode::Add) || BrushModeValue > static_cast<int32>(EVoxelBrushMode::Remove))
	{
		OutFailureReason = TEXT("InvalidBrushMode");
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetPlayerViewPoint(ViewLocation, ViewRotation);

	if (!FMath::IsFinite(ViewLocation.X)
		|| !FMath::IsFinite(ViewLocation.Y)
		|| !FMath::IsFinite(ViewLocation.Z)
		|| !FMath::IsFinite(ViewRotation.Pitch)
		|| !FMath::IsFinite(ViewRotation.Yaw)
		|| !FMath::IsFinite(ViewRotation.Roll))
	{
		OutFailureReason = TEXT("InvalidViewTransform");
		return false;
	}

	const float EffectiveRange = FMath::Max(VoxelShotRange, 1.f);
	const FVector TraceEnd = ViewLocation + (ViewRotation.Vector() * EffectiveRange);
	OutTraceStart = ViewLocation;
	OutTraceEnd = TraceEnd;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VoxelShotTrace), false);
	QueryParams.AddIgnoredActor(ControlledPawn);

	if (!World->LineTraceSingleByChannel(OutHitResult, ViewLocation, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		OutFailureReason = TEXT("NoHit");
		return false;
	}

	OutHitKind = ClassifyVoxelShotHit(
		Mode,
		OutHitResult.GetActor(),
		OutHitResult.GetComponent(),
		OutFailureReason);
	if (OutHitKind == ERTPSVoxelShotHitKind::NonVoxelRejected)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Server voxel shot trace rejected non-voxel WorldStatic actor. PlayerController=%s Pawn=%s HitActor=%s HitComponent=%s Mode=%d Reason=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ControlledPawn),
			*GetNameSafe(OutHitResult.GetActor()),
			*GetNameSafe(OutHitResult.GetComponent()),
			BrushModeValue,
			*OutFailureReason);
		return false;
	}

	if (OutHitKind != ERTPSVoxelShotHitKind::VoxelChunkHit
		&& OutHitKind != ERTPSVoxelShotHitKind::BuildSurfaceHit)
	{
		OutFailureReason = TEXT("InvalidHitResult");
		return false;
	}

	ConfigureVoxelShotBrushForHit(Mode, OutHitKind, OutHitResult, OutTraceStart, OutTraceEnd, OutBrush);
	OutFailureReason = TEXT("Accepted");
	return true;
}

void ARTPSPlayerController::ConfigureVoxelShotBrushForHit(
	EVoxelBrushMode Mode,
	ERTPSVoxelShotHitKind HitKind,
	const FHitResult& HitResult,
	const FVector& TraceStart,
	const FVector& TraceEnd,
	FVoxelBrush& OutBrush) const
{
	const FVector ImpactPoint = HitResult.ImpactPoint;
	const FVector ImpactNormal = HitResult.ImpactNormal;
	const auto BuildStableVoxelShotNormal = [this, &ImpactNormal, &TraceStart, &TraceEnd]()
	{
		FVector RawNormal = ImpactNormal.GetSafeNormal();
		bool bUsedFallback = false;
		if (RawNormal.IsNearlyZero())
		{
			RawNormal = FVector::UpVector;
			bUsedFallback = true;
		}

		const FVector IncomingDir = (TraceEnd - TraceStart).GetSafeNormal();
		FVector ShotOpposite = IncomingDir.IsNearlyZero() ? RawNormal : -IncomingDir;
		if (ShotOpposite.IsNearlyZero())
		{
			ShotOpposite = RawNormal;
			bUsedFallback = true;
		}

		const float DirectionBlend = FMath::Clamp(TerrainMudBlobShotDirectionBlend, 0.f, 1.f);
		FVector StableNormal = FMath::Lerp(RawNormal, ShotOpposite, DirectionBlend).GetSafeNormal();
		if (StableNormal.IsNearlyZero())
		{
			StableNormal = RawNormal;
			bUsedFallback = true;
		}
		if (FVector::DotProduct(StableNormal, ShotOpposite) < 0.f)
		{
			StableNormal *= -1.f;
		}

		if (bUsedFallback && bDebugVoxelShots)
		{
			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelShotDebug] Stable voxel shot normal fallback used. RawImpactNormal=%s RawNormal=%s IncomingDir=%s ShotOpposite=%s StableNormal=%s"),
				*ImpactNormal.ToCompactString(),
				*RawNormal.ToCompactString(),
				*IncomingDir.ToCompactString(),
				*ShotOpposite.ToCompactString(),
				*StableNormal.ToCompactString());
		}

		return StableNormal;
	};

	if (bUseUnifiedSphereVoxelShots)
	{
		// 기본 프로토타입 경로: Blob 계열은 시각 검증상 기본값에서 제외하고 Sphere + Falloff + TargetLerp로 통일한다.
		FVector SphereNormal = ImpactNormal.GetSafeNormal();
		if (SphereNormal.IsNearlyZero())
		{
			SphereNormal = FVector::UpVector;
		}

		FVector BrushCenter = ImpactPoint;
		const bool bAddMode = Mode == EVoxelBrushMode::Add;
		if (bAddMode && HitKind == ERTPSVoxelShotHitKind::VoxelChunkHit)
		{
			SphereNormal = BuildStableVoxelShotNormal();
			BrushCenter = ImpactPoint - (SphereNormal * FMath::Max(VoxelChunkAddSphereEmbedDepth, 0.f));
		}
		else if (bAddMode && HitKind == ERTPSVoxelShotHitKind::BuildSurfaceHit)
		{
			BrushCenter = ImpactPoint + (SphereNormal * FMath::Max(BuildSurfaceAddSphereOffset, 0.f));
		}

		OutBrush.WorldPosition = BrushCenter;
		OutBrush.Radius = bAddMode ? VoxelAddSphereRadius : VoxelRemoveSphereRadius;
		OutBrush.Strength = bAddMode ? VoxelAddSphereStrength : VoxelRemoveSphereStrength;
		OutBrush.Mode = Mode;
		OutBrush.Shape = EVoxelBrushShape::Sphere;
		OutBrush.Falloff = VoxelShotFalloff;
		OutBrush.SurfaceNormal = SphereNormal;
		OutBrush.SurfaceDepth = 100.f;
		OutBrush.BackDepth = 0.f;
		OutBrush.EmbedDepth = bAddMode && HitKind == ERTPSVoxelShotHitKind::VoxelChunkHit
			? FMath::Max(VoxelChunkAddSphereEmbedDepth, 0.f)
			: 0.f;
		OutBrush.ClumpRoundnessPower = 2.5f;
		OutBrush.ConvergenceAlpha = VoxelShotConvergenceAlpha;
		OutBrush.BlendMode = VoxelShotBlendMode;
		return;
	}

	const bool bTerrainMudBlob =
		HitKind == ERTPSVoxelShotHitKind::VoxelChunkHit
		&& Mode == EVoxelBrushMode::Add
		&& bUseTerrainMudBlobForVoxelChunkAdd;
	const bool bBuildSurfaceBlob =
		HitKind == ERTPSVoxelShotHitKind::BuildSurfaceHit
		&& Mode == EVoxelBrushMode::Add
		&& bUseSurfaceBlobForBuildSurfaceAdd;
	// 실험용 분기: SurfaceBlob/TerrainMudBlob 코드는 유지하지만 기본 LMB/RMB 사격에서는 사용하지 않는다.
	if (bTerrainMudBlob)
	{
		const FVector StableNormal = BuildStableVoxelShotNormal();
		const float EmbedDepth = FMath::Max(TerrainMudBlobEmbedDepth, 0.f);
		OutBrush.WorldPosition = ImpactPoint - (StableNormal * EmbedDepth);
		OutBrush.Radius = TerrainMudBlobRadius;
		OutBrush.Strength = TerrainMudBlobStrength;
		OutBrush.Mode = Mode;
		OutBrush.Shape = EVoxelBrushShape::TerrainMudBlob;
		OutBrush.Falloff = VoxelShotFalloff;
		OutBrush.SurfaceNormal = StableNormal;
		OutBrush.SurfaceDepth = TerrainMudBlobDepth;
		OutBrush.BackDepth = TerrainMudBlobBackDepth;
		OutBrush.EmbedDepth = EmbedDepth;
		OutBrush.ClumpRoundnessPower = TerrainMudBlobRoundnessPower;
		OutBrush.ConvergenceAlpha = TerrainMudBlobConvergenceAlpha;
		OutBrush.BlendMode = TerrainMudBlobBlendMode;
		return;
	}

	if (bBuildSurfaceBlob)
	{
		const FVector SurfaceNormal = ImpactNormal.GetSafeNormal();
		const bool bUseFallbackSurfaceNormal = SurfaceNormal.IsNearlyZero();
		if (bUseFallbackSurfaceNormal && bDebugVoxelShots)
		{
			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelShotDebug] SurfaceBlob normal fallback used. HitKind=%s Mode=%s ImpactNormal=%s Fallback=%s"),
				GetVoxelShotHitKindDebugName(HitKind),
				GetVoxelBrushModeDebugName(Mode),
				*ImpactNormal.ToCompactString(),
				*FVector::UpVector.ToCompactString());
		}

		OutBrush.WorldPosition = ImpactPoint;
		OutBrush.Radius = BuildSurfaceBlobRadius;
		OutBrush.Strength = BuildSurfaceBlobStrength;
		OutBrush.Mode = Mode;
		OutBrush.Shape = EVoxelBrushShape::SurfaceBlob;
		OutBrush.Falloff = VoxelShotFalloff;
		OutBrush.SurfaceNormal = bUseFallbackSurfaceNormal ? FVector::UpVector : SurfaceNormal;
		OutBrush.SurfaceDepth = BuildSurfaceBlobDepth;
		OutBrush.BackDepth = 0.f;
		OutBrush.EmbedDepth = 0.f;
		return;
	}

	OutBrush.WorldPosition = HitKind == ERTPSVoxelShotHitKind::BuildSurfaceHit
		? ImpactPoint + (ImpactNormal * FMath::Max(AddShotBuildSurfaceNormalOffset, 0.f))
		: ImpactPoint;
	OutBrush.Radius = VoxelShotRadius;
	OutBrush.Strength = VoxelShotStrength;
	OutBrush.Mode = Mode;
	OutBrush.Shape = EVoxelBrushShape::Sphere;
	OutBrush.Falloff = EVoxelBrushFalloff::Smooth;
	OutBrush.SurfaceNormal = FVector::UpVector;
	OutBrush.SurfaceDepth = 100.f;
	OutBrush.BackDepth = 0.f;
	OutBrush.EmbedDepth = 0.f;
	OutBrush.BlendMode = EVoxelBrushBlendMode::Additive;
}

bool ARTPSPlayerController::IsVoxelBuildSurface(const AActor* HitActor, const UActorComponent* HitComponent) const
{
	if (VoxelBuildSurfaceTag.IsNone())
	{
		return false;
	}

	return (HitActor != nullptr && HitActor->ActorHasTag(VoxelBuildSurfaceTag))
		|| (HitComponent != nullptr && HitComponent->ComponentHasTag(VoxelBuildSurfaceTag));
}

ERTPSVoxelShotHitKind ARTPSPlayerController::ClassifyVoxelShotHit(
	EVoxelBrushMode Mode,
	const AActor* HitActor,
	const UActorComponent* HitComponent,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();

	if (IsValid(Cast<const AVoxelChunk>(HitActor)))
	{
		return ERTPSVoxelShotHitKind::VoxelChunkHit;
	}

	const bool bBuildSurface = IsVoxelBuildSurface(HitActor, HitComponent);
	if (Mode == EVoxelBrushMode::Add && bBuildSurface)
	{
		return ERTPSVoxelShotHitKind::BuildSurfaceHit;
	}

	OutFailureReason = bBuildSurface && Mode == EVoxelBrushMode::Remove
		? TEXT("BuildSurfaceRemoveRejected")
		: TEXT("NonVoxelHitActor");
	return ERTPSVoxelShotHitKind::NonVoxelRejected;
}

void ARTPSPlayerController::LogVoxelShotRequestDebug(
	int64 ShotId,
	EVoxelBrushMode Mode,
	const FVector& TraceStart,
	const FVector& TraceEnd,
	float CooldownSeconds,
	double CooldownElapsedSeconds,
	bool bAccepted,
	const TCHAR* RejectionReason) const
{
	if (!bDebugVoxelShots)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const bool bCooldownReady = CooldownSeconds <= 0.f
		|| CooldownElapsedSeconds >= static_cast<double>(CooldownSeconds);

	UE_LOG(
		LogRTPSVoxelDebug,
		Log,
		TEXT("[VoxelShotDebug] Request ShotId=%lld NetMode=%s Authority=%d PlayerController=%s Pawn=%s Mode=%s TraceStart=%s TraceEnd=%s Range=%.2f CooldownSeconds=%.3f CooldownElapsed=%.3f CooldownReady=%d Accepted=%d RejectionReason=%s"),
		ShotId,
		GetNetModeDebugName(World),
		HasAuthority() ? 1 : 0,
		*GetNameSafe(this),
		*GetNameSafe(GetPawn()),
		GetVoxelBrushModeDebugName(Mode),
		*TraceStart.ToCompactString(),
		*TraceEnd.ToCompactString(),
		VoxelShotRange,
		CooldownSeconds,
		CooldownElapsedSeconds,
		bCooldownReady ? 1 : 0,
		bAccepted ? 1 : 0,
		RejectionReason != nullptr ? RejectionReason : TEXT("None"));
}

void ARTPSPlayerController::DrawVoxelShotDebug(
	const FVector& TraceStart,
	const FVector& TraceEnd,
	const FHitResult& HitResult,
	const FVoxelBrush* Brush,
	const AVoxelChunk* HitChunk,
	bool bAccepted) const
{
	if (!bDrawVoxelShotDebug)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Lifetime = FMath::Max(VoxelShotDebugDrawLifetimeSeconds, 0.1f);
	const FColor TraceColor = bAccepted ? FColor::Green : FColor::Red;
	DrawDebugLine(World, TraceStart, TraceEnd, TraceColor, false, Lifetime, 0, 2.f);

	if (!HitResult.bBlockingHit)
	{
		return;
	}

	DrawDebugSphere(World, HitResult.ImpactPoint, 18.f, 12, TraceColor, false, Lifetime, 0, 2.f);
	DrawDebugDirectionalArrow(
		World,
		HitResult.ImpactPoint,
		HitResult.ImpactPoint + (HitResult.ImpactNormal * 150.f),
		35.f,
		FColor::Cyan,
		false,
		Lifetime,
		0,
		2.f);

	if (Brush != nullptr && Brush->Shape == EVoxelBrushShape::Sphere)
	{
		DrawDebugSphere(World, Brush->WorldPosition, Brush->Radius, 24, FColor::Purple, false, Lifetime, 0, 2.f);
		DrawDebugSphere(World, Brush->WorldPosition, 10.f, 12, FColor::Yellow, false, Lifetime, 0, 2.f);
		if (Brush->Falloff == EVoxelBrushFalloff::Plateau)
		{
			DrawDebugSphere(World, Brush->WorldPosition, Brush->Radius * 0.5f, 16, FColor::Orange, false, Lifetime, 0, 1.f);
		}
	}
	else if (Brush != nullptr
		&& (Brush->Shape == EVoxelBrushShape::SurfaceBlob || Brush->Shape == EVoxelBrushShape::TerrainMudBlob))
	{
		const FVector SurfaceNormal = Brush->SurfaceNormal.GetSafeNormal();
		const FVector DrawNormal = SurfaceNormal.IsNearlyZero() ? FVector::UpVector : SurfaceNormal;
		FVector TangentX = FVector::ForwardVector;
		FVector TangentY = FVector::RightVector;
		DrawNormal.FindBestAxisVectors(TangentX, TangentY);

		const auto DrawOrientedRing = [World, Lifetime, TangentX, TangentY](const FVector& Center, float RingRadius, const FColor& Color, float Thickness = 2.f)
		{
			if (RingRadius <= KINDA_SMALL_NUMBER)
			{
				DrawDebugSphere(World, Center, 8.f, 8, Color, false, Lifetime, 0, Thickness);
				return;
			}

			constexpr int32 SegmentCount = 24;
			FVector PreviousPoint = Center + (TangentX * RingRadius);
			for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
			{
				const float AngleRadians = (2.f * PI * static_cast<float>(SegmentIndex)) / static_cast<float>(SegmentCount);
				const FVector NextPoint = Center
					+ (TangentX * FMath::Cos(AngleRadians) * RingRadius)
					+ (TangentY * FMath::Sin(AngleRadians) * RingRadius);
				DrawDebugLine(World, PreviousPoint, NextPoint, Color, false, Lifetime, 0, Thickness);
				PreviousPoint = NextPoint;
			}
		};

		if (Brush->Shape == EVoxelBrushShape::SurfaceBlob)
		{
			const FVector SurfaceCenter = Brush->WorldPosition;
			const FVector DepthCenter = Brush->WorldPosition + (DrawNormal * Brush->SurfaceDepth);
			DrawOrientedRing(SurfaceCenter, Brush->Radius, FColor::Purple);
			DrawOrientedRing(DepthCenter, Brush->Radius, FColor::Magenta);
			DrawDebugDirectionalArrow(World, SurfaceCenter, DepthCenter, 30.f, FColor::Orange, false, Lifetime, 0, 2.f);
			DrawDebugLine(World, SurfaceCenter + (TangentX * Brush->Radius), DepthCenter + (TangentX * Brush->Radius), FColor::Purple, false, Lifetime, 0, 1.5f);
			DrawDebugLine(World, SurfaceCenter - (TangentX * Brush->Radius), DepthCenter - (TangentX * Brush->Radius), FColor::Purple, false, Lifetime, 0, 1.5f);
			DrawDebugLine(World, SurfaceCenter + (TangentY * Brush->Radius), DepthCenter + (TangentY * Brush->Radius), FColor::Purple, false, Lifetime, 0, 1.5f);
			DrawDebugLine(World, SurfaceCenter - (TangentY * Brush->Radius), DepthCenter - (TangentY * Brush->Radius), FColor::Purple, false, Lifetime, 0, 1.5f);
		}
		else
		{
			const float FrontDepth = FMath::Max(Brush->SurfaceDepth, 0.f);
			const float BackDepth = FMath::Max(Brush->BackDepth, 0.f);
			const float EmbedDepth = FMath::Max(Brush->EmbedDepth, 0.f);
			const float RoundnessPower = FMath::Max(Brush->ClumpRoundnessPower, 1.f);
			const FVector Center = Brush->WorldPosition;
			const FVector ImpactPointApprox = Center + (DrawNormal * EmbedDepth);
			const FVector FrontCenter = Center + (DrawNormal * FrontDepth);
			const FVector BackCenter = Center - (DrawNormal * BackDepth);

			const auto DrawClumpRingAtHeight = [&](float Height, const FColor& Color)
			{
				float NormalizedHeight = 0.f;
				if (Height >= 0.f)
				{
					NormalizedHeight = FrontDepth > KINDA_SMALL_NUMBER ? Height / FrontDepth : 1.f;
				}
				else
				{
					NormalizedHeight = BackDepth > KINDA_SMALL_NUMBER ? -Height / BackDepth : 1.f;
				}

				const float HeightShape = FMath::Pow(FMath::Clamp(FMath::Abs(NormalizedHeight), 0.f, 1.f), RoundnessPower);
				const float RadiusScale = FMath::Pow(FMath::Clamp(1.f - HeightShape, 0.f, 1.f), 1.f / RoundnessPower);
				DrawOrientedRing(Center + (DrawNormal * Height), Brush->Radius * RadiusScale, Color);
			};

			DrawDebugSphere(World, Center, 14.f, 12, FColor::Orange, false, Lifetime, 0, 2.f);
			DrawDebugSphere(World, ImpactPointApprox, 10.f, 12, FColor::Yellow, false, Lifetime, 0, 2.f);
			DrawDebugDirectionalArrow(World, BackCenter, FrontCenter, 30.f, FColor::Orange, false, Lifetime, 0, 2.f);
			if (BackDepth > KINDA_SMALL_NUMBER)
			{
				DrawClumpRingAtHeight(-BackDepth, FColor::Orange);
				DrawClumpRingAtHeight(-BackDepth * 0.5f, FColor::Orange);
			}
			DrawClumpRingAtHeight(0.f, FColor::Purple);
			DrawClumpRingAtHeight(FrontDepth * 0.5f, FColor::Magenta);
			DrawClumpRingAtHeight(FrontDepth, FColor::Magenta);
			DrawDebugLine(World, Center + (TangentX * Brush->Radius), Center - (TangentX * Brush->Radius), FColor::Purple, false, Lifetime, 0, 1.5f);
			DrawDebugLine(World, Center + (TangentY * Brush->Radius), Center - (TangentY * Brush->Radius), FColor::Purple, false, Lifetime, 0, 1.5f);

			FBox ApproxBounds(ForceInit);
			ApproxBounds += Center + (TangentX * Brush->Radius);
			ApproxBounds += Center - (TangentX * Brush->Radius);
			ApproxBounds += Center + (TangentY * Brush->Radius);
			ApproxBounds += Center - (TangentY * Brush->Radius);
			ApproxBounds += FrontCenter;
			ApproxBounds += BackCenter;
			ApproxBounds = ApproxBounds.ExpandBy(5.f);
			DrawDebugBox(World, ApproxBounds.GetCenter(), ApproxBounds.GetExtent(), FColor::Cyan, false, Lifetime, 0, 1.f);
		}
	}

	if (HitChunk != nullptr)
	{
		const FVector ChunkWorldSize(
			HitChunk->ChunkDimensions.X * HitChunk->CellSize,
			HitChunk->ChunkDimensions.Y * HitChunk->CellSize,
			HitChunk->ChunkDimensions.Z * HitChunk->CellSize);
		const FVector HalfExtent = ChunkWorldSize * 0.5f;
		const FVector Center = HitChunk->GetActorLocation() + HalfExtent;
		DrawDebugBox(World, Center, HalfExtent, FColor::Blue, false, Lifetime, 0, 2.f);
	}
}

void ARTPSPlayerController::RequestDebugMinorKill()
{
	if (!IsLocalController() || IsLobbyMap())
	{
		return;
	}

	ServerRegisterDebugMinorKill();
}

void ARTPSPlayerController::RequestDebugBossKill()
{
	if (!IsLocalController() || IsLobbyMap())
	{
		return;
	}

	ServerRegisterDebugBossKill();
}

void ARTPSPlayerController::ToggleAutoRun()
{
	if (!IsLocalController())
	{
		return;
	}

	if (!IsQuestOrPlayMap())
	{
		return;
	}

	bAutoRunEnabled = !bAutoRunEnabled;
	ShowLobbyMessage(bAutoRunEnabled ? TEXT("Auto-run enabled.") : TEXT("Auto-run disabled."), FColor::Cyan);
}

void ARTPSPlayerController::RequestVoxelShot(EVoxelBrushMode Mode)
{
	if (!IsLocalController())
	{
		return;
	}

	ServerFireVoxelShot(Mode);
}

TArray<FString> ARTPSPlayerController::GetControlHintLines() const
{
	if (IsQuestOrPlayMap())
	{
		return {
			TEXT("Quest Controls"),
			TEXT("Enter : Open / send chat"),
			TEXT("R : Toggle auto-run"),
			TEXT("F6 : Debug minor kill"),
			TEXT("F7 : Debug boss kill"),
			TEXT("Hold Ctrl : Enable cursor")
		};
	}

	return Super::GetControlHintLines();
}

void ARTPSPlayerController::ServerRegisterDebugMinorKill_Implementation()
{
	if (ARTPSGameMode* RTPSGameMode = GetWorld()->GetAuthGameMode<ARTPSGameMode>())
	{
		RTPSGameMode->RegisterMinorMonsterKill();
	}
}

void ARTPSPlayerController::ServerRegisterDebugBossKill_Implementation()
{
	if (ARTPSGameMode* RTPSGameMode = GetWorld()->GetAuthGameMode<ARTPSGameMode>())
	{
		RTPSGameMode->RegisterTargetMonsterKill();
	}
}

void ARTPSPlayerController::ServerEnterVoxelEditorMode_Implementation()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] ServerEnterVoxelEditorMode rejected because World is invalid. PlayerController=%s"), *GetNameSafe(this));
		return;
	}

	APawn* CurrentPawn = GetPawn();
	if (IsValid(Cast<AVoxelEditorPawn>(CurrentPawn)))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerEnterVoxelEditorMode rejected because controller already possesses an editor pawn. PlayerController=%s EditorPawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(CurrentPawn),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	ARTPSCharacterPlayer* OriginalCharacter = Cast<ARTPSCharacterPlayer>(CurrentPawn);
	if (!IsValid(OriginalCharacter))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerEnterVoxelEditorMode rejected because current pawn is not ARTPSCharacterPlayer. PlayerController=%s Pawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(CurrentPawn),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	if (IsValid(ActiveVoxelEditorPawn))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerEnterVoxelEditorMode rejected because an active editor pawn is already cached. PlayerController=%s OriginalPawn=%s EditorPawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(VoxelEditorOriginalPawn.Get()),
			*GetNameSafe(ActiveVoxelEditorPawn.Get()),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	ResetVoxelEditorViewTransformState();

	const FVector EditorSpawnLocation = OriginalCharacter->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	const FRotator SpawnRotation = OriginalCharacter->GetActorRotation();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = OriginalCharacter;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AVoxelEditorPawn* EditorPawn = World->SpawnActor<AVoxelEditorPawn>(
		AVoxelEditorPawn::StaticClass(),
		EditorSpawnLocation,
		SpawnRotation,
		SpawnParameters);

	if (!IsValid(EditorPawn))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerEnterVoxelEditorMode rejected because editor pawn spawn failed. PlayerController=%s OriginalPawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(OriginalCharacter),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	UInputMappingContext* CharacterIMC = OriginalCharacter->GetDefaultMappingContext();
	EditorPawn->EnterEditorMode(OriginalCharacter, CharacterIMC);

	VoxelEditorOriginalPawn = OriginalCharacter;
	ActiveVoxelEditorPawn = EditorPawn;
	CachedVoxelEditorCharacterIMC = CharacterIMC;

	Possess(EditorPawn);

	if (GetPawn() != EditorPawn)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerEnterVoxelEditorMode failed after spawn because possess did not take effect. PlayerController=%s OriginalPawn=%s EditorPawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(OriginalCharacter),
			*GetNameSafe(EditorPawn),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());

		CleanupVoxelEditorPawn(TEXT("ServerEnterVoxelEditorModePossessFailed"));
		return;
	}

	ApprovedVoxelEditorViewLocation = EditorPawn->GetActorLocation();
	ApprovedVoxelEditorViewRotation = EditorPawn->GetActorRotation();
	LastApprovedVoxelEditorViewServerTime = FPlatformTime::Seconds();
	bHasApprovedVoxelEditorViewTransform = true;

	ClientSetVoxelEditorCharacterMapping(CharacterIMC, false);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ServerEnterVoxelEditorMode accepted. PlayerController=%s OriginalPawn=%s EditorPawn=%s Authority=%d Map=%s"),
		*GetNameSafe(this),
		*GetNameSafe(OriginalCharacter),
		*GetNameSafe(EditorPawn),
		HasAuthority() ? 1 : 0,
		*World->GetMapName());
}

void ARTPSPlayerController::ServerExitVoxelEditorMode_Implementation()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTPSValidation] ServerExitVoxelEditorMode rejected because World is invalid. PlayerController=%s"), *GetNameSafe(this));
		return;
	}

	AVoxelEditorPawn* EditorPawn = Cast<AVoxelEditorPawn>(GetPawn());
	if (!IsValid(EditorPawn))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerExitVoxelEditorMode rejected because current pawn is not AVoxelEditorPawn. PlayerController=%s Pawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	ARTPSCharacterPlayer* OriginalCharacter = VoxelEditorOriginalPawn.Get();
	if (!IsValid(OriginalCharacter))
	{
		OriginalCharacter = Cast<ARTPSCharacterPlayer>(EditorPawn->GetOriginalPawn());
	}

	if (!IsValid(OriginalCharacter))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerExitVoxelEditorMode rejected because original character pawn is invalid. PlayerController=%s EditorPawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(EditorPawn),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	UInputMappingContext* CharacterIMC = CachedVoxelEditorCharacterIMC.Get();
	if (!IsValid(CharacterIMC))
	{
		CharacterIMC = OriginalCharacter->GetDefaultMappingContext();
	}

	Possess(OriginalCharacter);

	if (GetPawn() != OriginalCharacter)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerExitVoxelEditorMode failed because character possess did not take effect. PlayerController=%s OriginalPawn=%s EditorPawn=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(OriginalCharacter),
			*GetNameSafe(EditorPawn),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
		return;
	}

	ClientSetVoxelEditorCharacterMapping(CharacterIMC, true);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ServerExitVoxelEditorMode accepted. PlayerController=%s OriginalPawn=%s EditorPawn=%s Authority=%d Map=%s"),
		*GetNameSafe(this),
		*GetNameSafe(OriginalCharacter),
		*GetNameSafe(EditorPawn),
		HasAuthority() ? 1 : 0,
		*World->GetMapName());

	if (ActiveVoxelEditorPawn.Get() != EditorPawn)
	{
		ActiveVoxelEditorPawn = EditorPawn;
	}
	CleanupVoxelEditorPawn(TEXT("ServerExitVoxelEditorMode"));
}

void ARTPSPlayerController::ServerUpdateVoxelEditorViewTransform_Implementation(FVector ViewLocation, FRotator ViewRotation)
{
	UWorld* World = GetWorld();
	const double CurrentTime = FPlatformTime::Seconds();

	auto LogRejectedUpdate = [&](const FString& Reason)
	{
		const float EffectiveLogInterval = FMath::Max(VoxelEditorViewTransformLogInterval, 1.0f);
		if (LastRejectedVoxelEditorViewLogServerTime >= 0.0
			&& CurrentTime - LastRejectedVoxelEditorViewLogServerTime < static_cast<double>(EffectiveLogInterval))
		{
			return;
		}

		LastRejectedVoxelEditorViewLogServerTime = CurrentTime;
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerUpdateVoxelEditorViewTransform rejected. Controller=%s Pawn=%s Reason=%s ViewLocation=%s ViewRotation=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			*Reason,
			*ViewLocation.ToCompactString(),
			*ViewRotation.ToCompactString(),
			HasAuthority() ? 1 : 0,
			World != nullptr ? *World->GetMapName() : TEXT("None"));
	};

	if (World == nullptr)
	{
		LogRejectedUpdate(TEXT("World is invalid."));
		return;
	}

	AVoxelEditorPawn* EditorPawn = Cast<AVoxelEditorPawn>(GetPawn());
	if (!IsValid(EditorPawn))
	{
		LogRejectedUpdate(TEXT("Controller is not currently possessing AVoxelEditorPawn."));
		return;
	}

	if (EditorPawn->GetController() != this)
	{
		LogRejectedUpdate(TEXT("Voxel editor pawn is not controlled by this PlayerController."));
		return;
	}

	if (!IsVoxelEditorViewTransformFinite(ViewLocation, ViewRotation))
	{
		LogRejectedUpdate(TEXT("View transform contains NaN or non-finite values."));
		return;
	}

	const APawn* OriginalPawn = VoxelEditorOriginalPawn.Get();
	if (!IsValid(OriginalPawn))
	{
		OriginalPawn = EditorPawn->GetOriginalPawn();
	}

	if (IsValid(OriginalPawn))
	{
		const float EffectiveMaxOriginDistance = FMath::Max(MaxVoxelEditorViewDistanceFromOriginalPawn, 1.f);
		const float OriginDistanceSquared = FVector::DistSquared(OriginalPawn->GetActorLocation(), ViewLocation);
		if (!FMath::IsFinite(OriginDistanceSquared))
		{
			LogRejectedUpdate(TEXT("Distance from original pawn was non-finite."));
			return;
		}

		if (OriginDistanceSquared > FMath::Square(EffectiveMaxOriginDistance))
		{
			LogRejectedUpdate(FString::Printf(
				TEXT("View transform is too far from original pawn. Distance=%.3f Max=%.3f"),
				FMath::Sqrt(OriginDistanceSquared),
				EffectiveMaxOriginDistance));
			return;
		}
	}

	if (bHasApprovedVoxelEditorViewTransform)
	{
		const float EffectiveMaxUpdateDistance = FMath::Max(MaxVoxelEditorViewUpdateDistance, 1.f);
		const float UpdateDistanceSquared = FVector::DistSquared(ApprovedVoxelEditorViewLocation, ViewLocation);
		if (!FMath::IsFinite(UpdateDistanceSquared))
		{
			LogRejectedUpdate(TEXT("Distance from previous approved editor view was non-finite."));
			return;
		}

		if (UpdateDistanceSquared > FMath::Square(EffectiveMaxUpdateDistance))
		{
			LogRejectedUpdate(FString::Printf(
				TEXT("View transform update jump is too large. Distance=%.3f Max=%.3f"),
				FMath::Sqrt(UpdateDistanceSquared),
				EffectiveMaxUpdateDistance));
			return;
		}
	}

	const bool bHadLoggedAcceptedUpdate = LastAcceptedVoxelEditorViewLogServerTime >= 0.0;
	ApprovedVoxelEditorViewLocation = ViewLocation;
	ApprovedVoxelEditorViewRotation = ViewRotation.GetNormalized();
	LastApprovedVoxelEditorViewServerTime = CurrentTime;
	bHasApprovedVoxelEditorViewTransform = true;

	const float EffectiveLogInterval = FMath::Max(VoxelEditorViewTransformLogInterval, 0.0f);
	const bool bShouldLogAccepted = !bHadLoggedAcceptedUpdate
		|| (EffectiveLogInterval > 0.f
			&& CurrentTime - LastAcceptedVoxelEditorViewLogServerTime >= static_cast<double>(EffectiveLogInterval));
	if (bShouldLogAccepted)
	{
		LastAcceptedVoxelEditorViewLogServerTime = CurrentTime;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] ServerUpdateVoxelEditorViewTransform accepted. Controller=%s Pawn=%s ViewLocation=%s ViewRotation=%s Authority=%d Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(EditorPawn),
			*ApprovedVoxelEditorViewLocation.ToCompactString(),
			*ApprovedVoxelEditorViewRotation.ToCompactString(),
			HasAuthority() ? 1 : 0,
			*World->GetMapName());
	}
}

void ARTPSPlayerController::ServerRequestVoxelChunkState_Implementation(FIntVector ChunkCoord, int32 ClientKnownRevision)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerRequestVoxelChunkState rejected because World is invalid. PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision);
		return;
	}

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Server chunk state compatibility request failed because no VoxelChunkManager was found. PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Map=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			*World->GetMapName());
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ServerRequestVoxelChunkState compatibility path converted to subscription. PlayerController=%s ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Map=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ChunkManager),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ClientKnownRevision,
		*World->GetMapName());

	ChunkManager->SubscribePlayerToChunk(this, ChunkCoord, ClientKnownRevision, TEXT("ServerRequestVoxelChunkStateCompatibility"));
}

void ARTPSPlayerController::ServerSubscribeVoxelChunk_Implementation(FIntVector ChunkCoord, int32 ClientKnownRevision)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerSubscribeVoxelChunk rejected because World is invalid. PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision);
		return;
	}

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerSubscribeVoxelChunk failed because no VoxelChunkManager was found. PlayerController=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Map=%s"),
			*GetNameSafe(this),
			ChunkCoord.X,
			ChunkCoord.Y,
			ChunkCoord.Z,
			ClientKnownRevision,
			*World->GetMapName());
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Server received voxel chunk subscribe. PlayerController=%s ChunkManager=%s ChunkCoord=(%d,%d,%d) ClientKnownRevision=%d Map=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ChunkManager),
		ChunkCoord.X,
		ChunkCoord.Y,
		ChunkCoord.Z,
		ClientKnownRevision,
		*World->GetMapName());

	ChunkManager->SubscribePlayerToChunk(this, ChunkCoord, ClientKnownRevision, TEXT("ServerSubscribeVoxelChunk"));
}

void ARTPSPlayerController::ServerUnsubscribeVoxelChunk_Implementation(FIntVector ChunkCoord)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass())))
	{
		ChunkManager->UnsubscribePlayerFromChunk(this, ChunkCoord, TEXT("ServerUnsubscribeVoxelChunk"));
	}
}

void ARTPSPlayerController::ServerUnsubscribeAllVoxelChunks_Implementation()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	if (AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass())))
	{
		ChunkManager->UnsubscribePlayerFromAllChunks(this, TEXT("ServerUnsubscribeAllVoxelChunks"));
	}
}

void ARTPSPlayerController::ClientReceiveVoxelChunkState_Implementation(const FRTPSVoxelChunkStatePayload& Payload)
{
	UWorld* World = GetWorld();
	if (!IsLocalController() || World == nullptr || Player == nullptr || GetLocalPlayer() == nullptr || IsPendingKillPending())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ClientReceiveVoxelChunkState ignored by stale or non-local controller. PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d bSuccess=%d WorldValid=%d PlayerValid=%d LocalPlayerValid=%d"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			Payload.bSuccess ? 1 : 0,
			World != nullptr ? 1 : 0,
			Player != nullptr ? 1 : 0,
			GetLocalPlayer() != nullptr ? 1 : 0);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Client received chunk state payload. PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d bSuccess=%d bHasDensity=%d DensityCount=%d FailureReason=%s Map=%s"),
		*GetNameSafe(this),
		Payload.ChunkCoord.X,
		Payload.ChunkCoord.Y,
		Payload.ChunkCoord.Z,
		Payload.Revision,
		Payload.bSuccess ? 1 : 0,
		Payload.bHasDensity ? 1 : 0,
		Payload.LatticeDensity.Num(),
		*Payload.FailureReason,
		*World->GetMapName());

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client chunk state payload ignored because no VoxelChunkManager was found. PlayerController=%s ChunkCoord=(%d,%d,%d) ServerRevision=%d Map=%s"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.Revision,
			*World->GetMapName());
		return;
	}

	ChunkManager->ApplyChunkStatePayloadLocal(Payload, TEXT("ClientReceiveVoxelChunkState"));
}

void ARTPSPlayerController::ClientReceiveVoxelChunkDelta_Implementation(const FRTPSVoxelChunkDeltaPayload& Payload)
{
	UWorld* World = GetWorld();
	if (!IsLocalController() || World == nullptr || Player == nullptr || GetLocalPlayer() == nullptr || IsPendingKillPending())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ClientReceiveVoxelChunkDelta ignored by stale or non-local controller. PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d bSuccess=%d WorldValid=%d PlayerValid=%d LocalPlayerValid=%d"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.FromRevision,
			Payload.ToRevision,
			Payload.EditOps.Num(),
			Payload.bSuccess ? 1 : 0,
			World != nullptr ? 1 : 0,
			Player != nullptr ? 1 : 0,
			GetLocalPlayer() != nullptr ? 1 : 0);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Client received chunk delta payload. PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d bSuccess=%d bRequiresFullSnapshot=%d FailureReason=%s Map=%s"),
		*GetNameSafe(this),
		Payload.ChunkCoord.X,
		Payload.ChunkCoord.Y,
		Payload.ChunkCoord.Z,
		Payload.FromRevision,
		Payload.ToRevision,
		Payload.EditOps.Num(),
		Payload.bSuccess ? 1 : 0,
		Payload.bRequiresFullSnapshot ? 1 : 0,
		*Payload.FailureReason,
		*World->GetMapName());

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client chunk delta payload ignored because no VoxelChunkManager was found. PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d Map=%s"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.FromRevision,
			Payload.ToRevision,
			*World->GetMapName());
		return;
	}

	const bool bApplied = ChunkManager->ApplyChunkDeltaPayloadLocal(Payload, TEXT("ClientReceiveVoxelChunkDelta"));
	if (!bApplied)
	{
		ChunkManager->MarkChunkForFullSnapshotResync(Payload.ChunkCoord, TEXT("ClientReceiveVoxelChunkDelta apply failed"));
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] Client chunk delta apply failed; marked chunk for full snapshot resync. PlayerController=%s ChunkCoord=(%d,%d,%d) FromRevision=%d ToRevision=%d EditOpCount=%d FailureReason=%s"),
			*GetNameSafe(this),
			Payload.ChunkCoord.X,
			Payload.ChunkCoord.Y,
			Payload.ChunkCoord.Z,
			Payload.FromRevision,
			Payload.ToRevision,
			Payload.EditOps.Num(),
			*Payload.FailureReason);
	}
}

void ARTPSPlayerController::ClientReceiveVoxelEditOp_Implementation(const FRTPSVoxelEditOp& EditOp)
{
	UWorld* World = GetWorld();
	if (!IsLocalController() || World == nullptr || Player == nullptr || GetLocalPlayer() == nullptr || IsPendingKillPending())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ClientReceiveVoxelEditOp ignored by stale or non-local controller. PlayerController=%s ServerSequence=%lld WorldValid=%d PlayerValid=%d LocalPlayerValid=%d"),
			*GetNameSafe(this),
			EditOp.ServerSequence,
			World != nullptr ? 1 : 0,
			Player != nullptr ? 1 : 0,
			GetLocalPlayer() != nullptr ? 1 : 0);
		return;
	}

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ClientReceiveVoxelEditOp ignored because no VoxelChunkManager was found. PlayerController=%s ServerSequence=%lld Map=%s"),
			*GetNameSafe(this),
			EditOp.ServerSequence,
			*World->GetMapName());
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] Client received targeted VoxelEditOp. PlayerController=%s ChunkManager=%s ServerSequence=%lld Map=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ChunkManager),
		EditOp.ServerSequence,
		*World->GetMapName());

	const int32 AppliedChunks = ChunkManager->ApplyEditOpLocal(EditOp);
	if (AppliedChunks == 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] Client live VoxelEditOp applied to zero local ready density chunks; waiting for subscribe payload if needed. PlayerController=%s ChunkManager=%s ServerSequence=%lld Map=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ChunkManager),
			EditOp.ServerSequence,
			*World->GetMapName());
	}
}

void ARTPSPlayerController::ClientSetVoxelEditorCharacterMapping_Implementation(UInputMappingContext* CharacterMappingContext, bool bEnableMapping)
{
	SetVoxelEditorCharacterMappingLocal(CharacterMappingContext, bEnableMapping);
}

void ARTPSPlayerController::SetVoxelEditorCharacterMappingLocal(UInputMappingContext* CharacterMappingContext, bool bEnableMapping)
{
	if (!IsLocalController() || !IsValid(CharacterMappingContext))
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (LocalPlayer == nullptr)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
	if (Subsystem == nullptr)
	{
		return;
	}

	if (bEnableMapping)
	{
		Subsystem->AddMappingContext(CharacterMappingContext, 0);
	}
	else
	{
		Subsystem->RemoveMappingContext(CharacterMappingContext);
	}
}

bool ARTPSPlayerController::IsVoxelBrushFinite(const FVoxelBrush& Brush) const
{
	return FMath::IsFinite(Brush.WorldPosition.X)
		&& FMath::IsFinite(Brush.WorldPosition.Y)
		&& FMath::IsFinite(Brush.WorldPosition.Z)
		&& FMath::IsFinite(Brush.Radius)
		&& FMath::IsFinite(Brush.Strength)
		&& FMath::IsFinite(Brush.SurfaceNormal.X)
		&& FMath::IsFinite(Brush.SurfaceNormal.Y)
		&& FMath::IsFinite(Brush.SurfaceNormal.Z)
		&& FMath::IsFinite(Brush.SurfaceDepth)
		&& FMath::IsFinite(Brush.BackDepth)
		&& FMath::IsFinite(Brush.EmbedDepth)
		&& FMath::IsFinite(Brush.ClumpRoundnessPower)
		&& FMath::IsFinite(Brush.ConvergenceAlpha);
}

bool ARTPSPlayerController::ValidateVoxelBrushRequest(
	const FVoxelBrush& Brush,
	FString& OutReason,
	FString* OutValidationSource,
	FString* OutValidationSourceReason,
	float* OutDistance,
	float* OutMaxDistance) const
{
	OutReason.Reset();
	if (OutValidationSource != nullptr)
	{
		*OutValidationSource = TEXT("Unresolved");
	}
	if (OutValidationSourceReason != nullptr)
	{
		*OutValidationSourceReason = TEXT("");
	}
	if (OutDistance != nullptr)
	{
		*OutDistance = 0.f;
	}
	if (OutMaxDistance != nullptr)
	{
		*OutMaxDistance = FMath::Max(MaxVoxelEditDistance, 1.f);
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutReason = TEXT("World is invalid.");
		return false;
	}

	const APawn* ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn))
	{
		OutReason = TEXT("PlayerController has no valid pawn.");
		return false;
	}

	if (!IsVoxelBrushFinite(Brush))
	{
		OutReason = TEXT("Brush contains NaN or non-finite values.");
		return false;
	}

	if (Brush.Radius <= 0.f)
	{
		OutReason = FString::Printf(TEXT("Brush radius must be positive. Radius=%.3f"), Brush.Radius);
		return false;
	}

	const float EffectiveMaxRadius = FMath::Max(MaxVoxelBrushRadius, KINDA_SMALL_NUMBER);
	if (Brush.Radius > EffectiveMaxRadius)
	{
		OutReason = FString::Printf(TEXT("Brush radius exceeds maximum. Radius=%.3f Max=%.3f"), Brush.Radius, EffectiveMaxRadius);
		return false;
	}

	if (Brush.Strength <= 0.f)
	{
		OutReason = FString::Printf(TEXT("Brush strength must be positive. Strength=%.3f"), Brush.Strength);
		return false;
	}

	const float EffectiveMaxStrength = FMath::Max(MaxVoxelBrushStrength, KINDA_SMALL_NUMBER);
	if (Brush.Strength > EffectiveMaxStrength)
	{
		OutReason = FString::Printf(TEXT("Brush strength exceeds maximum. Strength=%.3f Max=%.3f"), Brush.Strength, EffectiveMaxStrength);
		return false;
	}

	const int32 BrushModeValue = static_cast<int32>(Brush.Mode);
	if (BrushModeValue < static_cast<int32>(EVoxelBrushMode::Add) || BrushModeValue > static_cast<int32>(EVoxelBrushMode::Remove))
	{
		OutReason = FString::Printf(TEXT("Brush mode is invalid. Mode=%d"), BrushModeValue);
		return false;
	}

	const int32 BrushShapeValue = static_cast<int32>(Brush.Shape);
	if (BrushShapeValue < static_cast<int32>(EVoxelBrushShape::Sphere) || BrushShapeValue > static_cast<int32>(EVoxelBrushShape::TerrainMudBlob))
	{
		OutReason = FString::Printf(TEXT("Brush shape is invalid. Shape=%d"), BrushShapeValue);
		return false;
	}

	const int32 FalloffValue = static_cast<int32>(Brush.Falloff);
	if (FalloffValue < static_cast<int32>(EVoxelBrushFalloff::Linear) || FalloffValue > static_cast<int32>(EVoxelBrushFalloff::Plateau))
	{
		OutReason = FString::Printf(TEXT("Brush falloff is invalid. Falloff=%d"), FalloffValue);
		return false;
	}

	const int32 BlendModeValue = static_cast<int32>(Brush.BlendMode);
	if (BlendModeValue < static_cast<int32>(EVoxelBrushBlendMode::Additive) || BlendModeValue > static_cast<int32>(EVoxelBrushBlendMode::TargetLerp))
	{
		OutReason = FString::Printf(TEXT("Brush blend mode is invalid. BlendMode=%d"), BlendModeValue);
		return false;
	}

	if ((Brush.Shape == EVoxelBrushShape::Sphere || Brush.Shape == EVoxelBrushShape::TerrainMudBlob)
		&& Brush.BlendMode == EVoxelBrushBlendMode::TargetLerp
		&& (Brush.ConvergenceAlpha < 0.f || Brush.ConvergenceAlpha > 1.f))
	{
		OutReason = FString::Printf(TEXT("%s convergence alpha must be in [0,1]. ConvergenceAlpha=%.3f"), GetVoxelBrushShapeDebugName(Brush.Shape), Brush.ConvergenceAlpha);
		return false;
	}

	if (Brush.Shape == EVoxelBrushShape::SurfaceBlob || Brush.Shape == EVoxelBrushShape::TerrainMudBlob)
	{
		if (Brush.Mode != EVoxelBrushMode::Add)
		{
			OutReason = FString::Printf(TEXT("%s currently supports Add mode only."), GetVoxelBrushShapeDebugName(Brush.Shape));
			return false;
		}

		if (Brush.SurfaceDepth <= 0.f)
		{
			OutReason = FString::Printf(TEXT("%s depth must be positive. SurfaceDepth=%.3f"), GetVoxelBrushShapeDebugName(Brush.Shape), Brush.SurfaceDepth);
			return false;
		}

		if (Brush.SurfaceNormal.IsNearlyZero())
		{
			OutReason = FString::Printf(TEXT("%s surface normal must be non-zero."), GetVoxelBrushShapeDebugName(Brush.Shape));
			return false;
		}
	}

	if (Brush.Shape == EVoxelBrushShape::TerrainMudBlob)
	{
		if (Brush.BackDepth < 0.f)
		{
			OutReason = FString::Printf(TEXT("TerrainMudBlob back depth must be non-negative. BackDepth=%.3f"), Brush.BackDepth);
			return false;
		}

		if (Brush.EmbedDepth < 0.f)
		{
			OutReason = FString::Printf(TEXT("TerrainMudBlob embed depth must be non-negative. EmbedDepth=%.3f"), Brush.EmbedDepth);
			return false;
		}

		if (Brush.ClumpRoundnessPower < 1.f)
		{
			OutReason = FString::Printf(TEXT("TerrainMudBlob roundness power must be at least 1.0. ClumpRoundnessPower=%.3f"), Brush.ClumpRoundnessPower);
			return false;
		}

	}

	FVector ValidationLocation = ControlledPawn->GetActorLocation();
	FString ValidationSource = TEXT("ServerPawnLocation");
	FString ValidationSourceReason = TEXT("Controlled pawn is not AVoxelEditorPawn.");

	if (IsValid(Cast<const AVoxelEditorPawn>(ControlledPawn)))
	{
		FString EditorViewReason;
		if (TryGetApprovedVoxelEditorViewLocation(ValidationLocation, EditorViewReason))
		{
			ValidationSource = TEXT("EditorViewTransform");
			ValidationSourceReason = EditorViewReason;
		}
		else
		{
			ValidationSource = TEXT("ServerPawnLocation");
			ValidationSourceReason = EditorViewReason;
		}
	}

	if (OutValidationSource != nullptr)
	{
		*OutValidationSource = ValidationSource;
	}
	if (OutValidationSourceReason != nullptr)
	{
		*OutValidationSourceReason = ValidationSourceReason;
	}

	const float DistanceSquared = FVector::DistSquared(ValidationLocation, Brush.WorldPosition);
	if (!FMath::IsFinite(DistanceSquared))
	{
		OutReason = TEXT("Brush distance calculation was non-finite.");
		return false;
	}

	const float EffectiveMaxDistance = FMath::Max(MaxVoxelEditDistance, 1.f);
	const float MaxDistanceSquared = FMath::Square(EffectiveMaxDistance);
	if (OutDistance != nullptr)
	{
		*OutDistance = FMath::Sqrt(DistanceSquared);
	}
	if (OutMaxDistance != nullptr)
	{
		*OutMaxDistance = EffectiveMaxDistance;
	}
	if (DistanceSquared > MaxDistanceSquared)
	{
		OutReason = FString::Printf(
			TEXT("Brush is too far from validation source. Distance=%.3f Max=%.3f Source=%s SourceReason=%s"),
			FMath::Sqrt(DistanceSquared),
			EffectiveMaxDistance,
			*ValidationSource,
			*ValidationSourceReason);
		return false;
	}

	return true;
}

void ARTPSPlayerController::ServerApplyVoxelBrush_Implementation(FVoxelBrush Brush)
{
	FString RejectionReason;
	FString ValidationSource;
	FString ValidationSourceReason;
	float ValidationDistance = 0.f;
	float ValidationMaxDistance = FMath::Max(MaxVoxelEditDistance, 1.f);
	if (!ValidateVoxelBrushRequest(
		Brush,
		RejectionReason,
		&ValidationSource,
		&ValidationSourceReason,
		&ValidationDistance,
		&ValidationMaxDistance))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerApplyVoxelBrush rejected. PlayerController=%s Pawn=%s Source=%s SourceReason=%s Distance=%.2f MaxDistance=%.2f Reason=%s Position=%s Radius=%.2f Strength=%.2f Mode=%d Shape=%d"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			*ValidationSource,
			*ValidationSourceReason,
			ValidationDistance,
			ValidationMaxDistance,
			*RejectionReason,
			*Brush.WorldPosition.ToCompactString(),
			Brush.Radius,
			Brush.Strength,
			static_cast<int32>(Brush.Mode),
			static_cast<int32>(Brush.Shape));
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerApplyVoxelBrush rejected because no VoxelChunkManager was found. PlayerController=%s Map=%s"),
			*GetNameSafe(this),
			*World->GetMapName());
		return;
	}

	if (MinVoxelBrushInterval > 0.f)
	{
		const double CurrentTime = FPlatformTime::Seconds();
		const double ElapsedSeconds = CurrentTime - LastVoxelBrushRequestServerTime;
		if (LastVoxelBrushRequestServerTime >= 0.0 && ElapsedSeconds < static_cast<double>(MinVoxelBrushInterval))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[RTPSValidation] ServerApplyVoxelBrush throttled. PlayerController=%s Pawn=%s Elapsed=%.3f MinInterval=%.3f"),
				*GetNameSafe(this),
				*GetNameSafe(GetPawn()),
				ElapsedSeconds,
				MinVoxelBrushInterval);
			return;
		}

		LastVoxelBrushRequestServerTime = CurrentTime;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ServerApplyVoxelBrush accepted and forwarding. PlayerController=%s Pawn=%s Map=%s Source=%s SourceReason=%s Distance=%.2f MaxDistance=%.2f Position=%s Radius=%.2f Strength=%.2f Mode=%d Shape=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetPawn()),
		*World->GetMapName(),
		*ValidationSource,
		*ValidationSourceReason,
		ValidationDistance,
		ValidationMaxDistance,
		*Brush.WorldPosition.ToCompactString(),
		Brush.Radius,
		Brush.Strength,
		static_cast<int32>(Brush.Mode),
		static_cast<int32>(Brush.Shape));

	ChunkManager->ApplyBrushAuthoritative(Brush);
}

void ARTPSPlayerController::ServerFireVoxelShot_Implementation(EVoxelBrushMode Mode)
{
	const int64 ShotId = NextVoxelShotDebugId++;
	FVector TraceStart = FVector::ZeroVector;
	FVector TraceEnd = FVector::ZeroVector;
	const float EffectiveCooldown = FMath::Max(VoxelShotCooldownSeconds, 0.f);
	double ElapsedSeconds = LastVoxelShotRequestServerTime >= 0.0
		? FPlatformTime::Seconds() - LastVoxelShotRequestServerTime
		: TNumericLimits<double>::Max();
	const auto ShowVoxelShotSummary = [this](const FString& Summary)
	{
		if (bShowVoxelShotDebugOnScreen && GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, VoxelShotDebugDrawLifetimeSeconds, FColor::Cyan, Summary);
		}
	};

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, false, TEXT("InvalidWorld"));
		ShowVoxelShotSummary(FString::Printf(TEXT("VoxelShot #%lld %s | Rejected=InvalidWorld"), ShotId, GetVoxelBrushModeDebugName(Mode)));
		return;
	}

	const int32 BrushModeValue = static_cast<int32>(Mode);
	if (BrushModeValue < static_cast<int32>(EVoxelBrushMode::Add) || BrushModeValue > static_cast<int32>(EVoxelBrushMode::Remove))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerFireVoxelShot rejected because brush mode is invalid. PlayerController=%s Pawn=%s Mode=%d"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			BrushModeValue);
		LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, false, TEXT("InvalidBrushMode"));
		ShowVoxelShotSummary(FString::Printf(TEXT("VoxelShot #%lld Invalid | Rejected=InvalidBrushMode"), ShotId));
		return;
	}

	const double CurrentTime = FPlatformTime::Seconds();
	ElapsedSeconds = LastVoxelShotRequestServerTime >= 0.0
		? CurrentTime - LastVoxelShotRequestServerTime
		: TNumericLimits<double>::Max();
	if (EffectiveCooldown > 0.f
		&& LastVoxelShotRequestServerTime >= 0.0
		&& ElapsedSeconds < static_cast<double>(EffectiveCooldown))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerFireVoxelShot throttled. PlayerController=%s Pawn=%s Elapsed=%.3f Cooldown=%.3f"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			ElapsedSeconds,
			EffectiveCooldown);
		LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, false, TEXT("Cooldown"));
		ShowVoxelShotSummary(FString::Printf(TEXT("VoxelShot #%lld %s | Rejected=Cooldown"), ShotId, GetVoxelBrushModeDebugName(Mode)));
		return;
	}

	LastVoxelShotRequestServerTime = CurrentTime;

	FVoxelBrush Brush;
	FHitResult HitResult;
	FString TraceFailureReason;
	ERTPSVoxelShotHitKind HitKind = ERTPSVoxelShotHitKind::None;
	if (!TryBuildVoxelShotBrush(Mode, Brush, HitResult, TraceStart, TraceEnd, TraceFailureReason, HitKind))
	{
		if (bDebugVoxelShots && HitResult.bBlockingHit)
		{
			const float HitDistance = FVector::Dist(TraceStart, HitResult.ImpactPoint);
			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelShotDebug] Hit ShotId=%lld HitKind=%s HitActor=%s HitComponent=%s HitLocation=%s ImpactNormal=%s Distance=%.2f IsVoxelChunk=0 HitChunkCoord=None HitLocalPosition=None"),
				ShotId,
				GetVoxelShotHitKindDebugName(HitKind),
				*GetNameSafe(HitResult.GetActor()),
				*GetNameSafe(HitResult.GetComponent()),
				*HitResult.ImpactPoint.ToCompactString(),
				*HitResult.ImpactNormal.ToCompactString(),
				HitDistance);
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("[RTPSValidation] ServerFireVoxelShot produced no terrain hit. PlayerController=%s Pawn=%s Mode=%d Range=%.2f"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			BrushModeValue,
			VoxelShotRange);
		DrawVoxelShotDebug(TraceStart, TraceEnd, HitResult, nullptr, Cast<AVoxelChunk>(HitResult.GetActor()), false);
		LogVoxelShotRequestDebug(
			ShotId,
			Mode,
			TraceStart,
			TraceEnd,
			EffectiveCooldown,
			ElapsedSeconds,
			false,
			TraceFailureReason.IsEmpty() ? TEXT("InvalidHitResult") : *TraceFailureReason);
		ShowVoxelShotSummary(FString::Printf(
			TEXT("VoxelShot #%lld %s | Rejected=%s"),
			ShotId,
			GetVoxelBrushModeDebugName(Mode),
			TraceFailureReason.IsEmpty() ? TEXT("InvalidHitResult") : *TraceFailureReason));
		return;
	}

	FString RejectionReason;
	FString ValidationSource;
	FString ValidationSourceReason;
	float ValidationDistance = 0.f;
	float ValidationMaxDistance = FMath::Max(MaxVoxelEditDistance, 1.f);
	if (!ValidateVoxelBrushRequest(
		Brush,
		RejectionReason,
		&ValidationSource,
		&ValidationSourceReason,
		&ValidationDistance,
		&ValidationMaxDistance))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerFireVoxelShot rejected after server trace validation. PlayerController=%s Pawn=%s Source=%s SourceReason=%s Distance=%.2f MaxDistance=%.2f Reason=%s Position=%s Radius=%.2f Strength=%.2f Mode=%d Shape=%d"),
			*GetNameSafe(this),
			*GetNameSafe(GetPawn()),
			*ValidationSource,
			*ValidationSourceReason,
			ValidationDistance,
			ValidationMaxDistance,
			*RejectionReason,
			*Brush.WorldPosition.ToCompactString(),
			Brush.Radius,
			Brush.Strength,
			static_cast<int32>(Brush.Mode),
			static_cast<int32>(Brush.Shape));
		DrawVoxelShotDebug(TraceStart, TraceEnd, HitResult, &Brush, Cast<AVoxelChunk>(HitResult.GetActor()), false);
		LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, false, TEXT("BrushValidationFailed"));
		ShowVoxelShotSummary(FString::Printf(TEXT("VoxelShot #%lld %s | Rejected=BrushValidationFailed"), ShotId, GetVoxelBrushModeDebugName(Mode)));
		return;
	}

	AVoxelChunkManager* ChunkManager = Cast<AVoxelChunkManager>(
		UGameplayStatics::GetActorOfClass(World, AVoxelChunkManager::StaticClass()));
	if (!IsValid(ChunkManager))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[RTPSValidation] ServerFireVoxelShot rejected because no VoxelChunkManager was found. PlayerController=%s Map=%s"),
			*GetNameSafe(this),
			*World->GetMapName());
		DrawVoxelShotDebug(TraceStart, TraceEnd, HitResult, &Brush, Cast<AVoxelChunk>(HitResult.GetActor()), false);
		LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, false, TEXT("NoVoxelChunkManager"));
		ShowVoxelShotSummary(FString::Printf(TEXT("VoxelShot #%lld %s | Rejected=NoVoxelChunkManager"), ShotId, GetVoxelBrushModeDebugName(Mode)));
		return;
	}

	AVoxelChunk* HitChunk = Cast<AVoxelChunk>(HitResult.GetActor());
	if (HitKind == ERTPSVoxelShotHitKind::NonVoxelRejected
		|| (HitKind == ERTPSVoxelShotHitKind::VoxelChunkHit && !IsValid(HitChunk)))
	{
		DrawVoxelShotDebug(TraceStart, TraceEnd, HitResult, &Brush, nullptr, false);
		const TCHAR* FinalRejectionReason = TraceFailureReason.IsEmpty() ? TEXT("InvalidHitResult") : *TraceFailureReason;
		LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, false, FinalRejectionReason);
		ShowVoxelShotSummary(FString::Printf(TEXT("VoxelShot #%lld %s | Rejected=%s"), ShotId, GetVoxelBrushModeDebugName(Mode), FinalRejectionReason));
		return;
	}

	const bool bVoxelChunkHit = HitKind == ERTPSVoxelShotHitKind::VoxelChunkHit && IsValid(HitChunk);
	const bool bBuildSurfaceHit = HitKind == ERTPSVoxelShotHitKind::BuildSurfaceHit;
	const bool bNeedsBoundaryInfo = bVoxelChunkHit && (
		bDebugVoxelShots
		|| bShowVoxelShotDebugOnScreen
		|| ChunkManager->bDebugVoxelChunkBoundaries
		|| ChunkManager->bDrawVoxelChunkBoundaryDebug);
	FRTPSVoxelChunkBoundaryDebugInfo BoundaryInfo;
	if (bNeedsBoundaryInfo)
	{
		BoundaryInfo = ChunkManager->BuildChunkBoundaryDebugInfo(*HitChunk, HitResult.ImpactPoint);
	}
	TArray<FIntVector> DensityAffectedChunkCoords;
	TArray<FIntVector> MeshDirtyChunkCoords;
	if (bShowVoxelShotDebugOnScreen)
	{
		ChunkManager->GetBrushDebugChunkCoverage(Brush, DensityAffectedChunkCoords, MeshDirtyChunkCoords);
	}

	if (bDebugVoxelShots)
	{
		const float HitDistance = FVector::Dist(TraceStart, HitResult.ImpactPoint);
		if (bVoxelChunkHit)
		{
			const FVector DebugRawNormal = HitResult.ImpactNormal.GetSafeNormal();
			const FVector DebugIncomingDir = (TraceEnd - TraceStart).GetSafeNormal();
			const FVector DebugShotOpposite = DebugIncomingDir.IsNearlyZero() ? FVector::ZeroVector : -DebugIncomingDir;
			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelShotDebug] Hit ShotId=%lld HitKind=%s Mode=%s HitActor=%s HitComponent=%s HitLocation=%s ImpactNormal=%s RawNormal=%s ShotOpposite=%s ShotDirectionBlend=%.3f Distance=%.2f IsVoxelChunk=1 HitChunkCoord=(%d,%d,%d) HitLocalPosition=%s Shape=%s Falloff=%s BrushPosition=%s StableNormal=%s SurfaceDepth=%.2f BackDepth=%.2f EmbedDepth=%.2f RoundnessPower=%.2f Radius=%.2f Strength=%.3f BlendMode=%s ConvergenceAlpha=%.3f IsoLevel=%.3f UnifiedSphere=%d AddRadius=%.2f RemoveRadius=%.2f AddStrength=%.3f RemoveStrength=%.3f VoxelChunkAddEmbedDepth=%.2f"),
				ShotId,
				GetVoxelShotHitKindDebugName(HitKind),
				GetVoxelBrushModeDebugName(Mode),
				*GetNameSafe(HitResult.GetActor()),
				*GetNameSafe(HitResult.GetComponent()),
				*HitResult.ImpactPoint.ToCompactString(),
				*HitResult.ImpactNormal.ToCompactString(),
				*DebugRawNormal.ToCompactString(),
				*DebugShotOpposite.ToCompactString(),
				TerrainMudBlobShotDirectionBlend,
				HitDistance,
				HitChunk->ChunkCoord.X,
				HitChunk->ChunkCoord.Y,
				HitChunk->ChunkCoord.Z,
				*BoundaryInfo.LocalPosition.ToCompactString(),
				GetVoxelBrushShapeDebugName(Brush.Shape),
				GetVoxelBrushFalloffDebugName(Brush.Falloff),
				*Brush.WorldPosition.ToCompactString(),
				*Brush.SurfaceNormal.ToCompactString(),
				Brush.SurfaceDepth,
				Brush.BackDepth,
				Brush.EmbedDepth,
				Brush.ClumpRoundnessPower,
				Brush.Radius,
				Brush.Strength,
				GetVoxelBrushBlendModeDebugName(Brush.BlendMode),
				Brush.ConvergenceAlpha,
				HitChunk->IsoLevel,
				bUseUnifiedSphereVoxelShots ? 1 : 0,
				VoxelAddSphereRadius,
				VoxelRemoveSphereRadius,
				VoxelAddSphereStrength,
				VoxelRemoveSphereStrength,
				VoxelChunkAddSphereEmbedDepth);
		}
		else if (bBuildSurfaceHit)
		{
			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelShotDebug] Hit ShotId=%lld HitKind=%s HitActor=%s HitComponent=%s HitLocation=%s ImpactNormal=%s Distance=%.2f IsVoxelChunk=0 HitChunkCoord=None HitLocalPosition=None BuildSurfaceTag=%s Shape=%s Falloff=%s OriginalImpactPoint=%s BrushPosition=%s SurfaceNormal=%s Radius=%.2f Strength=%.3f BlendMode=%s ConvergenceAlpha=%.3f UnifiedSphere=%d BuildSurfaceAddSphereOffset=%.2f ExperimentalSurfaceBlobEnabled=%d"),
				ShotId,
				GetVoxelShotHitKindDebugName(HitKind),
				*GetNameSafe(HitResult.GetActor()),
				*GetNameSafe(HitResult.GetComponent()),
				*HitResult.ImpactPoint.ToCompactString(),
				*HitResult.ImpactNormal.ToCompactString(),
				HitDistance,
				*VoxelBuildSurfaceTag.ToString(),
				GetVoxelBrushShapeDebugName(Brush.Shape),
				GetVoxelBrushFalloffDebugName(Brush.Falloff),
				*HitResult.ImpactPoint.ToCompactString(),
				*Brush.WorldPosition.ToCompactString(),
				*Brush.SurfaceNormal.ToCompactString(),
				Brush.Radius,
				Brush.Strength,
				GetVoxelBrushBlendModeDebugName(Brush.BlendMode),
				Brush.ConvergenceAlpha,
				bUseUnifiedSphereVoxelShots ? 1 : 0,
				BuildSurfaceAddSphereOffset,
				bUseSurfaceBlobForBuildSurfaceAdd ? 1 : 0);
		}
	}

	if (bVoxelChunkHit && ChunkManager->bDebugVoxelChunkBoundaries)
	{
		UE_LOG(
			LogRTPSVoxelDebug,
			Log,
			TEXT("[VoxelBoundaryDebug] ShotId=%lld HitChunkCoord=(%d,%d,%d) HitLocalPosition=%s ChunkWorldOrigin=%s ChunkWorldSize=%s DistToMin=%s DistToMax=%s BoundaryAxes=%s ExpectedNeighbors=%d"),
			ShotId,
			BoundaryInfo.HitChunkCoord.X,
			BoundaryInfo.HitChunkCoord.Y,
			BoundaryInfo.HitChunkCoord.Z,
			*BoundaryInfo.LocalPosition.ToCompactString(),
			*BoundaryInfo.ChunkWorldOrigin.ToCompactString(),
			*BoundaryInfo.ChunkWorldSize.ToCompactString(),
			*BoundaryInfo.DistanceToMinBoundary.ToCompactString(),
			*BoundaryInfo.DistanceToMaxBoundary.ToCompactString(),
			*JoinBoundaryAxes(BoundaryInfo.BoundaryAxes),
			BoundaryInfo.ExpectedNeighborChunkCoords.Num());

		if (!BoundaryInfo.ExpectedNeighborChunkCoords.IsEmpty())
		{
			TArray<FString> NeighborStrings;
			NeighborStrings.Reserve(BoundaryInfo.ExpectedNeighborChunkCoords.Num());
			for (const FIntVector& NeighborCoord : BoundaryInfo.ExpectedNeighborChunkCoords)
			{
				NeighborStrings.Add(FString::Printf(TEXT("(%d,%d,%d)"), NeighborCoord.X, NeighborCoord.Y, NeighborCoord.Z));
			}

			UE_LOG(
				LogRTPSVoxelDebug,
				Log,
				TEXT("[VoxelBoundaryDebug] ShotId=%lld ExpectedNeighborCoords=%s"),
				ShotId,
				*FString::Join(NeighborStrings, TEXT(",")));
		}
	}

	const float DebugIsoLevel = HitChunk != nullptr ? HitChunk->IsoLevel : ChunkManager->IsoLevel;
	UE_LOG(
		LogTemp,
		Log,
		TEXT("[RTPSValidation] ServerFireVoxelShot accepted. PlayerController=%s Pawn=%s Map=%s Source=%s SourceReason=%s Distance=%.2f MaxDistance=%.2f HitKind=%s HitActor=%s Position=%s Radius=%.2f Strength=%.2f Mode=%d Shape=%s Falloff=%s SurfaceNormal=%s SurfaceDepth=%.2f BackDepth=%.2f EmbedDepth=%.2f RoundnessPower=%.2f BlendMode=%s ConvergenceAlpha=%.3f IsoLevel=%.3f"),
		*GetNameSafe(this),
		*GetNameSafe(GetPawn()),
		*World->GetMapName(),
		*ValidationSource,
		*ValidationSourceReason,
		ValidationDistance,
		ValidationMaxDistance,
		GetVoxelShotHitKindDebugName(HitKind),
		*GetNameSafe(HitResult.GetActor()),
		*Brush.WorldPosition.ToCompactString(),
		Brush.Radius,
		Brush.Strength,
		static_cast<int32>(Brush.Mode),
		GetVoxelBrushShapeDebugName(Brush.Shape),
		GetVoxelBrushFalloffDebugName(Brush.Falloff),
		*Brush.SurfaceNormal.ToCompactString(),
		Brush.SurfaceDepth,
		Brush.BackDepth,
		Brush.EmbedDepth,
		Brush.ClumpRoundnessPower,
		GetVoxelBrushBlendModeDebugName(Brush.BlendMode),
		Brush.ConvergenceAlpha,
		DebugIsoLevel);

	DrawVoxelShotDebug(TraceStart, TraceEnd, HitResult, &Brush, HitChunk, true);
	if (bVoxelChunkHit && ChunkManager->bDrawVoxelChunkBoundaryDebug && BoundaryInfo.IsNearBoundary())
	{
		ChunkManager->DrawChunkBoundaryDebug(BoundaryInfo);
	}

	ChunkManager->ApplyBrushAuthoritative(Brush);
	LogVoxelShotRequestDebug(ShotId, Mode, TraceStart, TraceEnd, EffectiveCooldown, ElapsedSeconds, true, TEXT("Accepted"));

	if (bBuildSurfaceHit)
	{
		ShowVoxelShotSummary(FString::Printf(
			TEXT("VoxelShot #%lld %s | BuildSurface | Shape=%s | Brush=%s | Density=%d | Dirty=%d | Accepted"),
			ShotId,
			GetVoxelBrushModeDebugName(Mode),
			GetVoxelBrushShapeDebugName(Brush.Shape),
			*Brush.WorldPosition.ToCompactString(),
			DensityAffectedChunkCoords.Num(),
			MeshDirtyChunkCoords.Num()));
	}
	else
	{
		ShowVoxelShotSummary(FString::Printf(
			TEXT("VoxelShot #%lld %s | HitChunk=(%d,%d,%d) | Shape=%s | Boundary=%s | Density=%d | Dirty=%d | Accepted"),
			ShotId,
			GetVoxelBrushModeDebugName(Mode),
			HitChunk->ChunkCoord.X,
			HitChunk->ChunkCoord.Y,
			HitChunk->ChunkCoord.Z,
			GetVoxelBrushShapeDebugName(Brush.Shape),
			*JoinBoundaryAxes(BoundaryInfo.BoundaryAxes),
			DensityAffectedChunkCoords.Num(),
			MeshDirtyChunkCoords.Num()));
	}
}
