#pragma once

// The world generator's maths: where islands sit, what shape they are, and how their sand moves when dug.
//
// Plain C++ with no engine headers, so it also builds and runs outside Unreal: Tools/worldgen_test.cpp tests it
// and draws preview maps from it. ARiptideWorldDirector and ARiptideIsland turn it into actors.
//
// Units are metres, Z up, sea level at 0. Everything is deterministic from the world seed: the same seed gives
// the same islands on every machine, so islands never need saving or sending over the network, only what
// players change on them.

#include <cstdint>
#include <vector>

namespace RiptideGen
{
	/** Rings of the map, measured from the start island. */
	enum class EZone : uint8_t
	{
		HomeChain,	// the starting island chain, inside the military cordon
		Cordon,		// the military ring around it
		OpenSea		// past the cordon: islands without end
	};

	enum class EIslandKind : uint8_t
	{
		Cay,	// low sand island: beach, dunes and palms
		Green,	// hilly island with beaches and jungle
		Rocky	// steep island with cliffs and only a few coves
	};

	enum class EIslandRole : uint8_t
	{
		Wild,		// filler island with no story role
		Start,		// where the crew washes up
		BigIsland,	// the first big island, close enough to row to
		FarIsland,	// as far past the big island as the big island is from the start
		WreckReef,	// a sand bar between the start and the big island, for a wreck
		Outpost		// a military rock in the cordon
	};

	enum class ESurface : uint8_t
	{
		Sand,	// beaches, cays and the seabed
		Soil,	// inland ground under the jungle
		Rock	// cliffs and steep ground: nothing loose to dig
	};

	enum class EScatter : uint8_t
	{
		Palm,
		Tree,
		Bush,
		Boulder,
		Count
	};

	/** Spots on an island where hand-built places go. */
	enum class EPlace : uint8_t
	{
		Landing,	// a gentle beach where a boat can be run ashore; faces out to sea
		Camp,		// flat, dry ground near a landing, for a camp, shack or base
		Bunker,		// flat ground inland on bigger islands, for a bunker hatch
		Lookout,	// the island's high point, for a watchtower or radio mast
		Wreck,		// shallow sand flats offshore, for a wrecked boat; faces along the shore
		Outpost,	// the flattest high ground on a cordon rock, for a military post
		Count
	};

	/** Sizes of the map's rings, in metres from the start island. */
	struct FWorldLayout
	{
		double HomeChainRadius = 4500.0;
		double CordonInnerRadius = 6500.0;
		double CordonOuterRadius = 9500.0;
		/** Open sea is generated in square cells this wide, each holding a handful of islands or none. */
		double OpenCellSize = 4000.0;
	};

	struct FIslandSite
	{
		uint64_t Id = 0;
		double X = 0.0;
		double Y = 0.0;
		/** Rough shoreline radius. The real coast wanders in and out around it. */
		float Radius = 100.f;
		float PeakHeight = 10.f;
		uint32_t Seed = 0;
		EIslandKind Kind = EIslandKind::Cay;
		EIslandRole Role = EIslandRole::Wild;
	};

	/** What the generator says about one spot on an island, before anyone digs. */
	struct FIslandSample
	{
		float Height = 0.f;
		/** Metres inland from the shoreline (negative offshore). */
		float Inland = 0.f;
		/** Width of the beach at this stretch of coast; 0 where the coast is rock. */
		float BeachWidth = 0.f;
	};

	struct FScatterPoint
	{
		double X = 0.0;
		double Y = 0.0;
		float Z = 0.f;
		float YawDeg = 0.f;
		float Scale = 1.f;
		EScatter Kind = EScatter::Palm;
	};

	struct FPlace
	{
		double X = 0.0;
		double Y = 0.0;
		float Z = 0.f;
		float YawDeg = 0.f;
		EPlace Kind = EPlace::Landing;
	};

	/** Inclusive rectangle of grid vertices. */
	struct FGridRect
	{
		int32_t MinX = 0;
		int32_t MinY = 0;
		int32_t MaxX = -1;
		int32_t MaxY = -1;

		bool IsEmpty() const { return MaxX < MinX || MaxY < MinY; }
		void Include(int32_t X, int32_t Y);
		void Include(const FGridRect& Other);
	};

	/** Smooth noise in about -1..1. Same inputs, same answer, on every platform. */
	double Noise(double X, double Y, uint32_t Seed);
	double Fbm(double X, double Y, uint32_t Seed, int32_t Octaves);

	/** The whole world for one seed: the fixed home chain, the cordon, and the open sea beyond. */
	class FWorld
	{
	public:
		FWorld(uint32_t InSeed, const FWorldLayout& InLayout = FWorldLayout());

		uint32_t GetSeed() const { return Seed; }
		const FWorldLayout& GetLayout() const { return Layout; }

		EZone ZoneAt(double X, double Y) const;

		/** Home chain and cordon outposts, in a fixed order. The first one is the start island. */
		const std::vector<FIslandSite>& GetFixedSites() const { return FixedSites; }
		const FIslandSite& GetStartIsland() const { return FixedSites[0]; }

		/** Open-sea islands whose centres fall in one cell. */
		std::vector<FIslandSite> OpenSeaSitesInCell(int32_t CellX, int32_t CellY) const;

		/** Every island whose shoreline comes within Range of the point. */
		std::vector<FIslandSite> SitesNear(double X, double Y, double Range) const;

		/** Where a boat starts: in water deep enough to float, off the start island's beach, facing it. */
		void GetStartSpawn(double& OutX, double& OutY, float& OutYawDeg) const;

	private:
		void BuildFixedSites();

		uint32_t Seed;
		FWorldLayout Layout;
		std::vector<FIslandSite> FixedSites;
	};

	/** The island's untouched ground at a point, relative to its centre. */
	FIslandSample SampleIsland(const FIslandSite& Site, double LocalX, double LocalY);

	/**
	 * An island's ground as a square grid of heights, with a layer of loose sand or soil over bedrock.
	 * Digging takes loose material off the top and piling puts it back; Settle() then lets it slump the way
	 * real sand does, so a hole's walls cave in until they stand at the material's natural slope, and a dumped
	 * pile spreads into a cone. Nothing digs below bedrock.
	 */
	class FTerrain
	{
	public:
		int32_t Size = 0;
		float Spacing = 1.f;
		double OriginX = 0.0;
		double OriginY = 0.0;

		std::vector<float> Height;
		std::vector<float> Bedrock;
		std::vector<float> OriginalHeight;
		std::vector<ESurface> Surface;

		int32_t Index(int32_t X, int32_t Y) const { return Y * Size + X; }

		/** Ground height under a world point, or a very low number off the grid. */
		float SampleHeight(double WorldX, double WorldY) const;

		/** Sum of every vertex's height times its area, in cubic metres. Changes only when digging or piling. */
		double TotalVolume() const;

		/** Scoops a bowl out of the ground. Returns the cubic metres actually removed (less near bedrock). */
		float Dig(double WorldX, double WorldY, float Radius, float Depth);

		/** Drops cubic metres of loose material in a mound. Returns what was placed (0 off the grid). */
		float Deposit(double WorldX, double WorldY, float Radius, float Volume);

		/** Runs passes of slumping. Returns true while material is still moving. */
		bool Settle(int32_t Passes);
		bool IsSettling() const { return bSettling; }

		/** Vertices whose height changed since the last call. Returns false if none did. */
		bool TakeChanges(FGridRect& OutRect);

		/** Whether any digging or piling has happened, so the island needs remembering when it unloads. */
		bool IsEdited() const { return bEdited; }

		/** The natural slope (rise over run) the loose material at a vertex will stand at. */
		float GetReposeSlope(int32_t I) const;

	private:
		bool SettlePass();
		void WakeUp(const FGridRect& Rect);
		void NoteChange(const FGridRect& Rect);
		FGridRect RectAround(double WorldX, double WorldY, float Radius) const;
		FGridRect Grow(const FGridRect& Rect, int32_t By) const;

		FGridRect Settling;
		FGridRect Changed;
		std::vector<float> Scratch;
		int32_t SettlePassesLeft = 0;
		bool bSettling = false;
		bool bEdited = false;
	};

	/** Generates an island's terrain. Big islands take tens of milliseconds, so the game runs this off the main thread. */
	FTerrain BuildIslandTerrain(const FIslandSite& Site);

	/** Where trees, palms, bushes and boulders grow on an island. */
	std::vector<FScatterPoint> ScatterFoliage(const FTerrain& Terrain, const FIslandSite& Site);

	/**
	 * Picks the spots for hand-built places on an island, from its untouched ground. Every island gets at least
	 * one landing; which other places it gets depends on its size, shape and role (see Docs/WORLD.md).
	 */
	std::vector<FPlace> FindPlaces(const FTerrain& Terrain, const FIslandSite& Site);

	const char* PlaceName(EPlace Place);
	const char* ZoneName(EZone Zone);
	const char* RoleName(EIslandRole Role);
	const char* KindName(EIslandKind Kind);
}
