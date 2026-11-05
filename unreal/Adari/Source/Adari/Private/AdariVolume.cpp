// SPDX-License-Identifier: Apache-2.0
#include "AdariVolume.h"

#include "Components/BoxComponent.h"

AAdariVolume::AAdariVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetBoxExtent(FVector(1000.0, 1000.0, 150.0));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetCanEverAffectNavigation(false);
	Bounds->ShapeColor = FColor(80, 200, 120);
	Bounds->bHiddenInGame = true;
	RootComponent = Bounds;
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}
