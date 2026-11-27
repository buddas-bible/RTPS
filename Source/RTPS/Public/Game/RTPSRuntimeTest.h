#pragma once

#include "CoreMinimal.h"
#include <initializer_list>

namespace RTPSRuntimeTest
{
	const FString& Directory();
	void Emit(const TCHAR* Event, std::initializer_list<TPair<FString, FString>> Fields = {});
}
