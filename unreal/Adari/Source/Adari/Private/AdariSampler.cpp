// SPDX-License-Identifier: Apache-2.0
#include "AdariSampler.h"

#include "AdariMarkerComponent.h"
#include "AdariVolume.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY(LogAdari);

namespace
{
	constexpr int64 MaxCells = 4'000'000;

	struct FMarkerInfo
	{
		const UAdariMarkerComponent* Marker = nullptr;
		AActor* Owner = nullptr;
	};

	std::vector<std::string> ToTags(const TArray<FString>& Tags)
	{
		std::vector<std::string> Out;
		for (const FString& T : Tags)
		{
			if (!T.IsEmpty()) Out.push_back(AdariConvert::Std(T));
		}
		return Out;
	}

	FString ActorName(const AActor* Actor)
	{
		return Actor ? Actor->GetActorNameOrLabel() : FString(TEXT("marker"));
	}
}

FVector FAdariSample::CellCentre(int32 Index, double Lift) const
{
	const adari::Cell C = Map.cell(Index);
	return FVector(Origin.X + (C.x + 0.5) * Cell, Origin.Y + (C.y + 0.5) * Cell,
		(GroundZ.IsValidIndex(Index) ? GroundZ[Index] : Origin.Z) + Lift);
}

bool AdariSampleVolume(const AAdariVolume& Volume, FAdariSample& Out, FString& Error)
{
	UWorld* World = Volume.GetWorld();
	if (!World || !Volume.Bounds)
	{
		Error = TEXT("the volume is not in a world");
		return false;
	}
	const FBox Box = Volume.Bounds->Bounds.GetBox();
	const double Cell = FMath::Max<double>(Volume.CellSize, 5.0);
	const int32 W = FMath::Max(1, FMath::CeilToInt((Box.Max.X - Box.Min.X) / Cell));
	const int32 H = FMath::Max(1, FMath::CeilToInt((Box.Max.Y - Box.Min.Y) / Cell));
	if (static_cast<int64>(W) * H > MaxCells)
	{
		Error = FString::Printf(TEXT("%d x %d cells is too many; raise CellSize or shrink the volume"), W, H);
		return false;
	}

	Out = FAdariSample();
	Out.Cell = Cell;
	Out.Origin = FVector(Box.Min.X, Box.Min.Y, Box.Min.Z);
	adari::Map& Map = Out.Map;
	Map.resize(W, H);
	Map.cell_size = Cell;
	Map.name = AdariConvert::Std(Volume.GetActorNameOrLabel());
	Map.source_file = AdariConvert::Std(World->GetPackage()->GetName());
	Out.GroundZ.Init(Box.Min.Z, W * H);

	// Markers, and the actors left out of collision sampling (doors and variant props).
	TArray<FMarkerInfo> Markers;
	TArray<AActor*> Ignored;
	Ignored.Add(const_cast<AAdariVolume*>(&Volume));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		TInlineComponentArray<UAdariMarkerComponent*> Found(Actor);
		for (const UAdariMarkerComponent* M : Found)
		{
			Markers.Add({M, Actor});
			if (M->bDoor || !M->Variant.IsEmpty()) Ignored.AddUnique(Actor);
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AdariSample), false);
	Params.AddIgnoredActors(Ignored);

	UNavigationSystemV1* Nav = Volume.bUseNavMesh ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	if (Volume.bUseNavMesh && !NavData)
	{
		Map.notes.push_back("no navigation mesh in the level, so collision decides walkability");
	}
	else if (NavData)
	{
		Map.notes.push_back("walkability comes from the navigation mesh");
	}

	const double CosMax = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Volume.MaxSlope)));
	const double Half = FMath::Max(1.0, (Volume.AgentHeight - Volume.StepHeight) * 0.5);
	const FVector CellHalf(Cell * 0.45, Cell * 0.45, Half);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Map.index(X, Y);
			const double CX = Box.Min.X + (X + 0.5) * Cell;
			const double CY = Box.Min.Y + (Y + 0.5) * Cell;
			FHitResult Hit;
			const bool bGround = World->LineTraceSingleByChannel(Hit, FVector(CX, CY, Box.Max.Z), FVector(CX, CY, Box.Min.Z),
				Volume.MovementChannel, Params);
			const double Z = bGround ? Hit.ImpactPoint.Z : Box.Min.Z;
			Out.GroundZ[I] = Z;

			bool bWalk = bGround && Hit.ImpactNormal.Z >= CosMax;
			if (bWalk)
			{
				const FVector Body(CX, CY, Z + Volume.StepHeight + Half);
				bWalk = !World->OverlapBlockingTestByChannel(Body, FQuat::Identity, Volume.MovementChannel,
					FCollisionShape::MakeBox(CellHalf), Params);
			}
			if (NavData)
			{
				FNavLocation Loc;
				bWalk = Nav->ProjectPointToNavigation(FVector(CX, CY, Z), Loc, FVector(Cell * 0.5, Cell * 0.5, Volume.StepHeight + 10.0), NavData)
					&& FVector::Dist2D(Loc.Location, FVector(CX, CY, Z)) <= Cell * 0.5;
			}
			const bool bSight = World->OverlapBlockingTestByChannel(FVector(CX, CY, Z + Volume.EyeHeight), FQuat::Identity,
				Volume.SightChannel, FCollisionShape::MakeBox(FVector(Cell * 0.45, Cell * 0.45, 5.0)), Params);
			Map.walk[static_cast<std::size_t>(I)] = bWalk ? 1 : 0;
			Map.sight[static_cast<std::size_t>(I)] = bSight ? 1 : 0;
		}
	}

	// Height steps between neighbours: climbing more than StepHeight is blocked in both directions.
	const int Dx[4] = {0, 1, 0, -1};
	const int Dy[4] = {-1, 0, 1, 0};
	const uint8 Bits[4] = {adari::kNorth, adari::kEast, adari::kSouth, adari::kWest};
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 A = Map.index(X, Y);
			for (int K = 0; K < 4; ++K)
			{
				if (!Map.inside(X + Dx[K], Y + Dy[K])) continue;
				const int32 B = Map.index(X + Dx[K], Y + Dy[K]);
				if (FMath::Abs(Out.GroundZ[B] - Out.GroundZ[A]) > Volume.StepHeight)
				{
					Map.edge_block[static_cast<std::size_t>(A)] |= Bits[K];
				}
			}
		}
	}

	auto ToCell = [&](const FVector& P) { return std::pair<double, double>((P.X - Box.Min.X) / Cell, (P.Y - Box.Min.Y) / Cell); };
	auto CellsInBox = [&](const FBox& B) {
		std::vector<int> Cells;
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const double CX = Box.Min.X + (X + 0.5) * Cell;
				const double CY = Box.Min.Y + (Y + 0.5) * Cell;
				if (CX >= B.Min.X && CX <= B.Max.X && CY >= B.Min.Y && CY <= B.Max.Y) Cells.push_back(Map.index(X, Y));
			}
		}
		return Cells;
	};

	for (const FMarkerInfo& Info : Markers)
	{
		const UAdariMarkerComponent& M = *Info.Marker;
		const FString Name = ActorName(Info.Owner);
		std::vector<std::string> Tags = ToTags(M.MarkerTags);
		if (!M.Variant.IsEmpty())
		{
			// The owner's colliding components become one option of a variant group.
			FString Group, Option;
			if (!M.Variant.Split(TEXT("."), &Group, &Option) || Group.IsEmpty() || Option.IsEmpty())
			{
				Error = FString::Printf(TEXT("variant '%s' on %s must be written group.option"), *M.Variant, *Name);
				return false;
			}
			const FBox Bounds = Info.Owner->GetComponentsBoundingBox(false);
			auto GroupIt = std::find_if(Map.groups.begin(), Map.groups.end(),
				[&](const adari::VariantGroup& G) { return G.name == AdariConvert::Std(Group); });
			if (GroupIt == Map.groups.end())
			{
				Map.groups.push_back(adari::VariantGroup{AdariConvert::Std(Group), {}, false});
				GroupIt = Map.groups.end() - 1;
			}
			GroupIt->optional = GroupIt->optional || M.bGroupCanBeEmpty;
			adari::VariantOption Opt{AdariConvert::Std(Option), Tags, {}, {}};
			if (Bounds.IsValid)
			{
				for (int C : CellsInBox(Bounds))
				{
					const double Ground = Out.GroundZ[C];
					if (Bounds.Max.Z > Ground + Volume.StepHeight && Bounds.Min.Z < Ground + Volume.AgentHeight) Opt.block_walk.push_back(C);
					if (Bounds.Max.Z > Ground + Volume.EyeHeight && Bounds.Min.Z < Ground + Volume.EyeHeight) Opt.block_sight.push_back(C);
				}
			}
			GroupIt->options.push_back(std::move(Opt));
			continue;
		}
		if (M.Shape == EAdariMarkerShape::Point && !M.bDoor)
		{
			const auto [PX, PY] = ToCell(M.GetComponentLocation());
			if (PX >= 0 && PY >= 0 && PX < W && PY < H) Map.points.push_back(adari::Point{AdariConvert::Std(Name), Tags, PX, PY});
			continue;
		}
		const FVector L = M.GetComponentLocation();
		const FVector E = M.Shape == EAdariMarkerShape::Box ? M.BoxExtent : FVector(Cell * 0.5);
		adari::Region R;
		R.name = AdariConvert::Std(Name);
		R.tags = Tags;
		R.door = M.bDoor;
		if (R.door && std::find(R.tags.begin(), R.tags.end(), "door") == R.tags.end()) R.tags.push_back("door");
		R.cells = CellsInBox(FBox(L - E, L + E));
		if (!R.cells.empty()) Map.regions.push_back(std::move(R));
	}

	// Actor tags such as "adari:entry" are points at the actor's location.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		std::vector<std::string> Tags;
		for (const FName& Tag : It->Tags)
		{
			const FString S = Tag.ToString();
			if (S.StartsWith(TEXT("adari:"))) Tags.push_back(AdariConvert::Std(S.Mid(6)));
		}
		if (Tags.empty()) continue;
		const auto [PX, PY] = ToCell(It->GetActorLocation());
		if (PX >= 0 && PY >= 0 && PX < W && PY < H) Map.points.push_back(adari::Point{AdariConvert::Std(ActorName(*It)), Tags, PX, PY});
	}

	const std::string Problem = adari::validate(Map);
	if (!Problem.empty())
	{
		Error = AdariConvert::String(Problem);
		return false;
	}
	return true;
}
