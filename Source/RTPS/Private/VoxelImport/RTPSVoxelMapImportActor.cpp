// Fill out your copyright notice in the Description page of Project Settings.

#include "VoxelImport/RTPSVoxelMapImportActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/EngineTypes.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelImport/RTPSMarchingCubes.h"

namespace
{
	struct FImportedVoxelCell
	{
		FString TypeId;
		FVector CenterCm = FVector::ZeroVector;
		FVector ExtentCm = FVector(50.0f, 50.0f, 50.0f);
		FIntVector Grid = FIntVector::ZeroValue;
		FIntVector WorldGrid = FIntVector::ZeroValue;
		bool bHasGrid = false;
	};

	struct FImportedMarker
	{
		FString Id;
		FString Type;
		FString LinkedRef;
		FString Comment;
		FVector CenterCm = FVector::ZeroVector;
		FVector ExtentCm = FVector(50.0f, 50.0f, 50.0f);
	};

	struct FImportedMapDocument
	{
		FString Schema;
		FString MapId;
		double VoxelSizeCm = 100.0;
		FIntVector DimensionsVoxels = FIntVector::ZeroValue;
		TArray<FImportedVoxelCell> Cells;
		TArray<FImportedMarker> Markers;
	};

	struct FImportedChunkDocument
	{
		FString Schema;
		FString MapId;
		FString ChunkFile;
		double VoxelSizeCm = 100.0;
		FIntVector ChunkCoord = FIntVector::ZeroValue;
		FIntVector ChunkDimensions = FIntVector::ZeroValue;
		FIntVector ChunkOriginGrid = FIntVector::ZeroValue;
		TArray<FImportedVoxelCell> Cells;
	};

	struct FImportedWorldDocument
	{
		FString Schema;
		FString MapId;
		TArray<FString> ChunkFiles;
		FString MarkerFile;
	};

	bool LoadJsonRootObject(const FString& FilePath, TSharedPtr<FJsonObject>& OutRootObject, FString& OutError)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
		{
			OutError = FString::Printf(TEXT("JSON 파일을 읽지 못했습니다: %s"), *FilePath);
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, OutRootObject) || !OutRootObject.IsValid())
		{
			OutError = FString::Printf(TEXT("JSON 파싱에 실패했습니다: %s"), *FilePath);
			return false;
		}

		return true;
	}

	FString ResolveImportPath(const FString& BaseFilePath, const FString& RelativeOrAbsolutePath)
	{
		if (RelativeOrAbsolutePath.IsEmpty())
		{
			return RelativeOrAbsolutePath;
		}

		if (FPaths::IsRelative(RelativeOrAbsolutePath))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::GetPath(BaseFilePath), RelativeOrAbsolutePath);
		}

		return RelativeOrAbsolutePath;
	}

	bool TryReadVectorObject(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
	{
		const TSharedPtr<FJsonObject>* VectorObject = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(FieldName, VectorObject) || !VectorObject || !VectorObject->IsValid())
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!(*VectorObject)->TryGetNumberField(TEXT("x"), X) ||
			!(*VectorObject)->TryGetNumberField(TEXT("y"), Y) ||
			!(*VectorObject)->TryGetNumberField(TEXT("z"), Z))
		{
			return false;
		}

		OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
		return true;
	}

	bool TryReadIntVectorObject(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FIntVector& OutVector)
	{
		const TSharedPtr<FJsonObject>* VectorObject = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(FieldName, VectorObject) || !VectorObject || !VectorObject->IsValid())
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!(*VectorObject)->TryGetNumberField(TEXT("x"), X) ||
			!(*VectorObject)->TryGetNumberField(TEXT("y"), Y) ||
			!(*VectorObject)->TryGetNumberField(TEXT("z"), Z))
		{
			return false;
		}

		OutVector = FIntVector(
			FMath::RoundToInt(X),
			FMath::RoundToInt(Y),
			FMath::RoundToInt(Z));
		return true;
	}

	bool TryReadGridCenter(const TSharedPtr<FJsonObject>& Object, double VoxelSizeCm, FVector& OutCenterCm)
	{
		const TSharedPtr<FJsonObject>* GridObject = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(TEXT("grid"), GridObject) || !GridObject || !GridObject->IsValid())
		{
			return false;
		}

		double GridX = 0.0;
		double GridY = 0.0;
		double GridZ = 0.0;
		if (!(*GridObject)->TryGetNumberField(TEXT("x"), GridX) ||
			!(*GridObject)->TryGetNumberField(TEXT("y"), GridY) ||
			!(*GridObject)->TryGetNumberField(TEXT("z"), GridZ))
		{
			return false;
		}

		OutCenterCm = FVector(
			static_cast<float>((GridX + 0.5) * VoxelSizeCm),
			static_cast<float>((GridZ + 0.5) * VoxelSizeCm),
			static_cast<float>((GridY + 0.5) * VoxelSizeCm));
		return true;
	}

	bool TryReadCellGrid(const TSharedPtr<FJsonObject>& Object, FImportedVoxelCell& OutCell)
	{
		if (TryReadIntVectorObject(Object, TEXT("worldGrid"), OutCell.WorldGrid))
		{
			OutCell.Grid = OutCell.WorldGrid;
			OutCell.bHasGrid = true;
			return true;
		}

		if (TryReadIntVectorObject(Object, TEXT("grid"), OutCell.Grid))
		{
			OutCell.WorldGrid = OutCell.Grid;
			OutCell.bHasGrid = true;
			return true;
		}

		return false;
	}

	bool TryReadMarkerFallback(const TSharedPtr<FJsonObject>& Object, double VoxelSizeCm, FVector& OutCenterCm, FVector& OutExtentCm)
	{
		const TSharedPtr<FJsonObject>* MinObject = nullptr;
		const TSharedPtr<FJsonObject>* SizeObject = nullptr;
		if (!Object.IsValid() ||
			!Object->TryGetObjectField(TEXT("authoringMin"), MinObject) || !MinObject || !MinObject->IsValid() ||
			!Object->TryGetObjectField(TEXT("authoringSizeVoxels"), SizeObject) || !SizeObject || !SizeObject->IsValid())
		{
			return false;
		}

		double MinX = 0.0;
		double MinY = 0.0;
		double MinZ = 0.0;
		double SizeX = 1.0;
		double SizeY = 1.0;
		double SizeZ = 1.0;
		if (!(*MinObject)->TryGetNumberField(TEXT("x"), MinX) ||
			!(*MinObject)->TryGetNumberField(TEXT("y"), MinY) ||
			!(*MinObject)->TryGetNumberField(TEXT("z"), MinZ) ||
			!(*SizeObject)->TryGetNumberField(TEXT("x"), SizeX) ||
			!(*SizeObject)->TryGetNumberField(TEXT("y"), SizeY) ||
			!(*SizeObject)->TryGetNumberField(TEXT("z"), SizeZ))
		{
			return false;
		}

		OutExtentCm = FVector(
			static_cast<float>(SizeX * VoxelSizeCm * 0.5),
			static_cast<float>(SizeZ * VoxelSizeCm * 0.5),
			static_cast<float>(SizeY * VoxelSizeCm * 0.5));
		OutCenterCm = FVector(
			static_cast<float>((MinX * VoxelSizeCm) + OutExtentCm.X),
			static_cast<float>((MinZ * VoxelSizeCm) + OutExtentCm.Y),
			static_cast<float>((MinY * VoxelSizeCm) + OutExtentCm.Z));
		return true;
	}

	bool LoadImportedMapDocumentFromRoot(const TSharedPtr<FJsonObject>& RootObject, const FString& FilePath, FImportedMapDocument& OutDocument, FString& OutError)
	{
		if (!RootObject.IsValid())
		{
			OutError = FString::Printf(TEXT("유효하지 않은 JSON root입니다: %s"), *FilePath);
			return false;
		}

		RootObject->TryGetStringField(TEXT("schema"), OutDocument.Schema);
		if (!RootObject->TryGetStringField(TEXT("mapId"), OutDocument.MapId))
		{
			OutDocument.MapId = FPaths::GetBaseFilename(FilePath);
		}

		const TSharedPtr<FJsonObject>* ContractObject = nullptr;
		if (RootObject->TryGetObjectField(TEXT("coordinateContract"), ContractObject) && ContractObject && ContractObject->IsValid())
		{
			double ParsedVoxelSizeCm = OutDocument.VoxelSizeCm;
			if ((*ContractObject)->TryGetNumberField(TEXT("voxelSizeCm"), ParsedVoxelSizeCm))
			{
				OutDocument.VoxelSizeCm = ParsedVoxelSizeCm;
			}
		}

		TryReadIntVectorObject(RootObject, TEXT("dimensionsVoxels"), OutDocument.DimensionsVoxels);

		const TArray<TSharedPtr<FJsonValue>>* OccupiedCells = nullptr;
		if (RootObject->TryGetArrayField(TEXT("occupiedCells"), OccupiedCells))
		{
			for (const TSharedPtr<FJsonValue>& CellValue : *OccupiedCells)
			{
				const TSharedPtr<FJsonObject> CellObject = CellValue.IsValid() ? CellValue->AsObject() : nullptr;
				if (!CellObject.IsValid())
				{
					continue;
				}

				FImportedVoxelCell Cell;
				if (!CellObject->TryGetStringField(TEXT("typeId"), Cell.TypeId))
				{
					continue;
				}

				TryReadCellGrid(CellObject, Cell);

				if (!TryReadVectorObject(CellObject, TEXT("unrealCenterCm"), Cell.CenterCm))
				{
					if (!TryReadGridCenter(CellObject, OutDocument.VoxelSizeCm, Cell.CenterCm))
					{
						continue;
					}
				}

				if (!TryReadVectorObject(CellObject, TEXT("unrealExtentCm"), Cell.ExtentCm))
				{
					const float HalfSize = static_cast<float>(OutDocument.VoxelSizeCm * 0.5);
					Cell.ExtentCm = FVector(HalfSize, HalfSize, HalfSize);
				}

				OutDocument.Cells.Add(Cell);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* MarkerArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("markers"), MarkerArray))
		{
			for (const TSharedPtr<FJsonValue>& MarkerValue : *MarkerArray)
			{
				const TSharedPtr<FJsonObject> MarkerObject = MarkerValue.IsValid() ? MarkerValue->AsObject() : nullptr;
				if (!MarkerObject.IsValid())
				{
					continue;
				}

				FImportedMarker Marker;
				MarkerObject->TryGetStringField(TEXT("id"), Marker.Id);
				MarkerObject->TryGetStringField(TEXT("type"), Marker.Type);
				MarkerObject->TryGetStringField(TEXT("linkedRef"), Marker.LinkedRef);
				MarkerObject->TryGetStringField(TEXT("comment"), Marker.Comment);

				if (!TryReadVectorObject(MarkerObject, TEXT("unrealCenterCm"), Marker.CenterCm) ||
					!TryReadVectorObject(MarkerObject, TEXT("unrealExtentCm"), Marker.ExtentCm))
				{
					if (!TryReadMarkerFallback(MarkerObject, OutDocument.VoxelSizeCm, Marker.CenterCm, Marker.ExtentCm))
					{
						continue;
					}
				}

				OutDocument.Markers.Add(Marker);
			}
		}

		return true;
	}

	bool LoadImportedChunkDocument(const FString& FilePath, FImportedChunkDocument& OutDocument, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject;
		if (!LoadJsonRootObject(FilePath, RootObject, OutError))
		{
			return false;
		}

		RootObject->TryGetStringField(TEXT("schema"), OutDocument.Schema);
		if (!RootObject->TryGetStringField(TEXT("mapId"), OutDocument.MapId))
		{
			OutDocument.MapId = FPaths::GetBaseFilename(FilePath);
		}
		OutDocument.ChunkFile = FPaths::GetCleanFilename(FilePath);

		const TSharedPtr<FJsonObject>* ContractObject = nullptr;
		if (RootObject->TryGetObjectField(TEXT("coordinateContract"), ContractObject) && ContractObject && ContractObject->IsValid())
		{
			double ParsedVoxelSizeCm = OutDocument.VoxelSizeCm;
			if ((*ContractObject)->TryGetNumberField(TEXT("voxelSizeCm"), ParsedVoxelSizeCm))
			{
				OutDocument.VoxelSizeCm = ParsedVoxelSizeCm;
			}
		}

		TryReadIntVectorObject(RootObject, TEXT("chunkCoord"), OutDocument.ChunkCoord);
		TryReadIntVectorObject(RootObject, TEXT("chunkDimensionsVoxels"), OutDocument.ChunkDimensions);
		TryReadIntVectorObject(RootObject, TEXT("chunkOriginGrid"), OutDocument.ChunkOriginGrid);

		const TArray<TSharedPtr<FJsonValue>>* OccupiedCells = nullptr;
		if (RootObject->TryGetArrayField(TEXT("occupiedCells"), OccupiedCells))
		{
			for (const TSharedPtr<FJsonValue>& CellValue : *OccupiedCells)
			{
				const TSharedPtr<FJsonObject> CellObject = CellValue.IsValid() ? CellValue->AsObject() : nullptr;
				if (!CellObject.IsValid())
				{
					continue;
				}

				FImportedVoxelCell Cell;
				if (!CellObject->TryGetStringField(TEXT("typeId"), Cell.TypeId))
				{
					continue;
				}

				TryReadCellGrid(CellObject, Cell);
				if (!TryReadVectorObject(CellObject, TEXT("unrealCenterCm"), Cell.CenterCm))
				{
					if (!TryReadGridCenter(CellObject, OutDocument.VoxelSizeCm, Cell.CenterCm))
					{
						continue;
					}
				}

				if (!TryReadVectorObject(CellObject, TEXT("unrealExtentCm"), Cell.ExtentCm))
				{
					const float HalfSize = static_cast<float>(OutDocument.VoxelSizeCm * 0.5);
					Cell.ExtentCm = FVector(HalfSize, HalfSize, HalfSize);
				}

				OutDocument.Cells.Add(Cell);
			}
		}

		return true;
	}

	bool LoadImportedMapDocument(const FString& FilePath, FImportedMapDocument& OutDocument, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject;
		if (!LoadJsonRootObject(FilePath, RootObject, OutError))
		{
			return false;
		}

		return LoadImportedMapDocumentFromRoot(RootObject, FilePath, OutDocument, OutError);
	}

	bool LoadImportedWorldDocument(const FString& FilePath, FImportedWorldDocument& OutDocument, FString& OutError)
	{
		TSharedPtr<FJsonObject> RootObject;
		if (!LoadJsonRootObject(FilePath, RootObject, OutError))
		{
			return false;
		}

		RootObject->TryGetStringField(TEXT("schema"), OutDocument.Schema);
		if (!RootObject->TryGetStringField(TEXT("mapId"), OutDocument.MapId))
		{
			OutDocument.MapId = FPaths::GetBaseFilename(FilePath);
		}
		RootObject->TryGetStringField(TEXT("markerFile"), OutDocument.MarkerFile);

		const TArray<TSharedPtr<FJsonValue>>* ChunkArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("chunks"), ChunkArray))
		{
			for (const TSharedPtr<FJsonValue>& ChunkValue : *ChunkArray)
			{
				const TSharedPtr<FJsonObject> ChunkObject = ChunkValue.IsValid() ? ChunkValue->AsObject() : nullptr;
				if (!ChunkObject.IsValid())
				{
					continue;
				}

				FString ChunkFile;
				if (ChunkObject->TryGetStringField(TEXT("file"), ChunkFile) && !ChunkFile.IsEmpty())
				{
					OutDocument.ChunkFiles.Add(ResolveImportPath(FilePath, ChunkFile));
				}
			}
		}

		OutDocument.MarkerFile = ResolveImportPath(FilePath, OutDocument.MarkerFile);
		return true;
	}

	FImportedChunkDocument BuildPseudoChunkDocument(const FImportedMapDocument& MapDocument, const FString& SourceFile)
	{
		FImportedChunkDocument ChunkDocument;
		ChunkDocument.Schema = TEXT("voxel-project.unreal.world.chunk.v1");
		ChunkDocument.MapId = MapDocument.MapId;
		ChunkDocument.ChunkFile = FPaths::GetCleanFilename(SourceFile);
		ChunkDocument.VoxelSizeCm = MapDocument.VoxelSizeCm;
		ChunkDocument.ChunkCoord = FIntVector::ZeroValue;
		ChunkDocument.ChunkOriginGrid = FIntVector::ZeroValue;
		ChunkDocument.ChunkDimensions = MapDocument.DimensionsVoxels;
		ChunkDocument.Cells = MapDocument.Cells;

		if (ChunkDocument.ChunkDimensions == FIntVector::ZeroValue)
		{
			FIntVector MaxGrid = FIntVector::ZeroValue;
			for (const FImportedVoxelCell& Cell : MapDocument.Cells)
			{
				if (!Cell.bHasGrid)
				{
					continue;
				}
				MaxGrid.X = FMath::Max(MaxGrid.X, Cell.WorldGrid.X);
				MaxGrid.Y = FMath::Max(MaxGrid.Y, Cell.WorldGrid.Y);
				MaxGrid.Z = FMath::Max(MaxGrid.Z, Cell.WorldGrid.Z);
			}
			ChunkDocument.ChunkDimensions = MaxGrid + FIntVector(1, 1, 1);
		}

		return ChunkDocument;
	}

	FLinearColor GetColorForVoxelType(const FString& TypeId)
	{
		if (TypeId == TEXT("grass")) return FLinearColor(0.26f, 0.62f, 0.23f);
		if (TypeId == TEXT("soil")) return FLinearColor(0.56f, 0.38f, 0.19f);
		if (TypeId == TEXT("water")) return FLinearColor(0.18f, 0.46f, 0.88f);
		if (TypeId == TEXT("stone")) return FLinearColor(0.58f, 0.58f, 0.62f);
		if (TypeId == TEXT("wood")) return FLinearColor(0.58f, 0.42f, 0.22f);
		if (TypeId == TEXT("lamp")) return FLinearColor(0.90f, 0.73f, 0.25f);

		const uint32 Hash = GetTypeHash(TypeId);
		const float Hue = static_cast<float>(Hash % 360);
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue / 360.0f * 255.0f), 160, 210);
	}

	FLinearColor GetColorForMarkerType(const FString& MarkerType)
	{
		if (MarkerType == TEXT("TriggerVolume")) return FLinearColor(0.93f, 0.56f, 0.16f);
		if (MarkerType == TEXT("InteractionPoint")) return FLinearColor(0.96f, 0.84f, 0.22f);
		if (MarkerType == TEXT("NpcSpawn")) return FLinearColor(0.32f, 0.80f, 0.68f);
		if (MarkerType == TEXT("EnemySpawn")) return FLinearColor(0.86f, 0.28f, 0.28f);
		if (MarkerType == TEXT("QuestMarker")) return FLinearColor(0.68f, 0.42f, 0.94f);
		if (MarkerType == TEXT("ZoneVolume")) return FLinearColor(0.20f, 0.60f, 0.94f);
		return FLinearColor(0.9f, 0.9f, 0.9f);
	}

	bool IsPointLikeMarker(const FString& MarkerType)
	{
		return MarkerType == TEXT("InteractionPoint") ||
			MarkerType == TEXT("NpcSpawn") ||
			MarkerType == TEXT("EnemySpawn") ||
			MarkerType == TEXT("QuestMarker");
	}

	UMaterialInterface* CreateColoredMaterial(UObject* Outer, UMaterialInterface* BaseMaterial, const FLinearColor& Color)
	{
		if (BaseMaterial == nullptr)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* MaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, Outer);
		if (MaterialInstance)
		{
			MaterialInstance->SetVectorParameterValue(TEXT("Color"), Color);
			MaterialInstance->SetVectorParameterValue(TEXT("Colour"), Color);
		}
		return MaterialInstance;
	}

	FString GetDominantVoxelType(const TArray<FImportedVoxelCell>& Cells)
	{
		TMap<FString, int32> Counts;
		FString DominantType;
		int32 DominantCount = 0;
		for (const FImportedVoxelCell& Cell : Cells)
		{
			int32& Count = Counts.FindOrAdd(Cell.TypeId);
			++Count;
			if (Count > DominantCount)
			{
				DominantCount = Count;
				DominantType = Cell.TypeId;
			}
		}

		return DominantType.IsEmpty() ? TEXT("stone") : DominantType;
	}

	void BuildBlockVoxelComponents(
		AActor* Owner,
		USceneComponent* Parent,
		UStaticMesh* CubeMesh,
		UMaterialInterface* BaseMaterial,
		const TArray<FImportedVoxelCell>& Cells,
		TArray<TObjectPtr<UActorComponent>>& GeneratedComponents,
		int32& OutImportedVoxelCount)
	{
		TMap<FString, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ComponentsByType;
		for (const FImportedVoxelCell& Cell : Cells)
		{
			TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* ExistingComponent = ComponentsByType.Find(Cell.TypeId);
			UHierarchicalInstancedStaticMeshComponent* TargetComponent = ExistingComponent ? ExistingComponent->Get() : nullptr;
			if (TargetComponent == nullptr)
			{
				const FName ComponentName(*FString::Printf(TEXT("Voxel_%s"), *Cell.TypeId));
				TargetComponent = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, ComponentName);
				TargetComponent->SetupAttachment(Parent);
				TargetComponent->SetMobility(EComponentMobility::Static);
				TargetComponent->SetStaticMesh(CubeMesh);
				TargetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				TargetComponent->SetCastShadow(false);
				TargetComponent->RegisterComponent();

				if (UMaterialInterface* Material = CreateColoredMaterial(TargetComponent, BaseMaterial, GetColorForVoxelType(Cell.TypeId)))
				{
					TargetComponent->SetMaterial(0, Material);
				}

				ComponentsByType.Add(Cell.TypeId, TargetComponent);
				GeneratedComponents.Add(TargetComponent);
			}

			const FVector Scale(
				FMath::Max(Cell.ExtentCm.X / 50.0f, 0.01f),
				FMath::Max(Cell.ExtentCm.Y / 50.0f, 0.01f),
				FMath::Max(Cell.ExtentCm.Z / 50.0f, 0.01f));

			TargetComponent->AddInstance(FTransform(FQuat::Identity, Cell.CenterCm, Scale));
			++OutImportedVoxelCount;
		}
	}

	void BuildMarchingCubesComponents(
		AActor* Owner,
		USceneComponent* Parent,
		UMaterialInterface* BaseMaterial,
		const TArray<FImportedChunkDocument>& Chunks,
		float IsoLevel,
		bool bGenerateCollision,
		TArray<TObjectPtr<UActorComponent>>& GeneratedComponents,
		int32& OutImportedVoxelCount,
		int32& OutGeneratedSurfaceChunkCount,
		int32& OutGeneratedSurfaceTriangleCount)
	{
		TSet<FIntVector> OccupiedWorldCells;
		for (const FImportedChunkDocument& Chunk : Chunks)
		{
			for (const FImportedVoxelCell& Cell : Chunk.Cells)
			{
				if (Cell.bHasGrid)
				{
					OccupiedWorldCells.Add(Cell.WorldGrid);
				}
			}
			OutImportedVoxelCount += Chunk.Cells.Num();
		}

		for (const FImportedChunkDocument& Chunk : Chunks)
		{
			RTPSVoxelImport::FMarchingCubesMeshData MeshData;
			if (!RTPSVoxelImport::BuildMarchingCubesChunkMesh(
				Chunk.ChunkDimensions,
				Chunk.ChunkOriginGrid,
				static_cast<float>(Chunk.VoxelSizeCm),
				IsoLevel,
				OccupiedWorldCells,
				MeshData))
			{
				continue;
			}

			const FString DominantType = GetDominantVoxelType(Chunk.Cells);
			const FName ComponentName(*FString::Printf(TEXT("Surface_%d_%d_%d"), Chunk.ChunkCoord.X, Chunk.ChunkCoord.Y, Chunk.ChunkCoord.Z));
			UProceduralMeshComponent* SurfaceComponent = NewObject<UProceduralMeshComponent>(Owner, ComponentName);
			SurfaceComponent->SetupAttachment(Parent);
			SurfaceComponent->SetMobility(EComponentMobility::Movable);
			SurfaceComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			SurfaceComponent->SetCollisionEnabled(bGenerateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			SurfaceComponent->bUseComplexAsSimpleCollision = true;
			SurfaceComponent->CanCharacterStepUpOn = ECB_Yes;
			SurfaceComponent->SetCastShadow(true);
			SurfaceComponent->RegisterComponent();
			SurfaceComponent->CreateMeshSection(
				0,
				MeshData.Vertices,
				MeshData.Triangles,
				MeshData.Normals,
				MeshData.UV0,
				MeshData.VertexColors,
				MeshData.Tangents,
				bGenerateCollision);

			if (UMaterialInterface* Material = CreateColoredMaterial(SurfaceComponent, BaseMaterial, GetColorForVoxelType(DominantType)))
			{
				SurfaceComponent->SetMaterial(0, Material);
			}

			GeneratedComponents.Add(SurfaceComponent);
			++OutGeneratedSurfaceChunkCount;
			OutGeneratedSurfaceTriangleCount += MeshData.GetTriangleCount();
		}
	}

	void BuildMarkerComponents(
		AActor* Owner,
		USceneComponent* Parent,
		UStaticMesh* CubeMesh,
		UStaticMesh* SphereMesh,
		UMaterialInterface* BaseMaterial,
		const TArray<FImportedMarker>& Markers,
		bool bCreateMarkerLabels,
		float PointMarkerVisualScale,
		TArray<TObjectPtr<UActorComponent>>& GeneratedComponents,
		int32& OutImportedMarkerCount)
	{
		for (const FImportedMarker& Marker : Markers)
		{
			const bool bPointLike = IsPointLikeMarker(Marker.Type);
			UStaticMesh* MarkerMesh = bPointLike ? SphereMesh : CubeMesh;
			if (MarkerMesh == nullptr)
			{
				continue;
			}

			const FName MarkerName(*FString::Printf(TEXT("Marker_%s"), *Marker.Id));
			UStaticMeshComponent* MarkerComponent = NewObject<UStaticMeshComponent>(Owner, MarkerName);
			MarkerComponent->SetupAttachment(Parent);
			MarkerComponent->SetMobility(EComponentMobility::Static);
			MarkerComponent->SetStaticMesh(MarkerMesh);
			MarkerComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MarkerComponent->SetCastShadow(false);

			FVector Scale = FVector(
				FMath::Max(Marker.ExtentCm.X / 50.0f, 0.01f),
				FMath::Max(Marker.ExtentCm.Y / 50.0f, 0.01f),
				FMath::Max(Marker.ExtentCm.Z / 50.0f, 0.01f));
			if (bPointLike)
			{
				const float PointScale = FMath::Max(Marker.ExtentCm.GetMax() / 50.0f * PointMarkerVisualScale, 0.25f);
				Scale = FVector(PointScale, PointScale, PointScale);
			}

			MarkerComponent->SetRelativeTransform(FTransform(FQuat::Identity, Marker.CenterCm, Scale));
			MarkerComponent->RegisterComponent();

			if (UMaterialInterface* Material = CreateColoredMaterial(MarkerComponent, BaseMaterial, GetColorForMarkerType(Marker.Type)))
			{
				MarkerComponent->SetMaterial(0, Material);
			}

			GeneratedComponents.Add(MarkerComponent);
			++OutImportedMarkerCount;

			if (bCreateMarkerLabels)
			{
				const FName LabelName(*FString::Printf(TEXT("MarkerLabel_%s"), *Marker.Id));
				UTextRenderComponent* LabelComponent = NewObject<UTextRenderComponent>(Owner, LabelName);
				LabelComponent->SetupAttachment(Parent);
				LabelComponent->SetHorizontalAlignment(EHTA_Center);
				LabelComponent->SetVerticalAlignment(EVRTA_TextBottom);
				LabelComponent->SetTextRenderColor(GetColorForMarkerType(Marker.Type).ToFColor(true));
				LabelComponent->SetWorldSize(48.0f);

				const FString LabelText = Marker.LinkedRef.IsEmpty()
					? FString::Printf(TEXT("%s (%s)"), *Marker.Id, *Marker.Type)
					: FString::Printf(TEXT("%s (%s:%s)"), *Marker.Id, *Marker.Type, *Marker.LinkedRef);
				LabelComponent->SetText(FText::FromString(LabelText));
				LabelComponent->SetRelativeLocation(Marker.CenterCm + FVector(0.0f, 0.0f, Marker.ExtentCm.Z + 60.0f));
				LabelComponent->RegisterComponent();
				GeneratedComponents.Add(LabelComponent);
			}
		}
	}
}

ARTPSVoxelMapImportActor::ARTPSVoxelMapImportActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	ImportedVoxelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ImportedVoxelRoot"));
	ImportedVoxelRoot->SetupAttachment(SceneRoot);

	ImportedMarkerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ImportedMarkerRoot"));
	ImportedMarkerRoot->SetupAttachment(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshRef(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshRef(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialRef(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	CubeMeshAsset = CubeMeshRef.Succeeded() ? CubeMeshRef.Object : nullptr;
	SphereMeshAsset = SphereMeshRef.Succeeded() ? SphereMeshRef.Object : nullptr;
	BasicShapeMaterialAsset = BasicMaterialRef.Succeeded() ? BasicMaterialRef.Object : nullptr;
}

void ARTPSVoxelMapImportActor::BeginPlay()
{
	Super::BeginPlay();

	if (bRebuildOnBeginPlay)
	{
		RebuildFromJson();
	}
}

void ARTPSVoxelMapImportActor::ClearImportedContent()
{
	for (int32 Index = GeneratedComponents.Num() - 1; Index >= 0; --Index)
	{
		if (UActorComponent* Component = GeneratedComponents[Index].Get())
		{
			Component->DestroyComponent();
		}
	}

	GeneratedComponents.Reset();
	ImportedVoxelCount = 0;
	ImportedMarkerCount = 0;
	GeneratedSurfaceChunkCount = 0;
	GeneratedSurfaceTriangleCount = 0;
	LastImportStatus = TEXT("Imported content cleared.");
}

void ARTPSVoxelMapImportActor::RebuildFromJson()
{
	ClearImportedContent();

	const FString FilePath = MapJsonFile.FilePath;
	if (FilePath.IsEmpty())
	{
		LastImportStatus = TEXT("MapJsonFile is empty.");
		return;
	}

	FString ErrorMessage;
	TSharedPtr<FJsonObject> RootObject;
	if (!LoadJsonRootObject(FilePath, RootObject, ErrorMessage))
	{
		LastImportStatus = ErrorMessage;
		UE_LOG(LogTemp, Warning, TEXT("%s"), *LastImportStatus);
		return;
	}

	FString Schema;
	RootObject->TryGetStringField(TEXT("schema"), Schema);

	FImportedMapDocument ImportedMap;
	TArray<FImportedChunkDocument> ImportedChunks;
	if (Schema == TEXT("voxel-project.unreal.world.v1"))
	{
		FImportedWorldDocument ImportedWorld;
		if (!LoadImportedWorldDocument(FilePath, ImportedWorld, ErrorMessage))
		{
			LastImportStatus = ErrorMessage;
			UE_LOG(LogTemp, Warning, TEXT("%s"), *LastImportStatus);
			return;
		}

		ImportedMap.Schema = ImportedWorld.Schema;
		ImportedMap.MapId = ImportedWorld.MapId;
		for (const FString& ChunkFilePath : ImportedWorld.ChunkFiles)
		{
			FImportedChunkDocument ChunkDocument;
			if (!LoadImportedChunkDocument(ChunkFilePath, ChunkDocument, ErrorMessage))
			{
				LastImportStatus = ErrorMessage;
				UE_LOG(LogTemp, Warning, TEXT("%s"), *LastImportStatus);
				return;
			}

			if (ImportedMap.DimensionsVoxels == FIntVector::ZeroValue)
			{
				ImportedMap.DimensionsVoxels = ChunkDocument.ChunkDimensions;
			}
			ImportedMap.VoxelSizeCm = ChunkDocument.VoxelSizeCm;
			ImportedMap.Cells.Append(ChunkDocument.Cells);
			ImportedChunks.Add(ChunkDocument);
		}

		if (bImportMarkers && !ImportedWorld.MarkerFile.IsEmpty())
		{
			FImportedMapDocument MarkerDocument;
			if (!LoadImportedMapDocument(ImportedWorld.MarkerFile, MarkerDocument, ErrorMessage))
			{
				LastImportStatus = ErrorMessage;
				UE_LOG(LogTemp, Warning, TEXT("%s"), *LastImportStatus);
				return;
			}

			ImportedMap.Markers.Append(MarkerDocument.Markers);
		}
	}
	else
	{
		if (!LoadImportedMapDocumentFromRoot(RootObject, FilePath, ImportedMap, ErrorMessage))
		{
			LastImportStatus = ErrorMessage;
			UE_LOG(LogTemp, Warning, TEXT("%s"), *LastImportStatus);
			return;
		}

		ImportedChunks.Add(BuildPseudoChunkDocument(ImportedMap, FilePath));
	}

	if (SurfaceMode == ERTPSVoxelSurfaceMode::MarchingCubes)
	{
		BuildMarchingCubesComponents(
			this,
			ImportedVoxelRoot,
			BasicShapeMaterialAsset,
			ImportedChunks,
			MarchingIsoLevel,
			bGenerateSurfaceCollision,
			GeneratedComponents,
			ImportedVoxelCount,
			GeneratedSurfaceChunkCount,
			GeneratedSurfaceTriangleCount);
	}
	else
	{
		BuildBlockVoxelComponents(
			this,
			ImportedVoxelRoot,
			CubeMeshAsset,
			BasicShapeMaterialAsset,
			ImportedMap.Cells,
			GeneratedComponents,
			ImportedVoxelCount);
	}

	if (bImportMarkers)
	{
		BuildMarkerComponents(
			this,
			ImportedMarkerRoot,
			CubeMeshAsset,
			SphereMeshAsset,
			BasicShapeMaterialAsset,
			ImportedMap.Markers,
			bCreateMarkerLabels,
			PointMarkerVisualScale,
			GeneratedComponents,
			ImportedMarkerCount);
	}

	if (SurfaceMode == ERTPSVoxelSurfaceMode::MarchingCubes)
	{
		LastImportStatus = FString::Printf(
			TEXT("Generated %d surface chunks (%d triangles), imported %d voxels and %d markers from %s"),
			GeneratedSurfaceChunkCount,
			GeneratedSurfaceTriangleCount,
			ImportedVoxelCount,
			ImportedMarkerCount,
			*ImportedMap.MapId);
	}
	else
	{
		LastImportStatus = FString::Printf(
			TEXT("Imported %d voxels and %d markers from %s"),
			ImportedVoxelCount,
			ImportedMarkerCount,
			*ImportedMap.MapId);
	}
	UE_LOG(LogTemp, Log, TEXT("%s"), *LastImportStatus);
}
