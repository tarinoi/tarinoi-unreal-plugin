// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

using UnrealBuildTool;

public class TarinoiEditor : ModuleRules
{
	public TarinoiEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Tarinoi",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"UnrealEd",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"InputCore",
			"Json",
			"Projects",
			"DeveloperSettings",
			"PropertyEditor",
			"MessageLog",
			"Settings",
			"DeveloperToolSettings",
			"HTTP",
		});
	}
}
