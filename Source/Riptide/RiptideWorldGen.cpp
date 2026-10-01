#include "RiptideWorldGen.h"

#include <algorithm>
#include <cmath>

namespace RiptideGen
{
	namespace
	{
		constexpr double TwoPi = 6.283185307179586;

		uint32_t Mix32(uint32_t X)
		{
			X ^= X >> 16;
			X *= 0x7feb352dU;
			X ^= X >> 15;
			X *= 0x846ca68bU;
			X ^= X >> 16;
			return X;
		}

		uint32_t HashCell(int32_t A, int32_t B, uint32_t Seed)
		{
			return Mix32(static_cast<uint32_t>(A) * 0x8da6b343U ^ Mix32(static_cast<uint32_t>(B) * 0xd8163841U ^ Seed));
		}

		uint64_t Mix64(uint64_t X)
		{
			X += 0x9e3779b97f4a7c15ULL;
			X = (X ^ (X >> 30)) * 0xbf58476d1ce4e5b9ULL;
			X = (X ^ (X >> 27)) * 0x94d049bb133111ebULL;
			return X ^ (X >> 31);
		}

		/** Small deterministic random stream (splitmix64). */
		struct FRng
		{
			uint64_t State;

			explicit FRng(uint64_t InState) : State(InState) {}

			uint64_t Next()
			{
				State += 0x9e3779b97f4a7c15ULL;
				return Mix64(State);
			}

			/** 0 (inclusive) to 1 (exclusive). */
			double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
			double Range(double Lo, double Hi) { return Lo + (Hi - Lo) * Unit(); }
			float RangeF(float Lo, float Hi) { return static_cast<float>(Range(Lo, Hi)); }
		};

		// 16 evenly spread gradient directions, so noise needs no trig per sample.
		constexpr double GradX[16] = { 1.0, 0.9239, 0.7071, 0.3827, 0.0, -0.3827, -0.7071, -0.9239,
			-1.0, -0.9239, -0.7071, -0.3827, 0.0, 0.3827, 0.7071, 0.9239 };
		constexpr double GradY[16] = { 0.0, 0.3827, 0.7071, 0.9239, 1.0, 0.9239, 0.7071, 0.3827,
			0.0, -0.3827, -0.7071, -0.9239, -1.0, -0.9239, -0.7071, -0.3827 };

		double GradDot(int32_t Ix, int32_t Iy, uint32_t Seed, double Dx, double Dy)
		{
			const uint32_t G = HashCell(Ix, Iy, Seed) & 15U;
			return GradX[G] * Dx + GradY[G] * Dy;
		}

		/** A fixed 0..1 number for an island's seed and a key, for picking its shape. */
		double SeedUnit(uint32_t Seed, uint32_t Key)
		{
			return static_cast<double>(Mix32(Seed ^ Mix32(Key)) >> 8) / 16777216.0;
		}

		double Fade(double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }
		double Lerp(double A, double B, double T) { return A + (B - A) * T; }
		double Clamp01(double V) { return std::min(1.0, std::max(0.0, V)); }

		double SmoothStep(double Edge0, double Edge1, double V)
		{
			const double T = Clamp01((V - Edge0) / (Edge1 - Edge0));
			return T * T * (3.0 - 2.0 * T);
		}

		/** How far a coast can wander out past an island's nominal radius (measured: never past 1.65). */
		constexpr double CoastReach = 1.7;

		/** Whether an island at X, Y would come within Gap metres of A, coast to coast. */
		bool Overlaps(const FIslandSite& A, double X, double Y, double Radius, double Gap)
		{
			const double Dx = A.X - X;
			const double Dy = A.Y - Y;
			const double MinDist = (A.Radius + Radius) * CoastReach + Gap;
			return Dx * Dx + Dy * Dy < MinDist * MinDist;
		}

		float PeakFor(EIslandKind Kind, float Radius, FRng& Rng)
		{
			switch (Kind)
			{
			case EIslandKind::Cay:
				return Rng.RangeF(3.f, 6.f);
			case EIslandKind::Green:
				return std::min(220.f, Radius * Rng.RangeF(0.18f, 0.3f) + 8.f);
			case EIslandKind::Rocky:
			default:
				return std::min(260.f, Radius * Rng.RangeF(0.3f, 0.45f) + 12.f);
			}
		}

		EIslandKind KindFor(float Radius, FRng& Rng)
		{
			const double Roll = Rng.Unit();
			if (Radius < 70.f)
			{
				return Roll < 0.75 ? EIslandKind::Cay : EIslandKind::Rocky;
			}
			if (Radius < 200.f)
			{
				return Roll < 0.35 ? EIslandKind::Cay : (Roll < 0.75 ? EIslandKind::Green : EIslandKind::Rocky);
			}
			return Roll < 0.65 ? EIslandKind::Green : EIslandKind::Rocky;
		}

		FIslandSite MakeSite(uint64_t Id, double X, double Y, float Radius, EIslandKind Kind, EIslandRole Role, FRng& Rng)
		{
			FIslandSite Site;
			Site.Id = Id;
			Site.X = X;
			Site.Y = Y;
			Site.Radius = Radius;
			Site.Kind = Kind;
			Site.Role = Role;
			Site.PeakHeight = PeakFor(Kind, Radius, Rng);
			Site.Seed = static_cast<uint32_t>(Rng.Next());
			return Site;
		}

		/** How far out from its centre an island's grid needs to reach to include its whole coast and shelf. */
		double TerrainHalfExtent(const FIslandSite& Site)
		{
			// The coast, plus enough seabed to fall away to about 27 m deep beyond it.
			return Site.Radius * CoastReach + 130.0;
		}

		// Natural slopes (rise over run) that loose ground stands at.
		constexpr float DrySandSlope = 0.67f;	// about 34 degrees
		constexpr float DampSandSlope = 1.0f;	// 45: damp sand near the water holds a steeper wall
		constexpr float WetSandSlope = 0.58f;	// 30: under water it slumps further
		constexpr float SoilSlope = 0.84f;		// 40
		constexpr float DampBand = 1.0f;		// sand up to this high above the sea is damp

		/** The loose layer is never thicker than this, which is also the deepest anyone can dig. */
		constexpr float MaxLooseDepth = 3.f;

		constexpr int32_t MaxSettlePasses = 4000;
	}

	// --- Noise ---

	double Noise(double X, double Y, uint32_t Seed)
	{
		const double Fx = std::floor(X);
		const double Fy = std::floor(Y);
		const int32_t Ix = static_cast<int32_t>(Fx);
		const int32_t Iy = static_cast<int32_t>(Fy);
		const double Dx = X - Fx;
		const double Dy = Y - Fy;

		const double N00 = GradDot(Ix, Iy, Seed, Dx, Dy);
		const double N10 = GradDot(Ix + 1, Iy, Seed, Dx - 1.0, Dy);
		const double N01 = GradDot(Ix, Iy + 1, Seed, Dx, Dy - 1.0);
		const double N11 = GradDot(Ix + 1, Iy + 1, Seed, Dx - 1.0, Dy - 1.0);

		const double U = Fade(Dx);
		const double V = Fade(Dy);
		// Gradient noise peaks near +-0.7; scale it out to about +-1.
		return 1.4 * Lerp(Lerp(N00, N10, U), Lerp(N01, N11, U), V);
	}

	double Fbm(double X, double Y, uint32_t Seed, int32_t Octaves)
	{
		double Sum = 0.0;
		double Amp = 1.0;
		double Norm = 0.0;
		for (int32_t O = 0; O < Octaves; ++O)
		{
			Sum += Amp * Noise(X, Y, Seed + static_cast<uint32_t>(O) * 1013U);
			Norm += Amp;
			Amp *= 0.5;
			X *= 2.03;
			Y *= 2.03;
		}
		return Sum / Norm;
	}

	// --- Grid rectangles ---

	void FGridRect::Include(int32_t X, int32_t Y)
	{
		if (IsEmpty())
		{
			MinX = MaxX = X;
			MinY = MaxY = Y;
			return;
		}
		MinX = std::min(MinX, X);
		MinY = std::min(MinY, Y);
		MaxX = std::max(MaxX, X);
		MaxY = std::max(MaxY, Y);
	}

	void FGridRect::Include(const FGridRect& Other)
	{
		if (Other.IsEmpty())
		{
			return;
		}
		Include(Other.MinX, Other.MinY);
		Include(Other.MaxX, Other.MaxY);
	}

	// --- World layout ---

	FWorld::FWorld(uint32_t InSeed, const FWorldLayout& InLayout)
		: Seed(InSeed)
		, Layout(InLayout)
	{
		BuildFixedSites();
	}

	EZone FWorld::ZoneAt(double X, double Y) const
	{
		const double Dist = std::sqrt(X * X + Y * Y);
		if (Dist < Layout.CordonInnerRadius)
		{
			return EZone::HomeChain;
		}
		return Dist < Layout.CordonOuterRadius ? EZone::Cordon : EZone::OpenSea;
	}

	void FWorld::BuildFixedSites()
	{
		FRng Rng(Mix64(Seed ^ 0x486f6d65ULL));
		uint64_t NextId = 1;

		// The start island: small, at the centre of the map, with beaches, a low wooded hill and a little rock
		// (trees for logs, plants for fibre, stone for flint) and nothing else.
		FixedSites.push_back(MakeSite(NextId++, 0.0, 0.0, Rng.RangeF(80.f, 105.f), EIslandKind::Green, EIslandRole::Start, Rng));
		FixedSites.back().PeakHeight = Rng.RangeF(12.f, 20.f);
		const float StartRadius = FixedSites.back().Radius;

		// The big island, a long row away but in plain sight.
		const double Heading = Rng.Range(0.0, TwoPi);
		const float BigRadius = Rng.RangeF(300.f, 400.f);
		const double BigDist = std::max(Rng.Range(950.0, 1150.0), (StartRadius + BigRadius) * CoastReach + 200.0);
		FixedSites.push_back(MakeSite(NextId++, std::cos(Heading) * BigDist, std::sin(Heading) * BigDist, BigRadius,
			EIslandKind::Green, EIslandRole::BigIsland, Rng));

		// Places a story island at a heading and distance, re-rolling both until it keeps clear water to every
		// island already placed. The last roll stands if none clears, which the tests would catch.
		const auto PlaceClear = [this, &Rng, &NextId](double FromX, double FromY, float Radius, EIslandKind Kind,
			EIslandRole Role, double Gap, const auto& RollHeading, const auto& RollDist)
		{
			double X = 0.0;
			double Y = 0.0;
			for (int32_t Attempt = 0; Attempt < 60; ++Attempt)
			{
				const double Angle = RollHeading();
				const double Dist = RollDist();
				X = FromX + std::cos(Angle) * Dist;
				Y = FromY + std::sin(Angle) * Dist;
				bool bClear = true;
				for (const FIslandSite& Other : FixedSites)
				{
					bClear = bClear && !Overlaps(Other, X, Y, Radius, Gap);
				}
				if (bClear)
				{
					break;
				}
			}
			FixedSites.push_back(MakeSite(NextId++, X, Y, Radius, Kind, Role, Rng));
		};

		// The third island sits about as far past the big island as the big island is from the start (as in the
		// Godot build), roughly in line with it, and never so close that the two coasts crowd each other.
		const FIslandSite Big = FixedSites.back();
		const float FarRadius = Rng.RangeF(200.f, 280.f);
		const double FarMin = (Big.Radius + FarRadius) * CoastReach + 300.0;
		PlaceClear(Big.X, Big.Y, FarRadius, EIslandKind::Rocky, EIslandRole::FarIsland, 250.0,
			[&Rng, Heading]() { return Heading + Rng.Range(-0.35, 0.35); },
			[&Rng, BigDist, FarMin]() { return std::max(FarMin, BigDist * Rng.Range(0.95, 1.15)); });

		// A sand bar off to one side of the crossing, where the wreck with the fuel drums lies.
		const double ReefSide = Rng.Unit() < 0.5 ? -1.0 : 1.0;
		PlaceClear(0.0, 0.0, Rng.RangeF(25.f, 40.f), EIslandKind::Cay, EIslandRole::WreckReef, 120.0,
			[&Rng, Heading, ReefSide]() { return Heading + ReefSide * Rng.Range(0.6, 1.2); },
			[&Rng, BigDist]() { return BigDist * Rng.Range(0.35, 0.6); });

		// Filler islands scattered through the rest of the chain, kept apart so each one reads as its own place.
		const int32_t Fillers = 7 + static_cast<int32_t>(Rng.Unit() * 4.0);
		int32_t Placed = 0;
		for (int32_t Attempt = 0; Attempt < 400 && Placed < Fillers; ++Attempt)
		{
			const double Roll = Rng.Unit();
			const float Radius = Roll < 0.5 ? Rng.RangeF(35.f, 120.f) : (Roll < 0.85 ? Rng.RangeF(120.f, 260.f) : Rng.RangeF(260.f, 380.f));
			const double Angle = Rng.Range(0.0, TwoPi);
			const double Dist = std::sqrt(Rng.Unit()) * (Layout.HomeChainRadius - Radius);
			const double X = std::cos(Angle) * Dist;
			const double Y = std::sin(Angle) * Dist;

			bool bClear = true;
			for (const FIslandSite& Other : FixedSites)
			{
				if (Overlaps(Other, X, Y, Radius, 350.0))
				{
					bClear = false;
					break;
				}
			}
			if (!bClear)
			{
				continue;
			}
			FixedSites.push_back(MakeSite(NextId++, X, Y, Radius, KindFor(Radius, Rng), EIslandRole::Wild, Rng));
			++Placed;
		}

		// Military rocks spaced around the cordon, for watch posts and gun positions.
		const int32_t Outposts = 10;
		const double RingMid = 0.5 * (Layout.CordonInnerRadius + Layout.CordonOuterRadius);
		const double RingHalfWidth = 0.5 * (Layout.CordonOuterRadius - Layout.CordonInnerRadius);
		const double RingStart = Rng.Range(0.0, TwoPi);
		for (int32_t K = 0; K < Outposts; ++K)
		{
			const double Angle = RingStart + TwoPi * (K + Rng.Range(-0.2, 0.2)) / Outposts;
			const double Dist = RingMid + Rng.Range(-0.5, 0.5) * RingHalfWidth;
			FixedSites.push_back(MakeSite(NextId++, std::cos(Angle) * Dist, std::sin(Angle) * Dist, Rng.RangeF(40.f, 75.f),
				EIslandKind::Rocky, EIslandRole::Outpost, Rng));
		}
	}

	std::vector<FIslandSite> FWorld::OpenSeaSitesInCell(int32_t CellX, int32_t CellY) const
	{
		std::vector<FIslandSite> Sites;
		const double Cell = Layout.OpenCellSize;
		const double CellMinX = CellX * Cell;
		const double CellMinY = CellY * Cell;

		// Large, slow noise sorts the open sea into archipelagos and empty stretches of water.
		const double Density = Clamp01(0.45 + 1.3 * Fbm(CellX * 0.15, CellY * 0.15, Seed ^ 0xA5C1U, 3));

		FRng Rng(Mix64((static_cast<uint64_t>(static_cast<uint32_t>(CellX)) << 32 | static_cast<uint32_t>(CellY)) ^ Mix64(Seed)));
		const int32_t Count = static_cast<int32_t>(std::floor(Density * Density * Density * 8.0 + Rng.Unit()));

		for (int32_t K = 0; K < Count; ++K)
		{
			for (int32_t Attempt = 0; Attempt < 12; ++Attempt)
			{
				const double Roll = Rng.Unit();
				const float Radius = Roll < 0.6 ? Rng.RangeF(30.f, 120.f)
					: (Roll < 0.92 ? Rng.RangeF(120.f, 350.f) : Rng.RangeF(350.f, 800.f));
				// Keep each island well inside its cell so islands in neighbouring cells never touch.
				const double Margin = Radius * CoastReach + 150.0;
				if (Margin * 2.0 >= Cell)
				{
					continue;
				}
				const double X = CellMinX + Rng.Range(Margin, Cell - Margin);
				const double Y = CellMinY + Rng.Range(Margin, Cell - Margin);

				// Nothing wild inside or near the cordon: the home chain and the ring are laid out by hand.
				if (std::sqrt(X * X + Y * Y) - Radius < Layout.CordonOuterRadius + 1500.0)
				{
					continue;
				}

				bool bClear = true;
				for (const FIslandSite& Other : Sites)
				{
					if (Overlaps(Other, X, Y, Radius, 300.0))
					{
						bClear = false;
						break;
					}
				}
				if (!bClear)
				{
					continue;
				}

				const uint64_t Id = (1ULL << 63) | (Mix64(Rng.State ^ static_cast<uint64_t>(K)) >> 1);
				Sites.push_back(MakeSite(Id, X, Y, Radius, KindFor(Radius, Rng), EIslandRole::Wild, Rng));
				break;
			}
		}
		return Sites;
	}

	std::vector<FIslandSite> FWorld::SitesNear(double X, double Y, double Range) const
	{
		std::vector<FIslandSite> Found;
		const auto Consider = [&Found, X, Y, Range](const FIslandSite& Site)
		{
			const double Dx = Site.X - X;
			const double Dy = Site.Y - Y;
			if (std::sqrt(Dx * Dx + Dy * Dy) - Site.Radius <= Range)
			{
				Found.push_back(Site);
			}
		};

		for (const FIslandSite& Site : FixedSites)
		{
			Consider(Site);
		}

		// Open-sea islands are never bigger than 800 m across their radius, so cells this much past the range can't reach it.
		const double Reach = Range + 1000.0;
		if (std::sqrt(X * X + Y * Y) + Reach < Layout.CordonOuterRadius)
		{
			return Found;
		}
		const double Cell = Layout.OpenCellSize;
		const int32_t MinCX = static_cast<int32_t>(std::floor((X - Reach) / Cell));
		const int32_t MaxCX = static_cast<int32_t>(std::floor((X + Reach) / Cell));
		const int32_t MinCY = static_cast<int32_t>(std::floor((Y - Reach) / Cell));
		const int32_t MaxCY = static_cast<int32_t>(std::floor((Y + Reach) / Cell));
		for (int32_t CY = MinCY; CY <= MaxCY; ++CY)
		{
			for (int32_t CX = MinCX; CX <= MaxCX; ++CX)
			{
				for (const FIslandSite& Site : OpenSeaSitesInCell(CX, CY))
				{
					Consider(Site);
				}
			}
		}
		return Found;
	}

	void FWorld::GetStartSpawn(double& OutX, double& OutY, float& OutYawDeg) const
	{
		const FIslandSite& Start = FixedSites[0];
		const FIslandSite& Big = FixedSites[1];

		// Out from the start island toward the big island, so turning round from the beach shows where to go next.
		double DirX = Big.X - Start.X;
		double DirY = Big.Y - Start.Y;
		const double Len = std::sqrt(DirX * DirX + DirY * DirY);
		DirX /= Len;
		DirY /= Len;

		// Walk out until the water is deep enough for a boat, then a little further.
		double Dist = 0.0;
		while (Dist < TerrainHalfExtent(Start) && SampleIsland(Start, DirX * Dist, DirY * Dist).Height > -1.5f)
		{
			Dist += 2.0;
		}
		Dist += 15.0;

		OutX = Start.X + DirX * Dist;
		OutY = Start.Y + DirY * Dist;
		OutYawDeg = static_cast<float>(std::atan2(-DirY, -DirX) * 360.0 / TwoPi);
	}

	// --- Island shape ---

	FIslandSample SampleIsland(const FIslandSite& Site, double LocalX, double LocalY)
	{
		const double R = Site.Radius;
		const uint32_t S = Site.Seed;

		// Each island is stretched along its own axis (keeping its area), so they come out long and lean
		// rather than round.
		const double Axis = SeedUnit(S, 30U) * TwoPi;
		const double Stretch = std::sqrt(1.0 + 0.6 * SeedUnit(S, 31U));
		const double Ax = (std::cos(Axis) * LocalX + std::sin(Axis) * LocalY) / Stretch;
		const double Ay = (-std::sin(Axis) * LocalX + std::cos(Axis) * LocalY) * Stretch;

		// Warp the plane so the coast grows bays and headlands instead of being a smooth outline.
		const double WarpScale = R * 0.8;
		const double Wx = Ax + R * 0.25 * Fbm(Ax / WarpScale, Ay / WarpScale, S + 1U, 3);
		const double Wy = Ay + R * 0.25 * Fbm(Ax / WarpScale, Ay / WarpScale, S + 2U, 3);
		double D = std::sqrt(Wx * Wx + Wy * Wy) / R;

		// A second, smaller lobe joined on: a headland, a hooked bay, or a second hill on a saddle.
		const double LobeAngle = SeedUnit(S, 32U) * TwoPi;
		const double LobeDist = R * (0.4 + 0.25 * SeedUnit(S, 33U));
		const double LobeRadius = R * (0.4 + 0.2 * SeedUnit(S, 34U));
		const double Lx = Wx - std::cos(LobeAngle) * LobeDist;
		const double Ly = Wy - std::sin(LobeAngle) * LobeDist;
		const double LobeD = std::sqrt(Lx * Lx + Ly * Ly) / LobeRadius;
		// Smooth minimum, so the two blend through a saddle instead of meeting in a crease.
		const double Blend = 0.25;
		const double Mix = Clamp01(0.5 + 0.5 * (LobeD - D) / Blend);
		D = Lerp(LobeD, D, Mix) - Blend * Mix * (1.0 - Mix);

		D *= 1.0 + 0.22 * Fbm(Wx / (R * 0.5), Wy / (R * 0.5), S + 3U, 4);

		FIslandSample Out;
		Out.Inland = static_cast<float>((1.0 - D) * R);
		const double Inland = Out.Inland;

		// Beach width changes along the coast. Rocky islands are mostly cliff, cays are mostly beach.
		const double CoastNoise = Fbm(Wx / (R * 0.35), Wy / (R * 0.35), S + 4U, 2);
		double Beach = 0.0;
		switch (Site.Kind)
		{
		case EIslandKind::Cay:
			Beach = std::min(R * 0.55, 45.0) * (0.75 + 0.25 * CoastNoise);
			break;
		case EIslandKind::Green:
			Beach = 28.0 * Clamp01(0.6 + 1.2 * CoastNoise);
			break;
		case EIslandKind::Rocky:
		default:
			Beach = 18.0 * Clamp01(-0.15 + 1.4 * CoastNoise);
			break;
		}
		Out.BeachWidth = static_cast<float>(Beach);
		// Narrow beaches end lower, so where the beach runs out the coast becomes a rock ledge.
		const double BeachTop = 1.9 * Clamp01(Beach / 10.0);

		// Low dunes toward the back of the beach, carried on inland so the two meet without a step.
		const double Dunes = 0.6 * (0.5 + 0.5 * Fbm(LocalX / 14.0, LocalY / 14.0, S + 6U, 2));

		double H = 0.0;
		if (Inland < 0.0)
		{
			// Seabed: turquoise sand flats, then the reef edge drops to deep blue water.
			const double Off = -Inland;
			H = -(Off * 0.04 + 2.0 * SmoothStep(0.0, 60.0, Off) + 26.0 * SmoothStep(60.0, 160.0, Off)
				+ 30.0 * SmoothStep(160.0, 280.0, Off));
			H += 0.15 * Noise(LocalX / 6.0, LocalY / 6.0, S + 5U);
		}
		else if (Inland < Beach)
		{
			const double T = Inland / Beach;
			H = BeachTop * std::pow(T, 0.9) + T * T * Dunes;
		}
		else
		{
			const double Span = std::max(1.0, R - Beach);
			const double U = Clamp01((Inland - Beach) / Span);
			const double Base = BeachTop + (Beach > 0.0 ? Dunes : 0.0);
			const double Rise = Site.PeakHeight - Base;
			const double Hills = 0.5 + 0.5 * Fbm(LocalX / (R * 0.45), LocalY / (R * 0.45), S + 7U, 4);
			switch (Site.Kind)
			{
			case EIslandKind::Cay:
				H = Base + Rise * SmoothStep(0.0, 1.0, U) * (0.6 + 0.4 * Hills);
				break;
			case EIslandKind::Green:
				H = Base + Rise * (0.55 * SmoothStep(0.0, 1.0, U) + 0.45 * U * Hills);
				break;
			case EIslandKind::Rocky:
			default:
				// A square-root rise is steepest right at the shore, which makes the cliffs.
				H = Base + Rise * (0.7 * std::sqrt(U) + 0.3 * U * Hills);
				break;
			}
			// Gullies and knolls, fading in from the back of the beach and bigger on higher ground.
			H += std::min(1.0, U * 20.0) * (0.3 + 0.03 * H) * Fbm(LocalX / 9.0, LocalY / 9.0, S + 8U, 3);
		}

		Out.Height = static_cast<float>(H);
		return Out;
	}

	FTerrain BuildIslandTerrain(const FIslandSite& Site)
	{
		FTerrain T;

		// Finer grids on smaller islands, where digging detail matters most. Capped so big islands stay affordable.
		const double Half = TerrainHalfExtent(Site);
		double Spacing = Site.Radius <= 150.f ? 0.5 : (Site.Radius <= 400.f ? 1.0 : 2.0);
		Spacing = std::max(Spacing, 2.0 * Half / 1200.0);
		T.Spacing = static_cast<float>(Spacing);
		T.Size = static_cast<int32_t>(std::ceil(2.0 * Half / Spacing)) + 1;
		T.OriginX = Site.X - Half;
		T.OriginY = Site.Y - Half;

		const size_t Count = static_cast<size_t>(T.Size) * static_cast<size_t>(T.Size);
		T.Height.resize(Count);
		T.Bedrock.resize(Count);
		T.Surface.resize(Count);
		std::vector<float> Inland(Count);
		std::vector<float> BeachWidth(Count);

		for (int32_t Y = 0; Y < T.Size; ++Y)
		{
			for (int32_t X = 0; X < T.Size; ++X)
			{
				const FIslandSample Sample = SampleIsland(Site, X * Spacing - Half, Y * Spacing - Half);
				const int32_t I = T.Index(X, Y);
				T.Height[static_cast<size_t>(I)] = Sample.Height;
				Inland[static_cast<size_t>(I)] = Sample.Inland;
				BeachWidth[static_cast<size_t>(I)] = Sample.BeachWidth;
			}
		}

		// How thick the loose layer is, and what it's made of.
		for (int32_t Y = 0; Y < T.Size; ++Y)
		{
			for (int32_t X = 0; X < T.Size; ++X)
			{
				const size_t I = static_cast<size_t>(T.Index(X, Y));
				const float H = T.Height[I];
				const double Wx = X * Spacing;
				const double Wy = Y * Spacing;

				// Sand reaches a little past the top of the beach, raggedly, before the soil takes over.
				const double SandEdge = BeachWidth[I] + 6.0 + 5.0 * Noise(Wx / 11.0, Wy / 11.0, Site.Seed + 9U);
				const bool bSand = Site.Kind == EIslandKind::Cay || Inland[I] < SandEdge;

				float Depth = 0.f;
				if (bSand)
				{
					Depth = Inland[I] < -70.f ? 1.0f : 2.5f + 0.5f * static_cast<float>(Noise(Wx / 17.0, Wy / 17.0, Site.Seed + 10U));
				}
				else
				{
					Depth = 1.0f + 0.4f * static_cast<float>(Noise(Wx / 13.0, Wy / 13.0, Site.Seed + 11U));
				}

				// Ground too steep to hold loose material is bare rock. Steepness is the drop to the lowest
				// neighbour, the same measure slumping uses, so untouched ground never starts sliding on its own.
				double Slope = 0.0;
				const int32_t Nx[4] = { X + 1, X - 1, X, X };
				const int32_t Ny[4] = { Y, Y, Y + 1, Y - 1 };
				for (int32_t N = 0; N < 4; ++N)
				{
					if (Nx[N] >= 0 && Ny[N] >= 0 && Nx[N] < T.Size && Ny[N] < T.Size)
					{
						Slope = std::max(Slope, (H - T.Height[static_cast<size_t>(T.Index(Nx[N], Ny[N]))]) / Spacing);
					}
				}
				const double Holds = bSand ? (H < 0.f ? WetSandSlope : DrySandSlope) : SoilSlope;

				ESurface Surface = bSand ? ESurface::Sand : ESurface::Soil;
				if (Slope > Holds * 0.95 || (Site.Kind == EIslandKind::Rocky && !bSand && H > 25.f
					&& Noise(Wx / 23.0, Wy / 23.0, Site.Seed + 12U) > 0.25))
				{
					Surface = ESurface::Rock;
					Depth = 0.f;
				}

				T.Surface[I] = Surface;
				T.Bedrock[I] = H - std::min(Depth, MaxLooseDepth);
			}
		}

		T.OriginalHeight = T.Height;
		return T;
	}

	// --- Digging and slumping ---

	FGridRect FTerrain::Grow(const FGridRect& Rect, int32_t By) const
	{
		FGridRect Out;
		Out.MinX = std::max(0, Rect.MinX - By);
		Out.MinY = std::max(0, Rect.MinY - By);
		Out.MaxX = std::min(Size - 1, Rect.MaxX + By);
		Out.MaxY = std::min(Size - 1, Rect.MaxY + By);
		return Out;
	}

	FGridRect FTerrain::RectAround(double WorldX, double WorldY, float Radius) const
	{
		FGridRect Rect;
		const double Gx = (WorldX - OriginX) / Spacing;
		const double Gy = (WorldY - OriginY) / Spacing;
		const double Gr = Radius / Spacing;
		Rect.MinX = std::max(0, static_cast<int32_t>(std::floor(Gx - Gr)));
		Rect.MinY = std::max(0, static_cast<int32_t>(std::floor(Gy - Gr)));
		Rect.MaxX = std::min(Size - 1, static_cast<int32_t>(std::ceil(Gx + Gr)));
		Rect.MaxY = std::min(Size - 1, static_cast<int32_t>(std::ceil(Gy + Gr)));
		return Rect;
	}

	float FTerrain::SampleHeight(double WorldX, double WorldY) const
	{
		const double Gx = (WorldX - OriginX) / Spacing;
		const double Gy = (WorldY - OriginY) / Spacing;
		if (Gx < 0.0 || Gy < 0.0 || Gx > Size - 1 || Gy > Size - 1)
		{
			return -100000.f;
		}
		const int32_t X0 = std::min(Size - 2, static_cast<int32_t>(Gx));
		const int32_t Y0 = std::min(Size - 2, static_cast<int32_t>(Gy));
		const double Tx = Gx - X0;
		const double Ty = Gy - Y0;
		const double H00 = Height[static_cast<size_t>(Index(X0, Y0))];
		const double H10 = Height[static_cast<size_t>(Index(X0 + 1, Y0))];
		const double H01 = Height[static_cast<size_t>(Index(X0, Y0 + 1))];
		const double H11 = Height[static_cast<size_t>(Index(X0 + 1, Y0 + 1))];
		return static_cast<float>(Lerp(Lerp(H00, H10, Tx), Lerp(H01, H11, Tx), Ty));
	}

	double FTerrain::TotalVolume() const
	{
		double Sum = 0.0;
		for (const float H : Height)
		{
			Sum += H;
		}
		return Sum * Spacing * Spacing;
	}

	float FTerrain::Dig(double WorldX, double WorldY, float Radius, float Depth)
	{
		// A scoop has to span a few vertices or it just pokes a spike-shaped dent.
		Radius = std::max(Radius, Spacing * 1.5f);
		const FGridRect Rect = RectAround(WorldX, WorldY, Radius);
		const double Area = static_cast<double>(Spacing) * Spacing;
		double Removed = 0.0;
		FGridRect Touched;

		for (int32_t Y = Rect.MinY; Y <= Rect.MaxY; ++Y)
		{
			for (int32_t X = Rect.MinX; X <= Rect.MaxX; ++X)
			{
				const double Dx = OriginX + X * static_cast<double>(Spacing) - WorldX;
				const double Dy = OriginY + Y * static_cast<double>(Spacing) - WorldY;
				const double R2 = (Dx * Dx + Dy * Dy) / (static_cast<double>(Radius) * Radius);
				if (R2 >= 1.0)
				{
					continue;
				}
				const size_t I = static_cast<size_t>(Index(X, Y));
				const float Want = Depth * static_cast<float>(1.0 - R2);
				const float Take = std::min(Want, std::max(0.f, Height[I] - Bedrock[I]));
				if (Take <= 0.f)
				{
					continue;
				}
				Height[I] -= Take;
				Removed += Take * Area;
				Touched.Include(X, Y);
			}
		}

		if (!Touched.IsEmpty())
		{
			bEdited = true;
			NoteChange(Touched);
			WakeUp(Touched);
		}
		return static_cast<float>(Removed);
	}

	float FTerrain::Deposit(double WorldX, double WorldY, float Radius, float Volume)
	{
		Radius = std::max(Radius, Spacing * 1.5f);
		const FGridRect Rect = RectAround(WorldX, WorldY, Radius);
		if (Rect.IsEmpty() || Volume <= 0.f)
		{
			return 0.f;
		}

		double WeightSum = 0.0;
		for (int32_t Pass = 0; Pass < 2; ++Pass)
		{
			for (int32_t Y = Rect.MinY; Y <= Rect.MaxY; ++Y)
			{
				for (int32_t X = Rect.MinX; X <= Rect.MaxX; ++X)
				{
					const double Dx = OriginX + X * static_cast<double>(Spacing) - WorldX;
					const double Dy = OriginY + Y * static_cast<double>(Spacing) - WorldY;
					const double W = 1.0 - (Dx * Dx + Dy * Dy) / (static_cast<double>(Radius) * Radius);
					if (W <= 0.0)
					{
						continue;
					}
					if (Pass == 0)
					{
						WeightSum += W;
					}
					else
					{
						Height[static_cast<size_t>(Index(X, Y))] += static_cast<float>(Volume * W / (WeightSum * Spacing * Spacing));
					}
				}
			}
			if (WeightSum <= 0.0)
			{
				return 0.f;
			}
		}

		bEdited = true;
		NoteChange(Rect);
		WakeUp(Rect);
		return Volume;
	}

	float FTerrain::GetReposeSlope(int32_t I) const
	{
		const size_t Idx = static_cast<size_t>(I);
		if (Surface[Idx] == ESurface::Soil)
		{
			return SoilSlope;
		}
		// Sand, or loose material dumped on rock.
		const float H = Height[Idx];
		if (H < 0.f)
		{
			return WetSandSlope;
		}
		return H < DampBand ? DampSandSlope : DrySandSlope;
	}

	void FTerrain::WakeUp(const FGridRect& Rect)
	{
		if (!bSettling)
		{
			Settling = FGridRect();
		}
		// The ground around the change may now be too steep, as well as the change itself.
		Settling.Include(Grow(Rect, 1));
		bSettling = true;
		SettlePassesLeft = MaxSettlePasses;
	}

	void FTerrain::NoteChange(const FGridRect& Rect)
	{
		Changed.Include(Rect);
	}

	bool FTerrain::TakeChanges(FGridRect& OutRect)
	{
		if (Changed.IsEmpty())
		{
			return false;
		}
		OutRect = Changed;
		Changed = FGridRect();
		return true;
	}

	bool FTerrain::Settle(int32_t Passes)
	{
		for (int32_t P = 0; P < Passes && bSettling; ++P)
		{
			bSettling = SettlePass() && --SettlePassesLeft > 0;
		}
		return bSettling;
	}

	bool FTerrain::SettlePass()
	{
		// Thermal-erosion style: wherever loose ground is steeper than it can stand, part of the excess slides
		// downhill to its lower neighbours. Moves are gathered first and applied after, so the pass doesn't
		// depend on the order cells are visited and the total volume never changes.
		if (Scratch.size() != Height.size())
		{
			Scratch.assign(Height.size(), 0.f);
		}

		const int32_t MinX = std::max(0, Settling.MinX - 1);
		const int32_t MinY = std::max(0, Settling.MinY - 1);
		const int32_t MaxX = std::min(Size - 1, Settling.MaxX + 1);
		const int32_t MaxY = std::min(Size - 1, Settling.MaxY + 1);

		constexpr int32_t NX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		constexpr int32_t NY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		const float Dist[8] = { Spacing, Spacing, Spacing, Spacing,
			Spacing * 1.41421356f, Spacing * 1.41421356f, Spacing * 1.41421356f, Spacing * 1.41421356f };

		bool bAnyMove = false;
		for (int32_t Y = Settling.MinY; Y <= Settling.MaxY; ++Y)
		{
			for (int32_t X = Settling.MinX; X <= Settling.MaxX; ++X)
			{
				const int32_t I = Index(X, Y);
				const size_t Idx = static_cast<size_t>(I);
				const float Loose = Height[Idx] - Bedrock[Idx];
				if (Loose <= 1e-5f)
				{
					continue;
				}
				const float Repose = GetReposeSlope(I);

				float Excess[8];
				float Total = 0.f;
				float Largest = 0.f;
				for (int32_t N = 0; N < 8; ++N)
				{
					Excess[N] = 0.f;
					const int32_t Nx = X + NX[N];
					const int32_t Ny = Y + NY[N];
					if (Nx < 0 || Ny < 0 || Nx >= Size || Ny >= Size)
					{
						continue;
					}
					const float Drop = Height[Idx] - Height[static_cast<size_t>(Index(Nx, Ny))] - Repose * Dist[N];
					if (Drop > 0.f)
					{
						Excess[N] = Drop;
						Total += Drop;
						Largest = std::max(Largest, Drop);
					}
				}
				if (Total <= 0.f)
				{
					continue;
				}

				// A quarter of the excess per pass: stable, and slow enough to watch the sand run.
				const float Move = std::min(Loose, 0.25f * Largest);
				if (Move < 1e-6f)
				{
					continue;
				}
				bAnyMove = true;
				Scratch[Idx] -= Move;
				for (int32_t N = 0; N < 8; ++N)
				{
					if (Excess[N] > 0.f)
					{
						Scratch[static_cast<size_t>(Index(X + NX[N], Y + NY[N]))] += Move * Excess[N] / Total;
					}
				}
			}
		}

		if (!bAnyMove)
		{
			return false;
		}

		FGridRect Moved;
		float Biggest = 0.f;
		for (int32_t Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32_t X = MinX; X <= MaxX; ++X)
			{
				const size_t Idx = static_cast<size_t>(Index(X, Y));
				const float D = Scratch[Idx];
				if (D != 0.f)
				{
					Height[Idx] += D;
					Scratch[Idx] = 0.f;
					Biggest = std::max(Biggest, std::fabs(D));
					Moved.Include(X, Y);
				}
			}
		}

		NoteChange(Moved);
		// Keep working only where things moved, plus a border: a cell uphill of one that lost sand may now be too steep.
		Settling = Grow(Moved, 1);
		return Biggest > 2e-4f;
	}

	// --- Foliage ---

	std::vector<FScatterPoint> ScatterFoliage(const FTerrain& Terrain, const FIslandSite& Site)
	{
		std::vector<FScatterPoint> Points;
		constexpr double Cell = 6.0;
		const double Span = (Terrain.Size - 1) * static_cast<double>(Terrain.Spacing);
		const int32_t Cells = static_cast<int32_t>(Span / Cell);

		for (int32_t CY = 0; CY < Cells; ++CY)
		{
			for (int32_t CX = 0; CX < Cells; ++CX)
			{
				FRng Rng(Mix64((static_cast<uint64_t>(static_cast<uint32_t>(CX)) << 32 | static_cast<uint32_t>(CY)) ^ Site.Seed));
				const double X = Terrain.OriginX + (CX + Rng.Unit()) * Cell;
				const double Y = Terrain.OriginY + (CY + Rng.Unit()) * Cell;
				const float Z = Terrain.SampleHeight(X, Y);
				if (Z < 0.7f)
				{
					continue;
				}

				const int32_t Gx = static_cast<int32_t>((X - Terrain.OriginX) / Terrain.Spacing);
				const int32_t Gy = static_cast<int32_t>((Y - Terrain.OriginY) / Terrain.Spacing);
				const ESurface Ground = Terrain.Surface[static_cast<size_t>(Terrain.Index(Gx, Gy))];
				// Jungle grows in clumps, with clearings between.
				const double Clump = 0.5 + 0.5 * Fbm(X / 40.0, Y / 40.0, Site.Seed + 20U, 2);
				const double Roll = Rng.Unit();

				EScatter Kind = EScatter::Count;
				if (Ground == ESurface::Rock)
				{
					if (Roll < 0.08)
					{
						Kind = EScatter::Boulder;
					}
				}
				else if (Ground == ESurface::Sand)
				{
					// Palms line the back of the beach; bare sand near the water.
					if (Z > 1.2f && Roll < 0.18 * Clump)
					{
						Kind = EScatter::Palm;
					}
					else if (Z > 1.5f && Roll < 0.25 * Clump)
					{
						Kind = EScatter::Bush;
					}
				}
				else
				{
					const double Lowland = Z < 30.f ? 1.0 : 0.3;
					if (Roll < 0.12 * Lowland * Clump)
					{
						Kind = EScatter::Palm;
					}
					else if (Roll < 0.12 * Lowland * Clump + 0.35 * Clump)
					{
						Kind = EScatter::Tree;
					}
					else if (Roll < 0.12 * Lowland * Clump + 0.35 * Clump + 0.3)
					{
						Kind = EScatter::Bush;
					}
				}
				if (Kind == EScatter::Count)
				{
					continue;
				}

				FScatterPoint Point;
				Point.X = X;
				Point.Y = Y;
				Point.Z = Z;
				Point.YawDeg = Rng.RangeF(0.f, 360.f);
				Point.Scale = Rng.RangeF(0.75f, 1.25f);
				Point.Kind = Kind;
				Points.push_back(Point);
			}
		}
		return Points;
	}

	// --- Names ---

	const char* ZoneName(EZone Zone)
	{
		switch (Zone)
		{
		case EZone::HomeChain: return "home chain";
		case EZone::Cordon: return "military cordon";
		case EZone::OpenSea: return "open sea";
		}
		return "?";
	}

	const char* RoleName(EIslandRole Role)
	{
		switch (Role)
		{
		case EIslandRole::Wild: return "wild";
		case EIslandRole::Start: return "start";
		case EIslandRole::BigIsland: return "big island";
		case EIslandRole::FarIsland: return "far island";
		case EIslandRole::WreckReef: return "wreck reef";
		case EIslandRole::Outpost: return "military outpost";
		}
		return "?";
	}

	const char* KindName(EIslandKind Kind)
	{
		switch (Kind)
		{
		case EIslandKind::Cay: return "cay";
		case EIslandKind::Green: return "green";
		case EIslandKind::Rocky: return "rocky";
		}
		return "?";
	}
}
