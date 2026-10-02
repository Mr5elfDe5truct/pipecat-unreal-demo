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

		// The Pipecat C++ client and its WebSocket transport, with the libraries
		// they need, built with Unreal's toolchain by ThirdParty/build-linux.sh.
		string ThirdParty = Path.Combine(PluginDirectory, "ThirdParty", "Linux");
		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "include"));
		foreach (string Library in Directory.GetFiles(Path.Combine(ThirdParty, "lib"), "*.a"))
		{
			PublicAdditionalLibraries.Add(Library);
		}

		// libcurl (with the libraries it needs) starts bots, OpenSSL secures the
		// WebSocket, and SDL captures the microphone, since Unreal's audio
		// capture has no Linux backend.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl", "nghttp2", "OpenSSL", "zlib", "SDL3");
	}
}
