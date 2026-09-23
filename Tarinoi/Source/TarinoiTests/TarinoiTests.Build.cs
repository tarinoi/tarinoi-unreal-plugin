// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

using UnrealBuildTool;

/// <summary>
/// Automation tests for the plugin. An editor-only module, so test fixtures (including the
/// UCLASSes some tests need) never ship in a packaged game.
/// </summary>
public class TarinoiTests : ModuleRules
{
	public TarinoiTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"SQLiteCore",
			"Tarinoi",
			"TarinoiEditor",
			"Projects",
		});
	}
}
