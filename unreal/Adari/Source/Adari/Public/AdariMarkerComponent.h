// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "AdariMarkerComponent.generated.h"

UENUM(BlueprintType)
enum class EAdariMarkerShape : uint8
{
	/** One point at the component's location (a spawn, an exit, a reward). */
	Point,
	/** An axis-aligned box around the component's location (an arena, a cover zone, a door). */
	Box
};

/**
 * Tells Adari what part of a level means for its rules. Add it to any actor: rule selectors such as
 * "entry", "exit" or "cover" match these tags. An actor tag of the form "adari:entry" works too, as a point
 * at the actor's location.
 *
 * A door marker's actor is left out of collision sampling, so the door counts as open unless a rule closes it.
 * A variant marker's actor is also left out; its collision becomes one option of a variant group, and Adari
 * checks the rules with and without it.
 */
UCLASS(ClassGroup = (Adari), meta = (BlueprintSpawnableComponent, DisplayName = "Adari Marker"))
class ADARI_API UAdariMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Rule selectors match these, e.g. "entry", "exit", "spawn", "cover", "boss". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari")
	TArray<FString> MarkerTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari")
	EAdariMarkerShape Shape = EAdariMarkerShape::Point;

	/** Half size of the box, in centimetres, for Box markers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari", meta = (EditCondition = "Shape == EAdariMarkerShape::Box"))
	FVector BoxExtent = FVector(100.0, 100.0, 100.0);

	/** A door: blocks movement and sight only while a rule closes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari")
	bool bDoor = false;

	/** "group.option": this actor's collision is one option of a variant group, e.g. "crates.left". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari")
	FString Variant;

	/** The variant group may also take no option at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adari", meta = (EditCondition = "Variant != \"\""))
	bool bGroupCanBeEmpty = true;
};
