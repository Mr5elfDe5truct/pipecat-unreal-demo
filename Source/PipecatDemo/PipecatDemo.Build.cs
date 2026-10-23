//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

using UnrealBuildTool;

public class PipecatDemo : ModuleRules
{
	public PipecatDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",
			"Niagara",
			"Pipecat",
			"ProceduralMeshComponent",
			"Slate",
			"SlateCore",
		});
	}
}
