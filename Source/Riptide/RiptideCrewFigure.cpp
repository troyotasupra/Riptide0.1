#include "RiptideCrewFigure.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RiptideCrewBody.h"
#include "RiptideCrewMannequin.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	enum EShape : int32 { Cube, Sphere, Cylinder };

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}

	/** Each uniform's overall colour, as it reads from a few metres (the patterns come with the real uniform). */
	FLinearColor UniformColour(int32 Camo)
	{
		static const FLinearColor Colours[] = { Srgb(110, 100, 72), Srgb(70, 76, 50), Srgb(170, 146, 108), Srgb(102, 104, 106),
			Srgb(78, 82, 54), Srgb(22, 22, 24) };
		return Colours[FMath::Clamp(Camo, 0, int32(UE_ARRAY_COUNT(Colours)) - 1)];
	}
}

// --- The placeholder figure ---

ARiptideCrewFigurePlaceholder::ARiptideCrewFigurePlaceholder()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Sizes in cm from the feet up, facing +X; the basic shapes are 1 m across, centred on their middle.
	Part(TEXT("LegL"), Cylinder, FVector(0, -10, 45), FVector(0.15f, 0.15f, 0.86f));
	Part(TEXT("LegR"), Cylinder, FVector(0, 10, 45), FVector(0.15f, 0.15f, 0.86f));
	Part(TEXT("BootL"), Cube, FVector(4, -10, 5), FVector(0.28f, 0.13f, 0.1f));
	Part(TEXT("BootR"), Cube, FVector(4, 10, 5), FVector(0.28f, 0.13f, 0.1f));
	Part(TEXT("Hips"), Cube, FVector(0, 0, 92), FVector(0.22f, 0.34f, 0.16f));
	Part(TEXT("Torso"), Cylinder, FVector(0, 0, 123), FVector(0.24f, 0.38f, 0.5f));
	Part(TEXT("ShoulderL"), Sphere, FVector(0, -19, 145), FVector(0.15f));
	Part(TEXT("ShoulderR"), Sphere, FVector(0, 19, 145), FVector(0.15f));
	Part(TEXT("Neck"), Cylinder, FVector(0, 0, 155), FVector(0.11f, 0.11f, 0.1f));
	Part(TEXT("Head"), Sphere, FVector(1, 0, 169), FVector(0.2f, 0.19f, 0.23f));
	// Arms: upper arms hang at the sides, forearms come forward to the rifle held across the body (low ready).
	Part(TEXT("UpperArmL"), Cylinder, FVector(0, -23, 130), FVector(0.1f, 0.1f, 0.3f), FRotator(0, 0, -10));
	Part(TEXT("UpperArmR"), Cylinder, FVector(0, 23, 130), FVector(0.1f, 0.1f, 0.3f), FRotator(0, 0, 10));
	Part(TEXT("ForearmL"), Cylinder, FVector(11, -17, 114), FVector(0.085f, 0.085f, 0.28f), FRotator(-70, 25, 0));
	Part(TEXT("ForearmR"), Cylinder, FVector(10, 17, 110), FVector(0.085f, 0.085f, 0.28f), FRotator(-75, -30, 0));
	Part(TEXT("HandL"), Sphere, FVector(23, -9, 119), FVector(0.08f));
	Part(TEXT("HandR"), Sphere, FVector(22, 8, 106), FVector(0.08f));
	// The rifle, muzzle down and to the left.
	Part(TEXT("Rifle"), Cube, FVector(26, -2, 112), FVector(0.05f, 0.62f, 0.07f), FRotator(0, 0, -28));
	Part(TEXT("Barrel"), Cylinder, FVector(26, -40, 92), FVector(0.025f, 0.025f, 0.3f), FRotator(0, 0, 62));
	Part(TEXT("Magazine"), Cube, FVector(27, -6, 103), FVector(0.035f, 0.05f, 0.16f), FRotator(0, 0, -15));
	Part(TEXT("Stock"), Cube, FVector(24, 26, 125), FVector(0.05f, 0.14f, 0.11f), FRotator(0, 0, -28));
	// Gear.
	Part(TEXT("Vest"), Cube, FVector(0, 0, 127), FVector(0.31f, 0.41f, 0.34f));
	Part(TEXT("VestPouches"), Cube, FVector(16, 0, 116), FVector(0.06f, 0.33f, 0.1f));
	Part(TEXT("ChestRig"), Cube, FVector(13, 0, 117), FVector(0.1f, 0.36f, 0.16f));
	Part(TEXT("PackAssault"), Cube, FVector(-21, 0, 128), FVector(0.18f, 0.32f, 0.42f));
	Part(TEXT("PackHydration"), Cube, FVector(-15, 0, 131), FVector(0.07f, 0.24f, 0.38f));
	Part(TEXT("Helmet"), Sphere, FVector(0, 0, 175), FVector(0.25f, 0.235f, 0.17f));
	Part(TEXT("HelmetMount"), Cube, FVector(11, 0, 180), FVector(0.04f, 0.05f, 0.05f));
	Part(TEXT("BoonieBrim"), Cylinder, FVector(0, 0, 174), FVector(0.44f, 0.44f, 0.015f));
	Part(TEXT("BoonieCrown"), Cylinder, FVector(0, 0, 179), FVector(0.22f, 0.22f, 0.1f));
	Part(TEXT("Cap"), Sphere, FVector(0, 0, 175), FVector(0.215f, 0.205f, 0.13f));
	Part(TEXT("CapBrim"), Cube, FVector(12, 0, 172), FVector(0.12f, 0.15f, 0.012f));
	Part(TEXT("Beanie"), Sphere, FVector(0, 0, 176), FVector(0.215f, 0.205f, 0.17f));
	Part(TEXT("Hair"), Sphere, FVector(-1, 0, 172), FVector(0.208f, 0.198f, 0.18f));
	Part(TEXT("HairLong"), Cube, FVector(-9, 0, 158), FVector(0.05f, 0.17f, 0.24f));
	Part(TEXT("HairBun"), Sphere, FVector(-10, 0, 181), FVector(0.09f));
	Part(TEXT("Beard"), Sphere, FVector(6, 0, 160), FVector(0.15f, 0.17f, 0.12f));
	Part(TEXT("Glasses"), Cube, FVector(11.5f, 0, 171), FVector(0.015f, 0.16f, 0.035f));
	Part(TEXT("Balaclava"), Sphere, FVector(1, 0, 168.5f), FVector(0.208f, 0.198f, 0.24f));
	Part(TEXT("Shemagh"), Cylinder, FVector(2, 0, 157), FVector(0.22f, 0.22f, 0.09f));
}

UStaticMeshComponent* ARiptideCrewFigurePlaceholder::Part(const TCHAR* Name, int32 Shape, const FVector& Location, const FVector& Scale,
	const FRotator& Rotation)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Mesh->SetupAttachment(Root);
	Mesh->SetRelativeLocation(Location);
	Mesh->SetRelativeRotation(Rotation);
	Mesh->SetRelativeScale3D(Scale);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	UStaticMesh* Shapes[] = { CubeMesh.Object, SphereMesh.Object, CylinderMesh.Object };
	Mesh->SetStaticMesh(Shapes[Shape]);
	Parts.Add(Name, Mesh);
	return Mesh;
}

void ARiptideCrewFigurePlaceholder::Tint(UStaticMeshComponent* Mesh, const FLinearColor& Colour)
{
	if (!Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!Material)
	{
		// The basic shapes' own material, which takes a colour. (Looked up each time: a cached pointer wouldn't keep
		// it loaded across map changes.)
		UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		Material = Mesh->CreateDynamicMaterialInstance(0, Plain);
	}
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Colour);
	}
}

void ARiptideCrewFigurePlaceholder::SetAppearance(const FRiptideAppearance& Look)
{
	auto Get = [&Look](ERiptideLook Which) { return int32(Look.Get(Which)); };
	auto Show = [this](const TCHAR* Name, bool bShow)
	{
		if (UStaticMeshComponent* Mesh = Parts.FindRef(Name))
		{
			Mesh->SetVisibility(bShow);
		}
	};
	auto Colour = [this](std::initializer_list<const TCHAR*> Names, const FLinearColor& C)
	{
		for (const TCHAR* Name : Names)
		{
			Tint(Parts.FindRef(Name), C);
		}
	};

	// Build: the female body is a little slighter in the shoulders and shorter.
	const bool bFemale = Get(ERiptideLook::Body) == 1;
	SetActorScale3D(FVector(bFemale ? 0.95f : 1.f));
	if (UStaticMeshComponent* Torso = Parts.FindRef(TEXT("Torso")))
	{
		Torso->SetRelativeScale3D(FVector(0.24f, bFemale ? 0.34f : 0.38f, 0.5f));
	}

	const FLinearColor Skin = URiptideAppearanceLibrary::GetSkinTone(Get(ERiptideLook::SkinTone));
	const FLinearColor HairColour = URiptideAppearanceLibrary::GetHairColour(Get(ERiptideLook::HairColour));
	const FLinearColor Gear = URiptideAppearanceLibrary::GetGearColour(Get(ERiptideLook::GearColour));
	const FLinearColor Uniform = UniformColour(Get(ERiptideLook::Camo));
	const FLinearColor Black = Srgb(16, 16, 18);
	const bool bGloves = Get(ERiptideLook::Gloves) == 1;

	Colour({ TEXT("Head"), TEXT("Neck") }, Skin);
	Colour({ TEXT("HandL"), TEXT("HandR") }, bGloves ? Black : Skin);
	Colour({ TEXT("LegL"), TEXT("LegR"), TEXT("Hips"), TEXT("Torso"), TEXT("ShoulderL"), TEXT("ShoulderR"), TEXT("UpperArmL"), TEXT("UpperArmR"),
		TEXT("ForearmL"), TEXT("ForearmR"), TEXT("BoonieBrim"), TEXT("BoonieCrown"), TEXT("Cap"), TEXT("CapBrim") }, Uniform);
	Colour({ TEXT("BootL"), TEXT("BootR") }, Srgb(46, 38, 30));
	Colour({ TEXT("Rifle"), TEXT("Barrel"), TEXT("Magazine"), TEXT("Stock"), TEXT("Glasses"), TEXT("HelmetMount") }, Srgb(20, 20, 22));
	Colour({ TEXT("Vest"), TEXT("VestPouches"), TEXT("ChestRig"), TEXT("PackAssault"), TEXT("PackHydration"), TEXT("Helmet"), TEXT("Beanie") }, Gear);
	Colour({ TEXT("Hair"), TEXT("HairLong"), TEXT("HairBun"), TEXT("Beard") }, HairColour);
	Colour({ TEXT("Balaclava") }, Black);
	Colour({ TEXT("Shemagh") }, Srgb(176, 160, 128));

	// Hair: shaved, buzz cut, short parted, long, buns.
	const int32 Hair = Get(ERiptideLook::Hair);
	const int32 Headgear = Get(ERiptideLook::Headgear);    // none, helmet, boonie, cap, beanie
	const int32 Face = Get(ERiptideLook::FaceCover);       // bare, sunglasses, ballistic glasses, balaclava, shemagh
	Show(TEXT("Hair"), Hair > 0 && Face != 3);
	if (UStaticMeshComponent* Cap = Parts.FindRef(TEXT("Hair")))
	{
		Cap->SetRelativeScale3D(Hair == 1 ? FVector(0.204f, 0.194f, 0.17f) : FVector(0.212f, 0.202f, 0.19f));
	}
	Show(TEXT("HairLong"), Hair == 3 && Face != 3);
	Show(TEXT("HairBun"), Hair == 4 && Face != 3 && Headgear != 1);
	Show(TEXT("Beard"), Get(ERiptideLook::Beard) == 1 && Face != 3 && Face != 4);

	Show(TEXT("Helmet"), Headgear == 1);
	Show(TEXT("HelmetMount"), Headgear == 1);
	Show(TEXT("BoonieBrim"), Headgear == 2);
	Show(TEXT("BoonieCrown"), Headgear == 2);
	Show(TEXT("Cap"), Headgear == 3);
	Show(TEXT("CapBrim"), Headgear == 3);
	Show(TEXT("Beanie"), Headgear == 4);

	Show(TEXT("Glasses"), Face == 1 || Face == 2);
	if (UStaticMeshComponent* Glasses = Parts.FindRef(TEXT("Glasses")))
	{
		// Ballistic glasses wrap further round and are a touch taller.
		Glasses->SetRelativeScale3D(Face == 2 ? FVector(0.02f, 0.19f, 0.045f) : FVector(0.015f, 0.16f, 0.035f));
	}
	Show(TEXT("Balaclava"), Face == 3);
	Show(TEXT("Shemagh"), Face == 4);

	const int32 Vest = Get(ERiptideLook::Vest);            // none, plate carrier, chest rig
	Show(TEXT("Vest"), Vest == 1);
	Show(TEXT("VestPouches"), Vest == 1);
	Show(TEXT("ChestRig"), Vest == 2);
	const int32 Pack = Get(ERiptideLook::Backpack);        // none, assault pack, hydration pack
	Show(TEXT("PackAssault"), Pack == 1);
	Show(TEXT("PackHydration"), Pack == 2);
}

AActor* RiptideCrewFigure::Spawn(UWorld* World, const FTransform& Where, const FRiptideAppearance& Look)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	// The real crew member (the game's own body and gear), standing with the rifle shouldered.
	if (ARiptideCrewMannequin* Mannequin = World->SpawnActor<ARiptideCrewMannequin>(ARiptideCrewMannequin::StaticClass(), Where, Params))
	{
		Mannequin->SetPose(ERiptideCrewPose::RifleReady);
		Mannequin->SetAppearance(Look);
		if (Mannequin->GetBody()->HasBody())
		{
			return Mannequin;
		}
		// The crew's models are built when the editor opens: without them, the shapes below stand in.
		Mannequin->Destroy();
	}
	ARiptideCrewFigurePlaceholder* Figure = World->SpawnActor<ARiptideCrewFigurePlaceholder>(ARiptideCrewFigurePlaceholder::StaticClass(), Where, Params);
	if (Figure)
	{
		Figure->SetAppearance(Look);
	}
	return Figure;
}

void RiptideCrewFigure::SetLook(AActor* Figure, const FRiptideAppearance& Look)
{
	if (ARiptideCrewMannequin* Mannequin = Cast<ARiptideCrewMannequin>(Figure))
	{
		Mannequin->SetAppearance(Look);
	}
	else if (ARiptideCrewFigurePlaceholder* Placeholder = Cast<ARiptideCrewFigurePlaceholder>(Figure))
	{
		Placeholder->SetAppearance(Look);
	}
}

// --- The crew screen's preview booth ---

ARiptideCrewPreview::ARiptideCrewPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// A dark backdrop and a floor disc, so the figure stands somewhere rather than floating in black.
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(RootComponent);
	Backdrop->SetStaticMesh(CubeMesh.Object);
	Backdrop->SetRelativeLocation(FVector(-320.f, 0.f, 150.f));
	Backdrop->SetRelativeScale3D(FVector(0.1f, 12.f, 8.f));
	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(RootComponent);
	Floor->SetStaticMesh(CylinderMesh.Object);
	Floor->SetRelativeLocation(FVector(0.f, 0.f, -1.f));
	Floor->SetRelativeScale3D(FVector(1.6f, 1.6f, 0.02f));
	for (UStaticMeshComponent* Mesh : { Backdrop.Get(), Floor.Get() })
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
	}

	// Studio lighting: a warm key from the front right, a cool rim from behind left, a soft fill.
	KeyLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(RootComponent);
	KeyLight->SetRelativeLocation(FVector(260.f, 170.f, 260.f));
	KeyLight->SetRelativeRotation((FVector(0.f, 0.f, 110.f) - FVector(260.f, 170.f, 260.f)).Rotation());
	KeyLight->SetIntensityUnits(ELightUnits::Candelas);
	KeyLight->SetIntensity(140.f);
	KeyLight->SetLightColor(FLinearColor(1.f, 0.9f, 0.78f));
	KeyLight->SetOuterConeAngle(32.f);
	KeyLight->SetInnerConeAngle(16.f);
	KeyLight->SetAttenuationRadius(1500.f);
	RimLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(RootComponent);
	RimLight->SetRelativeLocation(FVector(-130.f, -150.f, 240.f));
	RimLight->SetRelativeRotation((FVector(0.f, 0.f, 140.f) - FVector(-130.f, -150.f, 240.f)).Rotation());
	RimLight->SetIntensityUnits(ELightUnits::Candelas);
	RimLight->SetIntensity(160.f);
	RimLight->SetLightColor(FLinearColor(0.6f, 0.75f, 1.f));
	RimLight->SetOuterConeAngle(35.f);
	RimLight->SetAttenuationRadius(1500.f);
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetRelativeLocation(FVector(220.f, -220.f, 120.f));
	FillLight->SetIntensityUnits(ELightUnits::Candelas);
	FillLight->SetIntensity(30.f);
	FillLight->SetLightColor(FLinearColor(0.75f, 0.85f, 1.f));
	FillLight->SetAttenuationRadius(1500.f);
	for (ULocalLightComponent* Light : { (ULocalLightComponent*)KeyLight.Get(), (ULocalLightComponent*)RimLight.Get(), (ULocalLightComponent*)FillLight.Get() })
	{
		Light->SetVolumetricScatteringIntensity(0.f);
	}

	// The camera, full length on the figure. Only the booth is drawn (none of the sea, sky or fog around it), with
	// a fixed exposure so the picture doesn't pump as the figure turns.
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(RootComponent);
	Capture->SetRelativeLocation(FVector(420.f, 0.f, 110.f));
	Capture->SetRelativeRotation(FRotator(-4.f, 180.f, 0.f));
	Capture->FOVAngle = 30.f;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetMotionBlur(false);
	FPostProcessSettings& Post = Capture->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 0.f;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_DynamicGlobalIlluminationMethod = true;
	Post.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	Post.bOverride_VignetteIntensity = true;
	Post.VignetteIntensity = 0.6f;
	Post.bOverride_BloomIntensity = true;
	Post.BloomIntensity = 0.f;
}

void ARiptideCrewPreview::BeginPlay()
{
	Super::BeginPlay();
	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("CrewPreview"));
	RenderTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
	RenderTarget->ClearColor = FLinearColor(0.01f, 0.015f, 0.02f, 1.f);
	RenderTarget->InitAutoFormat(Width, Height);
	RenderTarget->UpdateResourceImmediate(true);
	Capture->TextureTarget = RenderTarget;

	Figure = RiptideCrewFigure::Spawn(GetWorld(), FTransform(FRotator(0.f, Yaw, 0.f), GetActorLocation()), FRiptideAppearance());
	if (Figure)
	{
		Figure->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		Capture->ShowOnlyActors.Add(Figure);
	}
	Capture->ShowOnlyActors.Add(this);

	auto Shade = [](UStaticMeshComponent* Mesh, const FLinearColor& Colour)
	{
		UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(0, Plain))
		{
			M->SetVectorParameterValue(TEXT("Color"), Colour);
		}
	};
	Shade(Backdrop, FLinearColor(0.012f, 0.016f, 0.022f));
	Shade(Floor, FLinearColor(0.03f, 0.033f, 0.036f));
}

void ARiptideCrewPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Yaw = FMath::FInterpTo(Yaw, TargetYaw, DeltaSeconds, 10.f);
	if (Figure)
	{
		Figure->SetActorRelativeRotation(FRotator(0.f, Yaw, 0.f));
	}
}

void ARiptideCrewPreview::SetLook(const FRiptideAppearance& Look)
{
	RiptideCrewFigure::SetLook(Figure, Look);
}

void ARiptideCrewPreview::Turn(float DeltaYaw)
{
	TargetYaw += DeltaYaw;
}

void ARiptideCrewPreview::SetFilming(bool bFilm)
{
	Capture->bCaptureEveryFrame = bFilm;
	SetActorTickEnabled(bFilm);
}
