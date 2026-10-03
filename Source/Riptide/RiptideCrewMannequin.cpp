#include "RiptideCrewMannequin.h"

#include "Components/SceneComponent.h"
#include "RiptideCrewBody.h"

ARiptideCrewMannequin::ARiptideCrewMannequin()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Body = CreateDefaultSubobject<URiptideCrewBodyComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	// The body model faces its own +Y: turned to face the actor's forward, feet on the actor's origin.
	Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	// In a menu it's always on screen: posed every frame, even when a render target is drawing it.
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->SetPose(ERiptideCrewPose::Idle);
}

void ARiptideCrewMannequin::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Shows the starting look while placed in a level in the editor, too.
	Body->SetPose(InitialPose == ERiptideCrewPose::Gameplay ? ERiptideCrewPose::Idle : InitialPose);
	Body->SetAppearance(InitialAppearance);
}

void ARiptideCrewMannequin::BeginPlay()
{
	Super::BeginPlay();
	if (!Body->HasBody())
	{
		Body->SetAppearance(InitialAppearance);
	}
}

void ARiptideCrewMannequin::SetAppearance(const FRiptideAppearance& Look)
{
	Body->SetAppearance(Look);
}

const FRiptideAppearance& ARiptideCrewMannequin::GetAppearance() const
{
	return Body->GetAppearance();
}

void ARiptideCrewMannequin::SetPose(ERiptideCrewPose Pose)
{
	Body->SetPose(Pose == ERiptideCrewPose::Gameplay ? ERiptideCrewPose::Idle : Pose);
}
