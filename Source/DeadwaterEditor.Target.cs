using UnrealBuildTool;

public class DeadwaterEditorTarget : TargetRules
{
	public DeadwaterEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Deadwater");
	}
}
