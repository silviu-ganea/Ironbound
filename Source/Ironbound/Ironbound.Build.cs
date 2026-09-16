// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Ironbound : ModuleRules
{
	public Ironbound(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Runtime-only: trajectory evaluation, equipment physics, and AI movement requests.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AIModule",
				"PhysicsControl",
			});
	}
}
