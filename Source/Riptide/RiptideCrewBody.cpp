#include "RiptideCrewBody.h"

#include "Animation/Skeleton.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ReferenceSkeleton.h"
#include "Engine/World.h"
#include "RiptideBoat.h"
#include "RiptideCharacter.h"
#include "RiptideCrewMannequin.h"

namespace
{
	const TCHAR* CharactersPath = TEXT("/Game/Riptide/Characters");

	// The uniform's camouflage, by the Camo option (RiptideAppearance.cpp's order): the pattern's style (0 soft
	// blotches like MultiCam and desert, 1 hard-edged woodland shapes, 2 plain cloth), its four colours (sRGB, base
	// first) and how big its shapes are (bigger is larger blotches).
	struct FCamo
	{
		float Style;
		FColor Colours[4];
		float Size;
	};
	const FCamo& Camo(int32 Option)
	{
		static const FCamo Patterns[] = {
			{ 0.f, { FColor(152, 136, 104), FColor(104, 106, 70), FColor(124, 94, 66), FColor(60, 50, 38) }, 1.f },      // multi-terrain
			{ 1.f, { FColor(84, 92, 60), FColor(98, 72, 48), FColor(30, 30, 26), FColor(150, 138, 100) }, 1.15f },      // woodland
			{ 0.f, { FColor(196, 172, 132), FColor(170, 140, 104), FColor(136, 108, 80), FColor(96, 78, 60) }, 1.25f },   // desert
			{ 1.f, { FColor(126, 128, 128), FColor(92, 94, 96), FColor(48, 50, 52), FColor(170, 170, 166) }, 1.1f },     // urban grey
			{ 2.f, { FColor(82, 84, 58), FColor(82, 84, 58), FColor(82, 84, 58), FColor(82, 84, 58) }, 1.f },            // plain olive
			{ 2.f, { FColor(26, 26, 28), FColor(26, 26, 28), FColor(26, 26, 28), FColor(26, 26, 28) }, 1.f },            // black
		};
		return Patterns[FMath::Clamp(Option, 0, int32(UE_ARRAY_COUNT(Patterns)) - 1)];
	}

	FString BodyFolder(const FRiptideAppearance& Look)
	{
		return Look.Get(ERiptideLook::Body) == 1 ? TEXT("Female") : TEXT("Male");
	}

	template <typename T>
	T* LoadCrewAsset(const FString& Folder, const TCHAR* Name)
	{
		const FString Path = Folder.IsEmpty()
			? FString::Printf(TEXT("%s/%s.%s"), CharactersPath, Name, Name)
			: FString::Printf(TEXT("%s/%s/%s.%s"), CharactersPath, *Folder, Name, Name);
		return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

URiptideCrewBodyComponent::URiptideCrewBodyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetAnimationMode(EAnimationMode::AnimationBlueprint);
	AnimClass = URiptideCrewAnimInstance::StaticClass();
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = true;
	bCastDynamicShadow = true;
	// Only bodies someone can see need posing; a player's own body (hidden from them, but casting its shadow) is
	// set to always pose in SetHiddenFromOwner.
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	PrimaryComponentTick.bCanEverTick = true;
}

void URiptideCrewBodyComponent::SetAppearance(const FRiptideAppearance& Look)
{
	FRiptideAppearance Sane = Look;
	Sane.Sanitise();
	const bool bBodyChanged = !bBuilt || Sane.Get(ERiptideLook::Body) != Appearance.Get(ERiptideLook::Body);
	Appearance = Sane;
	const FString Folder = BodyFolder(Appearance);
	if (bBodyChanged)
	{
		USkeletalMesh* BodyMesh = LoadCrewAsset<USkeletalMesh>(Folder, Folder == TEXT("Female") ? TEXT("SK_CrewFemale") : TEXT("SK_CrewMale"));
		UE_CLOG(!BodyMesh, LogTemp, Warning, TEXT("Riptide: the crew body for %s is missing (the editor builds it on launch)"), *Folder);
		EmptyOverrideMaterials();
		SetSkeletalMeshAsset(BodyMesh);
		if (BodyMesh)
		{
			for (int32 i = 0; i < BodyMesh->GetMaterials().Num(); ++i)
			{
				SetMaterial(i, Coloured(BodyMesh->GetMaterials()[i].MaterialInterface));
			}
		}
	}

	// What's worn. Hair goes flat under headgear (the "_Hat" variants) and away under a balaclava; a beard (male
	// only) under a balaclava or shemagh.
	const uint8 Head = Appearance.Get(ERiptideLook::Headgear);
	const uint8 Face = Appearance.Get(ERiptideLook::FaceCover);
	const bool bBalaclava = Face == 3, bShemagh = Face == 4;
	static const TCHAR* Hair[] = { nullptr, TEXT("SK_Hair_Buzzed"), TEXT("SK_Hair_Parted"), TEXT("SK_Hair_Long"), TEXT("SK_Hair_Buns") };
	static const TCHAR* HairHat[] = { nullptr, TEXT("SK_Hair_Buzzed_Hat"), TEXT("SK_Hair_Parted_Hat"), TEXT("SK_Hair_Long_Hat"), TEXT("SK_Hair_Buns_Hat") };
	static const TCHAR* Headgear[] = { nullptr, TEXT("SK_Helmet"), TEXT("SK_Boonie"), TEXT("SK_Cap"), TEXT("SK_Beanie") };
	static const TCHAR* FaceCover[] = { nullptr, TEXT("SK_Sunglasses"), TEXT("SK_BallisticGlasses"), TEXT("SK_Balaclava"), TEXT("SK_Shemagh") };
	static const TCHAR* Vest[] = { nullptr, TEXT("SK_PlateCarrier"), TEXT("SK_ChestRig") };
	const bool bOverPlate = Appearance.Get(ERiptideLook::Vest) == 1;
	static const TCHAR* Pack[] = { nullptr, TEXT("SK_AssaultPack"), TEXT("SK_HydrationPack") };
	static const TCHAR* PackOverPlate[] = { nullptr, TEXT("SK_AssaultPack_OverPlate"), TEXT("SK_HydrationPack_OverPlate") };
	const uint8 HairChoice = Appearance.Get(ERiptideLook::Hair);
	SetPart(EPart::Uniform, TEXT("SK_Uniform"));
	SetPart(EPart::Boots, TEXT("SK_Boots"));
	SetPart(EPart::Gloves, Appearance.Get(ERiptideLook::Gloves) == 1 ? TEXT("SK_Gloves") : nullptr);
	SetPart(EPart::Hair, bBalaclava ? nullptr : (Head != 0 ? HairHat : Hair)[FMath::Min<int32>(HairChoice, 4)]);
	SetPart(EPart::Beard, Folder == TEXT("Male") && Appearance.Get(ERiptideLook::Beard) == 1 && !bBalaclava && !bShemagh ? TEXT("SK_Beard") : nullptr);
	SetPart(EPart::Headgear, Headgear[FMath::Min<int32>(Head, 4)]);
	SetPart(EPart::FaceCover, FaceCover[FMath::Min<int32>(Face, 4)]);
	SetPart(EPart::Vest, Vest[FMath::Min<int32>(Appearance.Get(ERiptideLook::Vest), 2)]);
	SetPart(EPart::Pack, (bOverPlate ? PackOverPlate : Pack)[FMath::Min<int32>(Appearance.Get(ERiptideLook::Backpack), 2)]);

	// The eyebrows (a section of the body) would poke through a balaclava.
	if (USkeletalMesh* BodyMesh = GetSkeletalMeshAsset())
	{
		const FSkeletalMeshRenderData* Render = BodyMesh->GetResourceForRendering();
		for (int32 Slot = 0; Slot < BodyMesh->GetMaterials().Num() && Render && Render->LODRenderData.Num() > 0; ++Slot)
		{
			if (!BodyMesh->GetMaterials()[Slot].MaterialSlotName.ToString().StartsWith(TEXT("MI_Hair")))
			{
				continue;
			}
			const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[0].RenderSections;
			for (int32 Section = 0; Section < Sections.Num(); ++Section)
			{
				if (Sections[Section].MaterialIndex == Slot)
				{
					ShowMaterialSection(Slot, Section, !bBalaclava, 0);
				}
			}
		}
	}
	bBuilt = true;
	ColourAll();
	UpdateRifle();
}

USkeletalMeshComponent* URiptideCrewBodyComponent::PartComponent(EPart Part)
{
	Parts.SetNum(int32(EPart::Count));
	TObjectPtr<USkeletalMeshComponent>& Slot = Parts[int32(Part)];
	if (!Slot)
	{
		// Each part is a mesh on the same skeleton following this body's pose: no animation of its own, its bounds
		// taken from the body.
		AActor* Owner = GetOwner();
		Slot = NewObject<USkeletalMeshComponent>(Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(this), NAME_None, RF_Transient);
		Slot->SetupAttachment(this);
		Slot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Slot->SetGenerateOverlapEvents(false);
		Slot->bUseBoundsFromLeaderPoseComponent = true;
		Slot->CastShadow = true;
		ApplyOwnerVisibility(Slot);
		if (GetWorld())
		{
			Slot->RegisterComponentWithWorld(GetWorld());
		}
		Slot->SetLeaderPoseComponent(this);
	}
	return Slot;
}

void URiptideCrewBodyComponent::SetPart(EPart Part, const TCHAR* MeshName)
{
	USkeletalMesh* Mesh = MeshName ? LoadCrewAsset<USkeletalMesh>(BodyFolder(Appearance), MeshName) : nullptr;
	UE_CLOG(MeshName && !Mesh, LogTemp, Warning, TEXT("Riptide: crew mesh %s is missing"), MeshName);
	Parts.SetNum(int32(EPart::Count));
	if (!Mesh && !Parts[int32(Part)])
	{
		return;
	}
	USkeletalMeshComponent* Component = PartComponent(Part);
	if (Component->GetSkeletalMeshAsset() != Mesh)
	{
		Component->EmptyOverrideMaterials();
		Component->SetSkeletalMeshAsset(Mesh);
		if (Mesh)
		{
			for (int32 i = 0; i < Mesh->GetMaterials().Num(); ++i)
			{
				Component->SetMaterial(i, Coloured(Mesh->GetMaterials()[i].MaterialInterface));
			}
		}
		Component->SetLeaderPoseComponent(this, true);
	}
	Component->SetVisibility(Mesh != nullptr);
}

UMaterialInterface* URiptideCrewBodyComponent::Coloured(UMaterialInterface* Material)
{
	if (!Material)
	{
		return nullptr;
	}
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Dynamic.Find(Material))
	{
		return *Found;
	}
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
	Dynamic.Add(Material, Instance);
	return Instance;
}

void URiptideCrewBodyComponent::ColourAll()
{
	// Every crew material takes the parameters it has from the same set (a parameter a material doesn't have is
	// ignored), so one pass colours the lot: skin, hair, gear, boots and the uniform's camouflage.
	const FLinearColor Skin = URiptideAppearanceLibrary::GetSkinTone(Appearance.Get(ERiptideLook::SkinTone));
	const FLinearColor Hair = URiptideAppearanceLibrary::GetHairColour(Appearance.Get(ERiptideLook::HairColour));
	const int32 GearOption = Appearance.Get(ERiptideLook::GearColour);
	const FLinearColor Gear = URiptideAppearanceLibrary::GetGearColour(GearOption);
	// Boots: brown leather with the earth-tone gear (coyote, tan), black with the rest.
	const FLinearColor Boots = GearOption == 0 || GearOption == 4 ? FLinearColor::FromSRGBColor(FColor(66, 45, 30))
		: FLinearColor::FromSRGBColor(FColor(24, 23, 22));
	const FCamo& Pattern = Camo(Appearance.Get(ERiptideLook::Camo));
	for (const TPair<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>>& Pair : Dynamic)
	{
		UMaterialInstanceDynamic* M = Pair.Value;
		if (!M)
		{
			continue;
		}
		M->SetVectorParameterValue(TEXT("SkinTone"), Skin);
		M->SetVectorParameterValue(TEXT("HairColour"), Hair);
		M->SetVectorParameterValue(TEXT("GearColour"), Gear);
		M->SetVectorParameterValue(TEXT("BootColour"), Boots);
		M->SetScalarParameterValue(TEXT("CamoStyle"), Pattern.Style);
		M->SetScalarParameterValue(TEXT("CamoSize"), Pattern.Size);
		for (int32 i = 0; i < 4; ++i)
		{
			M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Camo%d"), i + 1)), FLinearColor::FromSRGBColor(Pattern.Colours[i]));
		}
	}
}

void URiptideCrewBodyComponent::SetPose(ERiptideCrewPose NewPose)
{
	Pose = NewPose;
	UpdateRifle();
}

void URiptideCrewBodyComponent::SetHiddenFromOwner(bool bHide)
{
	bHiddenFromOwner = bHide;
	// Its own player never sees the body, but its shadow on the deck still moves with it: posed every frame.
	VisibilityBasedAnimTickOption = bHide ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	ApplyOwnerVisibility(this);
	for (USkeletalMeshComponent* Part : Parts)
	{
		ApplyOwnerVisibility(Part);
	}
	ApplyOwnerVisibility(Rifle);
}

void URiptideCrewBodyComponent::ApplyOwnerVisibility(UPrimitiveComponent* Component) const
{
	if (Component)
	{
		Component->SetOwnerNoSee(bHiddenFromOwner);
		Component->bCastHiddenShadow = bHiddenFromOwner;
	}
}

void URiptideCrewBodyComponent::UpdateRifle()
{
	const bool bShow = Pose == ERiptideCrewPose::RifleReady && HasBody();
	if (bShow && !Rifle)
	{
		AActor* Owner = GetOwner();
		Rifle = NewObject<UStaticMeshComponent>(Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(this), NAME_None, RF_Transient);
		Rifle->SetupAttachment(this);
		Rifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rifle->SetStaticMesh(LoadCrewAsset<UStaticMesh>(FString(), TEXT("SM_Rifle")));
		ApplyOwnerVisibility(Rifle);
		if (GetWorld())
		{
			Rifle->RegisterComponentWithWorld(GetWorld());
		}
	}
	if (Rifle)
	{
		if (UStaticMesh* Mesh = Rifle->GetStaticMesh())
		{
			for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i)
			{
				Rifle->SetMaterial(i, Coloured(Mesh->GetStaticMaterials()[i].MaterialInterface));
			}
		}
		ColourAll();
		Rifle->SetVisibility(bShow);
	}
}

void URiptideCrewBodyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Rifle && Rifle->IsVisible())
	{
		// The rifle's grip in the right hand (between the wrist and the knuckles), aimed along the body's facing:
		// placed from the posed hand each frame, so it moves with the hands' sway. Its model's origin is 5 cm above
		// and 1.5 cm ahead of where the hand closes on its grip.
		const FVector Grip = FMath::Lerp(GetSocketLocation(TEXT("hand_r")), GetSocketLocation(TEXT("middle_01_r")), RifleGripAlongHand);
		const FQuat Frame = GetComponentQuat() * RifleAim() * FRotationMatrix::MakeFromXZ(FVector(0.f, 1.f, 0.f), FVector::UpVector).ToQuat();
		Rifle->SetWorldLocationAndRotation(Grip + Frame.RotateVector(FVector(-1.5f, 0.f, 5.f)), Frame);
	}
}

void URiptideCrewBodyComponent::OnUnregister()
{
	for (USkeletalMeshComponent* Part : Parts)
	{
		if (Part && Part->IsRegistered())
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	if (Rifle && Rifle->IsRegistered())
	{
		Rifle->DestroyComponent();
	}
	Rifle = nullptr;
	bBuilt = false;
	Super::OnUnregister();
}

ERiptideCrewAnimState URiptideCrewBodyComponent::GetAnimState() const
{
	const URiptideCrewAnimInstance* Anim = Cast<URiptideCrewAnimInstance>(GetAnimInstance());
	return Anim ? Anim->GetAnimState() : ERiptideCrewAnimState::MenuIdle;
}

FString URiptideCrewBodyComponent::DescribeParts() const
{
	static const TCHAR* Names[] = { TEXT("Uniform"), TEXT("Boots"), TEXT("Gloves"), TEXT("Hair"), TEXT("Beard"), TEXT("Headgear"),
		TEXT("FaceCover"), TEXT("Vest"), TEXT("Pack") };
	FString Out = FString::Printf(TEXT("Body=%s"), GetSkeletalMeshAsset() ? *GetSkeletalMeshAsset()->GetName() : TEXT("none"));
	for (int32 i = 0; i < Parts.Num() && i < int32(UE_ARRAY_COUNT(Names)); ++i)
	{
		if (Parts[i] && Parts[i]->GetSkeletalMeshAsset() && Parts[i]->IsVisible())
		{
			Out += FString::Printf(TEXT(" %s=%s"), Names[i], *Parts[i]->GetSkeletalMeshAsset()->GetName());
		}
	}
	return Out;
}

TArray<FString> URiptideCrewLibrary::GetCrewClips()
{
	TArray<FString> Out;
	for (const TPair<FString, FString>& Clip : URiptideCrewAnimInstance::GetClipTable())
	{
		Out.Add(Clip.Key + TEXT("=") + Clip.Value);
	}
	return Out;
}

bool URiptideCrewLibrary::SetUpCrewSkeleton(USkeleton* Skeleton)
{
#if WITH_EDITOR
	if (!Skeleton)
	{
		return false;
	}
	const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		const FName Name = Ref.GetBoneName(i);
		const bool bAnimated = Name == TEXT("root") || Name == TEXT("pelvis");
		Skeleton->SetBoneTranslationRetargetingMode(i, bAnimated ? EBoneTranslationRetargetingMode::Animation : EBoneTranslationRetargetingMode::Skeleton);
	}
	Skeleton->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

ARiptideCharacter* URiptideCrewLibrary::SpawnCrewMember(UObject* WorldContextObject, ARiptideBoat* Boat, FVector Location, float Yaw,
	const FRiptideAppearance& Look)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideCharacter* Crew = World->SpawnActor<ARiptideCharacter>(ARiptideCharacter::StaticClass(), Location, FRotator(0.f, Yaw, 0.f), Params);
	if (Crew)
	{
		Crew->SetHomeBoat(Boat);
		Crew->GetCrewBody()->SetAppearance(Look);
	}
	return Crew;
}

ARiptideCrewMannequin* URiptideCrewLibrary::SpawnMannequin(UObject* WorldContextObject, FVector Location, float Yaw, const FRiptideAppearance& Look,
	ERiptideCrewPose Pose)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARiptideCrewMannequin* Mannequin = World->SpawnActor<ARiptideCrewMannequin>(ARiptideCrewMannequin::StaticClass(), Location, FRotator(0.f, Yaw, 0.f), Params);
	if (Mannequin)
	{
		Mannequin->SetAppearance(Look);
		Mannequin->SetPose(Pose);
	}
	return Mannequin;
}
