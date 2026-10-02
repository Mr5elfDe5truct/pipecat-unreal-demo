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

		bool bWindows = Target.Platform == UnrealTargetPlatform.Win64;

		// The Pipecat C++ client and its WebSocket transport, with the libraries
		// they need, built with Unreal's toolchain by ThirdParty/build-linux.sh,
		// or with Visual Studio's by ThirdParty/build-windows.ps1.
		string ThirdParty = Path.Combine(PluginDirectory, "ThirdParty", bWindows ? "Win64" : "Linux");
		PublicSystemIncludePaths.Add(Path.Combine(ThirdParty, "include"));
		foreach (string Library in Directory.GetFiles(Path.Combine(ThirdParty, "lib"), bWindows ? "*.lib" : "*.a"))
		{
			PublicAdditionalLibraries.Add(Library);
		}

		// libcurl (with the libraries it needs) starts bots, and OpenSSL secures
		// the WebSocket.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl", "nghttp2", "OpenSSL", "zlib");

		if (bWindows)
		{
			// Unreal's audio capture captures the microphone, and its resampler
			// converts it to the sample rate the bot is sent.
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AudioCapture",
				"AudioCaptureCore",
				"AudioPlatformConfiguration",
			});

			// What the WebSocket transport's libraries need from Windows.
			PublicSystemLibraries.AddRange(new string[] { "ws2_32.lib", "iphlpapi.lib", "bcrypt.lib" });
		}
		else
		{
			// SDL captures the microphone, since Unreal's audio capture has no
			// Linux backend.
			AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL3");
		}
	}
}
