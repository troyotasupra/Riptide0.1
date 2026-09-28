using UnrealBuildTool;

public class DeadwaterTarget : TargetRules
{
	public DeadwaterTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Deadwater");
	}
}
