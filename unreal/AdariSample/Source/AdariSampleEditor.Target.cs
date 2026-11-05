// SPDX-License-Identifier: Apache-2.0
using UnrealBuildTool;

public class AdariSampleEditorTarget : TargetRules
{
	public AdariSampleEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AdariSample");
	}
}