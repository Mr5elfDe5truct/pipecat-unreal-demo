//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

using System.IO;
using UnrealBuildTool;

public class Pipecat : ModuleRules
{
	public Pipecat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The Pipecat C++ client reports errors with exceptions.
		bEnableExceptions = true;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		// The Pipecat C++ client and its Daily transport, built with Unreal's
		// toolchain by ThirdParty/build-linux.sh, and the Daily Core shared
		// library.
		string ThirdParty = Path.Combine(PluginDirectory, "ThirdParty", "Linux");
		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "include"));
		PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "lib", "libpipecat_daily.a"));
		PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "lib", "libpipecat.a"));
		PublicAdditionalLibraries.Add(Path.Combine(ThirdParty, "lib", "libdaily_core.so"));
		// Next to the module, where its rpath ($ORIGIN) finds it.
		RuntimeDependencies.Add(
			"$(BinaryOutputDir)/libdaily_core.so",
			Path.Combine(ThirdParty, "lib", "libdaily_core.so")
		);

		// libcurl (with the libraries it needs) starts bots, and SDL captures
		// the microphone, since Unreal's audio capture has no Linux backend.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl", "nghttp2", "OpenSSL", "zlib", "SDL3");
	}
}
