using UnrealBuildTool;

public class Riptide : ModuleRules
{
	public Riptide(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file compiles on its own: several keep small helpers (colours, fonts) in anonymous namespaces under the
		// same names, which clash whenever a unity build happens to group them into one file.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Water", "ProceduralMeshComponent", "Niagara", "UMG", "Slate", "SlateCore", "AnimationCore"
		});
	}
}
