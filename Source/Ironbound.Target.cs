// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class IronboundTarget : TargetRules
{
	public IronboundTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;

		// Must match the engine's own defaults. This project shares a build environment with
		// UnrealEditor, and UBT rejects a target that diverges on settings affecting that
		// environment ("has build products in common with UnrealEditor"). V7 is the 5.8 default.
		DefaultBuildSettings = BuildSettingsVersion.V7;

		// Pinned rather than Latest: the engine docs warn that Latest carries a high risk
		// of introducing compile errors on newer engine versions.
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;

		ExtraModuleNames.Add("Ironbound");
	}
}
