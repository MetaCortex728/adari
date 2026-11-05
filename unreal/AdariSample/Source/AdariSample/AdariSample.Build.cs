// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class AdariSample : ModuleRules
{
	public AdariSample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Adari" });
	}
}