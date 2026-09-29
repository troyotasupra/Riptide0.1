#include "RiptideWakeFoamComponent.h"

#include "WaterBodyComponent.h"

namespace
{
	// Keeps a trail from growing without bound if something emits every frame for a long time.
	constexpr int32 MaxPointsPerTrail = 400;
}

URiptideWakeFoamComponent::URiptideWakeFoamComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsyncCooking = true;
}

int32 URiptideWakeFoamComponent::AddTrail(const FTrailStyle& Style)
{
	FTrail& Trail = Trails.AddDefaulted_GetRef();
	Trail.Style = Style;
	return Trails.Num() - 1;
}

void URiptideWakeFoamComponent::UpdateTrail(int32 TrailIndex, float DeltaSeconds, const FVector& Location, const FVector& Side, float Strength)
{
	if (!Trails.IsValidIndex(TrailIndex))
	{
		return;
	}
	FTrail& Trail = Trails[TrailIndex];

	// Age the foam and drop what has faded.
	for (FPoint& Point : Trail.Points)
	{
		Point.Age += DeltaSeconds;
	}
	const float Life = Trail.Style.LifeSeconds;
	const int32 FirstAlive = Trail.Points.IndexOfByPredicate([Life](const FPoint& P) { return P.Age < Life; });
	if (FirstAlive == INDEX_NONE)
	{
		Trail.Points.Reset();
	}
	else if (FirstAlive > 0)
	{
		Trail.Points.RemoveAt(0, FirstAlive, EAllowShrinking::No);
		Trail.Points[0].bStartsStrip = true;
	}

	// Lay a new point every PointSpacing travelled while foam is being made. A pause starts a new strip.
	const FVector2D Here(Location.X, Location.Y);
	if (Strength <= 0.02f)
	{
		Trail.bEmitting = false;
		return;
	}
	if (Trail.bEmitting && FVector2D::DistSquared(Here, Trail.LastEmitted) < FMath::Square(PointSpacing))
	{
		return;
	}
	if (Trail.Points.Num() >= MaxPointsPerTrail)
	{
		Trail.Points.RemoveAt(0, 1, EAllowShrinking::No);
		Trail.Points[0].bStartsStrip = true;
	}
	FPoint& Point = Trail.Points.AddDefaulted_GetRef();
	Point.Position = Here;
	Point.Side = FVector2D(Side.X, Side.Y).GetSafeNormal();
	Point.Height = Location.Z;
	Point.Strength = FMath::Clamp(Strength, 0.f, 1.f);
	Point.bStartsStrip = !Trail.bEmitting;
	Trail.bEmitting = true;
	Trail.LastEmitted = Here;
}

void URiptideWakeFoamComponent::RebuildMesh()
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	for (const FTrail& Trail : Trails)
	{
		const FTrailStyle& Style = Trail.Style;
		for (int32 i = 0; i < Trail.Points.Num(); ++i)
		{
			const FPoint& Point = Trail.Points[i];
			const float Life01 = FMath::Clamp(Point.Age / Style.LifeSeconds, 0.f, 1.f);
			const float HalfWidth = Style.StartHalfWidth + Style.GrowthPerSecond * Point.Age;
			const FVector2D Centre2D = Point.Position + Point.Side * Style.DriftPerSecond * Point.Age;

			// Sit on the moving water surface, waves included, when we know it; otherwise at the height the foam was made.
			float Z = Point.Height;
			if (WaterBody)
			{
				const auto Query = WaterBody->TryQueryWaterInfoClosestToWorldLocation(
					FVector(Centre2D, Point.Height), EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::IncludeWaves);
				if (Query.HasValue())
				{
					Z = Query.GetValue().GetWaterSurfaceLocation().Z;
				}
			}
			const FVector Centre(Centre2D, Z + SurfaceOffset);
			const FVector Across(Point.Side * HalfWidth, 0.f);

			// Fades out with age, and the strip's two ends fade in so it has no hard edges.
			float Alpha = Style.Opacity * Point.Strength * FMath::Pow(1.f - Life01, 1.5f);
			const bool bEnd = Point.bStartsStrip || i + 1 == Trail.Points.Num() || Trail.Points[i + 1].bStartsStrip;
			if (bEnd)
			{
				Alpha = 0.f;
			}

			const int32 Base = Vertices.Num();
			Vertices.Add(Centre - Across);
			Vertices.Add(Centre + Across);
			Normals.Add(FVector::UpVector);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(0.f, Life01));
			UVs.Add(FVector2D(1.f, Life01));
			Colors.Add(FLinearColor(1.f, 1.f, 1.f, Alpha));
			Colors.Add(FLinearColor(1.f, 1.f, 1.f, Alpha));

			if (!Point.bStartsStrip && i > 0)
			{
				// Two triangles joining this point's edge to the previous one, facing up.
				Triangles.Append({ Base - 2, Base, Base - 1, Base - 1, Base, Base + 1 });
			}
		}
	}

	if (Triangles.Num() == 0)
	{
		ClearAllMeshSections();
		return;
	}
	CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
}
