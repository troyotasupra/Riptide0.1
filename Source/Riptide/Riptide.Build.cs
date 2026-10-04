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
			"OnlineSubsystem", "OnlineSubsystemUtils", "CoreOnline", "NetCore", "Sockets",
			// The radio's and loudhailer's sound (band-pass and drive on voices, URiptideVoiceComponent).
			"AudioExtensions", "Synthesis",
			// Two-bone IK for the crew's hands and feet on the ladder (URiptideCrewAnimInstance).
			"AnimationCore",
			// The item table written out for the model generator (URiptideDataLibrary).
			"Json", "JsonUtilities",
			// Listing the microphones for the settings menu and pointing the voice capture at one
			// (URiptideVoiceComponent::ListMicrophones, ApplyMicrophone).
			"AudioCaptureCore", "Voice"
		});
		// The microphone list needs the platform's capture backend loaded (as the AudioCapture plugin does it).
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.Add("AudioCaptureWasapi");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
		}
	}
}
