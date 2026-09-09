#include "VoxelAuthoring/VoxelAuthoringPersistence.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool UVoxelAuthoringPersistence::ExportToFile(const FVoxelDensitySnapshot& Snapshot, const FString& FilePath)
{
	FString ResolvedPath = FilePath;
	if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectDir(), ResolvedPath);
	}
	FPaths::NormalizeFilename(ResolvedPath);

	const FString DirPath = FPaths::GetPath(ResolvedPath);
	if (!DirPath.IsEmpty())
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirPath);
	}

	const FIntVector& Dims = Snapshot.Dimensions;
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("rtps.authoring.density.v1"));
	Root->SetNumberField(TEXT("gridX"), static_cast<double>(Dims.X));
	Root->SetNumberField(TEXT("gridY"), static_cast<double>(Dims.Y));
	Root->SetNumberField(TEXT("gridZ"), static_cast<double>(Dims.Z));
	Root->SetNumberField(TEXT("cellSize"), static_cast<double>(Snapshot.CellSize));
	Root->SetNumberField(TEXT("isoLevel"), static_cast<double>(Snapshot.IsoLevel));

	TArray<TSharedPtr<FJsonValue>> CellArray;
	for (int32 Z = 0; Z < Dims.Z; ++Z)
	for (int32 Y = 0; Y < Dims.Y; ++Y)
	for (int32 X = 0; X < Dims.X; ++X)
	{
		const float D = Snapshot.Grid[X + Dims.X * (Y + Dims.Y * Z)];
		if (D == 0.f) { continue; }
		TSharedRef<FJsonObject> Cell = MakeShared<FJsonObject>();
		Cell->SetNumberField(TEXT("x"), static_cast<double>(X));
		Cell->SetNumberField(TEXT("y"), static_cast<double>(Y));
		Cell->SetNumberField(TEXT("z"), static_cast<double>(Z));
		Cell->SetNumberField(TEXT("d"), static_cast<double>(D));
		CellArray.Add(MakeShared<FJsonValueObject>(Cell));
	}
	Root->SetArrayField(TEXT("nonZeroCells"), CellArray);

	FString OutputString;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&OutputString));

	if (!FFileHelper::SaveStringToFile(OutputString, *ResolvedPath))
	{
		LastIOStatus = FString::Printf(TEXT("Export failed: could not write to %s"), *ResolvedPath);
		return false;
	}

	LastIOStatus = FString::Printf(TEXT("Exported %d non-zero cells to %s"), CellArray.Num(), *ResolvedPath);
	return true;
}

bool UVoxelAuthoringPersistence::ImportFromFile(const FString& FilePath, FVoxelDensitySnapshot& OutSnapshot)
{
	FString ResolvedPath = FilePath;
	if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::Combine(FPaths::ProjectDir(), ResolvedPath);
	}
	FPaths::NormalizeFilename(ResolvedPath);

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ResolvedPath))
	{
		LastIOStatus = FString::Printf(TEXT("Import failed: could not read %s"), *ResolvedPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
	{
		LastIOStatus = FString::Printf(TEXT("Import failed: JSON parse error in %s"), *ResolvedPath);
		return false;
	}

	FString Schema;
	Root->TryGetStringField(TEXT("schema"), Schema);
	if (Schema != TEXT("rtps.authoring.density.v1"))
	{
		LastIOStatus = FString::Printf(TEXT("Import failed: unknown schema '%s'"), *Schema);
		return false;
	}

	double DX = 0, DY = 0, DZ = 0, NewCellSize = 100.0, NewIsoLevel = 0.5;
	Root->TryGetNumberField(TEXT("gridX"), DX);
	Root->TryGetNumberField(TEXT("gridY"), DY);
	Root->TryGetNumberField(TEXT("gridZ"), DZ);
	Root->TryGetNumberField(TEXT("cellSize"), NewCellSize);
	Root->TryGetNumberField(TEXT("isoLevel"), NewIsoLevel);

	const int32 NX = FMath::RoundToInt(DX);
	const int32 NY = FMath::RoundToInt(DY);
	const int32 NZ = FMath::RoundToInt(DZ);
	if (NX <= 0 || NY <= 0 || NZ <= 0)
	{
		LastIOStatus = FString::Printf(TEXT("Import failed: invalid dimensions %dx%dx%d"), NX, NY, NZ);
		return false;
	}

	OutSnapshot.Dimensions = FIntVector(NX, NY, NZ);
	OutSnapshot.CellSize = static_cast<float>(NewCellSize);
	OutSnapshot.IsoLevel = static_cast<float>(NewIsoLevel);
	OutSnapshot.Grid.SetNumZeroed(NX * NY * NZ);

	int32 LoadedCount = 0;
	const TArray<TSharedPtr<FJsonValue>>* CellArray = nullptr;
	if (Root->TryGetArrayField(TEXT("nonZeroCells"), CellArray) && CellArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *CellArray)
		{
			const TSharedPtr<FJsonObject> Cell = Val.IsValid() ? Val->AsObject() : nullptr;
			if (!Cell.IsValid()) { continue; }
			double CX = 0, CY = 0, CZ = 0, CD = 0;
			if (!Cell->TryGetNumberField(TEXT("x"), CX) ||
				!Cell->TryGetNumberField(TEXT("y"), CY) ||
				!Cell->TryGetNumberField(TEXT("z"), CZ) ||
				!Cell->TryGetNumberField(TEXT("d"), CD)) { continue; }
			const int32 IX = FMath::RoundToInt(CX);
			const int32 IY = FMath::RoundToInt(CY);
			const int32 IZ = FMath::RoundToInt(CZ);
			if (IX < 0 || IX >= NX || IY < 0 || IY >= NY || IZ < 0 || IZ >= NZ) { continue; }
			OutSnapshot.Grid[IX + NX * (IY + NY * IZ)] = static_cast<float>(CD);
			++LoadedCount;
		}
	}

	LastIOStatus = FString::Printf(
		TEXT("Imported %d non-zero cells from %s (Dim=%dx%dx%d)"),
		LoadedCount, *ResolvedPath, NX, NY, NZ);
	return true;
}
