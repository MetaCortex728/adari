// SPDX-License-Identifier: Apache-2.0
using System.IO;
using UnrealBuildTool;

public class Adari : ModuleRules
{
	public Adari(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The engine-independent core is compiled into this module from the repository's core/ folder,
		// so there is one copy of it. The plugin therefore lives in the repository (unreal/Adari); clone
		// the whole repository into a project's Plugins folder to use it.
		string RepoRoot = Path.GetFullPath(Path.Combine(PluginDirectory, "..", ".."));
		PublicIncludePaths.Add(Path.Combine(RepoRoot, "core", "include"));
		PrivateIncludePaths.Add(Path.Combine(RepoRoot, "core", "src"));
		PrivateIncludePaths.Add(Path.Combine(RepoRoot, "third_party", "nlohmann_json", "include"));

		// The core reports bad input through exceptions, which the plugin catches and logs.
		bEnableExceptions = true;
		// Each core source has its own anonymous namespace; unity files would merge them.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "NavigationSystem", "Projects" });
	}
}
