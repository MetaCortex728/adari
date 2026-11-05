// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AdariLibrary.generated.h"

class AAdariVolume;

USTRUCT(BlueprintType)
struct ADARI_API FAdariResult
{
	GENERATED_BODY()

	/** An error-level rule failed or could not be checked, or the level could not be sampled. */
	UPROPERTY(BlueprintReadOnly, Category = "Adari")
	bool bFailed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Adari")
	int32 Errors = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Adari")
	int32 Warnings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Adari")
	int32 RulesNotChecked = 0;

	/** The text report, or the reason the check could not run. */
	UPROPERTY(BlueprintReadOnly, Category = "Adari")
	FString Report;

	/** Where the map, reports and overlay were written (Saved/Adari/<volume>). */
	UPROPERTY(BlueprintReadOnly, Category = "Adari")
	FString OutputDir;
};

UCLASS()
class ADARI_API UAdariLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Samples the volume's level, checks its rules, writes reports and draws failures. */
	UFUNCTION(BlueprintCallable, Category = "Adari")
	static FAdariResult CheckVolume(AAdariVolume* Volume);

	/** Samples the volume's level and returns it as Adari JSON, for the command-line tool or CI. */
	UFUNCTION(BlueprintCallable, Category = "Adari")
	static FString SampleToJson(AAdariVolume* Volume);

	/** Checks every Adari volume in the world. */
	UFUNCTION(BlueprintCallable, Category = "Adari", meta = (WorldContext = "WorldContext"))
	static TArray<FAdariResult> CheckAll(UObject* WorldContext);
};
