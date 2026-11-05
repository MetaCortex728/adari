// SPDX-License-Identifier: Apache-2.0
#include "AdariLibrary.h"
#include "AdariCoreIncludes.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

namespace
{
	void RunCheck(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogAdari, Error, TEXT("Adari.Check: no world"));
			return;
		}
		const TArray<FAdariResult> Results = UAdariLibrary::CheckAll(World);
		int32 Failed = 0;
		for (const FAdariResult& R : Results) Failed += R.bFailed ? 1 : 0;
		UE_LOG(LogAdari, Display, TEXT("Adari.Check: %d volumes, %d failing"), Results.Num(), Failed);
	}

	FAutoConsoleCommandWithWorldAndArgs GAdariCheck(
		TEXT("Adari.Check"),
		TEXT("Samples every Adari volume in the level, checks its rules, draws failures and writes reports to Saved/Adari."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCheck));
}

IMPLEMENT_MODULE(FDefaultModuleImpl, Adari);
