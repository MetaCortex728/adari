// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"

#include "AdariVolume.generated.h"

class UBoxComponent;

/**
 * The part of a level Adari checks, and how to sample it. Place one around a room or a whole level, point it at
 * a rule file, and run "Adari.Check" in the console (or call UAdariLibrary::CheckVolume).
 *
 * The box is read as an axis-aligned area; its rotation is ignored. Each cell is sampled from the top of the box
 * down: ground under the cell centre, room for the agent above the ground, and whether something at eye height
 * blocks sight. When bUseNavMesh is set and the level has a navigation mesh, the navmesh decides walkability.
 */
UCLASS(hidecategories = (Physics, Collision, Lighting, Rendering, HLOD, Cooking, Replication, Networking, Input, LOD))
class ADARI_API AAdariVolume : public AActor
{
	GENERATED_BODY()

public:
	AAdariVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Adari")
	TObjectPtr<UBoxComponent> Bounds;

	/** Cell size in centimetres. Rule radii, distances and path lengths are in centimetres too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling", meta = (ClampMin = "5", UIMin = "10", UIMax = "200"))
	float CellSize = 50.0f;

	/** Use the navigation mesh for walkability when the level has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling")
	bool bUseNavMesh = true;

	/** Height the agent needs above the ground, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling", meta = (ClampMin = "10"))
	float AgentHeight = 180.0f;

	/** Obstacles lower than this are stepped over, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling", meta = (ClampMin = "0"))
	float StepHeight = 45.0f;

	/** Height above the ground at which sight is tested, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling", meta = (ClampMin = "1"))
	float EyeHeight = 150.0f;

	/** Steeper ground is not walkable, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling", meta = (ClampMin = "0", ClampMax = "89"))
	float MaxSlope = 44.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling")
	TEnumAsByte<ECollisionChannel> MovementChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Sampling")
	TEnumAsByte<ECollisionChannel> SightChannel = ECC_Visibility;

	/** Rule file (Adari JSON rules), relative to the project directory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Rules", meta = (FilePathFilter = "json"))
	FFilePath RulesFile;

	/** Rules written inline instead of a file; used when not empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Rules", meta = (MultiLine = true))
	FString RulesJson;

	/** Draw failing cells (red) and counterexample paths (orange) in the level after a check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Output")
	bool bDrawResults = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari|Output", meta = (ClampMin = "0"))
	float DrawSeconds = 30.0f;
};
