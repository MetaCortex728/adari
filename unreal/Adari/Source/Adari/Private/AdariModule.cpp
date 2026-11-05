// SPDX-License-Identifier: Apache-2.0
#include "AdariCoreIncludes.h"
#include "AdariLibrary.h"
#include "AdariMarkerComponent.h"
#include "AdariVolume.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

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

		// "Adari.Check shot": a viewport screenshot a second later, once the drawn results are on screen.
		if (Args.Contains(TEXT("shot")) && GEngine)
		{
			TWeakObjectPtr<UWorld> Weak(World);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float) {
				if (UWorld* W = Weak.Get()) GEngine->Exec(W, TEXT("HighResShot 1"));
				return false;
			}), 1.0f);
		}
	}

	/** A visible box with BlockAll collision: Centre and HalfSize in cm. */
	AActor* DemoBlock(UWorld* World, const FVector& Centre, const FVector& HalfSize, const TCHAR* Label)
	{
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(World->PersistentLevel, AActor::StaticClass(), FName(Label));
		AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Centre), Params);
		UBoxComponent* Collision = NewObject<UBoxComponent>(A, TEXT("Collision"));
		Collision->SetBoxExtent(HalfSize);
		Collision->SetCollisionProfileName(TEXT("BlockAll"));
		A->SetRootComponent(Collision);
		A->AddInstanceComponent(Collision);
		Collision->RegisterComponent();
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(A, TEXT("Mesh"));
			Mesh->SetStaticMesh(Cube);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetRelativeScale3D(HalfSize / 50.0);  // the engine cube is 100 cm, centred
			Mesh->SetupAttachment(Collision);
			A->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
		}
		A->SetActorLocation(Centre);
#if WITH_EDITOR
		A->SetActorLabel(Label);
#endif
		return A;
	}

	UAdariMarkerComponent* DemoMarker(AActor* Owner)
	{
		UAdariMarkerComponent* M = NewObject<UAdariMarkerComponent>(Owner, TEXT("AdariMarker"));
		if (Owner->GetRootComponent()) M->SetupAttachment(Owner->GetRootComponent());
		else Owner->SetRootComponent(M);
		Owner->AddInstanceComponent(M);
		M->RegisterComponent();
		return M;
	}

	/**
	 * Adari.Demo [x y z]: builds a 10 x 10 m test room at the given point (default: the origin) in the current
	 * level: outer walls, an inner wall with a 1 m gap, a crate that one variant places in the gap, entry and exit
	 * markers, and an Adari volume with two rules. Run Adari.Check afterwards. Meant for trying the plugin out.
	 */
	void RunDemo(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogAdari, Error, TEXT("Adari.Demo: no world"));
			return;
		}
		FVector O = FVector::ZeroVector;
		if (Args.Num() >= 3)
		{
			O = FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		}
		auto P = [&](double X, double Y, double Z) { return O + FVector(X, Y, Z); };
		DemoBlock(World, P(500, 500, -10), FVector(500, 500, 10), TEXT("AdariDemo_Floor"));
		DemoBlock(World, P(500, -10, 150), FVector(520, 10, 150), TEXT("AdariDemo_WallSouth"));
		DemoBlock(World, P(500, 1010, 150), FVector(520, 10, 150), TEXT("AdariDemo_WallNorth"));
		DemoBlock(World, P(-10, 500, 150), FVector(10, 500, 150), TEXT("AdariDemo_WallWest"));
		DemoBlock(World, P(1010, 500, 150), FVector(10, 500, 150), TEXT("AdariDemo_WallEast"));
		DemoBlock(World, P(500, 225, 150), FVector(10, 225, 150), TEXT("AdariDemo_InnerWallA"));
		DemoBlock(World, P(500, 775, 150), FVector(10, 225, 150), TEXT("AdariDemo_InnerWallB"));

		AActor* Crate = DemoBlock(World, P(500, 500, 60), FVector(40, 40, 60), TEXT("AdariDemo_Crate"));
		DemoMarker(Crate)->Variant = TEXT("props.crate");

		for (const auto& [Tag, X] : {std::pair<const TCHAR*, double>{TEXT("entry"), 150.0}, {TEXT("exit"), 850.0}})
		{
			AActor* A = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(P(X, 500, 50)));
			DemoMarker(A)->MarkerTags.Add(Tag);
			A->SetActorLocation(P(X, 500, 50));
#if WITH_EDITOR
			A->SetActorLabel(FString(TEXT("AdariDemo_")) + Tag);
#endif
		}

		AAdariVolume* V = World->SpawnActor<AAdariVolume>(AAdariVolume::StaticClass(), FTransform(P(500, 500, 100)));
		V->Bounds->SetBoxExtent(FVector(500, 500, 100));
		V->CellSize = 50.0f;
		V->bUseNavMesh = false;
		V->DrawSeconds = 120.0f;
		V->RulesJson = TEXT(R"({"rules":[{"id":"exit-reachable","template":"reachable","from":"entry","to":"exit","radius":20},)"
		                    R"({"id":"no-dead-pockets","template":"no_isolated_walkable","from":"entry","min_cells":4}]})");
#if WITH_EDITOR
		V->SetActorLabel(TEXT("AdariDemo_Volume"));
#endif
#if WITH_EDITOR
		// Look straight down at the room from the editor's level viewports.
		if (GEditor)
		{
			for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
			{
				if (!Client || !Client->IsPerspective()) continue;
				Client->SetViewLocation(P(500, 500, 2200));
				Client->SetViewRotation(FRotator(-89.9, 0, 0));
				Client->SetRealtime(true);
				Client->Invalidate();
			}
		}
#endif
		UE_LOG(LogAdari, Display, TEXT("Adari.Demo: test room built at %s; run Adari.Check"), *O.ToString());
	}

	FAutoConsoleCommandWithWorldAndArgs GAdariCheck(
		TEXT("Adari.Check"),
		TEXT("Samples every Adari volume in the level, checks its rules, draws failures and writes reports to Saved/Adari."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCheck));

	FAutoConsoleCommandWithWorldAndArgs GAdariDemo(
		TEXT("Adari.Demo"),
		TEXT("Adari.Demo [x y z]: builds a small test room with a variant crate, markers and an Adari volume in the current level."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunDemo));
}

IMPLEMENT_MODULE(FDefaultModuleImpl, Adari);
