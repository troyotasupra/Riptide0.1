using UnrealBuildTool;

public class Riptide : ModuleRules
{
	public Riptide(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file compiles on its own: in a unity build, files' private helpers with the same name (several files
		// have their own CmPerSecToKnots) collide whenever adding a file regroups them, on one machine and not another.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Water", "ProceduralMeshComponent", "Niagara", "UMG", "Slate", "SlateCore",
			"OnlineSubsystem", "OnlineSubsystemUtils", "Sockets",
			// Two-bone IK for the crew's hands and feet on the ladder (URiptideCrewAnimInstance).
			"AnimationCore"
		});
	}
}
