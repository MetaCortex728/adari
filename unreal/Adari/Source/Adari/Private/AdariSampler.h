// SPDX-License-Identifier: Apache-2.0
// Turns the part of a level inside an Adari volume into the core's grid map.
#pragma once

#include "AdariCoreIncludes.h"
#include "CoreMinimal.h"

class AAdariVolume;

struct FAdariSample
{
	adari::Map Map;
	/** Ground height of each cell's centre (the volume's floor where there is no ground). */
	TArray<double> GroundZ;
	FVector Origin = FVector::ZeroVector;  // world position of cell (0, 0)'s corner
	double Cell = 50.0;

	FVector CellCentre(int32 Index, double Lift = 0.0) const;
};

/** Samples the volume's level. Returns false and sets Error when it cannot. */
bool AdariSampleVolume(const AAdariVolume& Volume, FAdariSample& Out, FString& Error);
