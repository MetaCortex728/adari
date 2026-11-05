// SPDX-License-Identifier: Apache-2.0
// Builds small levels from collision boxes, samples them and checks rules. Runs headless (NullRHI).
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AdariLibrary.h"
#include "AdariMarkerComponent.h"
#include "AdariSampler.h"
#include "AdariVolume.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace AdariTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A 1000 x 1000 cm floor with its top at z = 0, an Adari volume over it (cells of 50 cm, top at 200 cm),
	 *  and helpers for walls, markers and variant props. */
	struct FLevel
	{
		UWorld* World = nullptr;
		AAdariVolume* Volume = nullptr;

		FLevel()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AdariTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Box(FVector(500, 500, -10), FVector(500, 500, 10));
			Volume = World->SpawnActor<AAdariVolume>(FVector(500, 500, 100), FRotator::ZeroRotator);
			Volume->Bounds->SetBoxExtent(FVector(500, 500, 100));
			Volume->CellSize = 50.0f;
			Volume->bUseNavMesh = false;
			Volume->bDrawResults = false;
		}

		~FLevel()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** A blocking box: Centre and half Extent in cm. */
		AActor* Box(const FVector& Centre, const FVector& Extent)
		{
			AActor* A = World->SpawnActor<AActor>();
			UBoxComponent* B = NewObject<UBoxComponent>(A, TEXT("Collision"));
			B->SetBoxExtent(Extent);
			B->SetCollisionProfileName(TEXT("BlockAll"));
			A->SetRootComponent(B);
			B->RegisterComponent();
			A->SetActorLocation(Centre);
			return A;
		}

		/** A wall 300 cm high from x0 to x1 at y (or the reverse), 20 cm thick. */
		AActor* WallX(double X0, double X1, double Y)
		{
			return Box(FVector((X0 + X1) / 2, Y, 150), FVector((X1 - X0) / 2, 10, 150));
		}

		UAdariMarkerComponent* Mark(AActor* A, std::initializer_list<const TCHAR*> Tags)
		{
			UAdariMarkerComponent* M = NewObject<UAdariMarkerComponent>(A);
			for (const TCHAR* T : Tags) M->MarkerTags.Add(T);
			if (A->GetRootComponent()) M->SetupAttachment(A->GetRootComponent());
			else A->SetRootComponent(M);
			M->RegisterComponent();
			return M;
		}

		UAdariMarkerComponent* Point(const FVector& At, std::initializer_list<const TCHAR*> Tags)
		{
			AActor* A = World->SpawnActor<AActor>(At, FRotator::ZeroRotator);
			UAdariMarkerComponent* M = Mark(A, Tags);
			A->SetActorLocation(At);
			return M;
		}

		FAdariResult Check(const TCHAR* Rules)
		{
			Volume->RulesJson = Rules;
			return UAdariLibrary::CheckVolume(Volume);
		}
	};

	const TCHAR* ExitRule = TEXT(R"({"rules":[{"id":"exit","template":"reachable","from":"entry","to":"exit","radius":20}]})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdariOpenRoomTest, "Adari.Sampling.OpenRoom", AdariTests::Flags)
bool FAdariOpenRoomTest::RunTest(const FString&)
{
	AdariTests::FLevel L;
	L.Point(FVector(100, 500, 50), {TEXT("entry")});
	L.Point(FVector(900, 500, 50), {TEXT("exit")});
	FAdariSample S;
	FString Error;
	if (!TestTrue(TEXT("The open room samples"), AdariSampleVolume(*L.Volume, S, Error))) return false;
	TestEqual(TEXT("20 x 20 cells of 50 cm"), S.Map.width * 100 + S.Map.height, 20 * 100 + 20);
	std::size_t Walk = 0;
	for (auto V : S.Map.walk) Walk += V;
	TestEqual(TEXT("Every cell of the open floor is walkable"), static_cast<int32>(Walk), 400);
	TestEqual(TEXT("Both markers became points"), static_cast<int32>(S.Map.points.size()), 2);
	TestTrue(TEXT("Ground found at the floor's top"), FMath::IsNearlyEqual(S.GroundZ[0], 0.0, 0.5));
	const FAdariResult R = L.Check(AdariTests::ExitRule);
	TestFalse(TEXT("The exit is reachable"), R.bFailed);
	TestTrue(TEXT("Reports were written"), FPaths::FileExists(FPaths::Combine(R.OutputDir, TEXT("report.sarif"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdariWallTest, "Adari.Sampling.WallSplitsRoom", AdariTests::Flags)
bool FAdariWallTest::RunTest(const FString&)
{
	// The failing check this test provokes logs a warning; it is expected.
	AddExpectedMessage(TEXT("error-level checks"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	AdariTests::FLevel L;
	L.Box(FVector(500, 500, 150), FVector(10, 500, 150));  // a wall across the whole room at x = 500
	L.Point(FVector(100, 500, 50), {TEXT("entry")});
	L.Point(FVector(900, 500, 50), {TEXT("exit")});
	const FAdariResult R = L.Check(AdariTests::ExitRule);
	TestTrue(TEXT("The wall cuts the exit off"), R.bFailed);
	TestEqual(TEXT("One error"), R.Errors, 1);
	FAdariSample S;
	FString Error;
	AdariSampleVolume(*L.Volume, S, Error);
	TestEqual(TEXT("The wall blocks sight at eye height"), static_cast<int32>(S.Map.sight[static_cast<std::size_t>(S.Map.index(10, 10))]), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdariGapTest, "Adari.Sampling.GapAndRadius", AdariTests::Flags)
bool FAdariGapTest::RunTest(const FString&)
{
	// The failing check this test provokes logs a warning; it is expected.
	AddExpectedMessage(TEXT("error-level checks"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	// A wall with a 100 cm gap: a 20 cm agent passes, a 60 cm one does not.
	AdariTests::FLevel L;
	L.Box(FVector(500, 225, 150), FVector(10, 225, 150));
	L.Box(FVector(500, 775, 150), FVector(10, 225, 150));
	L.Point(FVector(100, 500, 50), {TEXT("entry")});
	L.Point(FVector(900, 500, 50), {TEXT("exit")});
	TestFalse(TEXT("A narrow agent fits through the gap"), L.Check(AdariTests::ExitRule).bFailed);
	TestTrue(TEXT("A wide agent does not"),
		L.Check(TEXT(R"({"rules":[{"id":"exit","template":"reachable","from":"entry","to":"exit","radius":60}]})")).bFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdariVariantTest, "Adari.Sampling.VariantProp", AdariTests::Flags)
bool FAdariVariantTest::RunTest(const FString&)
{
	// The failing check this test provokes logs a warning; it is expected.
	AddExpectedMessage(TEXT("error-level checks"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	// The same gap, and a crate that may or may not stand in it.
	AdariTests::FLevel L;
	L.Box(FVector(500, 225, 150), FVector(10, 225, 150));
	L.Box(FVector(500, 775, 150), FVector(10, 225, 150));
	AActor* Crate = L.Box(FVector(500, 500, 60), FVector(40, 40, 60));
	UAdariMarkerComponent* M = L.Mark(Crate, {TEXT("crate")});
	M->Variant = TEXT("props.crate");
	L.Point(FVector(100, 500, 50), {TEXT("entry")});
	L.Point(FVector(900, 500, 50), {TEXT("exit")});
	FAdariSample S;
	FString Error;
	AdariSampleVolume(*L.Volume, S, Error);
	if (!TestEqual(TEXT("One variant group"), static_cast<int32>(S.Map.groups.size()), 1)) return false;
	TestTrue(TEXT("The group can be empty"), S.Map.groups[0].optional);
	TestTrue(TEXT("The crate blocks cells when chosen"), !S.Map.groups[0].options[0].block_walk.empty());
	TestEqual(TEXT("The crate is not sampled as a wall"), static_cast<int32>(S.Map.walk[static_cast<std::size_t>(S.Map.index(10, 10))]), 1);
	const FAdariResult R = L.Check(AdariTests::ExitRule);
	TestTrue(TEXT("With the crate the exit is cut off"), R.bFailed);
	TestTrue(TEXT("The report names the variant"), R.Report.Contains(TEXT("props=crate")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdariStepTest, "Adari.Sampling.StepHeight", AdariTests::Flags)
bool FAdariStepTest::RunTest(const FString&)
{
	// The failing check this test provokes logs a warning; it is expected.
	AddExpectedMessage(TEXT("error-level checks"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	// A 30 cm platform can be stepped onto; a 120 cm one cannot (its top is then an island).
	AdariTests::FLevel L;
	L.Box(FVector(750, 500, 15), FVector(250, 500, 15));
	L.Point(FVector(100, 500, 50), {TEXT("entry")});
	L.Point(FVector(900, 500, 80), {TEXT("exit")});
	TestFalse(TEXT("A 30 cm step is climbable"), L.Check(AdariTests::ExitRule).bFailed);
	AdariTests::FLevel High;
	High.Box(FVector(750, 500, 60), FVector(250, 500, 60));
	High.Point(FVector(100, 500, 50), {TEXT("entry")});
	High.Point(FVector(900, 500, 150), {TEXT("exit")});
	TestTrue(TEXT("A 120 cm ledge is not"), High.Check(AdariTests::ExitRule).bFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdariJsonTest, "Adari.Export.RoundTrip", AdariTests::Flags)
bool FAdariJsonTest::RunTest(const FString&)
{
	AdariTests::FLevel L;
	L.WallX(0, 600, 500);
	L.Point(FVector(100, 100, 50), {TEXT("entry")});
	const FString Json = UAdariLibrary::SampleToJson(L.Volume);
	if (!TestFalse(TEXT("The level exports"), Json.IsEmpty())) return false;
	FAdariSample S;
	FString Error;
	AdariSampleVolume(*L.Volume, S, Error);
	try
	{
		const adari::Map Back = adari::parse_native(AdariConvert::Std(Json), "export.json");
		TestTrue(TEXT("Walkability survives the round trip"), Back.walk == S.Map.walk);
		TestTrue(TEXT("Sight survives the round trip"), Back.sight == S.Map.sight);
		TestEqual(TEXT("Points survive"), static_cast<int32>(Back.points.size()), 1);
	}
	catch (const std::exception& E)
	{
		AddError(FString::Printf(TEXT("Export did not parse: %s"), UTF8_TO_TCHAR(E.what())));
	}
	return true;
}

#endif
