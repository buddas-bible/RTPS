#include "Game/RTPSRuntimeTest.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

const FString& RTPSRuntimeTest::Directory()
{
	static const FString Path = []()
	{
		FString Value;
#if UE_BUILD_DEVELOPMENT
		if (FParse::Value(FCommandLine::Get(), TEXT("RTPSRuntimeTestDir="), Value) && !Value.IsEmpty())
		{
			Value = FPaths::ConvertRelativePathToFull(Value);
			IFileManager::Get().MakeDirectory(*Value, true);
		}
#endif
		return Value;
	}();
	return Path;
}

void RTPSRuntimeTest::Emit(const TCHAR* Event, std::initializer_list<TPair<FString, FString>> Fields)
{
	if (Directory().IsEmpty())
	{
		return;
	}

	const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
	Record->SetStringField(TEXT("event"), Event);
	Record->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
	for (const TPair<FString, FString>& Field : Fields)
	{
		Record->SetStringField(Field.Key, Field.Value);
	}

	FString Line;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	if (FJsonSerializer::Serialize(Record, Writer))
	{
		Line += TEXT("\n");
		if (!FFileHelper::SaveStringToFile(Line, *(Directory() / TEXT("events.jsonl")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append | FILEWRITE_AllowRead))
		{
			UE_LOG(LogTemp, Error, TEXT("RTPS runtime test: failed to append %s."), Event);
		}
	}
}
