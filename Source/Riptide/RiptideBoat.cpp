#include "RiptideBoat.h"

#include "BuoyancyComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
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
#include "RiptideSprayComponent.h"
#include "RiptideStorageComponent.h"
#include "RiptideWakeFoamComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanActor.h"
#include "Net/UnrealNetwork.h"
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

	// Buoyancy pontoons, in cm. They sit only a quarter of their height in the water at rest, so the hull
	// has about six times its resting lift in reserve: a bow driven into a swell gets pushed back up hard
	// instead of burying. The waterline sits 20 cm up the hull.
	constexpr float PontoonRadius = 60.f;
	constexpr float PontoonRestDepth = 30.f;

	// The outboards' steering pivots on the transom (twin motors 76 cm apart), and the prop relative to a pivot
	// (see build_outboard in Content/Python/riptide_boat_mesh.py).
	// Each tilts on the tube at the top of its clamp bracket, which hooks over the transom's motor notch.
	const FVector OutboardPivot(-HullExtent.X - 5.f, -38.f, 40.f);
	const FVector OutboardPivotStarboard(-HullExtent.X - 5.f, 38.f, 40.f);
	const FVector PropInOutboard(-45.f, 0.f, -100.f);
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
	const FVector BowLightPoint(383.f, 0.f, DeckZ + 104.f);

	// The radar antenna's hub, on its pedestal on the T-top (riptide_boat_mesh.py's RADAR).
	const FVector RadarHub(-70.f, 0.f, DeckZ + 258.f);

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
		Light->SetCastShadows(false);
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
	for (UStaticMeshComponent* Part : { MotorBracket.Get(), MotorBracketStarboard.Get(), ThrottleLeverPort.Get(), ThrottleLeverStarboard.Get(), RadarArray.Get() })
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
	LeverModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_ThrottleLever.SM_ThrottleLever")));
	RadarModel = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Riptide/Boats/SM_RadarArray.SM_RadarArray")));

	// Propellers: at each outboard's prop, well below the waterline when the boat is level.
	Propeller = CreateDefaultSubobject<USceneComponent>(TEXT("Propeller"));
	Propeller->SetupAttachment(HullBody);
	Propeller->SetRelativeLocation(OutboardPivot + PropInOutboard);
	PropellerStarboard = CreateDefaultSubobject<USceneComponent>(TEXT("PropellerStarboard"));
	PropellerStarboard->SetupAttachment(HullBody);
	PropellerStarboard->SetRelativeLocation(OutboardPivotStarboard + PropInOutboard);

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

	// Pontoons run down both sides, where the hull's width resists rolling, plus one at the stern
	// that also tells us if the prop is wet. BeginPlay sizes their lift so they float PontoonRestDepth deep.
	Buoyancy = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("Buoyancy"));
	const float PontoonZ = WaterlineZ - PontoonRestDepth + PontoonRadius;
	const float ChineY = HullExtent.Y * 0.6f;
	const FVector PontoonOffsets[] = {
		FVector(HullExtent.X * 0.8f, ChineY, PontoonZ),
		FVector(HullExtent.X * 0.8f, -ChineY, PontoonZ),
		FVector(HullExtent.X * 0.27f, ChineY, PontoonZ),
		FVector(HullExtent.X * 0.27f, -ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.27f, ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.27f, -ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.8f, ChineY, PontoonZ),
		FVector(-HullExtent.X * 0.8f, -ChineY, PontoonZ),
		FVector(-HullExtent.X, 0.f, PontoonZ),
	};
	for (const FVector& Offset : PontoonOffsets)
	{
		FSphericalPontoon Pontoon;
		Pontoon.RelativeLocation = Offset;
		Pontoon.Radius = PontoonRadius;
		Buoyancy->BuoyancyData.Pontoons.Add(Pontoon);
	}
	SternPontoonIndex = Buoyancy->BuoyancyData.Pontoons.Num() - 1;
	BowPontoonIndex = 0;
}

void ARiptideBoat::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyModels();
}

void ARiptideBoat::ApplyModels()
{
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
	if (UStaticMesh* Head = SearchlightModel.LoadSynchronous())
	{
		SearchlightHead->SetStaticMesh(Head);
	}
	if (UStaticMesh* Radar = RadarModel.LoadSynchronous())
	{
		RadarArray->SetStaticMesh(Radar);
	}
	if (UStaticMesh* Lever = LeverModel.LoadSynchronous())
	{
		ThrottleLeverPort->SetStaticMesh(Lever);
		ThrottleLeverStarboard->SetStaticMesh(Lever);
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
	if (HasAuthority())
	{
		FuelLiters = FuelCapacityLiters;
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

float ARiptideBoat::WaterlineHalfBeam(float X)
{
	// The hull's planform (riptide_boat_mesh.py's station): full beam aft, narrowing to the stem; the waterline runs
	// a little inside the sheer.
	const float T = FMath::Clamp((X + HullExtent.X) / (2.f * HullExtent.X), 0.f, 1.f);
	const float SheerHalf = T < 0.35f ? HullExtent.Y : HullExtent.Y * FMath::Pow(FMath::Max(0.f, FMath::Cos((T - 0.35f) / 0.65f * UE_HALF_PI)), 0.75f);
	return SheerHalf * 0.82f;
}

void ARiptideBoat::SprayAtBow(float Strength)
{
	if (!Spray || !FApp::CanEverRender())
	{
		return;
	}
	// The bow slamming into a wave: water bursts out of both sides of the forward hull, crashing outward and up in
	// a sheet that breaks into droplets, with a cloud of mist hanging behind it. Bigger and higher for a harder hit
	// (and faster boat); carried forward with the boat.
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector HullVelocity = HullBody->GetPhysicsLinearVelocity();
	const float Speed01 = FMath::Clamp(GetSpeedKnots() / SprayFullKnots, 0.f, 1.2f);
	const float Power = FMath::Clamp(Strength * (0.4f + 0.8f * Speed01), 0.f, 1.2f);
	const FVector Right = Xf.GetUnitAxis(EAxis::Y);
	for (const float Side : { -1.f, 1.f })
	{
		for (int32 Point = 0; Point < 8; ++Point)
		{
			const float X = FMath::Lerp(100.f, 270.f, Point / 7.f);
			const FVector Origin = Xf.TransformPosition(FVector(X, Side * WaterlineHalfBeam(X), WaterlineZ + 8.f));
			const float SeaZ = GetSeaSurfaceZ(Origin);
			// Further forward, the water is thrown higher and flatter out.
			const float Fwd = Point / 7.f;
			const FVector Throw = HullVelocity * 0.8f + Right * Side * FMath::Lerp(500.f, 1300.f, Power)
				+ FVector::UpVector * FMath::Lerp(500.f, 1400.f, Power) * FMath::Lerp(0.7f, 1.1f, Fwd);
			Spray->ThrowSpray(Origin, Throw, FMath::Lerp(250.f, 500.f, Power), FMath::RoundToInt(FMath::Lerp(30.f, 90.f, Power)),
				10.f, FMath::Lerp(28.f, 45.f, Power), FMath::Lerp(1.2f, 2.0f, Power), SeaZ, 1.f, 0.05f);
			if (Point % 2 == 0)
			{
				Spray->ThrowSpray(Origin, HullVelocity * 0.55f + Right * Side * 350.f + FVector::UpVector * FMath::Lerp(300.f, 700.f, Power),
					200.f, FMath::RoundToInt(FMath::Lerp(1.f, 3.f, Power)), 100.f, FMath::Lerp(220.f, 380.f, Power), 2.0f, SeaZ, 0.15f, 0.f);
			}
		}
	}
}

void ARiptideBoat::UpdateSpray(float DeltaSeconds)
{
	if (!Spray || !FApp::CanEverRender() || !Buoyancy || !Buoyancy->IsInWaterBody())
	{
		return;
	}
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector HullVelocity = HullBody->GetPhysicsLinearVelocity();
	const float ForwardKnots = FVector::DotProduct(HullVelocity, Xf.GetUnitAxis(EAxis::X)) * CmPerSecToKnots;
	const float Speed01 = FMath::Clamp((ForwardKnots - SprayStartKnots) / (SprayFullKnots - SprayStartKnots), 0.f, 1.2f);
	if (Speed01 <= 0.f)
	{
		BowSprayOwed = 0.f;
		SternSprayOwed = 0.f;
		return;
	}
	const FVector Right = Xf.GetUnitAxis(EAxis::Y);

	// Bow: the bow wave peels off the forward hull in a sheet, thrown out sideways and up by the strakes and chines.
	// Heavier with speed, and heaviest while the bow is driving down into the water.
	const float Driving = FMath::Clamp(-HullVelocity.Z / 150.f, 0.f, 1.f);
	BowSprayOwed += BowSprayRate * FMath::Pow(Speed01, 1.5f) * (1.f + 2.f * Driving) * DeltaSeconds;
	int32 Bow = FMath::FloorToInt(BowSprayOwed);
	BowSprayOwed -= Bow;
	for (; Bow > 0; Bow -= 4)
	{
		for (const float Side : { -1.f, 1.f })
		{
			const float X = FMath::FRandRange(90.f, 230.f);
			const FVector Origin = Xf.TransformPosition(FVector(X, Side * WaterlineHalfBeam(X), WaterlineZ + 8.f));
			// A flat fan thrown out wide and a metre or so up, arcing back down into the sea.
			const FVector Throw = HullVelocity * 0.88f + Right * Side * FMath::Lerp(500.f, 900.f, Speed01)
				+ FVector::UpVector * FMath::Lerp(280.f, 560.f, Speed01) * (1.f + 0.8f * Driving);
			Spray->ThrowSpray(Origin, Throw, 220.f, FMath::Min(Bow, 4), 9.f, FMath::Lerp(22.f, 32.f, Speed01), 1.0f,
				GetSeaSurfaceZ(Origin), 1.f, 0.05f);
			if (FMath::FRand() < 0.04f)
			{
				Spray->ThrowSpray(Origin, HullVelocity * 0.6f + Right * Side * 300.f + FVector::UpVector * 200.f, 150.f, 1,
					60.f, 170.f, 1.4f, GetSeaSurfaceZ(Origin), 0.12f, 0.f);
			}
		}
	}

	// Stern: the props churn the water behind the transom into tumbling whitewater and mist, left behind the boat.
	SternSprayOwed += SternSprayRate * Speed01 * GetDriveFraction() * DeltaSeconds;
	int32 Stern = FMath::FloorToInt(SternSprayOwed);
	SternSprayOwed -= Stern;
	for (; Stern > 0; --Stern)
	{
		const float Y = FMath::FRandRange(-100.f, 100.f);
		const FVector Origin = Xf.TransformPosition(FVector(-HullExtent.X - FMath::FRandRange(40.f, 120.f), Y, WaterlineZ + 5.f));
		const FVector Throw = HullVelocity * 0.35f + Right * Y * 2.f + FVector::UpVector * FMath::FRandRange(150.f, 380.f);
		Spray->ThrowSpray(Origin, Throw, 140.f, 1, 12.f, 40.f, 0.8f, GetSeaSurfaceZ(Origin), 0.9f, 0.03f);
		if (FMath::FRand() < 0.05f)
		{
			Spray->ThrowSpray(Origin, HullVelocity * 0.3f + FVector::UpVector * 150.f, 100.f, 1, 80.f, 220.f, 1.5f,
				GetSeaSurfaceZ(Origin), 0.12f, 0.f);
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

	// Engine: runs while it has fuel and isn't dead. Pitch and volume follow the actual engine output (so a
	// sputter is heard as a dip), and it races when the prop leaves the water and loses its load.
	const bool bRunning = FuelLiters > 0.f && EngineHealth > 0.f;
	if (bRunning != bEngineSoundRunning)
	{
		bEngineSoundRunning = bRunning;
		for (UAudioComponent* Layer : { EngineAudio.Get(), EngineHighAudio.Get(), EngineAudioStarboard.Get(), EngineHighAudioStarboard.Get() })
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
	const float Output = GetDriveFraction();
	const float TargetRevs = FMath::Min(1.f, Output * (IsPropellerSubmerged() ? 1.f : 1.f + PropOutOverRev));
	EngineRevs = FMath::FInterpTo(EngineRevs, TargetRevs, DeltaSeconds, 6.f);

	// Engine speed moves between idle and full on a musical (log) scale. Each recording is pitched to that speed,
	// and the two crossfade at equal power by where the speed sits between them.
	const float Hz = EngineIdleHz * FMath::Pow(EngineFullHz / EngineIdleHz, EngineRevs);
	const float HighWeight = FMath::Clamp(
		FMath::Loge(Hz / EngineLowRecordingHz) / FMath::Loge(EngineHighRecordingHz / EngineLowRecordingHz), 0.f, 1.f);
	// Each engine at half power (-3 dB), so the pair adds up to the level the single engine was measured at.
	const float Volume = FMath::Lerp(EngineIdleVolume, 1.f, Output) * UE_INV_SQRT_2;
	const float LowGain = Volume * FMath::Max(0.f, FMath::Cos(HighWeight * UE_HALF_PI));
	const float HighGain = Volume * FMath::Max(0.f, FMath::Sin(HighWeight * UE_HALF_PI));
	EngineAudio->SetPitchMultiplier(Hz / EngineLowRecordingHz);
	EngineHighAudio->SetPitchMultiplier(Hz / EngineHighRecordingHz);
	EngineAudioStarboard->SetPitchMultiplier(StarboardEngineDetune * Hz / EngineLowRecordingHz);
	EngineHighAudioStarboard->SetPitchMultiplier(StarboardEngineDetune * Hz / EngineHighRecordingHz);
	EngineAudio->SetVolumeMultiplier(LowGain);
	EngineAudioStarboard->SetVolumeMultiplier(LowGain);
	EngineHighAudio->SetVolumeMultiplier(HighGain);
	EngineHighAudioStarboard->SetVolumeMultiplier(HighGain);

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
	DOREPLIFETIME(ARiptideBoat, TrimDeg);
	DOREPLIFETIME(ARiptideBoat, FuelLiters);
	DOREPLIFETIME(ARiptideBoat, EngineHealth);
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
		Input->BindActionValueLambda(DebugHudAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bShowDebugHud = !bShowDebugHud; });
		Input->BindActionValueLambda(SearchlightAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleLight(0); });
		Input->BindActionValueLambda(NavLightsAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleLight(1); });
		Input->BindActionValueLambda(DeckLightsAction, ETriggerEvent::Started, [this](const FInputActionValue&) { ServerToggleLight(2); });
		Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnThrottle);
		Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnThrottleReleased);
		Input->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ARiptideBoat::OnSteer);
		Input->BindAction(SteerAction, ETriggerEvent::Completed, this, &ARiptideBoat::OnSteerReleased);
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
	bCutThrottleRequested = true;
}

void ARiptideBoat::OnLook(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>();
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
		ServerLeaveHelm();
	}
}

void ARiptideBoat::ServerLeaveHelm_Implementation()
{
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
	if (HasAuthority())
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
	UE_LOG(LogRiptideBoat, Log, TEXT("%s left the helm of %s"), *Crew->GetName(), *GetName());
}

void ARiptideBoat::ServerSetControls_Implementation(float InThrottleInput, float InSteerInput, float InTrimInput, bool bInCutThrottle)
{
	ThrottleInput = FMath::Clamp(InThrottleInput, -1.f, 1.f);
	SteerInput = FMath::Clamp(InSteerInput, -1.f, 1.f);
	TrimInput = FMath::Clamp(InTrimInput, -1.f, 1.f);
	bCutThrottleRequested |= bInCutThrottle;
}

// --- Simulation ---

void ARiptideBoat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled() && !HasAuthority())
	{
		ServerSetControls(ThrottleInput, SteerInput, TrimInput, bCutThrottleRequested);
		bCutThrottleRequested = false;
	}

	UpdatePropImmersion();

	if (HasAuthority())
	{
		UpdateControls(DeltaSeconds);
		UpdateEngine(DeltaSeconds);
		ApplyThrust();
		ApplyHydrodynamics();
	}

	PoseOutboards();
	UpdateSearchlight(DeltaSeconds);

	// The radar sweeps while the engines are running.
	if (GetEngineRpm() > 0.f)
	{
		RadarArray->AddLocalRotation(FRotator(0.f, RadarRpm * 6.f * DeltaSeconds, 0.f));
	}
	// Throttle levers: forward for ahead, back for astern.
	ThrottleLeverPort->SetRelativeRotation(FRotator(-ThrottleLever * LeverSwingDeg, 0.f, 0.f));
	ThrottleLeverStarboard->SetRelativeRotation(FRotator(-ThrottleLever * LeverSwingDeg, 0.f, 0.f));

	UpdateSounds(DeltaSeconds);
	UpdateWakeFoam(DeltaSeconds);
	UpdateSpray(DeltaSeconds);

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
	Propeller->SetRelativeLocation(OutboardPivot + Pose.RotateVector(PropInOutboard));
	PropellerStarboard->SetRelativeLocation(OutboardPivotStarboard + Pose.RotateVector(PropInOutboard));
}

ERiptideGear ARiptideBoat::GetGear() const
{
	return ThrottleLever > NeutralDetent ? ERiptideGear::Forward : ThrottleLever < -NeutralDetent ? ERiptideGear::Reverse : ERiptideGear::Neutral;
}

float ARiptideBoat::GetThrottleOpening() const
{
	return FMath::Clamp((FMath::Abs(ThrottleLever) - NeutralDetent) / (1.f - NeutralDetent), 0.f, 1.f);
}

float ARiptideBoat::GetDriveFraction() const
{
	return FMath::Clamp((FMath::Abs(EngineOutput) - IdleThrustInGear) / (1.f - IdleThrustInGear), 0.f, 1.f);
}

float ARiptideBoat::GetEngineRpm() const
{
	if (FuelLiters <= 0.f || EngineHealth <= 0.f)
	{
		return 0.f;
	}
	return FMath::Lerp(IdleRpm, MaxRpm, EngineRevs);
}

void ARiptideBoat::UpdateEngine(float DeltaSeconds)
{
	// The lever sets the gear and throttle together, like a real binnacle control: in gear at idle the props turn
	// slowly and the boat creeps along; the throttle opens from there. In neutral the props don't drive at all.
	const ERiptideGear Gear = GetGear();
	const float GearSign = Gear == ERiptideGear::Forward ? 1.f : Gear == ERiptideGear::Reverse ? -1.f : 0.f;
	float Target = GearSign * FMath::Lerp(IdleThrustInGear, 1.f, GetThrottleOpening());

	if (FuelLiters <= 0.f || EngineHealth <= 0.f)
	{
		Target = 0.f;
	}
	else if (EngineHealth < 0.5f)
	{
		// A damaged engine cuts out at random, more often the worse it is.
		if (SputterTimeLeft > 0.f)
		{
			SputterTimeLeft -= DeltaSeconds;
			Target = 0.f;
		}
		else if (FMath::FRand() < (0.5f - EngineHealth) * 2.f * DeltaSeconds)
		{
			SputterTimeLeft = FMath::FRandRange(0.3f, 1.5f);
		}
	}

	EngineOutput = FMath::FInterpTo(EngineOutput, Target, DeltaSeconds, EngineSpoolRate);
	FuelLiters = FMath::Max(0.f, FuelLiters - FMath::Abs(EngineOutput) * FuelBurnPerSecond * DeltaSeconds);
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
	if (FMath::IsNearlyZero(EngineOutput, 0.001f))
	{
		return;
	}

	// Each motor gives half the thrust, and only while its own prop is in the water: a prop lifting clear on a
	// roll loses its half, and the other motor's push, off to one side, slews the stern.
	const float Scale = EngineOutput > 0.f ? 1.f : ReverseThrustScale;
	const float ThrustN = 0.5f * MaxThrust * EngineOutput * Scale;

	// Along the prop shafts: steering swings them (right pushes the stern left, turning the bow right), and trim
	// tilts them (out pushes down on the stern, lifting the bow; in pushes it up, holding the bow down).
	const FVector ThrustDir = HullBody->GetComponentQuat() * GetOutboardRotation().RotateVector(FVector::ForwardVector);
	float WetProps = 0.f;
	for (const USceneComponent* Prop : { Propeller.Get(), PropellerStarboard.Get() })
	{
		if (IsPropSubmerged(Prop))
		{
			HullBody->AddForceAtLocation(ThrustDir * ThrustN * NewtonsToUnreal, Prop->GetComponentLocation());
			WetProps += 0.5f;
		}
	}

	// Trim's hold on the running attitude, through the hull's planing lift: grows with speed and drive.
	const float ForwardKnots = FVector::DotProduct(HullBody->GetPhysicsLinearVelocity(), HullBody->GetForwardVector()) * CmPerSecToKnots;
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

	// Quadratic water drag in the hull's frame: slippery going forward, stubborn going sideways.
	const FTransform& Xf = HullBody->GetComponentTransform();
	const FVector LocalVelMs = Xf.InverseTransformVectorNoScale(HullBody->GetPhysicsLinearVelocity()) / 100.f;

	const FVector LocalDragN(
		-ForwardDrag * LocalVelMs.X * FMath::Abs(LocalVelMs.X),
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
		// Where the lift acts moves aft as the boat speeds up and rises onto the plane: well forward coming up onto
		// the plane (lifting the bow over its own bow wave), close over the centre of gravity at full speed, so the
		// hull settles to running a few degrees bow-up instead of standing on its tail.
		const float Planing01 = FMath::Clamp((LocalVelMs.X * 1.94384f - 8.f) / 20.f, 0.f, 1.f);
		const FVector LiftPoint = Xf.TransformPosition(FVector(HullExtent.X * FMath::Lerp(0.3f, -0.04f, Planing01), 0.f, -HullExtent.Z));

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

	// Resist spinning in place, and resist rocking so the hull settles after a wave instead of building up a roll.
	const FVector Up = HullBody->GetUpVector();
	const FVector AngVel = HullBody->GetPhysicsAngularVelocityInRadians();
	const float YawRate = FVector::DotProduct(AngVel, Up);
	const FVector RockRate = AngVel - Up * YawRate;
	HullBody->AddTorqueInRadians(-Up * YawRate * YawDamping - RockRate * RockDamping, NAME_None, true);
}

float ARiptideBoat::GetBowFreeboardCm() const
{
	if (!Buoyancy || !Buoyancy->BuoyancyData.Pontoons.IsValidIndex(BowPontoonIndex))
	{
		return 0.f;
	}
	const FSphericalPontoon& Bow = Buoyancy->BuoyancyData.Pontoons[BowPontoonIndex];
	const FVector BowDeckEdge = HullBody->GetComponentTransform().TransformPosition(FVector(HullExtent.X, 0.f, HullExtent.Z));
	return BowDeckEdge.Z - Bow.WaterHeight;
}

float ARiptideBoat::GetSeaSurfaceZ(FVector Location) const
{
	if (const AWaterBodyOcean* Ocean = Cast<AWaterBodyOcean>(UGameplayStatics::GetActorOfClass(this, AWaterBodyOcean::StaticClass())))
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
	return HullBody->GetComponentVelocity().Size() * CmPerSecToKnots;
}

void ARiptideBoat::ApplyEngineDamage(float Amount)
{
	if (HasAuthority())
	{
		EngineHealth = FMath::Clamp(EngineHealth - Amount, 0.f, 1.f);
	}
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
		GEngine->AddOnScreenDebugMessage(KeyBase + 3, 0.f, FColor::White, TEXT("E  Leave the helm      H  Tuning readout"));
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
			SteerAngleDeg, TrimDeg, FuelLiters, EngineHealth * 100.f));
	const bool bPort = IsPropSubmerged(Propeller);
	const bool bStarboard = IsPropSubmerged(PropellerStarboard);
	GEngine->AddOnScreenDebugMessage(KeyBase + 2, 0.f, bPort && bStarboard ? FColor::Green : bPort || bStarboard ? FColor::Yellow : FColor::Red,
		bPort && bStarboard ? TEXT("Props in water") : bPort || bStarboard ? TEXT("One prop out of water") : TEXT("Props out of water"));
}

