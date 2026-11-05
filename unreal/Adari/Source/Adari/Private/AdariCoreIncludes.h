// SPDX-License-Identifier: Apache-2.0
// The core's headers, included with Unreal's warning settings suspended.
#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "adari/grid.hpp"
#include "adari/map.hpp"
#include "adari/readers.hpp"
#include "adari/report.hpp"
#include "adari/rules.hpp"
THIRD_PARTY_INCLUDES_END

namespace AdariConvert
{
	inline FString String(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
	inline std::string Std(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}

DECLARE_LOG_CATEGORY_EXTERN(LogAdari, Log, All);
