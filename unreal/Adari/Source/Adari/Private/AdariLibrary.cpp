// SPDX-License-Identifier: Apache-2.0
#include "AdariLibrary.h"

#include "AdariSampler.h"
#include "AdariVolume.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <stdexcept>

namespace
{
	FAdariResult Failure(const FString& Why)
	{
		FAdariResult R;
		R.bFailed = true;
		R.Report = Why;
		UE_LOG(LogAdari, Error, TEXT("%s"), *Why);
		return R;
	}

	FString OutputDirFor(const AAdariVolume& Volume)
	{
		FString Name = Volume.GetActorNameOrLabel();
		for (TCHAR& C : Name)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) C = TEXT('_');
		}
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Adari"), Name));
	}

	adari::RuleSet LoadRules(const AAdariVolume& Volume)
	{
		if (!Volume.RulesJson.TrimStartAndEnd().IsEmpty()) return adari::parse_rules(AdariConvert::Std(Volume.RulesJson));
		if (Volume.RulesFile.FilePath.IsEmpty()) throw std::runtime_error("the volume has no rules (set RulesFile or RulesJson)");
		const FString Path = FPaths::IsRelative(Volume.RulesFile.FilePath)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Volume.RulesFile.FilePath)
			: Volume.RulesFile.FilePath;
		return adari::load_rules(AdariConvert::Std(Path));
	}

	void Draw(const AAdariVolume& Volume, const FAdariSample& S, const adari::Report& Rep)
	{
		UWorld* World = Volume.GetWorld();
		if (!World || !Volume.bDrawResults) return;
		const float Life = Volume.DrawSeconds;
		const FVector Half(S.Cell * 0.45, S.Cell * 0.45, 8.0);
		DrawDebugBox(World, Volume.Bounds->Bounds.Origin, Volume.Bounds->Bounds.BoxExtent,
			Rep.failed() ? FColor::Red : FColor::Green, false, Life, 0, 4.0f);
		for (const adari::Finding& F : Rep.findings)
		{
			const FColor Col = F.severity == adari::Severity::kError ? FColor(230, 40, 40) : FColor(240, 180, 30);
			int32 Drawn = 0;
			for (int C : F.cells)
			{
				if (++Drawn > 2000) break;  // keep huge regions cheap to draw
				DrawDebugSolidBox(World, S.CellCentre(C, 10.0), Half, Col.WithAlpha(110), false, Life);
			}
			for (std::size_t K = 0; K + 1 < F.path.size(); ++K)
			{
				DrawDebugLine(World, S.CellCentre(F.path[K], 30.0), S.CellCentre(F.path[K + 1], 30.0), FColor(250, 130, 20), false, Life, 0, 6.0f);
			}
		}
	}
}

FAdariResult UAdariLibrary::CheckVolume(AAdariVolume* Volume)
{
	if (!Volume) return Failure(TEXT("Adari: no volume"));
	FAdariSample Sample;
	FString Error;
	if (!AdariSampleVolume(*Volume, Sample, Error))
	{
		return Failure(FString::Printf(TEXT("Adari: cannot sample %s: %s"), *Volume->GetActorNameOrLabel(), *Error));
	}
	try
	{
		const adari::RuleSet Rules = LoadRules(*Volume);
		const adari::Report Rep = adari::evaluate(Sample.Map, Rules);

		FAdariResult R;
		R.bFailed = Rep.failed();
		R.Errors = static_cast<int32>(Rep.count(adari::Severity::kError));
		R.Warnings = static_cast<int32>(Rep.count(adari::Severity::kWarning));
		for (const adari::RuleResult& Res : Rep.results)
		{
			R.RulesNotChecked += Res.status == adari::Status::kNotChecked ? 1 : 0;
		}
		std::string Text = adari::summarize(Sample.Map) + "\n" + adari::to_text(Sample.Map, Rep);
		R.Report = AdariConvert::String(Text);
		R.OutputDir = OutputDirFor(*Volume);

		IFileManager::Get().MakeDirectory(*R.OutputDir, true);
		const std::string Dir = AdariConvert::Std(R.OutputDir) + "/";
		adari::write_file(Dir + "map.adari.json", adari::to_native_json(Sample.Map));
		adari::write_file(Dir + "report.txt", Text);
		adari::write_file(Dir + "report.json", adari::to_json(Sample.Map, Rep));
		adari::write_file(Dir + "report.sarif", adari::to_sarif(Sample.Map, Rep));
		adari::write_file(Dir + "overlay.png", adari::overlay_png(Sample.Map, Rep));

		Draw(*Volume, Sample, Rep);
		UE_LOG(LogAdari, Log, TEXT("%s\nReports: %s"), *R.Report, *R.OutputDir);
		if (R.bFailed)
		{
			UE_LOG(LogAdari, Warning, TEXT("Adari: %s fails %d error-level checks (%d rules not checked)"),
				*Volume->GetActorNameOrLabel(), R.Errors, R.RulesNotChecked);
		}
		return R;
	}
	catch (const std::exception& E)
	{
		return Failure(FString::Printf(TEXT("Adari: %s: %s"), *Volume->GetActorNameOrLabel(), UTF8_TO_TCHAR(E.what())));
	}
}

FString UAdariLibrary::SampleToJson(AAdariVolume* Volume)
{
	if (!Volume) return FString();
	FAdariSample Sample;
	FString Error;
	if (!AdariSampleVolume(*Volume, Sample, Error))
	{
		UE_LOG(LogAdari, Error, TEXT("Adari: cannot sample: %s"), *Error);
		return FString();
	}
	return AdariConvert::String(adari::to_native_json(Sample.Map));
}

TArray<FAdariResult> UAdariLibrary::CheckAll(UObject* WorldContext)
{
	TArray<FAdariResult> Out;
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World) return Out;
	for (TActorIterator<AAdariVolume> It(World); It; ++It)
	{
		Out.Add(CheckVolume(*It));
	}
	if (Out.IsEmpty()) UE_LOG(LogAdari, Warning, TEXT("Adari: no Adari volume in %s"), *World->GetName());
	return Out;
}
