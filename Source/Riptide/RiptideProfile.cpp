#include "RiptideProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"

bool URiptideProfileSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && FParse::Param(FCommandLine::Get(), TEXT("RiptideProfile"));
}

void URiptideProfileSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	bEnabled = true;
	Elapsed += DeltaTime;
	if (Elapsed < 10.f)
	{
		return;     // the map is still settling: shaders compiling, Nanite streaming in
	}
	SinceReport += DeltaTime;
	Frames += 1;
	Worst = FMath::Max(Worst, DeltaTime);
	if (SinceReport >= 5.f)
	{
		UE_LOG(LogTemp, Display, TEXT("RiptideProfile: %.1f ms a frame on average over %.0f s (worst %.1f ms, %.0f fps)"),
			SinceReport / Frames * 1000.f, SinceReport, Worst * 1000.f, Frames / SinceReport);
		SinceReport = 0.f;
		Frames = 0;
		Worst = 0.f;
	}
	if (!bDumped && Elapsed >= 20.f && GEngine)
	{
		bDumped = true;
		UE_LOG(LogTemp, Display, TEXT("RiptideProfile: GPU breakdown of one frame follows"));
		GEngine->Exec(World, TEXT("r.ProfileGPU.ShowUI 0"));
		GEngine->Exec(World, TEXT("ProfileGPU"));
	}
}
