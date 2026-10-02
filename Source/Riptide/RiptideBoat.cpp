#include "RiptideBoat.h"

#include "BuoyancyComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StructOnScope.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RiptideCharacter.h"
#include "RiptideGauge.h"
#include "RiptideSettings.h"
#include "RiptideSprayComponent.h"
#include "RiptideStorageComponent.h"
#include "RiptideWakeFoamComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogRiptideBoat, Log, All);

namespace
{
	// Unreal works in centimetres, so forces in Newtons are scaled by 100 (kg*cm/s^2).
	constexpr float NewtonsToUnreal = 100.f;
	constexpr float CmPerSecToKnots = 0.0194384f;

	// Half the hull's size in cm (length, beam, depth): a 7.9 m, 2.6 m beam patrol boat.
	const FVector HullExtent(395.f, 130.f, 35.f);

	// Buoyancy pontoons, in cm. They sit 45 cm deep at rest, which gives the hull about three times its resting lift
	// in reserve (like a real open boat's hull up to its gunwale), so a bow driven into a swell still gets pushed back
	// up hard. How deep they sit also sets how stiffly the hull rides the sea: shallower would bob it up and down like
	// a cork. The waterline sits 20 cm up the hull.
	constexpr float PontoonRadius = 60.f;
	constexpr float PontoonRestDepth = 45.f;

	// The outboards' steering pivots on the transom (twin motors 76 cm apart), and the prop relative to a pivot
	// (see build_outboard in Content/Python/riptide_boat_mesh.py).
	// Each tilts on the tube at the top of its clamp bracket, which hooks over the transom's motor notch.
	const FVector OutboardPivot(-HullExtent.X - 5.f, -38.f, 40.f);
	const FVector OutboardPivotStarboard(-HullExtent.X - 5.f, 38.f, 40.f);
	const FVector PropInOutboard(-58.f, 0.f, -100.f);
	const float WaterlineZ = -HullExtent.Z + 20.f;

	// The deck, where the crew stands (see riptide_boat_mesh.py): flat at this height from the transom to the
	// foredeck. Spots: the helm, behind the console; the aft deck; its two corners; the foredeck.
	constexpr float DeckZ = 20.f;
	// The throttle binnacle on the console's back, right of the wheel: where the twin levers pivot (riptide_boat_mesh.py).
	const FVector ThrottlePivotPort(-49.f, 28.f, DeckZ + 92.f);
	const FVector ThrottlePivotStarboard(-49.f, 36.f, DeckZ + 92.f);
	constexpr float LeverSwingDeg = 35.f;

	// Lights (riptide_boat_mesh.py): the searchlight's pivot on the T-top, the masthead light on its pole, the bow
	// light at the stem, and floods under the canopy over the cockpit.
	const FVector SearchlightPivot(26.f, 40.f, DeckZ + 244.f);
	const FVector MastheadLightPoint(-162.f, 0.f, DeckZ + 317.f);
	const FVector BowLightPoint(381.f, 0.f, DeckZ + 102.f);

	// The radar antenna's hub, on its pedestal on the T-top (riptide_boat_mesh.py's RADAR).
	// The compass card's pivot, in the dome on the console top (riptide_boat_mesh.py's _fittings: the dome at
	// DeckZ + 112, its base ring up to 113.5).
	const FVector CompassPivot(-8.f, 0.f, DeckZ + 114.5f);
	const FVector RadarHub(-70.f, 0.f, DeckZ + 258.f);

	// The steering wheel's hub on the helm's shaft, its face tilted back toward the helmsman (WHEEL_CENTRE and
	// WHEEL_TILT_DEG).
	const FVector WheelCentre(-56.f, 0.f, DeckZ + 88.f);
	constexpr float WheelTiltDeg = 35.f;

	// The radio's hand mic: its clip under the overhead box, the cord's jack on the radio, and where the cord leaves
	// the mic (MIC_HOOK, MIC_CORD_JACK, MIC_CORD_EXIT). The cord is coiled: this long hanging slack.
	const FVector MicHook(-77.5f, 29.f, DeckZ + 199.f);
	const FVector MicCordJack(-77.f, 20.5f, DeckZ + 202.5f);
	const FVector MicCordExit(0.f, 0.f, -12.6f);
	constexpr float MicCordRestLength = 32.f;
	// In the hand: in front of the eyes, low and to the right, its grille toward the mouth.
	const FVector MicInHand(24.f, 8.f, -3.f);
	const FRotator MicInHandRotation(-12.f, -12.f, 0.f);

	// The dash (riptide_boat_mesh.py's _dash_and_wheel): a slope from (-45, DeckZ + 95) up to (-20, DeckZ + 112),
	// facing the helm. Returns a point Fraction of the way up it, Y across, Out off its face.
	FVector OnDash(float Fraction, float Y, float Out)
	{
		const FVector2D Lo(-45.f, DeckZ + 95.f), Hi(-20.f, DeckZ + 112.f);
		const FVector2D Up = (Hi - Lo).GetSafeNormal();
		const FVector2D Normal(-Up.Y, Up.X);
		const FVector2D P = Lo + (Hi - Lo) * Fraction + Normal * Out;
		return FVector(P.X, Y, P.Y);
	}
	// Facing out of the dash (widgets face along their +X), with their tops up the slope.
	const FRotator DashFacing(FMath::RadiansToDegrees(FMath::Atan2(25.f, 17.f)), 180.f, 0.f);

	const FVector DeckSpots[] = {
		FVector(-110.f, 0.f, DeckZ),
		FVector(-270.f, 0.f, DeckZ),
		FVector(-270.f, -70.f, DeckZ),
		FVector(-270.f, 70.f, DeckZ),
		FVector(150.f, 0.f, DeckZ),
	};
}

ARiptideBoat::ARiptideBoat()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Relevant out to a kilometre: a crew member left far astern in the sea still needs to see their boat (and the
	// wake simulation it carries), and dropping it would destroy and remake it on their machine.
	SetNetCullDistanceSquared(FMath::Square(100000.f));
	SetReplicatingMovement(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// The physics body is an unscaled box so pontoon and propeller offsets stay in real centimetres.
	HullBody = CreateDefaultSubobject<UBoxComponent>(TEXT("HullBody"));
	HullBody->SetBoxExtent(HullExtent);
	HullBody->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	// The crew walks on DeckCollision instead; the box would stand them on its lid, above the deck.
	HullBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	HullBody->SetSimulatePhysics(true);
	// Set the override directly: SetMassOverrideInKg recalculates mass, which can't run during CDO construction.
	HullBody->BodyInstance.SetMassOverride(HullMassKg, true);
	HullBody->SetLinearDamping(0.f);
	HullBody->SetAngularDamping(0.5f);
	// A floating hull never comes to rest: left to the physics engine's default it can fall asleep on flat water,
	// heeled over wherever a small knock left it, until something wakes it.
	HullBody->BodyInstance.SleepFamily = ESleepFamily::Custom;
	HullBody->BodyInstance.CustomSleepThresholdMultiplier = 0.f;
	RootComponent = HullBody;

	// Placeholder visuals, replaced by the boat model in ApplyModels.
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(HullBody);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->SetRelativeScale3D(HullExtent / 50.f);
	if (CubeMesh.Succeeded())
	{
		HullMesh->SetStaticMesh(CubeMesh.Object);
	}

	DeckCollision = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeckCollision"));
	DeckCollision->SetupAttachment(HullBody);
	DeckCollision->SetHiddenInGame(true);
	DeckCollision->SetVisibility(false);
	DeckCollision->SetCastShadow(false);
	// Not welded into the hull's physics body: it follows the hull each frame, so it can be the deck's exact
	// concave shape without changing how the hull floats or handles.
	DeckCollision->BodyInstance.bAutoWeld = false;
	DeckCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DeckCollision->SetCollisionObjectType(ECC_WorldDynamic);
	DeckCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	DeckCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	DeckCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DeckCollision->SetCanEverAffectNavigation(false);

	{
		// The cockpit's inner walls, measured off the model (riptide_boat_mesh.py's HullInside, innermost at deck
		// level): X along the boat, Y of the wall on the starboard side (port is the mirror). Forward of the last
		// point the sides close into the bow.
		const FVector2D WallLine[] = { { -338.f, 116.f }, { -120.f, 115.9f }, { -80.f, 114.7f }, { -40.f, 112.1f }, { 0.f, 108.1f },
			{ 40.f, 102.7f }, { 80.f, 95.9f }, { 120.f, 87.7f }, { 160.f, 78.2f }, { 200.f, 67.3f }, { 240.f, 54.9f }, { 280.f, 40.9f } };
		constexpr float Inboard = 1.5f;        // the box's face this far inside the model's wall, so it's met first
		constexpr float Thick = 30.f;
		constexpr float Overlap = 3.f;         // each box runs on past its ends, closing the joints
		const float BottomZ = DeckZ - 10.f;
		auto MakeWall = [this](const FString& Name, const FVector& Centre, const FVector& Extent, float Yaw)
		{
			UBoxComponent* Wall = CreateDefaultSubobject<UBoxComponent>(*Name);
			Wall->SetupAttachment(HullBody);
			Wall->SetRelativeLocationAndRotation(Centre, FRotator(0.f, Yaw, 0.f));
			Wall->SetBoxExtent(Extent);
			// Crew only, and not part of the hull's physics (it would change how it floats).
			Wall->BodyInstance.bAutoWeld = false;
			Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Wall->SetCollisionObjectType(ECC_WorldDynamic);
			Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
			Wall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			Wall->SetCanEverAffectNavigation(false);
			Wall->SetHiddenInGame(true);
			BulwarkWalls.Add(Wall);
		};
		for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(WallLine); ++i)
		{
			const FVector2D A = WallLine[i], B = WallLine[i + 1];
			for (const float Side : { 1.f, -1.f })
			{
				const FVector2D P(A.X, Side * (A.Y - Inboard)), Q(B.X, Side * (B.Y - Inboard));
				const FVector2D Along = (Q - P).GetSafeNormal();
				const FVector2D Out = FVector2D(-Along.Y, Along.X) * (FVector2D::DotProduct(FVector2D(-Along.Y, Along.X), FVector2D(0.f, Side)) > 0.f ? 1.f : -1.f);
				const FVector2D Mid = (P + Q) * 0.5f + Out * (Thick * 0.5f);
				// Up to the gunwale cap's height there (it rises toward the bow): the cap itself stays the top.
				const float TopZ = DeckZ + 55.f + 45.f * FMath::Pow(FMath::Clamp((B.X + 395.f) / 790.f, 0.f, 1.f), 2.2f);
				MakeWall(FString::Printf(TEXT("BulwarkWall%s%d"), Side > 0.f ? TEXT("S") : TEXT("P"), i),
					FVector(Mid.X, Mid.Y, (BottomZ + TopZ) * 0.5f), FVector((Q - P).Size() * 0.5f + Overlap, Thick * 0.5f, (TopZ - BottomZ) * 0.5f),
					FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)));
			}
		}
		// The stern bulkhead across the back of the cockpit, its face at X = -340 (BULKHEAD_X), deck to the stern box top.
		const float BulkheadTop = DeckZ + 55.f;
		MakeWall(TEXT("BulwarkWallStern"), FVector(-340.f + Inboard - Thick * 0.5f, 0.f, (BottomZ + BulkheadTop) * 0.5f),
			FVector(Thick * 0.5f, 118.f, (BulkheadTop - BottomZ) * 0.5f), 0.f);
	}

	MotorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorMesh"));
	MotorMesh->SetupAttachment(HullBody);
	MotorMeshStarboard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorMeshStarboard"));
	MotorMeshStarboard->SetupAttachment(HullBody);
	for (UStaticMeshComponent* Motor : { MotorMesh.Get(), MotorMeshStarboard.Get() })
	{
		const FVector Pivot = Motor == MotorMesh ? OutboardPivot : OutboardPivotStarboard;
		Motor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Motor->SetRelativeLocation(FVector(Pivot.X - 15.f, Pivot.Y, -10.f));
		Motor->SetRelativeScale3D(FVector(0.25f, 0.25f, 0.9f));
		if (CylinderMesh.Succeeded())
		{
			Motor->SetStaticMesh(CylinderMesh.Object);
		}
	}

	MotorBracket = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorBracket"));
	MotorBracket->SetupAttachment(HullBody);
	MotorBracket->SetRelativeLocation(OutboardPivot);
	MotorBracketStarboard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorBracketStarboard"));
	MotorBracketStarboard->SetupAttachment(HullBody);
	MotorBracketStarboard->SetRelativeLocation(OutboardPivotStarboard);
	MotorSwivel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorSwivel"));
	MotorSwivel->SetupAttachment(HullBody);
	MotorSwivel->SetRelativeLocation(OutboardPivot);
	MotorSwivelStarboard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MotorSwivelStarboard"));
	MotorSwivelStarboard->SetupAttachment(HullBody);
	MotorSwivelStarboard->SetRelativeLocation(OutboardPivotStarboard);
	ThrottleLeverPort = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThrottleLeverPort"));
	ThrottleLeverPort->SetupAttachment(HullBody);
	ThrottleLeverPort->SetRelativeLocation(ThrottlePivotPort);
	ThrottleLeverStarboard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThrottleLeverStarboard"));
	ThrottleLeverStarboard->SetupAttachment(HullBody);
	ThrottleLeverStarboard->SetRelativeLocation(ThrottlePivotStarboard);
	SearchlightHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SearchlightHead"));
	SearchlightHead->SetupAttachment(HullBody);
	SearchlightHead->SetRelativeLocation(SearchlightPivot);
	SearchlightHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SearchlightBeam = CreateDefaultSubobject<USpotLightComponent>(TEXT("SearchlightBeam"));
	SearchlightBeam->SetupAttachment(SearchlightHead);
	SearchlightBeam->SetRelativeLocation(FVector(13.f, 0.f, 0.f));
	SearchlightBeam->SetIntensityUnits(ELightUnits::Candelas);
	SearchlightBeam->SetIntensity(250000.f);
	SearchlightBeam->SetAttenuationRadius(25000.f);
	SearchlightBeam->SetInnerConeAngle(2.5f);
	SearchlightBeam->SetOuterConeAngle(6.f);
	SearchlightBeam->SetLightColor(FLinearColor(1.f, 0.96f, 0.88f));
	// No shadows: at a searchlight's grazing angle the sea shadows its own lit patch and the beam never shows.
	SearchlightBeam->SetCastShadows(false);
	SearchlightBeam->SetVolumetricScatteringIntensity(1.5f);
	SearchlightBeam->SetVisibility(false);
	SearchlightModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_Searchlight.SM_Searchlight")));
	LampOnMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Riptide/Materials/MI_Boat_LampOn.MI_Boat_LampOn")));

	auto MakeNavLight = [this](const TCHAR* Name, const FVector& Where, const FLinearColor& Colour, float Candelas, float Radius)
	{
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(HullBody);
		Light->SetRelativeLocation(Where);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Candelas);
		Light->SetAttenuationRadius(Radius);
		Light->SetLightColor(Colour);
		Light->SetCastShadows(false);
		return Light;
	};
	// Real navigation lights are for being seen, not for seeing by: dim. The masthead light shines all round from
	// above everything; the sidelights are screened, each lighting only its own side from dead ahead to just abaft
	// the beam (112.5 degrees), so none of their light falls on the deck.
	MastheadLight = MakeNavLight(TEXT("MastheadLight"), MastheadLightPoint, FLinearColor(1.f, 0.97f, 0.9f), 6.f, 900.f);
	auto MakeSidelight = [this](const TCHAR* Name, float Side, const FLinearColor& Colour)
	{
		USpotLightComponent* Light = CreateDefaultSubobject<USpotLightComponent>(Name);
		Light->SetupAttachment(HullBody);
		Light->SetRelativeLocationAndRotation(BowLightPoint + FVector(4.f, Side * 4.f, 0.f), FRotator(0.f, Side * 56.25f, 0.f));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(4.f);
		Light->SetAttenuationRadius(600.f);
		Light->SetInnerConeAngle(45.f);
		Light->SetOuterConeAngle(56.25f);
		Light->SetLightColor(Colour);
		// Shadowed, so the bow screens the water close under it, as the hull would: unshadowed, a red and a green
		// glow lit the sea right through the hull.
		Light->SetCastShadows(true);
		return Light;
	};
	BowLightPort = MakeSidelight(TEXT("BowLightPort"), -1.f, FLinearColor(1.f, 0.05f, 0.03f));
	BowLightStarboard = MakeSidelight(TEXT("BowLightStarboard"), 1.f, FLinearColor(0.05f, 1.f, 0.2f));

	auto MakeFlood = [this](const TCHAR* Name, float Y)
	{
		USpotLightComponent* Flood = CreateDefaultSubobject<USpotLightComponent>(Name);
		Flood->SetupAttachment(HullBody);
		// Under the canopy's aft edge, over the leaning post, lighting the cockpit and aft deck (not the helm's face).
		Flood->SetRelativeLocationAndRotation(FVector(-150.f, Y, DeckZ + 212.f), FRotator(-62.f, 180.f, 0.f));
		Flood->SetIntensityUnits(ELightUnits::Candelas);
		Flood->SetIntensity(60.f);
		Flood->SetAttenuationRadius(600.f);
		Flood->SetInnerConeAngle(22.f);
		Flood->SetOuterConeAngle(38.f);
		Flood->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f));
		Flood->SetCastShadows(false);
		Flood->SetVisibility(false);
		return Flood;
	};
	DeckFloodPort = MakeFlood(TEXT("DeckFloodPort"), -30.f);
	DeckFloodStarboard = MakeFlood(TEXT("DeckFloodStarboard"), 30.f);

	RadarArray = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RadarArray"));
	RadarArray->SetupAttachment(HullBody);
	RadarArray->SetRelativeLocation(RadarHub);
	CompassCard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CompassCard"));
	CompassCard->SetupAttachment(HullBody);
	CompassCard->SetRelativeLocation(CompassPivot);
	CompassCard->SetCastShadow(false);
	for (UStaticMeshComponent* Part : { MotorBracket.Get(), MotorBracketStarboard.Get(), MotorSwivel.Get(), MotorSwivelStarboard.Get(),
			ThrottleLeverPort.Get(), ThrottleLeverStarboard.Get(), RadarArray.Get(), CompassCard.Get() })
	{
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Dash instruments. Each is a live drawing (SRiptideGauge) on a widget component set into the dash, sized in cm
	// (a widget pixel is a centimetre at scale 1).
	auto MakeGauge = [this](const TCHAR* Name, const FVector& Where, FIntPoint Pixels, float WidthCm)
	{
		UWidgetComponent* Gauge = CreateDefaultSubobject<UWidgetComponent>(Name);
		Gauge->SetupAttachment(HullBody);
		Gauge->SetRelativeLocationAndRotation(Where, DashFacing);
		Gauge->SetRelativeScale3D(FVector(WidthCm / Pixels.X));
		Gauge->SetDrawSize(Pixels);
		Gauge->SetWidgetSpace(EWidgetSpace::World);
		Gauge->SetBlendMode(EWidgetBlendMode::Masked);
		Gauge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Gauge->SetCastShadow(false);
		return Gauge;
	};
	GaugeTach = MakeGauge(TEXT("GaugeTach"), OnDash(0.5f, -28.5f, 1.3f), FIntPoint(256, 256), 13.6f);
	GaugeSpeed = MakeGauge(TEXT("GaugeSpeed"), OnDash(0.5f, 28.5f, 1.3f), FIntPoint(256, 256), 13.6f);
	GaugeDisplay = MakeGauge(TEXT("GaugeDisplay"), OnDash(0.51f, 0.f, 1.1f), FIntPoint(480, 320), 33.f);

	// The boat model, generated by Content/Python/riptide_boat_mesh.py and imported when the editor opens. The
	// box and cylinder above stand in until it exists (on the very first launch).
	HullModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_PatrolSkiff.SM_PatrolSkiff")));
	OutboardModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_Outboard.SM_Outboard")));
	BracketModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_OutboardBracket.SM_OutboardBracket")));
	SwivelModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_OutboardSwivel.SM_OutboardSwivel")));
	LeverModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_ThrottleLever.SM_ThrottleLever")));
	RadarModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_RadarArray.SM_RadarArray")));
	CompassModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_CompassCard.SM_CompassCard")));

	// Propellers: at each outboard's prop, well below the waterline when the boat is level.
	Propeller = CreateDefaultSubobject<USceneComponent>(TEXT("Propeller"));
	Propeller->SetupAttachment(HullBody);
	Propeller->SetRelativeLocation(OutboardPivot + PropInOutboard);
	PropellerStarboard = CreateDefaultSubobject<USceneComponent>(TEXT("PropellerStarboard"));
	PropellerStarboard->SetupAttachment(HullBody);
	PropellerStarboard->SetRelativeLocation(OutboardPivotStarboard + PropInOutboard);

	// The props, spinning on each motor's shaft, and the steering wheel.
	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	PropMesh->SetupAttachment(MotorMesh);
	PropMeshStarboard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMeshStarboard"));
	PropMeshStarboard->SetupAttachment(MotorMeshStarboard);
	WheelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WheelMesh"));
	WheelMesh->SetupAttachment(HullBody);
	WheelMesh->SetRelativeLocationAndRotation(WheelCentre, FRotator(WheelTiltDeg, 180.f, 0.f));
	// The radio's hand mic on its clip, and its cord.
	MicMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MicMesh"));
	MicMesh->SetupAttachment(HullBody);
	MicMesh->SetRelativeLocation(MicHook);
	MicCord = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MicCord"));
	MicCord->SetupAttachment(HullBody);
	MicCord->bUseAsyncCooking = false;
	for (UPrimitiveComponent* Part : { (UPrimitiveComponent*)PropMesh.Get(), (UPrimitiveComponent*)PropMeshStarboard.Get(),
			(UPrimitiveComponent*)WheelMesh.Get(), (UPrimitiveComponent*)MicMesh.Get(), (UPrimitiveComponent*)MicCord.Get() })
	{
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	PropellerModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_Propeller.SM_Propeller")));
	WheelModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_HelmWheel.SM_HelmWheel")));
	MicModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_RadioMic.SM_RadioMic")));

	// Standing at the helm, an arm's length behind the wheel, eyes about 1.7 m above the deck.
	HelmCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HelmCamera"));
	HelmCamera->SetupAttachment(HullBody);
	HelmCamera->SetRelativeLocation(FVector(-110.f, 0.f, DeckZ + 170.f));
	HelmCamera->bUsePawnControlRotation = false;

	// At the waterline, amidships.
	WakeSource = CreateDefaultSubobject<USceneComponent>(TEXT("WakeSource"));
	WakeSource->SetupAttachment(HullBody);
	WakeSource->SetRelativeLocation(FVector(0.f, 0.f, WaterlineZ));

	// Foam is laid in world space; the component ignores the hull's transform.
	WakeFoam = CreateDefaultSubobject<URiptideWakeFoamComponent>(TEXT("WakeFoam"));
	WakeFoam->SetupAttachment(HullBody);
	WakeFoamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Riptide/Materials/M_WakeFoam.M_WakeFoam")));
	WakeForceMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Riptide/Materials/MI_WakeForce.MI_WakeForce")));
	Lockers = CreateDefaultSubobject<URiptideStorageComponent>(TEXT("Lockers"));

	Spray = CreateDefaultSubobject<URiptideSprayComponent>(TEXT("Spray"));
	Spray->SetupAttachment(HullBody);
	SprayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Riptide/Materials/M_Spray.M_Spray")));

	// The engine sounds from the motor; the wash from the hull at the waterline.
	EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(MotorMesh);
	EngineAudio->bAutoActivate = false;
	EngineHighAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineHighAudio"));
	EngineHighAudio->SetupAttachment(MotorMesh);
	EngineHighAudio->bAutoActivate = false;
	EngineAudioStarboard = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudioStarboard"));
	EngineAudioStarboard->SetupAttachment(MotorMeshStarboard);
	EngineAudioStarboard->bAutoActivate = false;
	EngineHighAudioStarboard = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineHighAudioStarboard"));
	EngineHighAudioStarboard->SetupAttachment(MotorMeshStarboard);
	EngineHighAudioStarboard->bAutoActivate = false;
	WashAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WashAudio"));
	WashAudio->SetupAttachment(HullBody);
	WashAudio->SetRelativeLocation(FVector(0.f, 0.f, WaterlineZ));
	WashAudio->bAutoActivate = false;

	// The sounds are imported by Content/Python/init_unreal.py when the editor opens, so they're referenced
	// by path and loaded at BeginPlay rather than looked up here.
	EngineSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Riptide/Audio/S_Engine_Low.S_Engine_Low")));
	EngineHighSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Riptide/Audio/S_Engine_High.S_Engine_High")));
	WashSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Riptide/Audio/S_Hull_Wash.S_Hull_Wash")));
	for (const TCHAR* Slap : { TEXT("04"), TEXT("06"), TEXT("08"), TEXT("13"), TEXT("15") })
	{
		HullSlapSounds.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(
			FString::Printf(TEXT("/Game/Riptide/Audio/S_Hull_Slap_%s.S_Hull_Slap_%s"), Slap, Slap))));
	}

	// Weight sits low and aft (engine, fuel, crew on the floor), which keeps the hull from rolling over.
	HullBody->BodyInstance.COMNudge = FVector(-30.f, 0.f, -30.f);
	// How hard the hull is to set rocking and spinning, set to a real boat's: about 3,300 kg m^2 to roll (its weight
	// out in the shell and up in the T-top, plus the water that rolls with it) and 27,000 to pitch or to turn. The
	// physics engine works the box's figures out from how its mass is spread along each axis (one axis's inertia
	// comes from the spread along the other two), and the scale stretches those spreads; left alone it rocks like a
	// solid block, quicker than a real hull. These give the figures above (the handling test logs them): with the
	// pontoons, about a 2 second roll and a 1.5 second pitch.
	HullBody->BodyInstance.InertiaTensorScale = FVector(1.234f, 0.957f, 3.55f);

	// Pontoons stand in for the hull's buoyancy: down both sides, plus one on the centreline aft. BeginPlay sizes their
	// lift so they float PontoonRestDepth deep. How far out they sit sets how stiffly the hull resists rolling and
	// pitching, so they sit where the hull's own waterline does its work: half a metre either side of the keel (where
	// a V-bottom's buoyancy is centred, well inside its full beam), and closer in toward the bow, where the hull
	// narrows and lifts out of the water. Spread to the hull's edges they rolled it back upright in under a second,
	// like a raft. (A real boat this size has a metacentric height of about a metre: these give that.)
	Buoyancy = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("Buoyancy"));
	// The engine's buoyancy shares out the weight between the pontoons about the centre of mass itself; letting it
	// centre their positions on the centre of mass first takes the COM nudge off twice, and the hull floats out of trim.
	Buoyancy->BuoyancyData.bCenterPontoonsOnCOM = false;
	const float PontoonZ = WaterlineZ - PontoonRestDepth + PontoonRadius;
	const FVector PontoonOffsets[] = {
		FVector(220.f, 30.f, PontoonZ),
		FVector(220.f, -30.f, PontoonZ),
		FVector(70.f, 50.f, PontoonZ),
		FVector(70.f, -50.f, PontoonZ),
		FVector(-110.f, 50.f, PontoonZ),
		FVector(-110.f, -50.f, PontoonZ),
		FVector(-260.f, 50.f, PontoonZ),
		FVector(-260.f, -50.f, PontoonZ),
		FVector(-330.f, 0.f, PontoonZ),
	};
	for (const FVector& Offset : PontoonOffsets)
	{
		FSphericalPontoon Pontoon;
		Pontoon.RelativeLocation = Offset;
		Pontoon.Radius = PontoonRadius;
		Buoyancy->BuoyancyData.Pontoons.Add(Pontoon);
	}
}

void ARiptideBoat::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyModels();
}

void ARiptideBoat::ApplyModels()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && GetWorld() && GetWorld()->IsGameWorld() && HullModel.IsNull() == false && !HullModel.LoadSynchronous())
	{
		UE_LOG(LogRiptideBoat, Error, TEXT("The boat model %s is missing: it's generated by Content/Python/init_unreal.py when the editor opens"),
			*HullModel.ToString());
	}
	if (UStaticMesh* Hull = HullModel.LoadSynchronous())
	{
		HullMesh->SetStaticMesh(Hull);
		HullMesh->SetRelativeLocation(FVector::ZeroVector);
		HullMesh->SetRelativeScale3D(FVector::OneVector);
		// The crew walks on the model's own triangles (it's imported with complex collision used as simple).
		DeckCollision->SetStaticMesh(Hull);
	}
	if (UStaticMesh* Outboard = OutboardModel.LoadSynchronous())
	{
		MotorMesh->SetStaticMesh(Outboard);
		MotorMesh->SetRelativeLocation(OutboardPivot);
		MotorMesh->SetRelativeScale3D(FVector::OneVector);
		MotorMeshStarboard->SetStaticMesh(Outboard);
		MotorMeshStarboard->SetRelativeLocation(OutboardPivotStarboard);
		MotorMeshStarboard->SetRelativeScale3D(FVector::OneVector);
	}
	if (UStaticMesh* Bracket = BracketModel.LoadSynchronous())
	{
		MotorBracket->SetStaticMesh(Bracket);
		MotorBracketStarboard->SetStaticMesh(Bracket);
	}
	if (UStaticMesh* Swivel = SwivelModel.LoadSynchronous())
	{
		MotorSwivel->SetStaticMesh(Swivel);
		MotorSwivelStarboard->SetStaticMesh(Swivel);
	}
	if (UStaticMesh* Head = SearchlightModel.LoadSynchronous())
	{
		SearchlightHead->SetStaticMesh(Head);
	}
	if (UStaticMesh* Radar = RadarModel.LoadSynchronous())
	{
		RadarArray->SetStaticMesh(Radar);
	}
	if (UStaticMesh* Compass = CompassModel.LoadSynchronous())
	{
		CompassCard->SetStaticMesh(Compass);
	}
	if (UStaticMesh* Lever = LeverModel.LoadSynchronous())
	{
		ThrottleLeverPort->SetStaticMesh(Lever);
		ThrottleLeverStarboard->SetStaticMesh(Lever);
	}
	if (UStaticMesh* Prop = PropellerModel.LoadSynchronous())
	{
		PropMesh->SetStaticMesh(Prop);
		PropMeshStarboard->SetStaticMesh(Prop);
		PropMesh->SetRelativeLocation(PropInOutboard);
		PropMeshStarboard->SetRelativeLocation(PropInOutboard);
	}
	if (UStaticMesh* Wheel = WheelModel.LoadSynchronous())
	{
		WheelMesh->SetStaticMesh(Wheel);
	}
	if (UStaticMesh* Mic = MicModel.LoadSynchronous())
	{
		MicMesh->SetStaticMesh(Mic);
	}
}

void ARiptideBoat::BeginPlay()
{
	ApplyModels();

	// Size buoyancy to the hull's mass before the buoyancy component starts (it begins play inside Super).
	// The engine spreads one pontoon's worth of lift across all pontoons (their coefficients sum to 1),
	// so lift = submerged volume of one pontoon * BuoyancyCoefficient. Pick the coefficient that holds the
	// boat up with the pontoons PontoonRestDepth under (a spherical cap), which puts the waterline at WaterlineZ.
	{
		const float R = PontoonRadius;
		const float D = PontoonRestDepth;
		const float RestVolumeCm3 = (UE_PI / 3.f) * D * D * (3.f * R - D);
		const float WeightUnreal = HullMassKg * FMath::Abs(GetWorld()->GetGravityZ());
		Buoyancy->BuoyancyData.BuoyancyCoefficient = WeightUnreal / RestVolumeCm3;
		// The engine clamps each pontoon's force; leave room for a fully buried pontoon's full reserve.
		Buoyancy->BuoyancyData.MaxBuoyantForce = WeightUnreal * 20.f;
	}

	Super::BeginPlay();

	HullBody->SetMassOverrideInKg(NAME_None, HullMassKg, true);
	// Placed on a sloping wave, the hull settles into it in its first moments: not a slam, so no slap or spray (the
	// main menu's boat used to splash down as it loaded).
	SlapCooldownLeft = SettleSeconds;
	if (HasAuthority())
	{
		FuelLiters = FuelCapacityLiters * StartingFuelFraction;
		SetUpLockers();
	}

	// The wake foam, sounds and instruments are cosmetic, so every machine runs its own for each boat.
	if (FApp::CanEverRender())
	{
		const TPair<UWidgetComponent*, ERiptideGaugeKind> Gauges[] = {
			{ GaugeTach, ERiptideGaugeKind::Tachometer }, { GaugeSpeed, ERiptideGaugeKind::Speedometer }, { GaugeDisplay, ERiptideGaugeKind::Display } };
		for (const auto& Gauge : Gauges)
		{
			Gauge.Key->SetSlateWidget(SNew(SRiptideGauge).Boat(this).Kind(Gauge.Value));
		}
	}
	// The navigation lights' lenses glow while they're on: dynamic copies of their materials to dim when off.
	for (const TCHAR* Slot : { TEXT("NavRed"), TEXT("NavGreen"), TEXT("NavWhite") })
	{
		const int32 Index = HullMesh->GetMaterialIndex(FName(Slot));
		if (Index != INDEX_NONE)
		{
			NavLenses.Add(HullMesh->CreateDynamicMaterialInstance(Index));
		}
	}
	const int32 LampIndex = SearchlightHead->GetMaterialIndex(TEXT("Lamp"));
	LampOffMaterial = LampIndex != INDEX_NONE ? SearchlightHead->GetMaterial(LampIndex) : nullptr;
	LampOnMaterial.LoadSynchronous();
	ApplyLights();

	RegisterWithWakeSimulation();
	StartSounds();
	StartWakeFoam();
}

void ARiptideBoat::StartWakeFoam()
{
	if (!FApp::CanEverRender())
	{
		return;
	}
	WakeFoam->SetMaterial(0, WakeFoamMaterial.LoadSynchronous());
	if (AWaterBodyOcean* Ocean = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass())))
	{
		WakeFoam->SetWaterBody(Ocean->GetWaterBodyComponent());
	}

	// Churned water from the prop: a wide band that spreads and lingers.
	URiptideWakeFoamComponent::FTrailStyle Churn;
	Churn.StartHalfWidth = HullExtent.Y * 0.8f;
	Churn.GrowthPerSecond = 65.f;
	Churn.LifeSeconds = 9.f;
	Churn.Opacity = 0.7f;
	Churn.WobbleAtBirth = 5.f;
	Churn.WobbleWhenOld = 40.f;
	Churn.WobbleWavelength = 1200.f;
	Churn.WidthVariation = 0.3f;
	Churn.Patchiness = 0.35f;
	SternFoamTrail = WakeFoam->AddTrail(Churn);

	Spray->SetMaterial(0, SprayMaterial.LoadSynchronous());
	// Spray never shows inside the boat: water that ends up inside the hull (a turn sliding the boat into its own
	// spray, or a burst falling into the cockpit) is gone.
	TWeakObjectPtr<ARiptideBoat> WeakThis(this);
	Spray->ClearanceFromSolid = [WeakThis](const FVector& World) { return WeakThis.IsValid() ? WeakThis->ClearanceFromHull(World) : 1e6f; };

	// The mic's cord, in the hand mic's own black.
	const int32 Trim = HullMesh->GetMaterialIndex(TEXT("Trim"));
	MicCord->SetMaterial(0, Trim != INDEX_NONE ? HullMesh->GetMaterial(Trim) : nullptr);
	MicCord->SetCastShadow(false);
	ApplyMicHolder();
}

void ARiptideBoat::UpdateWakeFoam(float DeltaSeconds)
{
	if (SternFoamTrail == INDEX_NONE)
	{
		return;
	}

	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector Right = FVector::VectorPlaneProject(Xf.GetUnitAxis(EAxis::Y), FVector::UpVector).GetSafeNormal();
	const bool bInWater = Buoyancy && Buoyancy->IsInWaterBody();
	const float Speed01 = bInWater ? FMath::Clamp(GetSpeedKnots() / FoamFullSpeedKnots, 0.f, 1.f) : 0.f;

	// Prop churn: from moving through the water, and from the prop turning even at low speed. It builds quickly
	// but dies away slowly, so the prop lifting clear on a swell for a moment thins the trail instead of breaking it.
	const float ChurnTarget = IsPropellerSubmerged() ? FMath::Max(Speed01, 0.35f * FMath::Abs(EngineOutput)) : 0.f;
	ChurnLevel = FMath::FInterpTo(ChurnLevel, ChurnTarget, DeltaSeconds, ChurnTarget > ChurnLevel ? 8.f : 1.f);
	WakeFoam->UpdateTrail(SternFoamTrail, DeltaSeconds, Xf.TransformPosition(FVector(-HullExtent.X, 0.f, WaterlineZ)), Right, ChurnLevel);

	// The wake's V (the bow waves and their foam) comes from the wake simulation, so only the prop's churn is
	// laid here.
	WakeFoam->RebuildMesh();
}

void ARiptideBoat::SetUpLockers()
{
	if (Lockers->Num() > 0)
	{
		return;
	}
	// Each opens from its lid (riptide_boat_mesh.py's _fittings and _stern_box). Stocked the way a crew running this
	// boat keeps it: whoever takes the boat takes what's aboard.
	const int32 Forward = Lockers->AddStorage(NSLOCTEXT("Riptide", "ForwardLocker", "Forward locker"), 8, 5, FVector(102.f, 0.f, DeckZ));
	const int32 Bow = Lockers->AddStorage(NSLOCTEXT("Riptide", "BowLocker", "Bow locker"), 6, 4, FVector(205.f, 0.f, DeckZ + 4.f));
	const int32 SternPort = Lockers->AddStorage(NSLOCTEXT("Riptide", "SternPort", "Stern locker (port)"), 5, 3, FVector(-367.f, -97.f, DeckZ + 55.f));
	const int32 SternStarboard = Lockers->AddStorage(NSLOCTEXT("Riptide", "SternStarboard", "Stern locker (starboard)"), 5, 3, FVector(-367.f, 97.f, DeckZ + 55.f));
	const int32 Anchor = Lockers->AddStorage(NSLOCTEXT("Riptide", "AnchorLocker", "Anchor locker"), 3, 3, FVector(372.f, 0.f, DeckZ + 95.f));
	auto Stock = [this](int32 Locker, const TCHAR* Id, int32 Count) { Lockers->GetStorage(Locker)->Grid.Add(FName(Id), Count); };
	Stock(Forward, TEXT("first_aid_kit"), 1);
	Stock(Forward, TEXT("flare_gun"), 1);
	Stock(Forward, TEXT("flare"), 6);
	Stock(Forward, TEXT("binoculars"), 1);
	Stock(Forward, TEXT("sea_chart"), 1);
	Stock(Forward, TEXT("bandage"), 8);
	Stock(Forward, TEXT("handheld_radio"), 1);
	Stock(Bow, TEXT("ammo_556"), 120);
	Stock(Bow, TEXT("ration_pack"), 4);
	Stock(Bow, TEXT("canteen_clean"), 2);
	Stock(Bow, TEXT("m1911"), 1);
	Stock(Bow, TEXT("ammo_9mm"), 30);
	Stock(SternPort, TEXT("fuel_drum"), 1);
	Stock(SternPort, TEXT("rope"), 10);
	Stock(SternStarboard, TEXT("tool_kit"), 1);
	Stock(SternStarboard, TEXT("cleaning_kit"), 1);
	Stock(Anchor, TEXT("rope"), 20);
}

namespace
{
	// riptide_boat_mesh.py's station(): full beam aft, narrowing to the stem; a deep V whose keel and chines sweep
	// up into the bow. Chine half-width and height, keel height, sheer half-width and height at T (0 transom, 1 stem).
	struct FHullStation
	{
		float ChineY, ChineZ, KeelZ, SheerY, SheerZ;
	};

	FHullStation ComputeHullStation(float T)
	{
		FHullStation S;
		S.SheerY = T < 0.35f ? HullExtent.Y : HullExtent.Y * FMath::Pow(FMath::Max(0.f, FMath::Cos((T - 0.35f) / 0.65f * UE_HALF_PI)), 0.75f);
		S.SheerZ = DeckZ + 55.f + 45.f * FMath::Pow(T, 2.2f);
		S.KeelZ = -42.f + 50.f * FMath::Pow(FMath::SmoothStep(0.55f, 1.f, T), 1.7f);
		S.ChineY = S.SheerY * (0.88f - 0.1f * FMath::SmoothStep(0.6f, 1.f, T));
		S.ChineZ = FMath::Min(-24.f + 34.f * FMath::SmoothStep(0.55f, 1.f, T), S.SheerZ - 6.f);
		// The keel runs up into the stem to meet the chines there, always below them, the V sharpening to the tip.
		S.KeelZ = FMath::Min(S.KeelZ, S.ChineZ - 3.f - 15.f * (1.f - FMath::SmoothStep(0.93f, 1.f, T)));
		return S;
	}

	// The same, tabulated every 3 cm or so along the hull: the spray asks for it thousands of times a frame.
	constexpr int32 HullTableSize = 257;
	const FHullStation* HullTable()
	{
		static const TArray<FHullStation> Table = []()
		{
			TArray<FHullStation> T;
			T.SetNum(HullTableSize);
			for (int32 i = 0; i < HullTableSize; ++i)
			{
				T[i] = ComputeHullStation(float(i) / (HullTableSize - 1));
			}
			return T;
		}();
		return Table.GetData();
	}
}

void ARiptideBoat::HullSection(float X, float& OutChineY, float& OutChineZ, float& OutKeelZ, float* OutSheerY, float* OutSheerZ)
{
	const float U = FMath::Clamp((X + HullExtent.X) / (2.f * HullExtent.X), 0.f, 1.f) * (HullTableSize - 1);
	const int32 I = FMath::Min(FMath::FloorToInt(U), HullTableSize - 2);
	const float F = U - I;
	const FHullStation& A = HullTable()[I];
	const FHullStation& B = HullTable()[I + 1];
	OutChineY = FMath::Lerp(A.ChineY, B.ChineY, F);
	OutChineZ = FMath::Lerp(A.ChineZ, B.ChineZ, F);
	OutKeelZ = FMath::Lerp(A.KeelZ, B.KeelZ, F);
	if (OutSheerY)
	{
		*OutSheerY = FMath::Lerp(A.SheerY, B.SheerY, F);
	}
	if (OutSheerZ)
	{
		*OutSheerZ = FMath::Lerp(A.SheerZ, B.SheerZ, F);
	}
}

float ARiptideBoat::HullHalfWidthAt(float X, float Z)
{
	// The bottom's V from the keel out to the chine, then the topsides flaring out to the sheer.
	float ChineY, ChineZ, KeelZ, SheerY, SheerZ;
	HullSection(X, ChineY, ChineZ, KeelZ, &SheerY, &SheerZ);
	if (Z <= KeelZ)
	{
		return 0.f;
	}
	if (Z <= ChineZ)
	{
		return ChineY * FMath::Pow(FMath::Clamp((Z - KeelZ) / FMath::Max(ChineZ - KeelZ, 1.f), 0.f, 1.f), 1.f / 0.9f);
	}
	// As the model draws the topsides: a small shelf at the chine, then out through 99% of the sheer's half-width
	// at mid-height, to the sheer.
	const float Shelf = ChineY + (SheerY - ChineY) * 0.35f;
	const float Mid = 0.5f * (ChineZ + SheerZ);
	if (Z <= ChineZ + 1.f)
	{
		return FMath::Lerp(ChineY, Shelf, FMath::Clamp(Z - ChineZ, 0.f, 1.f));
	}
	if (Z <= Mid)
	{
		return FMath::Lerp(Shelf, 0.99f * SheerY, (Z - ChineZ - 1.f) / FMath::Max(Mid - ChineZ - 1.f, 1.f));
	}
	return FMath::Lerp(0.99f * SheerY, SheerY, FMath::Clamp((Z - Mid) / FMath::Max(SheerZ - Mid, 1.f), 0.f, 1.f));
}

float ARiptideBoat::HullSideSlope(float X, float Z)
{
	return FMath::Max(0.f, (HullHalfWidthAt(X - 5.f, Z) - HullHalfWidthAt(X + 5.f, Z)) / 10.f);
}

float ARiptideBoat::ClearanceFromHull(const FVector& World) const
{
	const FVector Local = HullBody->GetComponentTransform().InverseTransformPosition(World);
	const float X = FMath::Clamp(Local.X, -HullExtent.X, HullExtent.X);
	float ChineY, ChineZ, KeelZ, SheerY, SheerZ;
	HullSection(X, ChineY, ChineZ, KeelZ, &SheerY, &SheerZ);
	const float Z = FMath::Clamp(Local.Z, KeelZ, SheerZ);
	// Off the side (square to it, where it angles in toward the bow), past the ends, above the gunwale (and the
	// cockpit inside it), below the keel: whichever is furthest out. All negative means inside.
	const float Width = HullHalfWidthAt(X, Z) + (Z > SheerZ - 20.f ? 9.f : 0.f);
	const float Slope = HullSideSlope(X, Z);
	const float OffSide = (FMath::Abs(Local.Y) - Width) / FMath::Sqrt(1.f + Slope * Slope);
	const float PastEnd = FMath::Abs(Local.X) - HullExtent.X;
	const float Above = Local.Z - (SheerZ + 4.f);
	const float Below = KeelZ - Local.Z;
	return FMath::Max(FMath::Max(OffSide, PastEnd), FMath::Max(Above, Below));
}

bool ARiptideBoat::IsInsideHull(const FVector& World) const
{
	const FVector Local = HullBody->GetComponentTransform().InverseTransformPosition(World);
	if (FMath::Abs(Local.X) > HullExtent.X)
	{
		return false;
	}
	float ChineY, ChineZ, KeelZ, SheerY, SheerZ;
	HullSection(Local.X, ChineY, ChineZ, KeelZ, &SheerY, &SheerZ);
	if (Local.Z < KeelZ || Local.Z > SheerZ + 4.f)
	{
		return false;
	}
	// The fender collar stands 9 cm out round the top.
	const float Collar = Local.Z > SheerZ - 16.f ? 9.f : 0.f;
	return FMath::Abs(Local.Y) < HullHalfWidthAt(Local.X, FMath::Min(Local.Z, SheerZ)) + Collar;
}

FVector ARiptideBoat::SprayOriginAt(float X, float LocalSeaZ, float Side) const
{
	float ChineY, ChineZ, KeelZ, SheerY, SheerZ;
	HullSection(X, ChineY, ChineZ, KeelZ, &SheerY, &SheerZ);
	// At the sea's height on the hull there (on the V of the bottom, or up the topsides if the sea is over the
	// chine), just outside the skin: never inside it, where the topsides flare out above the chine.
	const float Z = FMath::Clamp(LocalSeaZ, KeelZ + 1.f, SheerZ - 16.f);
	const float Slope = HullSideSlope(X, Z);
	const float Y = HullHalfWidthAt(X, Z) + 7.f * FMath::Sqrt(1.f + Slope * Slope) + (Z > SheerZ - 20.f ? 9.f : 0.f);
	return HullBody->GetComponentTransform().TransformPosition(FVector(X, Side * Y, Z));
}

void ARiptideBoat::SampleChines(FChineSample (&Out)[2][ChineSamples]) const
{
	const FTransform& Xf = HullBody->GetComponentTransform();
	for (int32 i = 0; i < ChineSamples; ++i)
	{
		const float X = FMath::Lerp(HullExtent.X - 10.f, -HullExtent.X + 10.f, i / float(ChineSamples - 1));
		float ChineY, ChineZ, KeelZ;
		HullSection(X, ChineY, ChineZ, KeelZ);
		const FVector Keel = Xf.TransformPosition(FVector(X, 0.f, KeelZ));
		for (int32 S = 0; S < 2; ++S)
		{
			FChineSample& Sample = Out[S][i];
			Sample.X = X;
			Sample.Keel = Keel;
			Sample.Chine = Xf.TransformPosition(FVector(X, (S == 0 ? -1.f : 1.f) * ChineY, ChineZ));
			Sample.SeaZ = GetSeaSurfaceZ(Sample.Chine);
			Sample.KeelDepth = Sample.SeaZ - Keel.Z;
			Sample.ChineDepth = Sample.SeaZ - Sample.Chine.Z;
			Sample.LocalSeaZ = Xf.InverseTransformPosition(FVector(Sample.Chine.X, Sample.Chine.Y, Sample.SeaZ)).Z;
		}
	}
}

void ARiptideBoat::SprayAtBow(float Strength)
{
	if (!Spray || !FApp::CanEverRender())
	{
		return;
	}
	// The bow slamming down into a wave. The water it lands on has nowhere to go but out from under the hull: it
	// bursts out sideways from both sides of the forward bottom, wherever the hull meets the sea, in a sheet that
	// fans out low and wide and breaks into droplets, with mist hanging where it was. It isn't carried along with
	// the boat: it's the sea's water, shoved aside, so the boat runs on past it. Bigger for a harder hit.
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector Right = Xf.GetUnitAxis(EAxis::Y);
	const float Speed01 = FMath::Clamp(GetSpeedKnots() / SprayFullKnots, 0.f, 1.2f);
	const float Power = FMath::Clamp(Strength * (0.35f + 0.75f * Speed01), 0.f, 1.2f);
	FChineSample Samples[2][ChineSamples];
	SampleChines(Samples);
	for (int32 S = 0; S < 2; ++S)
	{
		const float Side = S == 0 ? -1.f : 1.f;
		for (int32 i = 0; i < ChineSamples; ++i)
		{
			const FChineSample& At = Samples[S][i];
			// The forward half of the bottom, where it's in the sea or just about to be.
			if (At.X < -60.f || At.KeelDepth <= 0.f || At.ChineDepth < -25.f)
			{
				continue;
			}
			const FVector Origin = SprayOriginAt(At.X, At.LocalSeaZ, Side);
			const float Fwd01 = FMath::Clamp((At.X + 60.f) / (HullExtent.X + 60.f), 0.f, 1.f);
			const float Immersed = FMath::Clamp((At.ChineDepth + 25.f) / 40.f, 0.3f, 1.f);
			// Shoved out square to the hull's side there. Where the side angles in toward the stem it meets the
			// water head on and pushes it forward and out at the speed it's coming at it; along the straight sides aft
			// it only pushes it out. Plus the splash of the landing itself, out and up (higher forward, where the
			// flare is steeper). Relative to the boat it always leaves outward, never back into the hull.
			const float SideAngle = FMath::Atan(HullSideSlope(At.X, At.LocalSeaZ));
			const FVector Out = Xf.GetUnitAxis(EAxis::X) * FMath::Sin(SideAngle) + Right * Side * FMath::Cos(SideAngle);
			const float Ahead = FMath::Max(0.f, FVector::DotProduct(GetDeckPointVelocity(Origin), Xf.GetUnitAxis(EAxis::X)));
			const FVector Throw = Out * (Ahead * FMath::Sin(SideAngle) + FMath::Lerp(300.f, 800.f, Power) * Immersed)
				+ FVector::UpVector * FMath::Lerp(180.f, 560.f, Power) * FMath::Lerp(0.6f, 1.f, Fwd01) * Immersed;
			Spray->ThrowSpray(Origin, Throw, FMath::Lerp(100.f, 240.f, Power), FMath::RoundToInt(FMath::Lerp(14.f, 45.f, Power * Immersed)),
				4.5f, FMath::Lerp(13.f, 20.f, Power), FMath::Lerp(0.9f, 1.5f, Power), At.SeaZ, 0.8f, 0.06f, Out);
			if (i % 3 == 0)
			{
				Spray->ThrowSpray(Origin + FVector::UpVector * 20.f, Throw * 0.35f, 80.f, 1, 80.f, FMath::Lerp(200.f, 320.f, Power),
					1.8f, At.SeaZ, FMath::Lerp(0.06f, 0.14f, Power), 0.f);
			}
		}
	}
}

void ARiptideBoat::UpdateSpray(float DeltaSeconds)
{
	if (!Spray || !FApp::CanEverRender() || !Buoyancy || !Buoyancy->IsInWaterBody())
	{
		bHaveChineDepths = false;
		return;
	}
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector Fwd = Xf.GetUnitAxis(EAxis::X);
	const FVector Right = Xf.GetUnitAxis(EAxis::Y);
	const FVector HullVelocity = HullBody->GetPhysicsLinearVelocity();
	const float ForwardKnots = FVector::DotProduct(HullVelocity, Fwd) * CmPerSecToKnots;
	const float Speed01 = FMath::Clamp((ForwardKnots - SprayStartKnots) / (SprayFullKnots - SprayStartKnots), 0.f, 1.2f);
	if (Speed01 <= 0.f)
	{
		BowSprayOwed[0] = BowSprayOwed[1] = 0.f;
		SternSprayOwed = 0.f;
		bHaveChineDepths = false;
		return;
	}

	// Spray off a planing hull comes from its spray root: the line where the sea first meets the bottom. The water
	// there is shoved out from under the V, runs up to the chine and leaves it as a thin sheet. Relative to the boat
	// it leaves at about the speed the water is going past, angled out 15-30 degrees from straight aft, and only a
	// little up, because the reverse chine is there to throw it flat. So on the sea it flies out sideways at about
	// a third of the boat's speed, hardly carried forward at all, and lands 2-4 m out while the boat runs on past
	// it: a low wing of spray streaming off each side, starting where the hull meets the water. That root moves:
	// forward when the bow drops into a trough, aft (and the spray stops) when the bow flies off a crest. Where the
	// hull is driving down into the sea the water is thrown harder and higher.
	FChineSample Samples[2][ChineSamples];
	SampleChines(Samples);
	float Closing[2][ChineSamples];
	for (int32 S = 0; S < 2; ++S)
	{
		for (int32 i = 0; i < ChineSamples; ++i)
		{
			Closing[S][i] = bHaveChineDepths ? (Samples[S][i].ChineDepth - PrevChineDepth[S][i]) / FMath::Max(DeltaSeconds, 1e-3f) : 0.f;
			PrevChineDepth[S][i] = Samples[S][i].ChineDepth;
		}
	}
	bHaveChineDepths = true;

	for (int32 S = 0; S < 2; ++S)
	{
		const float Side = S == 0 ? -1.f : 1.f;
		int32 Root = INDEX_NONE;
		for (int32 i = 0; i < ChineSamples; ++i)
		{
			if (Samples[S][i].KeelDepth > 0.f)
			{
				Root = i;
				break;
			}
		}
		if (Root == INDEX_NONE)
		{
			BowSprayOwed[S] = 0.f;     // this side is out of the water
			continue;
		}
		// How much water it's pushing aside: how deep the bottom is a metre or so aft of the root, and how hard that
		// part of the hull is coming down onto the sea.
		const int32 Loaded = FMath::Min(Root + 3, ChineSamples - 1);
		const float Load = FMath::Clamp(Samples[S][Loaded].KeelDepth / 30.f, 0.4f, 1.6f);
		float Plunge = 0.f;
		for (int32 i = Root; i <= Loaded; ++i)
		{
			Plunge = FMath::Max(Plunge, FMath::Clamp(Closing[S][i] / 250.f, 0.f, 1.5f));
		}
		BowSprayOwed[S] += BowSprayRate * FMath::Pow(Speed01, 1.5f) * Load * (1.f + 1.5f * Plunge) * DeltaSeconds;
		const int32 Count = FMath::Min(FMath::FloorToInt(BowSprayOwed[S]), 60);
		BowSprayOwed[S] -= FMath::FloorToInt(BowSprayOwed[S]);

		for (int32 n = 0; n < Count; ++n)
		{
			// Anywhere along the first metre of wetted chine.
			const float Along = FMath::FRand() * 3.f;
			const int32 A = FMath::Min(Root + FMath::FloorToInt(Along), ChineSamples - 1);
			const int32 B = FMath::Min(A + 1, ChineSamples - 1);
			const float F = FMath::Frac(Along);
			const FChineSample& SA = Samples[S][A];
			const FChineSample& SB = Samples[S][B];
			const float SeaZ = FMath::Lerp(SA.SeaZ, SB.SeaZ, F);
			const float ChineDepth = FMath::Lerp(SA.ChineDepth, SB.ChineDepth, F);
			const FVector Origin = SprayOriginAt(FMath::Lerp(SA.X, SB.X, F), FMath::Lerp(SA.LocalSeaZ, SB.LocalSeaZ, F), Side);
			const FVector PointVelocity = GetDeckPointVelocity(Origin);
			const float U = FMath::Max(0.f, FVector::DotProduct(PointVelocity, Fwd));
			// Out from the side it runs along: near the stem the side itself angles in steeply, and water thrown at
			// a fixed angle off the centreline there would fly straight back into it.
			const float SideAngle = FMath::Atan(HullSideSlope(FMath::Lerp(SA.X, SB.X, F), FMath::Lerp(SA.LocalSeaZ, SB.LocalSeaZ, F)));
			const float Angle = FMath::Min(SideAngle + FMath::DegreesToRadians(FMath::FRandRange(15.f, 30.f)), FMath::DegreesToRadians(55.f));
			// Flat off a dry chine; climbing higher up the topsides when the sea is over it; highest driving down.
			const float Lift = FMath::FRandRange(0.08f, 0.2f) + 0.15f * FMath::Clamp(ChineDepth / 20.f, 0.f, 1.f) + 0.2f * Plunge;
			const FVector Throw = PointVelocity - Fwd * U * FMath::Cos(Angle) + Right * Side * U * FMath::Sin(Angle)
				+ FVector::UpVector * U * Lift;
			Spray->ThrowSpray(Origin, Throw, 0.05f * U, 1, 3.5f, FMath::Lerp(9.f, 14.f, Speed01), 0.9f, SeaZ, 0.7f, 0.06f);
		}
		// A haze of fine mist hanging over the sheet at speed, left behind where it formed.
		if (FMath::FRand() < DeltaSeconds * 10.f * Speed01 * Load)
		{
			const FChineSample& At = Samples[S][FMath::Min(Root + FMath::RandRange(0, 4), ChineSamples - 1)];
			const FVector Origin = SprayOriginAt(At.X, At.LocalSeaZ, Side) + Right * Side * 60.f + FVector::UpVector * 15.f;
			Spray->ThrowSpray(Origin, HullVelocity * 0.25f + Right * Side * 220.f + FVector::UpVector * 60.f, 60.f, 1, 60.f, 200.f, 1.4f,
				At.SeaZ, 0.07f, 0.f);
		}
	}

	// Stern: each prop churns up a low mound of whitewater behind its motor, tumbling and left behind the boat.
	SternSprayOwed += SternSprayRate * Speed01 * GetDriveFraction() * DeltaSeconds;
	int32 Stern = FMath::FloorToInt(SternSprayOwed);
	SternSprayOwed -= Stern;
	for (; Stern > 0; --Stern)
	{
		const float Y = (FMath::RandBool() ? -1.f : 1.f) * FMath::Abs(OutboardPivot.Y) + FMath::FRandRange(-25.f, 25.f);
		const FVector Origin = Xf.TransformPosition(FVector(-HullExtent.X - FMath::FRandRange(60.f, 160.f), Y, WaterlineZ + 5.f));
		const FVector Throw = HullVelocity * 0.35f + Right * Y * 1.5f + FVector::UpVector * FMath::FRandRange(100.f, 260.f);
		Spray->ThrowSpray(Origin, Throw, 120.f, 1, 12.f, 36.f, 0.8f, GetSeaSurfaceZ(Origin), 0.85f, 0.03f);
		if (FMath::FRand() < 0.05f)
		{
			Spray->ThrowSpray(Origin, HullVelocity * 0.3f + FVector::UpVector * 120.f, 90.f, 1, 80.f, 200.f, 1.5f,
				GetSeaSurfaceZ(Origin), 0.1f, 0.f);
		}
	}
}

AActor* ARiptideBoat::SpawnWakeSimulation(UClass* SimClass)
{
	// Each machine runs its own simulation around its own player, so it's created locally rather than placed
	// in the level. It renders into textures, so machines that can't render (dedicated servers, headless test
	// runs) skip it; its setup divides by the render size and crashes without a renderer.
	if (!FApp::CanEverRender())
	{
		return nullptr;
	}
	AActor* Ocean = UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass());
	if (!Ocean)
	{
		return nullptr;
	}

	// Settings go in before the Blueprint's construction script runs, which sizes its render targets from them.
	AActor* Sim = GetWorld()->SpawnActorDeferred<AActor>(SimClass, FTransform(GetActorLocation()));
	if (!Sim)
	{
		return nullptr;
	}
	auto Set = [Sim](const TCHAR* Name, TFunctionRef<void(FProperty*, void*)> Apply)
	{
		if (FProperty* Prop = Sim->GetClass()->FindPropertyByName(Name))
		{
			Apply(Prop, Prop->ContainerPtrToValuePtr<void>(Sim));
		}
		else
		{
			UE_LOG(LogRiptideBoat, Warning, TEXT("Wake simulation has no '%s' setting"), Name);
		}
	};
	Set(TEXT("WaterBody"), [Ocean](FProperty* P, void* V) { if (FObjectPropertyBase* O = CastField<FObjectPropertyBase>(P)) { O->SetObjectPropertyValue(V, Ocean); } });
	// Its patch of water is centred on the simulation actor itself, which the boat carries along (see Tick). Left to
	// follow the player, it would centre on whoever is walking about the deck, and the ripples would slide around as
	// they moved and looked about.
	Set(TEXT("Follow Player "), [](FProperty* P, void* V) { if (FBoolProperty* B = CastField<FBoolProperty>(P)) { B->SetPropertyValue(V, false); } });  // trailing space is in the Blueprint's name
	Set(TEXT("Simulation World Size"), [this](FProperty* P, void* V) { if (FNumericProperty* N = CastField<FNumericProperty>(P)) { N->SetFloatingPointPropertyValue(V, WakeSimulationSize); } });
	Set(TEXT("Damping"), [this](FProperty* P, void* V) { if (FNumericProperty* N = CastField<FNumericProperty>(P)) { N->SetFloatingPointPropertyValue(V, WakeSimulationDamping); } });
	Set(TEXT("Travel Speed"), [this](FProperty* P, void* V) { if (FNumericProperty* N = CastField<FNumericProperty>(P)) { N->SetFloatingPointPropertyValue(V, WakeSimulationWaveSpeed); } });
	Sim->FinishSpawning(FTransform(GetActorLocation()));
	return Sim;
}

void ARiptideBoat::UpdateEngineRevs(float DeltaSeconds)
{
	// Each engine's revs follow its throttle. With its prop out of the water it has nothing to push against and races,
	// past its usual full revs at full throttle up to the rev limiter (the tachometer goes into the red, the note
	// climbs), and drops back as the prop bites again.
	const USceneComponent* Props[2] = { Propeller, PropellerStarboard };
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		const float Output = GetDriveFraction(Motor);
		const float TargetRevs = IsPropSubmerged(Props[Motor]) ? Output : FMath::Min(1.f + PropOutOverRev, Output * (1.f + 3.f * PropOutOverRev));
		MotorRevs[Motor] = FMath::FInterpTo(MotorRevs[Motor], TargetRevs, DeltaSeconds, 6.f);
	}
}

void ARiptideBoat::StartSounds()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Other boats fade out over 150 m; at the helm (within 5 m of the motor) it's full volume.
	SoundFalloff = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& Falloff = SoundFalloff->Attenuation;
	Falloff.bAttenuate = true;
	Falloff.bSpatialize = true;
	Falloff.AttenuationShape = EAttenuationShape::Sphere;
	Falloff.AttenuationShapeExtents = FVector(500.f, 0.f, 0.f);
	Falloff.FalloffDistance = 15000.f;

	for (UAudioComponent* Layer : { EngineAudio.Get(), EngineAudioStarboard.Get() })
	{
		Layer->AttenuationSettings = SoundFalloff;
		Layer->SetSound(EngineSound.LoadSynchronous());
	}
	for (UAudioComponent* Layer : { EngineHighAudio.Get(), EngineHighAudioStarboard.Get() })
	{
		Layer->AttenuationSettings = SoundFalloff;
		Layer->SetSound(EngineHighSound.LoadSynchronous());
	}
	WashAudio->AttenuationSettings = SoundFalloff;
	WashAudio->SetSound(WashSound.LoadSynchronous());
	WashAudio->SetVolumeMultiplier(0.f);
	WashAudio->Play();
	for (const TSoftObjectPtr<USoundBase>& Slap : HullSlapSounds)
	{
		Slap.LoadSynchronous();
	}
}

void ARiptideBoat::UpdateSounds(float DeltaSeconds)
{
	if (!SoundFalloff)
	{
		return;
	}

	// Engines: each motor runs while it has fuel and isn't dead, with its own pair of recordings. Pitch and volume
	// follow that motor's actual output (a sputter is heard as a dip), and it races when its prop leaves the water.
	UAudioComponent* MotorLayers[2][2] = { { EngineAudio, EngineHighAudio }, { EngineAudioStarboard, EngineHighAudioStarboard } };
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		const bool bRunning = IsMotorRunning(Motor);
		if (bRunning != bMotorSoundRunning[Motor])
		{
			bMotorSoundRunning[Motor] = bRunning;
			for (UAudioComponent* Layer : MotorLayers[Motor])
			{
				if (bRunning)
				{
					Layer->FadeIn(0.5f);
				}
				else
				{
					Layer->FadeOut(1.5f, 0.f);
				}
			}
		}
		const float Output = GetDriveFraction(Motor);

		// Engine speed moves between idle and full on a musical (log) scale. Each recording is pitched to that speed,
		// and the two crossfade at equal power by where the speed sits between them. The starboard motor runs a touch
		// sharp, like two real engines.
		const float Hz = EngineIdleHz * FMath::Pow(EngineFullHz / EngineIdleHz, MotorRevs[Motor]) * (Motor == 1 ? StarboardEngineDetune : 1.f);
		const float HighWeight = FMath::Clamp(
			FMath::Loge(Hz / EngineLowRecordingHz) / FMath::Loge(EngineHighRecordingHz / EngineLowRecordingHz), 0.f, 1.f);
		// Each motor at half power (-3 dB), so the pair adds up to the level the single engine was measured at.
		const float Volume = FMath::Lerp(EngineIdleVolume, 1.f, Output) * UE_INV_SQRT_2;
		MotorLayers[Motor][0]->SetPitchMultiplier(Hz / EngineLowRecordingHz);
		MotorLayers[Motor][1]->SetPitchMultiplier(Hz / EngineHighRecordingHz);
		MotorLayers[Motor][0]->SetVolumeMultiplier(Volume * FMath::Max(0.f, FMath::Cos(HighWeight * UE_HALF_PI)));
		MotorLayers[Motor][1]->SetVolumeMultiplier(Volume * FMath::Max(0.f, FMath::Sin(HighWeight * UE_HALF_PI)));
	}

	// Wash: water rushing past the hull, rising with speed. Silent out of the water.
	const bool bInWater = Buoyancy && Buoyancy->IsInWaterBody();
	const float SpeedFraction = bInWater ? FMath::Clamp(GetSpeedKnots() / WashFullSpeedKnots, 0.f, 1.f) : 0.f;
	WashAudio->SetVolumeMultiplier(FMath::Pow(SpeedFraction, 1.5f));
	WashAudio->SetPitchMultiplier(FMath::Lerp(0.85f, 1.15f, SpeedFraction));

	// Hull slap: the bow dropping into the water (or a wave rising into it) fast enough, near the surface.
	// Louder the harder it hits.
	// Measured only between two frames spent in the water: before that the bow's water reading isn't valid.
	const float Freeboard = GetBowFreeboardCm();
	const bool bHadFreeboard = bHaveBowFreeboard;
	const float ClosingSpeed = bHadFreeboard ? (PrevBowFreeboard - Freeboard) / FMath::Max(DeltaSeconds, 1e-3f) : 0.f;
	PrevBowFreeboard = Freeboard;
	bHaveBowFreeboard = bInWater;
	SlapCooldownLeft -= DeltaSeconds;
	if (bHadFreeboard && bInWater && SlapCooldownLeft <= 0.f && ClosingSpeed > SlapMinSpeed && Freeboard < 25.f && HullSlapSounds.Num() > 0)
	{
		const float Strength = FMath::Clamp((ClosingSpeed - SlapMinSpeed) / (SlapFullSpeed - SlapMinSpeed), 0.f, 1.f);
		const float SlapVolume = FMath::Lerp(0.25f, 1.f, Strength);
		if (USoundBase* Slap = HullSlapSounds[FMath::RandRange(0, HullSlapSounds.Num() - 1)].Get())
		{
			const FVector Bow = HullBody->GetComponentTransform().TransformPosition(FVector(HullExtent.X * 0.8f, 0.f, WaterlineZ));
			UGameplayStatics::PlaySoundAtLocation(this, Slap, Bow, SlapVolume, FMath::FRandRange(0.9f, 1.1f), 0.f, SoundFalloff);
		}
		SprayAtBow(Strength);
		SlapCooldownLeft = SlapCooldown;
		UE_LOG(LogRiptideBoat, Verbose, TEXT("Hull slap at %.0f cm/s, volume %.2f"), ClosingSpeed, SlapVolume);
	}
}

void ARiptideBoat::RegisterWithWakeSimulation()
{
	// The Water plugin's fluid simulation (BP_FluidSim_01) ripples the water surface around the local player.
	// It's a Blueprint, so its "Register Dynamic Force" function and FluidForceDynamic struct are reached
	// through reflection, matching the struct's fields by their name prefix.
	static const TCHAR* SimClassPath = TEXT("/Water/FluidSimulation/Blueprints/BP_FluidSim_01.BP_FluidSim_01_C");
	UClass* SimClass = LoadClass<AActor>(nullptr, SimClassPath);
	if (!SimClass)
	{
		return;
	}
	AActor* Sim = UGameplayStatics::GetActorOfClass(this, SimClass);
	if (!Sim)
	{
		Sim = SpawnWakeSimulation(SimClass);
		WakeSimulation = Sim;
	}
	if (!Sim)
	{
		return;
	}

	UFunction* Register = Sim->FindFunction(TEXT("Register Dynamic Force"));
	if (!Register)
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: wake simulation has no 'Register Dynamic Force'; no wake for %s"), *GetName());
		return;
	}

	auto SetNumber = [](FProperty* Prop, void* Container, double Value)
	{
		if (FNumericProperty* Num = CastField<FNumericProperty>(Prop))
		{
			void* Ptr = Num->ContainerPtrToValuePtr<void>(Container);
			if (Num->IsFloatingPoint())
			{
				Num->SetFloatingPointPropertyValue(Ptr, Value);
			}
		}
	};
	auto SetObject = [](FProperty* Prop, void* Container, UObject* Value)
	{
		if (FObjectPropertyBase* Obj = CastField<FObjectPropertyBase>(Prop))
		{
			Obj->SetObjectPropertyValue(Obj->ContainerPtrToValuePtr<void>(Container), Value);
		}
	};

	FStructOnScope Params(Register);
	uint8* ParamMemory = Params.GetStructMemory();
	bool bFilledForce = false;
	for (TFieldIterator<FProperty> It(Register); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		const FString ParamName = Param->GetName();
		if (FStructProperty* ForceParam = CastField<FStructProperty>(Param))
		{
			void* Force = ForceParam->ContainerPtrToValuePtr<void>(ParamMemory);
			for (TFieldIterator<FProperty> Field(ForceParam->Struct); Field; ++Field)
			{
				const FString FieldName = Field->GetName();
				if (FieldName.StartsWith(TEXT("ForceRadius")))
				{
					SetNumber(*Field, Force, WakeRadius);
				}
				else if (FieldName.StartsWith(TEXT("ForceComponent")))
				{
					SetObject(*Field, Force, WakeSource);
					bFilledForce = true;
				}
				else if (FieldName.StartsWith(TEXT("MaterialOverride")))
				{
					SetObject(*Field, Force, WakeForceMaterial.LoadSynchronous());
				}
			}
		}
		else if (ParamName.StartsWith(TEXT("Tracked")))
		{
			SetObject(Param, ParamMemory, WakeSource);
		}
		else if (ParamName.StartsWith(TEXT("WaterLevel")))
		{
			// Sea level. The test maps put the ocean surface at Z = 0.
			SetNumber(Param, ParamMemory, 0.0);
		}
	}

	if (!bFilledForce)
	{
		UE_LOG(LogTemp, Warning, TEXT("Riptide: wake simulation's force struct didn't match; no wake for %s"), *GetName());
		return;
	}
	Sim->ProcessEvent(Register, ParamMemory);
	UE_LOG(LogTemp, Log, TEXT("Riptide: %s registered with the wake simulation"), *GetName());
}

void ARiptideBoat::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARiptideBoat, ThrottleLever);
	DOREPLIFETIME(ARiptideBoat, EngineOutput);
	DOREPLIFETIME(ARiptideBoat, SteerAngleDeg);
	DOREPLIFETIME(ARiptideBoat, MicHolder);
	DOREPLIFETIME(ARiptideBoat, LadderUser);
	DOREPLIFETIME(ARiptideBoat, TrimDeg);
	DOREPLIFETIME(ARiptideBoat, FuelLiters);
	DOREPLIFETIME(ARiptideBoat, EngineHealthPort);
	DOREPLIFETIME(ARiptideBoat, EngineHealthStarboard);
	DOREPLIFETIME(ARiptideBoat, MotorOutputPort);
	DOREPLIFETIME(ARiptideBoat, MotorOutputStarboard);
	DOREPLIFETIME(ARiptideBoat, Helmsman);
	DOREPLIFETIME(ARiptideBoat, bSearchlightOn);
	DOREPLIFETIME(ARiptideBoat, bNavLightsOn);
	DOREPLIFETIME(ARiptideBoat, bDeckLightsOn);
	DOREPLIFETIME(ARiptideBoat, SearchlightYaw);
	DOREPLIFETIME(ARiptideBoat, SearchlightPitch);
}

// --- Input ---

void ARiptideBoat::BuildInput()
{
	if (HelmMapping)
	{
		return;
	}

	// Built in code so the project runs without any input assets authored in the editor.
	ThrottleAction = NewObject<UInputAction>(this, TEXT("IA_Throttle"));
	ThrottleAction->ValueType = EInputActionValueType::Axis1D;

	SteerAction = NewObject<UInputAction>(this, TEXT("IA_Steer"));
	SteerAction->ValueType = EInputActionValueType::Axis1D;

	CutThrottleAction = NewObject<UInputAction>(this, TEXT("IA_CutThrottle"));
	CutThrottleAction->ValueType = EInputActionValueType::Boolean;

	LookAction = NewObject<UInputAction>(this, TEXT("IA_Look"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	HelmMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Helm"));

	HelmMapping->MapKey(ThrottleAction, EKeys::W);
	FEnhancedActionKeyMapping& ThrottleDown = HelmMapping->MapKey(ThrottleAction, EKeys::S);
	ThrottleDown.Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));

	HelmMapping->MapKey(SteerAction, EKeys::D);
	FEnhancedActionKeyMapping& SteerLeft = HelmMapping->MapKey(SteerAction, EKeys::A);
	SteerLeft.Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));

	HelmMapping->MapKey(CutThrottleAction, EKeys::X);
	HelmMapping->MapKey(LookAction, EKeys::Mouse2D);

	// The same key that takes the helm on foot lets go of it.
	LeaveHelmAction = NewObject<UInputAction>(this, TEXT("IA_LeaveHelm"));
	LeaveHelmAction->ValueType = EInputActionValueType::Boolean;
	HelmMapping->MapKey(LeaveHelmAction, EKeys::E);
	HelmMapping->MapKey(LeaveHelmAction, EKeys::Gamepad_FaceButton_Left);

	// Trim switch: R trims out (bow up), F trims in (bow down); the d-pad on a gamepad.
	TrimAction = NewObject<UInputAction>(this, TEXT("IA_Trim"));
	TrimAction->ValueType = EInputActionValueType::Axis1D;
	HelmMapping->MapKey(TrimAction, EKeys::R);
	HelmMapping->MapKey(TrimAction, EKeys::F).Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));
	HelmMapping->MapKey(TrimAction, EKeys::Gamepad_DPad_Up);
	HelmMapping->MapKey(TrimAction, EKeys::Gamepad_DPad_Down).Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));

	// H shows the tuning readout (the dash instruments are the real display).
	DebugHudAction = NewObject<UInputAction>(this, TEXT("IA_DebugHud"));
	DebugHudAction->ValueType = EInputActionValueType::Boolean;
	HelmMapping->MapKey(DebugHudAction, EKeys::H);

	// Lights: L the searchlight (it follows where you look), N the navigation lights, K the cockpit floods.
	SearchlightAction = NewObject<UInputAction>(this, TEXT("IA_Searchlight"));
	SearchlightAction->ValueType = EInputActionValueType::Boolean;
	HelmMapping->MapKey(SearchlightAction, EKeys::L);
	HelmMapping->MapKey(SearchlightAction, EKeys::Gamepad_DPad_Left);
	NavLightsAction = NewObject<UInputAction>(this, TEXT("IA_NavLights"));
	NavLightsAction->ValueType = EInputActionValueType::Boolean;
	HelmMapping->MapKey(NavLightsAction, EKeys::N);
	DeckLightsAction = NewObject<UInputAction>(this, TEXT("IA_DeckLights"));
	DeckLightsAction->ValueType = EInputActionValueType::Boolean;
	HelmMapping->MapKey(DeckLightsAction, EKeys::K);
	MicAction = NewObject<UInputAction>(this, TEXT("IA_Mic"));
	MicAction->ValueType = EInputActionValueType::Boolean;
	HelmMapping->MapKey(MicAction, EKeys::M);
	HelmMapping->MapKey(MicAction, EKeys::Gamepad_RightThumbstick);
	HelmMapping->MapKey(DeckLightsAction, EKeys::Gamepad_DPad_Right);

	// Gamepad: right trigger / left trigger for throttle, left stick to steer, right stick to look.
	HelmMapping->MapKey(ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	FEnhancedActionKeyMapping& PadReverse = HelmMapping->MapKey(ThrottleAction, EKeys::Gamepad_LeftTriggerAxis);
	PadReverse.Modifiers.Add(NewObject<UInputModifierNegate>(HelmMapping));
	HelmMapping->MapKey(SteerAction, EKeys::Gamepad_LeftX);
	HelmMapping->MapKey(CutThrottleAction, EKeys::Gamepad_FaceButton_Right);
	HelmMapping->MapKey(LookAction, EKeys::Gamepad_Right2D);
}

void ARiptideBoat::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	BuildInput();

	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(LeaveHelmAction, ETriggerEvent::Started, this, &ARiptideBoat::OnLeaveHelm);
		Input->BindAction(TrimAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnTrim);
		Input->BindAction(TrimAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnTrimReleased);
		Input->BindAction(TrimAction, ETriggerEvent::Canceled, this, &ARiptideBoat::OnTrimReleased);
		Input->BindActionValueLambda(DebugHudAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bShowDebugHud = !bShowDebugHud; });
		Input->BindActionValueLambda(SearchlightAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleLight(0); });
		Input->BindActionValueLambda(NavLightsAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleLight(1); });
		Input->BindActionValueLambda(DeckLightsAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleLight(2); });
		Input->BindActionValueLambda(MicAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleMic(); });
		Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnThrottle);
		Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnThrottleReleased);
		Input->BindAction(ThrottleAction, ETriggerEvent::Canceled, this, &ARiptideBoat::OnThrottleReleased);
		Input->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnSteer);
		Input->BindAction(SteerAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnSteerReleased);
		Input->BindAction(SteerAction, ETriggerEvent::Canceled, this, &ARiptideBoat::OnSteerReleased);
		Input->BindAction(CutThrottleAction, ETriggerEvent::Started, this, &ARiptideBoat::OnCutThrottle);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnLook);
	}
}

void ARiptideBoat::NotifyControllerChanged()
{
	// Swap the helm controls in or out as a player takes or leaves the helm (before Super, which forgets the
	// previous controller).
	BuildInput();
	auto InputFor = [](AController* C) -> UEnhancedInputLocalPlayerSubsystem*
	{
		const APlayerController* PC = Cast<APlayerController>(C);
		return PC && PC->IsLocalController() ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
	};
	if (UEnhancedInputLocalPlayerSubsystem* Old = InputFor(PreviousController))
	{
		Old->RemoveMappingContext(HelmMapping);
	}
	if (UEnhancedInputLocalPlayerSubsystem* New = InputFor(Controller))
	{
		New->AddMappingContext(HelmMapping, 0);
	}
	// Whoever had the helm before isn't holding its keys now (on every machine: a client keeps its own copy of what
	// it was holding, and would send it straight back the next time it took the helm).
	ThrottleInput = 0.f;
	SteerInput = 0.f;
	TrimInput = 0.f;
	if (Controller && IsLocallyControlled())
	{
		// Carry on looking where the crew member was looking, relative to the boat (the server worked this out too,
		// but the helm camera turns on the helmsman's own machine).
		const FRotator View = Controller->GetControlRotation();
		LookYaw = FMath::Clamp(FRotator::NormalizeAxis(View.Yaw - GetActorRotation().Yaw), -170.f, 170.f);
		LookPitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch), -70.f, 70.f);
		HelmCamera->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));
	}
	Super::NotifyControllerChanged();
}

void ARiptideBoat::UnPossessed()
{
	Super::UnPossessed();
	// Nobody's holding the keys any more: the lever stays where it was left and the wheel swings back to centre.
	ThrottleInput = 0.f;
	SteerInput = 0.f;
	TrimInput = 0.f;
}

void ARiptideBoat::OnThrottle(const FInputActionValue& Value)
{
	ThrottleInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ARiptideBoat::OnThrottleReleased(const FInputActionValue& Value)
{
	ThrottleInput = 0.f;
}

void ARiptideBoat::OnSteer(const FInputActionValue& Value)
{
	SteerInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ARiptideBoat::OnSteerReleased(const FInputActionValue& Value)
{
	SteerInput = 0.f;
}

void ARiptideBoat::OnTrim(const FInputActionValue& Value)
{
	TrimInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f);
}

void ARiptideBoat::OnTrimReleased(const FInputActionValue& Value)
{
	TrimInput = 0.f;
}

void ARiptideBoat::OnCutThrottle(const FInputActionValue& Value)
{
	if (HasAuthority())
	{
		bCutThrottleRequested = true;
	}
	else
	{
		ServerCutThrottle();
	}
}

void ARiptideBoat::ServerCutThrottle_Implementation()
{
	bCutThrottleRequested = true;
}

void ARiptideBoat::OnLook(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>() * RiptideSettings::LookScale();     // the player's sensitivity and invert
	LookYaw = FMath::Clamp(LookYaw + Delta.X * LookSensitivity, -170.f, 170.f);
	LookPitch = FMath::Clamp(LookPitch + Delta.Y * LookSensitivity, -70.f, 70.f);
	HelmCamera->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));
}

void ARiptideBoat::OnLeaveHelm(const FInputActionValue& Value)
{
	if (HasAuthority())
	{
		LeaveHelm();
	}
	else
	{
		ServerLeaveHelm(LookYaw, LookPitch);
	}
}

void ARiptideBoat::ServerLeaveHelm_Implementation(float InLookYaw, float InLookPitch)
{
	if (FMath::IsFinite(InLookYaw) && FMath::IsFinite(InLookPitch))
	{
		LookYaw = FMath::Clamp(InLookYaw, -170.f, 170.f);
		LookPitch = FMath::Clamp(InLookPitch, -70.f, 70.f);
		HelmCamera->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));
	}
	LeaveHelm();
}

// --- Crew ---

FTransform ARiptideBoat::GetHelmStandTransform() const
{
	return GetDeckSpotTransform(0);
}

FTransform ARiptideBoat::GetDeckSpotTransform(int32 Index) const
{
	const int32 NumSpots = UE_ARRAY_COUNT(DeckSpots);
	const FVector Spot = DeckSpots[((Index % NumSpots) + NumSpots) % NumSpots];
	const FTransform& Xf = HullBody->GetComponentTransform();
	return FTransform(Xf.GetRotation(), Xf.TransformPosition(Spot));
}

// --- Lights ---

void ARiptideBoat::SetSearchlightOn(bool bOn)
{
	if (HasAuthority())
	{
		bSearchlightOn = bOn;
		ApplyLights();
	}
}

void ARiptideBoat::SetNavLightsOn(bool bOn)
{
	if (HasAuthority())
	{
		bNavLightsOn = bOn;
		ApplyLights();
	}
}

void ARiptideBoat::SetDeckLightsOn(bool bOn)
{
	if (HasAuthority())
	{
		bDeckLightsOn = bOn;
		ApplyLights();
	}
}

void ARiptideBoat::ServerToggleLight_Implementation(uint8 Which)
{
	switch (Which)
	{
	case 0: SetSearchlightOn(!bSearchlightOn); break;
	case 1: SetNavLightsOn(!bNavLightsOn); break;
	default: SetDeckLightsOn(!bDeckLightsOn); break;
	}
}

void ARiptideBoat::AimSearchlight(float YawDeg, float PitchDeg)
{
	// Clamping doesn't catch a NaN (it passes straight through), so anything that isn't a real angle is ignored.
	if (HasAuthority() && FMath::IsFinite(YawDeg) && FMath::IsFinite(PitchDeg))
	{
		// Its mount turns almost all the way round and tilts from well down to a little up.
		SearchlightYaw = FMath::Clamp(FRotator::NormalizeAxis(YawDeg), -170.f, 170.f);
		SearchlightPitch = FMath::Clamp(PitchDeg, -35.f, 20.f);
	}
}

void ARiptideBoat::ServerAimSearchlight_Implementation(float YawDeg, float PitchDeg)
{
	AimSearchlight(YawDeg, PitchDeg);
}

void ARiptideBoat::OnRep_Lights()
{
	ApplyLights();
}

void ARiptideBoat::ApplyLights()
{
	SearchlightBeam->SetVisibility(bSearchlightOn);
	const int32 LampIndex = SearchlightHead->GetMaterialIndex(TEXT("Lamp"));
	if (LampIndex != INDEX_NONE)
	{
		UMaterialInterface* Lens = bSearchlightOn ? LampOnMaterial.Get() : LampOffMaterial.Get();
		if (Lens)
		{
			SearchlightHead->SetMaterial(LampIndex, Lens);
		}
	}
	MastheadLight->SetVisibility(bNavLightsOn);
	BowLightPort->SetVisibility(bNavLightsOn);
	BowLightStarboard->SetVisibility(bNavLightsOn);
	for (UMaterialInstanceDynamic* Lens : NavLenses)
	{
		if (Lens)
		{
			Lens->SetScalarParameterValue(TEXT("Glow"), bNavLightsOn ? 8.f : 0.f);
		}
	}
	DeckFloodPort->SetVisibility(bDeckLightsOn);
	DeckFloodStarboard->SetVisibility(bDeckLightsOn);
}

void ARiptideBoat::UpdateSearchlight(float DeltaSeconds)
{
	// The helmsman aims it by looking: it follows the helm camera's direction.
	if (IsLocallyControlled() && Helmsman)
	{
		if (HasAuthority())
		{
			AimSearchlight(LookYaw, LookPitch);
		}
		else
		{
			ServerAimSearchlight(LookYaw, LookPitch);
		}
	}
	const float Step = SearchlightSlewDeg * DeltaSeconds;
	SearchlightYawNow = FMath::FixedTurn(SearchlightYawNow, SearchlightYaw, Step);
	SearchlightPitchNow = FMath::FInterpConstantTo(SearchlightPitchNow, SearchlightPitch, DeltaSeconds, SearchlightSlewDeg);
	SearchlightHead->SetRelativeRotation(FRotator(SearchlightPitchNow, SearchlightYawNow, 0.f));
}

// --- Handholds ---

namespace
{
	/** The hull's sheer (gunwale) at X: half-width and height (riptide_boat_mesh.py's station). */
	void SheerAt(float X, float& OutHalf, float& OutZ)
	{
		const float T = FMath::Clamp((X + HullExtent.X) / (2.f * HullExtent.X), 0.f, 1.f);
		OutHalf = T < 0.35f ? HullExtent.Y : HullExtent.Y * FMath::Pow(FMath::Max(0.f, FMath::Cos((T - 0.35f) / 0.65f * UE_HALF_PI)), 0.75f);
		OutZ = DeckZ + 55.f + 45.f * FMath::Pow(T, 2.2f);
	}

	float DistToSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		return FMath::PointDistToSegment(P, A, B);
	}
}

bool ARiptideBoat::IsHandholdNear(FVector World, float Reach) const
{
	const FVector P = HullBody->GetComponentTransform().InverseTransformPosition(World);
	// The gunwale all round (and the bow rail above it forward): within reach of the side, at hand height.
	float Half, SheerZ;
	SheerAt(P.X, Half, SheerZ);
	if (P.X > -HullExtent.X - 20.f && P.X < HullExtent.X && Half - FMath::Abs(P.Y) < Reach && FMath::Abs(P.Z - SheerZ) < 120.f)
	{
		return true;
	}
	// The stern: the transom and stern box top.
	if (P.X < -HullExtent.X + 60.f + Reach && FMath::Abs(P.Z - (DeckZ + 55.f)) < 120.f)
	{
		return true;
	}
	struct FRail { FVector A, B; };
	static const FRail Rails[] = {
		{ FVector(-38.f, -49.f, DeckZ + 70.f), FVector(22.f, -49.f, DeckZ + 70.f) },    // console grab rails
		{ FVector(-38.f, 49.f, DeckZ + 70.f), FVector(22.f, 49.f, DeckZ + 70.f) },
		{ FVector(-150.f, -48.f, DeckZ), FVector(-158.f, -60.f, DeckZ + 220.f) },       // T-top legs
		{ FVector(-150.f, 48.f, DeckZ), FVector(-158.f, 60.f, DeckZ + 220.f) },
		{ FVector(20.f, -48.f, DeckZ), FVector(28.f, -60.f, DeckZ + 220.f) },
		{ FVector(20.f, 48.f, DeckZ), FVector(28.f, 60.f, DeckZ + 220.f) },
		{ FVector(-182.f, -36.f, DeckZ + 124.f), FVector(-182.f, 36.f, DeckZ + 124.f) }, // leaning post rail
		{ FVector(-45.f, -30.f, DeckZ + 95.f), FVector(-45.f, 30.f, DeckZ + 95.f) },    // the dash and wheel
		{ FVector(345.f, 0.f, DeckZ + 14.f), FVector(345.f, 0.f, DeckZ + 46.f) },       // tow post
	};
	for (const FRail& Rail : Rails)
	{
		if (DistToSegment(P, Rail.A, Rail.B) < Reach)
		{
			return true;
		}
	}
	return false;
}

FVector ARiptideBoat::GetDeckPointVelocity(const FVector& World) const
{
	return HullBody->GetPhysicsLinearVelocity()
		+ FVector::CrossProduct(HullBody->GetPhysicsAngularVelocityInRadians(), World - HullBody->GetCenterOfMass());
}

FTransform ARiptideBoat::GetLadderFootTransform() const
{
	// The ladder hangs off the transom's port side (riptide_boat_mesh.py's _fittings), its foot in the water.
	const FTransform& Xf = HullBody->GetComponentTransform();
	return FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(-HullExtent.X - 6.f, -104.f, WaterlineZ - 15.f)));
}

FTransform ARiptideBoat::GetLadderTopTransform() const
{
	// The top of the ladder, where a climber comes over the transom and the stern box.
	const FTransform& Xf = HullBody->GetComponentTransform();
	return FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(-HullExtent.X + 10.f, -104.f, DeckZ + 70.f)));
}

FTransform ARiptideBoat::GetLadderLandingTransform() const
{
	const FTransform& Xf = HullBody->GetComponentTransform();
	return FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(-310.f, -85.f, DeckZ)));
}

bool ARiptideBoat::TakeHelm(ARiptideCharacter* Crew)
{
	if (!HasAuthority() || !Crew || Helmsman)
	{
		return false;
	}
	AController* Driver = Crew->GetController();
	if (!Driver)
	{
		return false;
	}

	// Carry on looking where the crew member was looking, relative to the boat.
	const FRotator View = Driver->GetControlRotation();
	LookYaw = FMath::Clamp(FRotator::NormalizeAxis(View.Yaw - GetActorRotation().Yaw), -170.f, 170.f);
	LookPitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch), -70.f, 70.f);
	HelmCamera->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));

	Helmsman = Crew;
	Crew->SetManningHelm(true);
	Driver->Possess(this);
	ApplyMicHolder();
	UE_LOG(LogRiptideBoat, Log, TEXT("%s took the helm of %s"), *Crew->GetName(), *GetName());
	return true;
}

void ARiptideBoat::LeaveHelm()
{
	if (!HasAuthority() || !Helmsman)
	{
		return;
	}
	ARiptideCharacter* Crew = Helmsman;
	Helmsman = nullptr;
	AController* Driver = GetController();

	// Step back onto the deck at the helm, facing where the helm camera was looking.
	const FRotator View = HelmCamera->GetComponentRotation();
	Crew->SetManningHelm(false);
	Crew->SetActorRotation(FRotator(0.f, View.Yaw, 0.f));
	if (Driver)
	{
		Driver->Possess(Crew);
		Driver->ClientSetRotation(FRotator(View.Pitch, View.Yaw, 0.f));
		Driver->SetControlRotation(FRotator(View.Pitch, View.Yaw, 0.f));
	}
	ApplyMicHolder();
	UE_LOG(LogRiptideBoat, Log, TEXT("%s left the helm of %s"), *Crew->GetName(), *GetName());
}

void ARiptideBoat::ServerSetControls_Implementation(float InThrottleInput, float InSteerInput, float InTrimInput)
{
	// Clamping doesn't catch a NaN (it passes straight through, and would then spread through the whole simulation),
	// so a frame of controls that aren't real numbers is ignored.
	if (!FMath::IsFinite(InThrottleInput) || !FMath::IsFinite(InSteerInput) || !FMath::IsFinite(InTrimInput))
	{
		return;
	}
	ThrottleInput = FMath::Clamp(InThrottleInput, -1.f, 1.f);
	SteerInput = FMath::Clamp(InSteerInput, -1.f, 1.f);
	TrimInput = FMath::Clamp(InTrimInput, -1.f, 1.f);
}

// --- Simulation ---

void ARiptideBoat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled() && !HasAuthority())
	{
		ServerSetControls(ThrottleInput, SteerInput, TrimInput);
	}

	UpdatePropImmersion();

	// The helm, the engines and the fuel are the server's to decide (and replicate).
	if (HasAuthority())
	{
		RescueIfOffTheSea();
		UpdateControls(DeltaSeconds);
		UpdateEngine(DeltaSeconds);
	}
	// The water's forces act on every machine's copy of the hull, from the replicated engine output, steering and
	// trim and this machine's own reading of the sea at the props. Between the server's corrections a client's hull
	// is simulating too, and without them it would have no drag, damping or planing lift and would jitter.
	ApplyThrust();
	ApplyHydrodynamics();
	UpdateEngineRevs(DeltaSeconds);

	PoseOutboards();
	UpdateSearchlight(DeltaSeconds);

	// The radar sweeps while the engines are running.
	if (GetEngineRpm() > 0.f)
	{
		RadarArray->AddLocalRotation(FRotator(0.f, RadarRpm * 6.f * DeltaSeconds, 0.f));
	}
	// The compass card keeps its North to the world's North (+X), whichever way the boat heads.
	CompassCard->SetRelativeRotation(FRotator(0.f, -GetActorRotation().Yaw, 0.f));
	// Throttle levers: forward for ahead, back for astern.
	ThrottleLeverPort->SetRelativeRotation(FRotator(-ThrottleLever * LeverSwingDeg, 0.f, 0.f));
	ThrottleLeverStarboard->SetRelativeRotation(FRotator(-ThrottleLever * LeverSwingDeg, 0.f, 0.f));

	UpdateSounds(DeltaSeconds);
	UpdateWakeFoam(DeltaSeconds);
	UpdateSpray(DeltaSeconds);
	UpdatePropsAndWheel(DeltaSeconds);

	// The mic's cord only reaches so far: walk off with the mic, or go over the side, and it's pulled from the hand
	// back onto its clip.
	if (HasAuthority() && MicHolder)
	{
		const bool bGone = !IsValid(MicHolder) || MicHolder->IsInSea() || MicHolder->GetHomeBoat() != this
			|| FVector::Dist(GetMicLocation(), HullBody->GetComponentTransform().TransformPosition(MicCordJack)) > MicCordReach;
		if (bGone)
		{
			HangUpMic();
		}
	}
	if (!MicHolder || MicHolder == Helmsman)
	{
		UpdateMicCord();
	}

	// Carry the wake simulation's patch of water along with the boat, at sea level.
	if (AActor* Sim = WakeSimulation.Get())
	{
		const FVector Here = GetActorLocation();
		Sim->SetActorLocation(FVector(Here.X, Here.Y, 0.f));
	}

	if (IsLocallyControlled())
	{
		DrawDebugHud();
	}
}

void ARiptideBoat::UpdateControls(float DeltaSeconds)
{
	if (bCutThrottleRequested)
	{
		ThrottleLever = 0.f;
		bCutThrottleRequested = false;
	}

	// The lever moves while the key is held and stays put when released, like a real throttle.
	ThrottleLever = FMath::Clamp(ThrottleLever + ThrottleInput * ThrottleLeverRate * DeltaSeconds, -1.f, 1.f);

	// The motor swings back to centre when the wheel is let go.
	const float TargetSteer = SteerInput * MaxSteerAngleDeg;
	SteerAngleDeg = FMath::FInterpConstantTo(SteerAngleDeg, TargetSteer, DeltaSeconds, SteerRateDeg);

	// The trim moves while its switch is held and stays where it's left.
	TrimDeg = FMath::Clamp(TrimDeg + TrimInput * TrimRateDeg * DeltaSeconds, MinTrimDeg, MaxTrimDeg);
}

FQuat ARiptideBoat::GetOutboardRotation() const
{
	// Trim tilts the motor about its bracket (out swings the lower unit aft, which pitches the prop shaft down);
	// steering then swings it about its own, tilted, steering axis. Steering right swings the prop to push the
	// stern left.
	return FQuat(FRotator(-TrimDeg, 0.f, 0.f)) * FQuat(FRotator(0.f, -SteerAngleDeg, 0.f));
}

void ARiptideBoat::PoseOutboards()
{
	const FQuat Pose = GetOutboardRotation();
	MotorMesh->SetRelativeRotation(Pose);
	MotorMeshStarboard->SetRelativeRotation(Pose);
	// The swivel brackets tilt with the trim but don't steer: the motors turn in their steering tubes.
	const FRotator Tilt(-TrimDeg, 0.f, 0.f);
	MotorSwivel->SetRelativeRotation(Tilt);
	MotorSwivelStarboard->SetRelativeRotation(Tilt);
	Propeller->SetRelativeLocation(OutboardPivot + Pose.RotateVector(PropInOutboard));
	PropellerStarboard->SetRelativeLocation(OutboardPivotStarboard + Pose.RotateVector(PropInOutboard));
}

ERiptideGear ARiptideBoat::GetGear() const
{
	return ThrottleLever > NeutralDetent ? ERiptideGear::Forward : ThrottleLever < -NeutralDetent ? ERiptideGear::Reverse : ERiptideGear::Neutral;
}

float ARiptideBoat::GetThrottleOpening() const
{
	const float Closed = NeutralDetent + GearIdleBand;
	return FMath::Clamp((FMath::Abs(ThrottleLever) - Closed) / FMath::Max(1.f - Closed, 0.01f), 0.f, 1.f);
}

float ARiptideBoat::GetDriveFraction(int32 Motor) const
{
	const float Output = Motor < 0 ? EngineOutput : GetMotorOutput(Motor);
	return FMath::Clamp((FMath::Abs(Output) - IdleThrustInGear) / (1.f - IdleThrustInGear), 0.f, 1.f);
}

float ARiptideBoat::GetEngineRpm() const
{
	// The running motors' average (the tachometer reads the pair).
	float Rpm = 0.f;
	int32 Running = 0;
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		if (IsMotorRunning(Motor))
		{
			Rpm += FMath::Lerp(IdleRpm, MaxRpm, MotorRevs[Motor]);
			++Running;
		}
	}
	return Running > 0 ? Rpm / Running : 0.f;
}

void ARiptideBoat::UpdateEngine(float DeltaSeconds)
{
	// The lever sets the gear and throttle together, like a real binnacle control: in gear at idle the props turn
	// slowly and the boat creeps along; the throttle opens from there. In neutral the props don't drive at all.
	const ERiptideGear Gear = GetGear();
	const float GearSign = Gear == ERiptideGear::Forward ? 1.f : Gear == ERiptideGear::Reverse ? -1.f : 0.f;
	float Target = GearSign * FMath::Lerp(IdleThrustInGear, 1.f, GetThrottleOpening());

	// Each motor runs on its own: a dead or dry one stops, a damaged one cuts out at random (more often the worse it
	// is). Losing one leaves the other pushing off to one side, so the boat pulls toward the dead motor's side.
	float Burn = 0.f;
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		const float Health = GetMotorHealth(Motor);
		float MotorTarget = Target;
		if (!IsMotorRunning(Motor))
		{
			MotorTarget = 0.f;
		}
		else if (Health < 0.5f)
		{
			if (SputterTimeLeft[Motor] > 0.f)
			{
				SputterTimeLeft[Motor] -= DeltaSeconds;
				MotorTarget = 0.f;
			}
			else if (FMath::FRand() < (0.5f - Health) * 2.f * DeltaSeconds)
			{
				SputterTimeLeft[Motor] = FMath::FRandRange(0.3f, 1.5f);
			}
		}
		float& Output = Motor == 1 ? MotorOutputStarboard : MotorOutputPort;
		Output = FMath::FInterpTo(Output, MotorTarget, DeltaSeconds, EngineSpoolRate);
		if (IsMotorRunning(Motor))
		{
			Burn += FMath::Lerp(FuelBurnIdlePerMotor, FuelBurnFullPerMotor, FMath::Pow(GetDriveFraction(Motor), 1.8f));
		}
	}
	EngineOutput = 0.5f * (MotorOutputPort + MotorOutputStarboard);
	FuelLiters = FMath::Max(0.f, FuelLiters - Burn * DeltaSeconds);
}

bool ARiptideBoat::IsPropellerSubmerged() const
{
	return IsPropSubmerged(Propeller) || IsPropSubmerged(PropellerStarboard);
}

bool ARiptideBoat::IsPropSubmerged(const USceneComponent* Prop) const
{
	return Prop == PropellerStarboard ? bPropWet[1] : bPropWet[0];
}

void ARiptideBoat::UpdatePropImmersion()
{
	const USceneComponent* Props[2] = { Propeller, PropellerStarboard };
	for (int32 i = 0; i < 2; ++i)
	{
		// The sea right at the prop, waves included (the stern's buoyancy reading is taken on the centreline, a
		// metre forward, and is only refreshed while the hull overlaps the ocean).
		// It has to stay clear of the surface for a moment before it counts as out: a prop skimming through wave tops
		// at speed would otherwise flick in and out, and the engine note with it.
		const FVector At = Props[i]->GetComponentLocation();
		const float Depth = GetSeaSurfaceZ(At) - At.Z;
		const bool bChanging = bPropWet[i] ? Depth < -PropDryMargin : Depth > 0.f;
		PropStateTime[i] = bChanging ? PropStateTime[i] + GetWorld()->GetDeltaSeconds() : 0.f;
		if (PropStateTime[i] >= 0.12f)
		{
			bPropWet[i] = !bPropWet[i];
			PropStateTime[i] = 0.f;
		}
	}
}

void ARiptideBoat::ApplyThrust()
{
	if (FMath::IsNearlyZero(MotorOutputPort, 0.001f) && FMath::IsNearlyZero(MotorOutputStarboard, 0.001f))
	{
		return;
	}

	// Each motor gives up to half the thrust from its own output, and only while its own prop is in the water: a
	// prop lifting clear on a roll, or a motor dead or sputtering, leaves the other pushing off to one side.

	// Along the prop shafts: steering swings them (right pushes the stern left, turning the bow right), and trim
	// tilts them (out pushes down on the stern, lifting the bow; in pushes it up, holding the bow down).
	const FVector ThrustDir = HullBody->GetComponentQuat() * GetOutboardRotation().RotateVector(FVector::ForwardVector);
	const float ForwardKnots = FVector::DotProduct(HullBody->GetPhysicsLinearVelocity(), HullBody->GetForwardVector()) * CmPerSecToKnots;
	float WetProps = 0.f;
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		const USceneComponent* Prop = Motor == 1 ? PropellerStarboard.Get() : Propeller.Get();
		const float Output = GetMotorOutput(Motor);
		if (IsPropSubmerged(Prop))
		{
			// A prop bites less the faster the water already comes at it (the way it's pushing).
			const float Inflow01 = FMath::Clamp(ForwardKnots * FMath::Sign(Output) / ThrustFalloffKnots, 0.f, 1.f);
			const float ThrustN = 0.5f * MaxThrust * Output * (Output > 0.f ? 1.f : ReverseThrustScale) * (1.f - ThrustFalloff * Inflow01);
			HullBody->AddForceAtLocation(ThrustDir * ThrustN * NewtonsToUnreal, Prop->GetComponentLocation());
			WetProps += 0.5f;
		}
	}

	// Trim's hold on the running attitude, through the hull's planing lift: grows with speed and drive.
	const float SpeedFactor = FMath::Min(FMath::Square(FMath::Max(0.f, ForwardKnots) / TrimFullEffectKnots), 1.2f);
	float MomentNm = (TrimDeg > 0.f ? TrimMomentPerDeg : TrimInMomentPerDeg) * TrimDeg * SpeedFactor * WetProps * FMath::Max(0.f, EngineOutput);
	if (MomentNm > 0.f)
	{
		// Lifting the bow needs the hull's bottom planing on the water to push against. A planing hull runs a few
		// degrees bow-up at most, so the lift fades out between 3 and 7 degrees; without that, a bow trimmed high
		// enough would keep climbing and blow the boat over backwards off a crest.
		const float BowUpDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(HullBody->GetForwardVector().Z, -1.f, 1.f)));
		MomentNm *= 1.f - FMath::SmoothStep(3.f, 7.f, BowUpDeg);
	}
	// Bow up is a torque about the hull's left (-Y) axis. N*m to Unreal's kg*cm^2/s^2 is 100 * 100.
	HullBody->AddTorqueInRadians(-HullBody->GetRightVector() * MomentNm * NewtonsToUnreal * 100.f);
}

void ARiptideBoat::ApplyHydrodynamics()
{
	if (!Buoyancy || !Buoyancy->IsInWaterBody())
	{
		// Airborne off a crest: the air slows the hull's tumbling a little (far less than water does), so it lands
		// roughly as it left instead of cartwheeling.
		HullBody->AddTorqueInRadians(-HullBody->GetPhysicsAngularVelocityInRadians() * AirRockDamping, NAME_None, true);
		return;
	}

	// Water resistance in the hull's frame: along its length the hull's own resistance curve (see
	// GetHullResistanceN), sideways the keel's stubborn quadratic drag.
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector LocalVelMs = Xf.InverseTransformVectorNoScale(HullBody->GetPhysicsLinearVelocity()) / 100.f;

	const FVector LocalDragN(
		-GetHullResistanceN(LocalVelMs.X),
		-LateralDrag * LocalVelMs.Y * FMath::Abs(LocalVelMs.Y),
		0.f);
	HullBody->AddForce(Xf.TransformVectorNoScale(LocalDragN) * NewtonsToUnreal);

	// Heave: the water resists the hull moving up and down through it, so this acts on the hull's vertical speed
	// relative to the sea's own rise and fall under it. (Against its absolute speed, it held the hull up in the air
	// as a swell dropped away beneath it, lifting the props clear.)
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds();
	const float SeaZ = GetSeaSurfaceZ(Xf.GetLocation());
	const float SeaVzMs = bHaveSeaZ && DeltaSeconds > 0.f ? (SeaZ - PrevSeaZ) / DeltaSeconds / 100.f : 0.f;
	PrevSeaZ = SeaZ;
	bHaveSeaZ = true;
	const float RelativeVzMs = HullBody->GetPhysicsLinearVelocity().Z / 100.f - SeaVzMs;
	HullBody->AddForce(FVector::UpVector * -HeaveDamping * RelativeVzMs * NewtonsToUnreal);

	// Planing lift: water striking the forward hull bottom pushes up ahead of the centre of mass,
	// so the bow trims up with speed. Only going forward, and capped so crests don't launch the boat.
	// It only exists where the hull is in the water: scaled by how deep the forward bottom sits compared
	// with its resting draft, so a bow lifting clear (or leaving a crest) loses the lift and settles back.
	if (LocalVelMs.X > 0.f)
	{
		// Where the lift acts moves aft a little as the boat speeds up and rises onto the plane: well forward coming
		// up onto it (lifting the bow over its own bow wave), still about 1.4 m ahead of the centre of gravity at full
		// speed, so the hull runs a couple of degrees bow-up like a real deep-V at neutral trim, rather than flat.
		const float Planing01 = FMath::Clamp((LocalVelMs.X * 1.94384f - 8.f) / 20.f, 0.f, 1.f);
		const FVector LiftPoint = Xf.TransformPosition(FVector(HullExtent.X * FMath::Lerp(0.32f, 0.28f, Planing01), 0.f, -HullExtent.Z));

		float WaterHeightSum = 0.f;
		int32 NumForward = 0;
		for (const FSphericalPontoon& Pontoon : Buoyancy->BuoyancyData.Pontoons)
		{
			if (Pontoon.RelativeLocation.X > 0.f)
			{
				WaterHeightSum += Pontoon.WaterHeight;
				++NumForward;
			}
		}
		const float RestDraft = WaterlineZ + HullExtent.Z;
		const float Wetness = NumForward > 0
			? FMath::Clamp((WaterHeightSum / NumForward - LiftPoint.Z) / RestDraft, 0.f, 1.f)
			: 0.f;

		const float WeightN = HullMassKg * FMath::Abs(GetWorld()->GetGravityZ()) / 100.f;
		const float LiftN = Wetness * FMath::Min(PlaningLift * LocalVelMs.X * LocalVelMs.X, WeightN * MaxPlaningLiftFraction);
		HullBody->AddForceAtLocation(Xf.GetUnitAxis(EAxis::Z) * LiftN * NewtonsToUnreal, LiftPoint);

		// Climbing the hump, the hull sits in the trough of its own wave, bow on the crest and stern in the hollow
		// behind it: the bow rises a few degrees while it gets onto the plane, then drops as it planes off.
		const float Knots = LocalVelMs.X * 1.94384f;
		const float Hump01 = FMath::Exp(-FMath::Square((Knots - HumpKnots) / FMath::Max(HumpWidthKnots, 0.1f)));
		HullBody->AddTorqueInRadians(-Xf.GetUnitAxis(EAxis::Y) * HumpBowRiseNm * Hump01 * Wetness * NewtonsToUnreal * 100.f);
	}

	// A planing hull's pitch stability: as its bow rises past its natural running angle, the pressure on its bottom
	// moves aft and pushes the bow back down, harder the faster it goes. This is what stops an overtrimmed boat
	// standing on its tail.
	if (LocalVelMs.X > 0.f)
	{
		const float BowUpDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Xf.GetUnitAxis(EAxis::X).Z, -1.f, 1.f)));
		const float Excess = BowUpDeg - PlaningRunningTrimDeg;
		if (Excess > 0.f)
		{
			const float Speed01 = FMath::Min(FMath::Square(LocalVelMs.X * 1.94384f / TrimFullEffectKnots), 1.3f);
			const float RestoreNm = PlaningPitchStiffness * Excess * Speed01;
			// Bow down is a torque about the hull's right (+Y) axis.
			HullBody->AddTorqueInRadians(Xf.GetUnitAxis(EAxis::Y) * RestoreNm * NewtonsToUnreal * 100.f);
		}
	}

	// Resist spinning in place, and resist rocking so the hull settles after a wave instead of building up a roll. Roll
	// damps harder on the plane, where the bottom's lift pushes back on whichever side goes down.
	const FVector Up = HullBody->GetUpVector();
	const FVector Fwd = Xf.GetUnitAxis(EAxis::X);
	const FVector AngVel = HullBody->GetPhysicsAngularVelocityInRadians();
	const float YawRate = FVector::DotProduct(AngVel, Up);
	const float RollRate = FVector::DotProduct(AngVel, Fwd);
	const FVector PitchRate = AngVel - Up * YawRate - Fwd * RollRate;
	const float OnPlane01 = FMath::Clamp(LocalVelMs.X * 1.94384f / TrimFullEffectKnots, 0.f, 1.f);
	const float RollDampingNow = FMath::Lerp(RollDamping, RollDampingPlaning, OnPlane01);
	HullBody->AddTorqueInRadians(-Up * YawRate * YawDamping - Fwd * RollRate * RollDampingNow - PitchRate * PitchDamping,
		NAME_None, true);
}

float ARiptideBoat::GetHullResistanceN(float ForwardMs) const
{
	if (ForwardMs < 0.f)
	{
		// Transom first: the flat stern shoves the water ahead of it.
		return -AsternDrag * ForwardMs * ForwardMs;
	}
	// Ahead: friction and spray, plus the wave-making hump on the way onto the plane, easing to the planing drag past it.
	const float Knots = ForwardMs * 1.94384f;
	const float Hump = FMath::Exp(-FMath::Square((Knots - HumpKnots) / FMath::Max(HumpWidthKnots, 0.1f)));
	const float WaveN = Knots <= HumpKnots ? HumpDragN * Hump : PlaningDragN + (HumpDragN - PlaningDragN) * Hump;
	// Fades out at a crawl, so it never holds a boat at rest (the hump curve's tail doesn't quite reach zero).
	return ForwardDrag * ForwardMs * ForwardMs + WaveN * FMath::Clamp(Knots / 2.f, 0.f, 1.f);
}

float ARiptideBoat::GetBowFreeboardCm() const
{
	// The sea right under the stem (the bow pontoons sit well aft of it, where the hull's buoyancy is).
	const FVector BowDeckEdge = HullBody->GetComponentTransform().TransformPosition(FVector(HullExtent.X, 0.f, HullExtent.Z));
	return BowDeckEdge.Z - GetSeaSurfaceZ(BowDeckEdge);
}

float ARiptideBoat::GetSeaSurfaceZ(FVector Location) const
{
	if (!CachedOcean.IsValid() && GetWorld() && GetWorld()->GetTimeSeconds() >= NextOceanSearch)
	{
		NextOceanSearch = GetWorld()->GetTimeSeconds() + 2.0;
		CachedOcean = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass()));
	}
	if (const AWaterBodyOcean* Ocean = CachedOcean.Get())
	{
		const auto Query = Ocean->GetWaterBodyComponent()->TryQueryWaterInfoClosestToWorldLocation(
			Location, EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
		if (Query.HasValue())
		{
			return Query.GetValue().GetWaterSurfaceLocation().Z;
		}
	}
	return 0.f;
}

float ARiptideBoat::GetSpeedKnots() const
{
	// Over the sea, as a GPS reads it: the hull's rise and fall on the swell isn't speed.
	return HullBody->GetComponentVelocity().Size2D() * CmPerSecToKnots;
}

void ARiptideBoat::ApplyEngineDamage(float Amount, int32 Motor)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Motor != 1)
	{
		EngineHealthPort = FMath::Clamp(EngineHealthPort - Amount, 0.f, 1.f);
	}
	if (Motor != 0)
	{
		EngineHealthStarboard = FMath::Clamp(EngineHealthStarboard - Amount, 0.f, 1.f);
	}
}

float ARiptideBoat::AddFuel(float Liters)
{
	if (!HasAuthority())
	{
		return 0.f;
	}
	const float Added = FMath::Clamp(Liters, 0.f, FuelCapacityLiters - FuelLiters);
	FuelLiters += Added;
	return Added;
}

FTransform ARiptideBoat::GetFuelFillerTransform() const
{
	// On the starboard gunwale, aft (riptide_boat_mesh.py's _fittings: station 0.2).
	const FTransform& Xf = HullBody->GetComponentTransform();
	return FTransform(Xf.GetRotation(), Xf.TransformPosition(FVector(-237.f, 121.5f, DeckZ + 56.f)));
}

void ARiptideBoat::SetTrimInput(float Trim)
{
	if (HasAuthority())
	{
		TrimInput = FMath::Clamp(Trim, -1.f, 1.f);
	}
}

void ARiptideBoat::SetHelmInput(float Throttle, float Steer)
{
	if (HasAuthority())
	{
		ThrottleInput = FMath::Clamp(Throttle, -1.f, 1.f);
		SteerInput = FMath::Clamp(Steer, -1.f, 1.f);
	}
}

void ARiptideBoat::DrawDebugHud() const
{
	if (!GEngine)
	{
		return;
	}
	const uint64 KeyBase = 0x52495054ull;
	if (Helmsman)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 3, 0.f, FColor::White, MicHolder && MicHolder == Helmsman ? TEXT("E  Leave the helm      M  Hang up the mic      H  Tuning readout")
			: TEXT("E  Leave the helm      M  Radio mic      H  Tuning readout"));
	}
	if (!bShowDebugHud)
	{
		return;
	}
	GEngine->AddOnScreenDebugMessage(KeyBase + 0, 0.f, FColor::White,
		FString::Printf(TEXT("Speed %.1f kn   Throttle %+.0f%%   Engine %+.0f%%"),
			GetSpeedKnots(), ThrottleLever * 100.f, EngineOutput * 100.f));
	GEngine->AddOnScreenDebugMessage(KeyBase + 1, 0.f, FColor::White,
		FString::Printf(TEXT("Motors %+.0f deg   Trim %+.0f deg (R/F)   Fuel %.1f L   Engine health %.0f%%"),
			SteerAngleDeg, TrimDeg, FuelLiters, GetEngineHealth() * 100.f));
	const bool bPort = IsPropSubmerged(Propeller);
	const bool bStarboard = IsPropSubmerged(PropellerStarboard);
	GEngine->AddOnScreenDebugMessage(KeyBase + 2, 0.f, bPort && bStarboard ? FColor::Green : bPort || bStarboard ? FColor::Yellow : FColor::Red,
		bPort && bStarboard ? TEXT("Props in water") : bPort || bStarboard ? TEXT("One prop out of water") : TEXT("Props out of water"));
}

// --- Props and wheel ---

float ARiptideBoat::GetWheelAngleDeg() const
{
	return SteerAngleDeg * WheelTurnRatio;
}

void ARiptideBoat::UpdatePropsAndWheel(float DeltaSeconds)
{
	// Each prop turns with its motor while in gear: slowly at idle, fast at full throttle, backwards astern, and
	// winding down when the motor's in neutral or stops. Drawn below the real speed (thousands of rpm), which a
	// screen can't show without the blades seeming to stand still or turn backwards: never more than 40 degrees a
	// frame, under half the gap between two of the three blades.
	const ERiptideGear Gear = GetGear();
	const float GearSign = Gear == ERiptideGear::Forward ? 1.f : Gear == ERiptideGear::Reverse ? -1.f : 0.f;
	UStaticMeshComponent* Props[2] = { PropMesh, PropMeshStarboard };
	for (int32 Motor = 0; Motor < 2; ++Motor)
	{
		const float Target = IsMotorRunning(Motor) ? GearSign * (3.f + 6.f * MotorRevs[Motor]) : 0.f;
		PropSpinRate[Motor] = FMath::FInterpTo(PropSpinRate[Motor], Target, DeltaSeconds, 3.f);
		const float MaxStep = 40.f;
		const float Step = FMath::Clamp(PropSpinRate[Motor] * 360.f * DeltaSeconds, -MaxStep, MaxStep);
		// Right-handed props: clockwise seen from astern when going ahead (and the starboard one counter-rotating,
		// as twin installations do, so neither pulls the boat sideways).
		PropAngle[Motor] = FMath::Fmod(PropAngle[Motor] + (Motor == 0 ? Step : -Step), 360.f);
		Props[Motor]->SetRelativeRotation(FRotator(0.f, 0.f, PropAngle[Motor]));
	}
	// The wheel follows the motors (a hydraulic helm): turned right, the top of the wheel goes right.
	WheelMesh->SetRelativeRotation(FRotator(WheelTiltDeg, 180.f, -GetWheelAngleDeg()));
}

// --- The radio's hand mic ---

bool ARiptideBoat::GrabMic(ARiptideCharacter* Crew)
{
	if (!HasAuthority() || !Crew || (MicHolder && MicHolder != Crew))
	{
		return false;
	}
	MicHolder = Crew;
	ApplyMicHolder();
	return true;
}

void ARiptideBoat::HangUpMic()
{
	if (!HasAuthority() || !MicHolder)
	{
		return;
	}
	MicHolder = nullptr;
	ApplyMicHolder();
}

void ARiptideBoat::ServerToggleMic_Implementation()
{
	if (!Helmsman)
	{
		return;
	}
	if (MicHolder == Helmsman)
	{
		HangUpMic();
	}
	else
	{
		GrabMic(Helmsman);
	}
}

void ARiptideBoat::OnRep_MicHolder()
{
	ApplyMicHolder();
}

void ARiptideBoat::ApplyMicHolder()
{
	if (!MicMesh)
	{
		return;
	}
	USceneComponent* Hand = nullptr;
	if (MicHolder)
	{
		// In front of the holder's eyes; the helmsman's eyes are the helm camera.
		Hand = MicHolder == Helmsman ? static_cast<USceneComponent*>(HelmCamera) : MicHolder->GetFirstPersonCamera();
	}
	if (Hand)
	{
		MicMesh->AttachToComponent(Hand, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		MicMesh->SetRelativeLocationAndRotation(MicInHand, MicInHandRotation);
	}
	else
	{
		MicMesh->AttachToComponent(HullBody, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		MicMesh->SetRelativeLocationAndRotation(MicHook, FRotator::ZeroRotator);
	}
	bMicCordAtRest = false;
}

FVector ARiptideBoat::GetMicLocation() const
{
	return MicMesh->GetComponentLocation();
}

FVector ARiptideBoat::GetMicHookLocation() const
{
	return HullBody->GetComponentTransform().TransformPosition(MicHook);
}

/** A coiled cord from A to B (in one frame): a tight coil wound round a line that sags into a loop when the cord is
 * slack and runs straight when it's stretched, the coils drawing out as it stretches. */
static void BuildCoiledCord(const FVector& A, const FVector& B, float RestLength, float Reach, TArray<FVector>& Verts,
	TArray<int32>& Tris, TArray<FVector>& Normals)
{
	const float Dist = FVector::Dist(A, B);
	const float Slack = FMath::Max(0.f, RestLength - Dist);
	const float Stretch = FMath::Clamp((Dist - RestLength) / FMath::Max(Reach - RestLength, 1.f), 0.f, 1.f);
	// The slack hangs in a loop below the ends (a quadratic curve whose middle sags by about half the slack).
	const FVector Mid = (A + B) * 0.5f - FVector::UpVector * (Slack * 0.9f + 3.f * (1.f - Stretch));
	auto Line = [&](float T) { return FMath::Lerp(FMath::Lerp(A, Mid, T), FMath::Lerp(Mid, B, T), T); };

	constexpr int32 Turns = 34;
	constexpr int32 PerTurn = 8;
	constexpr int32 Samples = Turns * PerTurn;
	constexpr int32 Sides = 5;
	const float CoilRadius = FMath::Lerp(0.95f, 0.45f, Stretch);
	const float WireRadius = 0.3f;

	TArray<FVector> Coil;
	Coil.SetNum(Samples + 1);
	FVector Ref = FVector::UpVector;
	for (int32 i = 0; i <= Samples; ++i)
	{
		const float T = float(i) / Samples;
		const FVector P = Line(T);
		const FVector D = (Line(FMath::Min(1.f, T + 0.01f)) - Line(FMath::Max(0.f, T - 0.01f))).GetSafeNormal();
		if (FMath::Abs(FVector::DotProduct(D, Ref)) > 0.95f)
		{
			Ref = FVector::ForwardVector;
		}
		const FVector U = FVector::CrossProduct(D, Ref).GetSafeNormal();
		const FVector W = FVector::CrossProduct(D, U);
		// Straight for the last centimetre or so at each end, where it plugs in.
		const float EndTaper = FMath::SmoothStep(0.f, 0.04f, T) * FMath::SmoothStep(0.f, 0.04f, 1.f - T);
		const float Phase = UE_TWO_PI * i / PerTurn;
		Coil[i] = P + (U * FMath::Cos(Phase) + W * FMath::Sin(Phase)) * CoilRadius * EndTaper;
	}
	Verts.Reset();
	Tris.Reset();
	Normals.Reset();
	for (int32 i = 0; i <= Samples; ++i)
	{
		const FVector D = (Coil[FMath::Min(i + 1, Samples)] - Coil[FMath::Max(i - 1, 0)]).GetSafeNormal();
		const FVector Side = FMath::Abs(D.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		const FVector U = FVector::CrossProduct(D, Side).GetSafeNormal();
		const FVector W = FVector::CrossProduct(D, U);
		for (int32 k = 0; k < Sides; ++k)
		{
			const float Angle = UE_TWO_PI * k / Sides;
			const FVector N = U * FMath::Cos(Angle) + W * FMath::Sin(Angle);
			Verts.Add(Coil[i] + N * WireRadius);
			Normals.Add(N);
		}
		if (i > 0)
		{
			const int32 Base = (i - 1) * Sides;
			for (int32 k = 0; k < Sides; ++k)
			{
				const int32 A0 = Base + k, A1 = Base + (k + 1) % Sides, B0 = A0 + Sides, B1 = A1 + Sides;
				Tris.Append({ A0, B0, A1, A1, B0, B1 });
			}
		}
	}
}

void ARiptideBoat::UpdateMicCord()
{
	if (!FApp::CanEverRender() || !MicCord)
	{
		return;
	}
	// Hanging on its clip the cord doesn't move (it's in the boat's frame): drawn once. In a hand it's redrawn every
	// frame between the radio and the mic.
	if (!MicHolder && bMicCordAtRest)
	{
		return;
	}
	const FTransform& Boat = HullBody->GetComponentTransform();
	const FVector Exit = Boat.InverseTransformPosition(MicMesh->GetComponentTransform().TransformPosition(MicCordExit));
	BuildCoiledCord(MicCordJack, Exit, MicCordRestLength, MicCordReach, CordVerts, CordTris, CordNormals);
	if (MicCord->GetNumSections() == 0)
	{
		MicCord->CreateMeshSection(0, CordVerts, CordTris, CordNormals, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	}
	else
	{
		MicCord->UpdateMeshSection(0, CordVerts, CordNormals, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>());
	}
	bMicCordAtRest = !MicHolder;
}

void ARiptideBoat::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Sim = WakeSimulation.Get())
	{
		Sim->Destroy();
	}
	LadderUser = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ARiptideBoat::RescueIfOffTheSea()
{
	// Twenty metres under sea level, the boat has left the sea: driven off its edge, where there's no water to float
	// on, it falls through the world. Put it back on the water just inside the edge, upright and stopped, with its
	// crew aboard where they were standing.
	const FVector Here = GetActorLocation();
	if (Here.Z > -2000.f)
	{
		return;
	}
	FVector Target = Here;
	if (!CachedOcean.IsValid())
	{
		CachedOcean = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass()));
	}
	if (const AWaterBodyOcean* Ocean = CachedOcean.Get())
	{
		if (const UWaterBodyComponent* Body = Ocean->GetWaterBodyComponent())
		{
			const FVector Centre = Ocean->GetActorLocation();
			const FVector Reach = Body->GetCollisionExtents() * 0.95f;
			Target.X = FMath::Clamp(Target.X, Centre.X - Reach.X, Centre.X + Reach.X);
			Target.Y = FMath::Clamp(Target.Y, Centre.Y - Reach.Y, Centre.Y + Reach.Y);
		}
	}
	Target.Z = GetSeaSurfaceZ(FVector(Target.X, Target.Y, 0.f)) + 15.f;
	const FRotator Upright(0.f, GetActorRotation().Yaw, 0.f);
	const FTransform Old = GetActorTransform();
	const FTransform New(Upright, Target);
	for (ARiptideCharacter* Crew : TActorRange<ARiptideCharacter>(GetWorld()))
	{
		if (Crew->GetHomeBoat() == this && !Crew->IsInSea())
		{
			Crew->SetActorLocation(New.TransformPosition(Old.InverseTransformPosition(Crew->GetActorLocation())) + FVector(0.f, 0.f, 5.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			Crew->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		}
	}
	SetActorLocationAndRotation(Target, Upright, false, nullptr, ETeleportType::ResetPhysics);
	SlapCooldownLeft = SettleSeconds;
	bHaveBowFreeboard = false;
	HullBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
	HullBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	UE_LOG(LogRiptideBoat, Warning, TEXT("%s went off the edge of the sea at %s: back on the water at %s"), *GetName(), *Here.ToString(), *Target.ToString());
}
