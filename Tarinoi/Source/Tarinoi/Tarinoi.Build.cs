// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

using UnrealBuildTool;

public class Tarinoi : ModuleRules
{
	public Tarinoi(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"Json",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"SQLiteCore",
			"HTTP",
			"Projects",
		});
	}
}
