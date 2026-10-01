#include "RiptideMenuGameMode.h"

#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "RiptideBoat.h"
#include "RiptideCrewFigure.h"
#include "RiptideGameInstance.h"
#include "RiptideMainMenu.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"

// --- The menu shot ---

ARiptideMenuCamera::ARiptideMenuCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the boat has moved for the frame, so the framing doesn't lag it.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	UCameraComponent* Lens = GetCameraComponent();
	Lens->SetFieldOfView(48.f);
	Lens->bConstrainAspectRatio = false;

	// The night's look: a fixed exposure (the searchlight would otherwise make the eye adapt and black out the sea),
	// a soft bloom round the lamps, a vignette, a little grain, and the shadows pulled cool.
	FPostProcessSettings& Post = Lens->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 1.5f;
	Post.bOverride_BloomIntensity = true;
	Post.BloomIntensity = 0.5f;
	Post.bOverride_VignetteIntensity = true;
	Post.VignetteIntensity = 0.55f;
	Post.bOverride_FilmGrainIntensity = true;
	Post.FilmGrainIntensity = 0.12f;
	Post.bOverride_ColorGammaShadows = true;
	Post.ColorGammaShadows = FVector4(0.92f, 0.97f, 1.08f, 1.f);
	Post.bOverride_MotionBlurAmount = true;
	Post.MotionBlurAmount = 0.f;
}

void ARiptideMenuCamera::BeginPlay()
{
	Super::BeginPlay();
	if (APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0))
	{
		Player->SetViewTarget(this);
	}
}

void ARiptideMenuCamera::SetBoat(ARiptideBoat* InBoat)
{
	Boat = InBoat;
	if (!Boat)
	{
		return;
	}
	// The menu is its own little world (no network), so the server-only switches are ours to throw.
	Boat->SetNavLightsOn(true);
	Boat->SetDeckLightsOn(true);
	Boat->SetSearchlightOn(true);
	Boat->AimSearchlight(SweepCentreYaw, SweepPitch);

	// A crew member at the bow rail, looking out where the light searches. Nobody stands at the helm.
	const URiptideGameInstance* Game = GetGameInstance<URiptideGameInstance>();
	Crew = RiptideCrewFigure::Spawn(GetWorld(), FTransform(), Game ? Game->GetAppearance() : FRiptideAppearance());
	if (Crew)
	{
		Crew->AttachToActor(Boat, FAttachmentTransformRules::KeepRelativeTransform);
		Crew->SetActorRelativeLocation(CrewSpot);
		Crew->SetActorRelativeRotation(FRotator(0.f, CrewFacing, 0.f));
	}
}

void ARiptideMenuCamera::SetCrewLook(const FRiptideAppearance& Look)
{
	RiptideCrewFigure::SetLook(Crew, Look);
}

void ARiptideMenuCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsValid(Boat))
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	Time += Dt;
	SweepTime += Dt;

	// The searchlight: a slow sweep across the water off the port bow and back, easing at each end like a
	// hand on the lamp.
	const float SweepYaw = SweepCentreYaw + SweepHalfArc * FMath::Sin(UE_TWO_PI * SweepTime / SweepPeriod);
	Boat->AimSearchlight(SweepYaw, SweepPitch);

	// Frame on the boat's position and heading, smoothed so its rolling and pitching don't shake the shot.
	const FVector BoatAt = Boat->GetActorLocation();
	const float BoatYaw = Boat->GetActorRotation().Yaw;
	if (!bHaveFrame)
	{
		bHaveFrame = true;
		SmoothedYaw = BoatYaw;
		SmoothedBoat = BoatAt;
		SmoothedSeaZ = Boat->GetSeaSurfaceZ(BoatAt);
	}
	SmoothedYaw += FMath::FindDeltaAngleDegrees(SmoothedYaw, BoatYaw) * FMath::Min(1.f, Dt * 0.4f);
	SmoothedBoat = FMath::VInterpTo(SmoothedBoat, BoatAt, Dt, 0.8f);
	const FRotator Frame(0.f, SmoothedYaw, 0.f);

	// Drift: slow, unrelated sways, so it never repeats visibly.
	FVector Where = SmoothedBoat + Frame.RotateVector(FVector(CameraOffset.X, CameraOffset.Y, 0.f)
		+ FVector(45.f * FMath::Sin(Time * 0.11f), 35.f * FMath::Sin(Time * 0.071f + 1.3f), 0.f));
	// Bob: riding the swell under the camera, slowed as a bigger boat would.
	SmoothedSeaZ = FMath::FInterpTo(SmoothedSeaZ, Boat->GetSeaSurfaceZ(Where), Dt, 0.7f);
	Where.Z = SmoothedSeaZ + CameraOffset.Z + 6.f * FMath::Sin(Time * 0.53f);

	FVector Target = SmoothedBoat + Frame.RotateVector(LookAtOffset);
	// Measured from sea level, following a third of the swell under the camera (the eye half-follows the horizon).
	Target.Z = LookAtOffset.Z + 0.35f * SmoothedSeaZ;
	FRotator View = (Target - Where).Rotation();
	View.Yaw += 0.6f * FMath::Sin(Time * 0.13f);
	View.Pitch += 0.4f * FMath::Sin(Time * 0.23f + 0.7f);
	View.Roll = 0.9f * FMath::Sin(Time * 0.37f) + 0.4f * FMath::Sin(Time * 0.61f + 2.f);
	SetActorLocationAndRotation(Where, View);
}

// --- The game mode ---

ARiptideMenuGameMode::ARiptideMenuGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = ARiptideMenuHUD::StaticClass();
}

void ARiptideMenuGameMode::SetUpScene()
{
	if (MenuCamera)
	{
		return;
	}
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	// The boat, launched on the swell at the player start (as ARiptideGameMode launches it, sitting on the water
	// where it is so it doesn't drop into a crest).
	FTransform Start;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Start = It->GetActorTransform();
		break;
	}
	FVector Location = Start.GetLocation();
	float SurfaceZ = 0.f;
	if (const AWaterBodyOcean* Ocean = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass())))
	{
		const auto Query = Ocean->GetWaterBodyComponent()->TryQueryWaterInfoClosestToWorldLocation(
			FVector(Location.X, Location.Y, 0.f), EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
		if (Query.HasValue())
		{
			SurfaceZ = Query.GetValue().GetWaterSurfaceLocation().Z;
		}
	}
	Location.Z = SurfaceZ + 15.f;
	ARiptideBoat* Boat = World->SpawnActor<ARiptideBoat>(ARiptideBoat::StaticClass(), Location, FRotator(0.f, Start.Rotator().Yaw, 0.f), Params);

	MenuCamera = World->SpawnActor<ARiptideMenuCamera>(ARiptideMenuCamera::StaticClass(), Location + FVector(-1000.f, -600.f, 150.f),
		FRotator::ZeroRotator, Params);
	MenuCamera->SetBoat(Boat);

	// The crew screen's booth: high above the sea, well out of the shot.
	CrewPreview = World->SpawnActor<ARiptideCrewPreview>(ARiptideCrewPreview::StaticClass(), FVector(0.f, 0.f, 150000.f), FRotator::ZeroRotator, Params);
}

void ARiptideMenuGameMode::StartPlay()
{
	SetUpScene();
	Super::StartPlay();
}

void ARiptideMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No body to spawn: the player watches the menu shot.
	SetUpScene();
	if (NewPlayer && MenuCamera)
	{
		NewPlayer->SetViewTarget(MenuCamera);
	}
}
