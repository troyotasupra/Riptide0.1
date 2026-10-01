// Tests the island generator outside Unreal, and draws preview maps of the world it makes.
//
// Build and run from the project folder (any C++17 compiler: clang on macOS, clang or MSVC on Windows):
//   c++ -std=c++17 -O2 -ISource/Riptide Tools/worldgen_test.cpp Source/Riptide/RiptideWorldGen.cpp -o worldgen_test
//   ./worldgen_test [seed] [output folder]
// It prints PASS or FAIL for each check, and writes PNG previews into the output folder (default: Saved/WorldPreview).

#include "RiptideWorldGen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace RiptideGen;

namespace
{
	int Failures = 0;

	void Check(bool bOk, const std::string& What)
	{
		std::printf("%s  %s\n", bOk ? "PASS" : "FAIL", What.c_str());
		if (!bOk)
		{
			++Failures;
		}
	}

	// --- Minimal PNG writer (uncompressed deflate), so the tool needs no libraries ---

	uint32_t Crc(const unsigned char* Data, size_t Len, uint32_t C = 0xffffffffU)
	{
		for (size_t I = 0; I < Len; ++I)
		{
			C ^= Data[I];
			for (int K = 0; K < 8; ++K)
			{
				C = (C >> 1) ^ (0xedb88320U & (0U - (C & 1U)));
			}
		}
		return C;
	}

	void Put32(std::vector<unsigned char>& Out, uint32_t V)
	{
		Out.push_back(static_cast<unsigned char>(V >> 24));
		Out.push_back(static_cast<unsigned char>(V >> 16));
		Out.push_back(static_cast<unsigned char>(V >> 8));
		Out.push_back(static_cast<unsigned char>(V));
	}

	void Chunk(std::vector<unsigned char>& Out, const char* Type, const std::vector<unsigned char>& Data)
	{
		Put32(Out, static_cast<uint32_t>(Data.size()));
		std::vector<unsigned char> Body(Type, Type + 4);
		Body.insert(Body.end(), Data.begin(), Data.end());
		Out.insert(Out.end(), Body.begin(), Body.end());
		Put32(Out, Crc(Body.data(), Body.size()) ^ 0xffffffffU);
	}

	struct FImage
	{
		int W = 0;
		int H = 0;
		std::vector<unsigned char> Rgb;

		FImage(int InW, int InH) : W(InW), H(InH), Rgb(static_cast<size_t>(InW) * InH * 3, 0) {}

		void Set(int X, int Y, int R, int G, int B)
		{
			if (X < 0 || Y < 0 || X >= W || Y >= H)
			{
				return;
			}
			const size_t I = (static_cast<size_t>(Y) * W + X) * 3;
			Rgb[I] = static_cast<unsigned char>(std::clamp(R, 0, 255));
			Rgb[I + 1] = static_cast<unsigned char>(std::clamp(G, 0, 255));
			Rgb[I + 2] = static_cast<unsigned char>(std::clamp(B, 0, 255));
		}

		void Disc(int Cx, int Cy, int Radius, int R, int G, int B)
		{
			for (int Y = -Radius; Y <= Radius; ++Y)
			{
				for (int X = -Radius; X <= Radius; ++X)
				{
					if (X * X + Y * Y <= Radius * Radius)
					{
						Set(Cx + X, Cy + Y, R, G, B);
					}
				}
			}
		}

		void Ring(int Cx, int Cy, double Radius, int R, int G, int B)
		{
			const int Steps = std::max(64, static_cast<int>(Radius * 8.0));
			for (int K = 0; K < Steps; ++K)
			{
				const double A = 6.283185307 * K / Steps;
				Set(Cx + static_cast<int>(std::lround(std::cos(A) * Radius)), Cy + static_cast<int>(std::lround(std::sin(A) * Radius)), R, G, B);
			}
		}

		bool Save(const std::string& Path) const
		{
			std::vector<unsigned char> Raw;
			for (int Y = 0; Y < H; ++Y)
			{
				Raw.push_back(0);
				Raw.insert(Raw.end(), Rgb.begin() + static_cast<long>(Y) * W * 3, Rgb.begin() + static_cast<long>(Y + 1) * W * 3);
			}

			std::vector<unsigned char> Z = { 0x78, 0x01 };
			uint32_t A = 1;
			uint32_t B = 0;
			for (const unsigned char C : Raw)
			{
				A = (A + C) % 65521U;
				B = (B + A) % 65521U;
			}
			for (size_t Pos = 0; Pos < Raw.size() || Pos == 0; Pos += 65535)
			{
				const size_t Len = std::min<size_t>(65535, Raw.size() - Pos);
				Z.push_back(Pos + Len >= Raw.size() ? 1 : 0);
				Z.push_back(static_cast<unsigned char>(Len));
				Z.push_back(static_cast<unsigned char>(Len >> 8));
				Z.push_back(static_cast<unsigned char>(~Len));
				Z.push_back(static_cast<unsigned char>(~Len >> 8));
				Z.insert(Z.end(), Raw.begin() + static_cast<long>(Pos), Raw.begin() + static_cast<long>(Pos + Len));
			}
			Put32(Z, (B << 16) | A);

			std::vector<unsigned char> Out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
			std::vector<unsigned char> Header;
			Put32(Header, static_cast<uint32_t>(W));
			Put32(Header, static_cast<uint32_t>(H));
			Header.insert(Header.end(), { 8, 2, 0, 0, 0 });
			Chunk(Out, "IHDR", Header);
			Chunk(Out, "IDAT", Z);
			Chunk(Out, "IEND", {});

			FILE* File = std::fopen(Path.c_str(), "wb");
			if (!File)
			{
				return false;
			}
			std::fwrite(Out.data(), 1, Out.size(), File);
			std::fclose(File);
			return true;
		}
	};

	// --- Colours ---

	void SeaColour(float H, int& R, int& G, int& B)
	{
		// Clear turquoise over the sand flats, darkening to deep blue off the reef.
		const double T = std::clamp(-H / 35.0, 0.0, 1.0);
		R = static_cast<int>(70 - 55 * T);
		G = static_cast<int>(200 - 140 * T);
		B = static_cast<int>(200 - 60 * T);
	}

	void GroundColour(float H, ESurface Surface, bool bExposedRock, int& R, int& G, int& B)
	{
		if (H < 0.f)
		{
			SeaColour(H, R, G, B);
			return;
		}
		if (bExposedRock || Surface == ESurface::Rock)
		{
			R = 120; G = 115; B = 105;
		}
		else if (Surface == ESurface::Sand)
		{
			const bool bWet = H < 0.5f;
			R = bWet ? 190 : 235; G = bWet ? 175 : 220; B = bWet ? 140 : 175;
		}
		else
		{
			const double T = std::clamp(H / 150.0, 0.0, 1.0);
			R = static_cast<int>(60 + 50 * T); G = static_cast<int>(120 - 20 * T); B = static_cast<int>(50 + 20 * T);
		}
	}

	void LandColourFromHeight(const FIslandSite& Site, float H, int& R, int& G, int& B)
	{
		if (H < 0.f)
		{
			SeaColour(H, R, G, B);
		}
		else if (H < 2.2f || (Site.Kind == EIslandKind::Cay && H < 4.f))
		{
			R = 235; G = 220; B = 175;
		}
		else
		{
			const double T = std::clamp(H / Site.PeakHeight, 0.0f, 1.0f);
			const bool bRocky = Site.Kind == EIslandKind::Rocky;
			R = static_cast<int>((bRocky ? 105 : 60) + 50 * T);
			G = static_cast<int>((bRocky ? 100 : 125) - 10 * T);
			B = static_cast<int>((bRocky ? 85 : 50) + 20 * T);
		}
	}

	void RoleMarker(const FIslandSite& Site, int& R, int& G, int& B)
	{
		switch (Site.Role)
		{
		case EIslandRole::Start: R = 255; G = 230; B = 0; break;
		case EIslandRole::BigIsland: R = 255; G = 140; B = 0; break;
		case EIslandRole::FarIsland: R = 200; G = 80; B = 255; break;
		case EIslandRole::WreckReef: R = 255; G = 255; B = 255; break;
		case EIslandRole::Outpost: R = 255; G = 30; B = 30; break;
		default: R = -1; G = -1; B = -1; break;
		}
	}

	/** Top-down map of a square of the world. Islands are drawn from their real shapes once big enough to see. */
	void DrawWorld(const FWorld& World, double CentreX, double CentreY, double HalfSpan, int Pixels, const std::string& Path)
	{
		FImage Img(Pixels, Pixels);
		const double MetresPerPixel = 2.0 * HalfSpan / Pixels;
		const FWorldLayout& L = World.GetLayout();

		for (int Py = 0; Py < Pixels; ++Py)
		{
			for (int Px = 0; Px < Pixels; ++Px)
			{
				const double X = CentreX - HalfSpan + (Px + 0.5) * MetresPerPixel;
				const double Y = CentreY + HalfSpan - (Py + 0.5) * MetresPerPixel;
				int R = 15, G = 50, B = 120;
				switch (World.ZoneAt(X, Y))
				{
				case EZone::HomeChain: R = 20; G = 60; B = 130; break;
				case EZone::Cordon: R = 70; G = 35; B = 85; break;
				case EZone::OpenSea: break;
				}
				Img.Set(Px, Py, R, G, B);
			}
		}

		const auto ToPx = [&](double X, double Y, int& Px, int& Py)
		{
			Px = static_cast<int>((X - (CentreX - HalfSpan)) / MetresPerPixel);
			Py = static_cast<int>(((CentreY + HalfSpan) - Y) / MetresPerPixel);
		};

		int Cx = 0;
		int Cy = 0;
		ToPx(0.0, 0.0, Cx, Cy);
		Img.Ring(Cx, Cy, L.HomeChainRadius / MetresPerPixel, 90, 140, 200);

		const std::vector<FIslandSite> Sites = World.SitesNear(CentreX, CentreY, HalfSpan * 1.42);
		for (const FIslandSite& Site : Sites)
		{
			int Px = 0;
			int Py = 0;
			ToPx(Site.X, Site.Y, Px, Py);
			const double Extent = Site.Radius * 1.6;
			const int Reach = static_cast<int>(Extent / MetresPerPixel) + 1;
			if (Reach <= 2)
			{
				int R = 0, G = 0, B = 0;
				LandColourFromHeight(Site, Site.PeakHeight * 0.5f, R, G, B);
				Img.Disc(Px, Py, std::max(1, Reach - 1), R, G, B);
			}
			else
			{
				for (int Dy = -Reach; Dy <= Reach; ++Dy)
				{
					for (int Dx = -Reach; Dx <= Reach; ++Dx)
					{
						const double Lx = Dx * MetresPerPixel;
						const double Ly = -Dy * MetresPerPixel;
						const float H = SampleIsland(Site, Lx, Ly).Height;
						if (H < -8.f)
						{
							continue;
						}
						int R = 0, G = 0, B = 0;
						LandColourFromHeight(Site, H, R, G, B);
						Img.Set(Px + Dx, Py + Dy, R, G, B);
					}
				}
			}
			int R = 0, G = 0, B = 0;
			RoleMarker(Site, R, G, B);
			if (R >= 0)
			{
				Img.Ring(Px, Py, std::max(5.0, Extent / MetresPerPixel + 3.0), R, G, B);
			}
		}

		Check(Img.Save(Path), "wrote " + Path);
	}

	/** Shaded relief of one island's terrain grid, with foliage dots and place markers. */
	void DrawTerrain(const FTerrain& T, const std::vector<FScatterPoint>* Scatter, int Pixels, const std::string& Path,
		const std::vector<FPlace>* Places = nullptr)
	{
		FImage Img(Pixels, Pixels);
		const double Scale = static_cast<double>(T.Size - 1) / Pixels;
		for (int Py = 0; Py < Pixels; ++Py)
		{
			for (int Px = 0; Px < Pixels; ++Px)
			{
				const int X = std::min(T.Size - 2, static_cast<int>(Px * Scale));
				const int Y = std::min(T.Size - 2, static_cast<int>((Pixels - 1 - Py) * Scale));
				const size_t I = static_cast<size_t>(T.Index(X, Y));
				const float H = T.Height[I];
				int R = 0, G = 0, B = 0;
				const bool bExposed = H <= T.Bedrock[I] + 0.02f && T.OriginalHeight[I] - H > 0.05f;
				GroundColour(H, T.Surface[I], bExposed, R, G, B);
				if (H >= 0.f)
				{
					// Light from the north-west.
					const float Dzx = T.Height[static_cast<size_t>(T.Index(X + 1, Y))] - H;
					const float Dzy = T.Height[static_cast<size_t>(T.Index(X, Y + 1))] - H;
					const double Shade = std::clamp(1.0 + 1.5 * (-Dzx + Dzy) / T.Spacing, 0.55, 1.3);
					R = static_cast<int>(R * Shade); G = static_cast<int>(G * Shade); B = static_cast<int>(B * Shade);
				}
				Img.Set(Px, Py, R, G, B);
			}
		}
		if (Scatter)
		{
			for (const FScatterPoint& P : *Scatter)
			{
				const int Px = static_cast<int>((P.X - T.OriginX) / T.Spacing / Scale);
				const int Py = Pixels - 1 - static_cast<int>((P.Y - T.OriginY) / T.Spacing / Scale);
				switch (P.Kind)
				{
				case EScatter::Palm: Img.Disc(Px, Py, 1, 20, 90, 20); break;
				case EScatter::Tree: Img.Disc(Px, Py, 1, 10, 60, 15); break;
				case EScatter::Bush: Img.Set(Px, Py, 50, 110, 30); break;
				case EScatter::Boulder: Img.Set(Px, Py, 90, 90, 90); break;
				default: break;
				}
			}
		}
		if (Places)
		{
			for (const FPlace& P : *Places)
			{
				const int Px = static_cast<int>((P.X - T.OriginX) / T.Spacing / Scale);
				const int Py = Pixels - 1 - static_cast<int>((P.Y - T.OriginY) / T.Spacing / Scale);
				int R = 255, G = 255, B = 255;
				switch (P.Kind)
				{
				case EPlace::Landing: R = 255; G = 255; B = 255; break;
				case EPlace::Camp: R = 255; G = 150; B = 0; break;
				case EPlace::Bunker: R = 230; G = 30; B = 30; break;
				case EPlace::Lookout: R = 160; G = 60; B = 255; break;
				case EPlace::Wreck: R = 20; G = 20; B = 20; break;
				default: R = 255; G = 0; B = 255; break;
				}
				Img.Disc(Px, Py, 7, 0, 0, 0);
				Img.Disc(Px, Py, 5, R, G, B);
				// A tick showing which way the place faces.
				const double A = P.YawDeg * 3.14159265 / 180.0;
				for (int K = 6; K < 16; ++K)
				{
					Img.Set(Px + static_cast<int>(std::cos(A) * K), Py - static_cast<int>(std::sin(A) * K), R, G, B);
				}
			}
		}
		Check(Img.Save(Path), "wrote " + Path);
	}

	/** Side view of the ground along a line, at three moments: untouched, just dug, and after it settles. */
	void DrawProfile(const std::vector<std::vector<float>>& Lines, const std::vector<float>& Floor, float Spacing, const std::string& Path)
	{
		const int W = 900;
		const int H = 360;
		FImage Img(W, H);
		for (int Y = 0; Y < H; ++Y)
		{
			for (int X = 0; X < W; ++X)
			{
				Img.Set(X, Y, 245, 245, 245);
			}
		}
		float Lo = 1e9f;
		float Hi = -1e9f;
		for (const auto& Line : Lines)
		{
			for (const float V : Line)
			{
				Lo = std::min(Lo, V);
				Hi = std::max(Hi, V);
			}
		}
		for (const float V : Floor)
		{
			Lo = std::min(Lo, V);
		}
		Lo -= 0.3f;
		Hi += 0.3f;
		// Same scale both ways, so slopes look like they really are.
		const double PxPerM = std::min((W - 20.0) / (Floor.size() * Spacing), (H - 20.0) / (Hi - Lo));
		const auto Plot = [&](const std::vector<float>& Line, int R, int G, int B)
		{
			for (size_t K = 0; K < Line.size(); ++K)
			{
				const int X = 10 + static_cast<int>(K * Spacing * PxPerM);
				const int Y = H - 10 - static_cast<int>((Line[K] - Lo) * PxPerM);
				Img.Disc(X, Y, 2, R, G, B);
			}
		};
		Plot(Floor, 60, 60, 60);
		// Sea level.
		const int SeaY = H - 10 - static_cast<int>((0.f - Lo) * PxPerM);
		for (int X = 0; X < W; X += 3)
		{
			Img.Set(X, SeaY, 40, 120, 220);
		}
		const int Colours[3][3] = { { 200, 170, 90 }, { 220, 40, 40 }, { 20, 140, 40 } };
		for (size_t K = 0; K < Lines.size() && K < 3; ++K)
		{
			Plot(Lines[K], Colours[K][0], Colours[K][1], Colours[K][2]);
		}
		Check(Img.Save(Path), "wrote " + Path);
	}

	double Seconds(std::chrono::steady_clock::time_point Since)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - Since).count();
	}

	bool Same(const FIslandSite& A, const FIslandSite& B)
	{
		return A.Id == B.Id && A.X == B.X && A.Y == B.Y && A.Radius == B.Radius && A.Seed == B.Seed && A.Kind == B.Kind;
	}

	/** Steepest slope anywhere loose ground sits, as a multiple of what it should stand at. */
	double WorstLooseSlopeRatio(const FTerrain& T, int MinX, int MinY, int MaxX, int MaxY)
	{
		double Worst = 0.0;
		for (int Y = std::max(1, MinY); Y <= std::min(T.Size - 2, MaxY); ++Y)
		{
			for (int X = std::max(1, MinX); X <= std::min(T.Size - 2, MaxX); ++X)
			{
				const int I = T.Index(X, Y);
				if (T.Height[static_cast<size_t>(I)] - T.Bedrock[static_cast<size_t>(I)] < 0.01f)
				{
					continue;
				}
				const int Ns[4] = { T.Index(X + 1, Y), T.Index(X - 1, Y), T.Index(X, Y + 1), T.Index(X, Y - 1) };
				for (const int N : Ns)
				{
					const double Slope = (T.Height[static_cast<size_t>(I)] - T.Height[static_cast<size_t>(N)]) / T.Spacing;
					Worst = std::max(Worst, Slope / T.GetReposeSlope(I));
				}
			}
		}
		return Worst;
	}
}

int main(int Argc, char** Argv)
{
	const uint32_t Seed = Argc > 1 ? static_cast<uint32_t>(std::strtoul(Argv[1], nullptr, 10)) : 1337U;
	const std::string OutDir = Argc > 2 ? Argv[2] : "Saved/WorldPreview";
	std::error_code DirError;
	std::filesystem::create_directories(OutDir, DirError);
	std::printf("World seed %u, previews in %s\n\n", Seed, OutDir.c_str());

	const FWorld World(Seed);
	const FWorldLayout& L = World.GetLayout();
	const std::vector<FIslandSite>& Fixed = World.GetFixedSites();

	// --- Layout ---

	{
		const FWorld Again(Seed);
		bool bSame = Again.GetFixedSites().size() == Fixed.size();
		for (size_t K = 0; bSame && K < Fixed.size(); ++K)
		{
			bSame = Same(Fixed[K], Again.GetFixedSites()[K]);
		}
		Check(bSame, "same seed gives the same home chain");
		const FWorld Other(Seed + 1);
		Check(!Same(Other.GetFixedSites()[1], Fixed[1]), "a different seed gives a different home chain");
	}

	int Starts = 0, Bigs = 0, Fars = 0, Reefs = 0, Outposts = 0, Wild = 0;
	bool bInRing = true;
	bool bInChain = true;
	for (const FIslandSite& S : Fixed)
	{
		const double D = std::sqrt(S.X * S.X + S.Y * S.Y);
		switch (S.Role)
		{
		case EIslandRole::Start: ++Starts; break;
		case EIslandRole::BigIsland: ++Bigs; break;
		case EIslandRole::FarIsland: ++Fars; break;
		case EIslandRole::WreckReef: ++Reefs; break;
		case EIslandRole::Outpost: ++Outposts; break;
		case EIslandRole::Wild: ++Wild; break;
		}
		if (S.Role == EIslandRole::Outpost)
		{
			bInRing = bInRing && World.ZoneAt(S.X, S.Y) == EZone::Cordon;
		}
		else
		{
			bInChain = bInChain && D + S.Radius < L.CordonInnerRadius;
		}
	}
	Check(Starts == 1 && Bigs == 1 && Fars == 1 && Reefs == 1, "home chain has one start, big, far and reef island");
	Check(Wild >= 5, "home chain has filler islands (" + std::to_string(Wild) + ")");
	Check(Outposts == 10 && bInRing, "10 military outposts, all inside the cordon ring");
	Check(bInChain, "every home island sits inside the cordon");

	const auto Dist = [](const FIslandSite& A, const FIslandSite& B) { return std::hypot(A.X - B.X, A.Y - B.Y); };
	{
		const double ToBig = Dist(Fixed[0], Fixed[1]);
		const double BigToFar = Dist(Fixed[1], Fixed[2]);
		Check(ToBig > 900.0 && ToBig < 1400.0, "big island is " + std::to_string(static_cast<int>(ToBig)) + " m from the start");
		Check(BigToFar > 0.9 * ToBig && BigToFar < 1.6 * ToBig, "far island is " + std::to_string(static_cast<int>(BigToFar))
			+ " m past the big island (about as far again, more if both are large)");
		Check(Dist(Fixed[0], Fixed[2]) > ToBig * 1.5, "far island lies beyond the big island, not beside the start");
	}

	// Many seeds: the story islands always get clear water between them, and no two coasts ever touch.
	{
		int Bad = 0;
		int BadSeed = -1;
		double WorstReach = 0.0;
		for (uint32_t S = 1; S <= 500; ++S)
		{
			const FWorld W(S);
			const std::vector<FIslandSite>& Sites = W.GetFixedSites();
			for (size_t A = 0; A < Sites.size(); ++A)
			{
				for (size_t B = A + 1; B < Sites.size(); ++B)
				{
					if (Dist(Sites[A], Sites[B]) < (Sites[A].Radius + Sites[B].Radius) * 1.7 + 100.0)
					{
						++Bad;
						BadSeed = static_cast<int>(S);
					}
				}
			}
			if (S <= 60)
			{
				for (const FIslandSite& Site : Sites)
				{
					for (int Deg = 0; Deg < 360; Deg += 4)
					{
						const double A = Deg * 3.14159265 / 180.0;
						for (double R = Site.Radius * 2.5; R > 0.0; R -= Site.Radius * 0.002)
						{
							if (SampleIsland(Site, std::cos(A) * R, std::sin(A) * R).Height > 0.f)
							{
								WorstReach = std::max(WorstReach, R / Site.Radius);
								break;
							}
						}
					}
				}
			}
		}
		Check(Bad == 0, "500 seeds: home islands always have open water between them" + (Bad ? " (seed " + std::to_string(BadSeed) + ")" : std::string()));
		Check(WorstReach < 1.7, "coasts never reach past 1.7x an island's radius (worst " + std::to_string(WorstReach) + ")");
	}

	// Open sea, over a 120 km square, checked for overlaps across cell borders.
	{
		const std::vector<FIslandSite> Open = World.SitesNear(0.0, 0.0, 60000.0);
		int OpenCount = 0;
		bool bClearOfCordon = true;
		bool bNoOverlap = true;
		std::vector<const FIslandSite*> Wilds;
		for (const FIslandSite& S : Open)
		{
			if (S.Id < (1ULL << 63))
			{
				continue;
			}
			++OpenCount;
			Wilds.push_back(&S);
			bClearOfCordon = bClearOfCordon && std::sqrt(S.X * S.X + S.Y * S.Y) - S.Radius > L.CordonOuterRadius;
		}
		for (size_t A = 0; A < Wilds.size(); ++A)
		{
			for (size_t B = A + 1; B < Wilds.size(); ++B)
			{
				// Coasts reach at most 1.7 times an island's radius.
				if (Dist(*Wilds[A], *Wilds[B]) < (Wilds[A]->Radius + Wilds[B]->Radius) * 1.7)
				{
					bNoOverlap = false;
				}
			}
		}
		Check(OpenCount > 100, std::to_string(OpenCount) + " open-sea islands within 60 km");
		Check(bClearOfCordon, "no open-sea island inside the cordon");
		Check(bNoOverlap, "open-sea islands never overlap");

		// Far out, the sea still has islands: it never runs dry.
		const std::vector<FIslandSite> Far = World.SitesNear(5000000.0, -3000000.0, 20000.0);
		Check(!Far.empty(), std::to_string(Far.size()) + " islands within 20 km of a point 5,800 km out");

		const std::vector<FIslandSite> AgainOpen = World.SitesNear(0.0, 0.0, 60000.0);
		bool bStable = AgainOpen.size() == Open.size();
		for (size_t K = 0; bStable && K < Open.size(); ++K)
		{
			bStable = Same(Open[K], AgainOpen[K]);
		}
		Check(bStable, "open sea comes out the same every time");
	}

	// --- Start island terrain ---

	const FIslandSite& Start = World.GetStartIsland();
	auto Clock = std::chrono::steady_clock::now();
	FTerrain T = BuildIslandTerrain(Start);
	std::printf("      start island: radius %.0f m, %d x %d grid at %.2f m, built in %.0f ms\n", Start.Radius, T.Size, T.Size,
		T.Spacing, Seconds(Clock) * 1000.0);

	{
		int Land = 0;
		float Peak = -1e9f;
		for (const float H : T.Height)
		{
			Land += H > 0.f ? 1 : 0;
			Peak = std::max(Peak, H);
		}
		const double LandArea = Land * T.Spacing * T.Spacing;
		Check(LandArea > 5000.0, "start island has " + std::to_string(static_cast<int>(LandArea)) + " m2 of dry land");
		Check(Peak > 8.f && Peak < 25.f, "start island tops out at " + std::to_string(Peak) + " m");

		double Sx = 0.0, Sy = 0.0;
		float Yaw = 0.f;
		World.GetStartSpawn(Sx, Sy, Yaw);
		const float Depth = -T.SampleHeight(Sx, Sy);
		Check(Depth > 1.4f, "boat spawns in " + std::to_string(Depth) + " m of water");
		const double FacingX = std::cos(Yaw * 3.14159265 / 180.0);
		const double FacingY = std::sin(Yaw * 3.14159265 / 180.0);
		Check((Start.X - Sx) * FacingX + (Start.Y - Sy) * FacingY > 0.0, "boat spawns facing the island");

		bool bBedrockOk = true;
		for (size_t I = 0; I < T.Height.size(); ++I)
		{
			bBedrockOk = bBedrockOk && T.Bedrock[I] <= T.Height[I] && T.Height[I] - T.Bedrock[I] <= 3.0001f;
		}
		Check(bBedrockOk, "loose layer is between 0 and 3 m everywhere");
	}

	// --- Digging ---

	// Find a beach spot: dry sand a metre or two above the sea, with deep sand under it.
	double DigX = 0.0, DigY = 0.0;
	for (int Deg = 0; Deg < 360 && DigX == 0.0; Deg += 10)
	{
		const double A = Deg * 3.14159265 / 180.0;
		for (double D = Start.Radius * 2.0; D > 0.0; D -= 0.5)
		{
			const double X = Start.X + std::cos(A) * D;
			const double Y = Start.Y + std::sin(A) * D;
			const size_t I = static_cast<size_t>(T.Index(static_cast<int>((X - T.OriginX) / T.Spacing), static_cast<int>((Y - T.OriginY) / T.Spacing)));
			if (T.Height[I] > 1.0f && T.Height[I] < 2.5f && T.Surface[I] == ESurface::Sand && T.Height[I] - T.Bedrock[I] > 2.2f)
			{
				DigX = X;
				DigY = Y;
				break;
			}
		}
	}
	Check(DigX != 0.0, "found a sandy beach on the start island to dig");
	const int Gx = static_cast<int>((DigX - T.OriginX) / T.Spacing);
	const int Gy = static_cast<int>((DigY - T.OriginY) / T.Spacing);
	const int Half = static_cast<int>(6.0 / T.Spacing);
	const auto Profile = [&]()
	{
		std::vector<float> Line;
		for (int X = Gx - Half; X <= Gx + Half; ++X)
		{
			Line.push_back(T.Height[static_cast<size_t>(T.Index(X, Gy))]);
		}
		return Line;
	};
	std::vector<float> Floor;
	for (int X = Gx - Half; X <= Gx + Half; ++X)
	{
		Floor.push_back(T.Bedrock[static_cast<size_t>(T.Index(X, Gy))]);
	}
	std::printf("      dig spot: ground %.2f m above the sea, %.2f m of sand\n", T.SampleHeight(DigX, DigY),
		T.Height[static_cast<size_t>(T.Index(Gx, Gy))] - T.Bedrock[static_cast<size_t>(T.Index(Gx, Gy))]);

	std::vector<std::vector<float>> Lines;
	Lines.push_back(Profile());
	const double VolumeBefore = T.TotalVolume();

	// Forty shovelfuls in the same place, without letting the sand settle in between: a steep-walled hole.
	double Removed = 0.0;
	for (int K = 0; K < 40; ++K)
	{
		Removed += T.Dig(DigX, DigY, 0.6f, 0.25f);
	}
	Lines.push_back(Profile());
	Check(std::fabs((VolumeBefore - T.TotalVolume()) - Removed) < 1e-3 * std::max(1.0, Removed),
		"digging removes exactly what it reports (" + std::to_string(Removed * 1000.0) + " litres)");
	const float HoleDepth = T.OriginalHeight[static_cast<size_t>(T.Index(Gx, Gy))] - T.Height[static_cast<size_t>(T.Index(Gx, Gy))];
	Check(HoleDepth <= 3.0001f, "hole stops at bedrock (" + std::to_string(HoleDepth) + " m deep)");

	const double VolumeDug = T.TotalVolume();
	Clock = std::chrono::steady_clock::now();
	int Passes = 0;
	while (T.Settle(1))
	{
		++Passes;
	}
	Lines.push_back(Profile());
	std::printf("      hole settled in %d passes (%.1f ms; the game runs 30 passes a second, so about %.1f s)\n", Passes,
		Seconds(Clock) * 1000.0, Passes / 30.0);
	const float SettledDepth = T.OriginalHeight[static_cast<size_t>(T.Index(Gx, Gy))] - T.Height[static_cast<size_t>(T.Index(Gx, Gy))];
	Check(SettledDepth < HoleDepth - 0.3f, "sand slumps into the hole (" + std::to_string(HoleDepth) + " m deep when dug, "
		+ std::to_string(SettledDepth) + " m once settled)");
	Check(std::fabs(T.TotalVolume() - VolumeDug) < 1e-3 * std::max(1.0, std::fabs(VolumeDug)) + 1e-2,
		"slumping moves sand without making or losing any");
	{
		const int Around = static_cast<int>(3.0 / T.Spacing);
		const double Worst = WorstLooseSlopeRatio(T, Gx - Around, Gy - Around, Gx + Around, Gy + Around);
		Check(Worst < 1.1, "settled hole walls stand at or below the sand's natural slope (worst " + std::to_string(Worst) + "x)");
		const double Natural = WorstLooseSlopeRatio(BuildIslandTerrain(Start), 0, 0, T.Size, T.Size);
		Check(Natural < 1.3, "untouched sand and soil are close to stable (steepest " + std::to_string(Natural) + "x its natural slope)");
	}
	DrawProfile(Lines, Floor, T.Spacing, OutDir + "/dig_profile.png");

	// Piling: dump the sand back a few metres inland and let it spread.
	{
		const double PileX = DigX + 5.0;
		const double Before = T.TotalVolume();
		T.Deposit(PileX, DigY, 0.5f, static_cast<float>(Removed));
		while (T.Settle(64))
		{
		}
		Check(std::fabs(T.TotalVolume() - Before - Removed) < 1e-3 * std::max(1.0, Removed) + 1e-2, "piled sand keeps its volume as it spreads");
		const int Px = static_cast<int>((PileX - T.OriginX) / T.Spacing);
		const double Worst = WorstLooseSlopeRatio(T, Px - Half * 2, Gy - Half * 2, Px + Half * 2, Gy + Half * 2);
		Check(Worst < 1.1, "the pile spreads into a cone at the natural slope (worst " + std::to_string(Worst) + "x)");
	}

	// Rock doesn't dig.
	{
		bool bFound = false;
		for (size_t I = 0; I < T.Height.size() && !bFound; ++I)
		{
			if (T.Surface[I] == ESurface::Rock && T.Height[I] > 0.f)
			{
				const int X = static_cast<int>(I % static_cast<size_t>(T.Size));
				const int Y = static_cast<int>(I / static_cast<size_t>(T.Size));
				const float Took = T.Dig(T.OriginX + X * T.Spacing, T.OriginY + Y * T.Spacing, 0.1f, 1.f);
				// The scoop spans neighbours, which may be sand; the rock vertex itself must not drop.
				Check(T.Height[I] == T.OriginalHeight[I], "bare rock can't be dug (" + std::to_string(Took * 1000.f) + " litres came off sandy neighbours)");
				bFound = true;
			}
		}
		if (!bFound)
		{
			std::printf("      (no bare rock on the start island to test)\n");
		}
		while (T.Settle(64))
		{
		}
	}

	DrawTerrain(BuildIslandTerrain(Start), nullptr, 900, OutDir + "/start_island.png");
	{
		FTerrain Fresh = BuildIslandTerrain(Start);
		const std::vector<FScatterPoint> Plants = ScatterFoliage(Fresh, Start);
		Check(!Plants.empty(), std::to_string(Plants.size()) + " palms and plants on the start island");
		DrawTerrain(Fresh, &Plants, 900, OutDir + "/start_island_foliage.png");
		const std::vector<FPlace> Places = FindPlaces(Fresh, Start);
		DrawTerrain(Fresh, nullptr, 900, OutDir + "/start_island_places.png", &Places);
	}

	// --- Places ---

	{
		int Islands = 0, NoLanding = 0, StartBad = 0, BigBad = 0, ReefBad = 0, OutpostBad = 0, RuleBad = 0;
		std::string RuleWhy;
		double SlowestMs = 0.0;
		for (uint32_t S = 1; S <= 40; ++S)
		{
			const FWorld W(S);
			for (const FIslandSite& Site : W.GetFixedSites())
			{
				if (Site.Role == EIslandRole::Wild && S > 10)
				{
					continue;
				}
				const FTerrain Ground = BuildIslandTerrain(Site);
				Clock = std::chrono::steady_clock::now();
				const std::vector<FPlace> Places = FindPlaces(Ground, Site);
				SlowestMs = std::max(SlowestMs, Seconds(Clock) * 1000.0);
				++Islands;
				int Count[static_cast<int>(EPlace::Count)] = {};
				for (const FPlace& P : Places)
				{
					++Count[static_cast<int>(P.Kind)];
					bool bOk = true;
					switch (P.Kind)
					{
					case EPlace::Landing: bOk = P.Z > 0.f && P.Z < 1.f; break;
					case EPlace::Camp: bOk = P.Z >= 1.5f && P.Z < 10.f; break;
					case EPlace::Bunker: bOk = P.Z >= 6.f; break;
					case EPlace::Wreck: bOk = P.Z < -1.5f && P.Z > -4.5f; break;
					default: break;
					}
					if (!bOk)
					{
						++RuleBad;
						RuleWhy = std::string(PlaceName(P.Kind)) + " at height " + std::to_string(P.Z) + " on seed " + std::to_string(S);
					}
				}
				const auto N = [&Count](EPlace K) { return Count[static_cast<int>(K)]; };
				NoLanding += N(EPlace::Landing) == 0 ? 1 : 0;
				switch (Site.Role)
				{
				case EIslandRole::Start: StartBad += (N(EPlace::Camp) < 1 || N(EPlace::Bunker) > 0 || N(EPlace::Wreck) > 0) ? 1 : 0; break;
				case EIslandRole::BigIsland: BigBad += (N(EPlace::Landing) < 2 || N(EPlace::Camp) < 1 || N(EPlace::Bunker) < 1 || N(EPlace::Lookout) != 1) ? 1 : 0; break;
				case EIslandRole::WreckReef: ReefBad += N(EPlace::Wreck) != 1 ? 1 : 0; break;
				case EIslandRole::Outpost: OutpostBad += N(EPlace::Outpost) != 1 ? 1 : 0; break;
				default: break;
				}
			}
		}
		std::printf("      places checked on %d islands; slowest took %.0f ms\n", Islands, SlowestMs);
		Check(NoLanding == 0, "every island has somewhere to land a boat (" + std::to_string(NoLanding) + " without)");
		Check(StartBad == 0, "start island always has a camp spot, and no bunker or wreck (" + std::to_string(StartBad) + " of 40 wrong)");
		Check(BigBad == 0, "big island always has 2+ landings, a camp, a bunker and one lookout (" + std::to_string(BigBad) + " of 40 wrong)");
		Check(ReefBad == 0, "wreck reef always has exactly one wreck (" + std::to_string(ReefBad) + " of 40 wrong)");
		Check(OutpostBad == 0, "every cordon rock has an outpost spot (" + std::to_string(OutpostBad) + " wrong)");
		Check(RuleBad == 0, "every place sits on suitable ground" + (RuleBad ? " (" + RuleWhy + ")" : std::string()));
	}

	// --- Bigger islands ---

	for (size_t K = 1; K <= 2; ++K)
	{
		const FIslandSite& S = Fixed[K];
		Clock = std::chrono::steady_clock::now();
		const FTerrain Big = BuildIslandTerrain(S);
		const double Ms = Seconds(Clock) * 1000.0;
		float Peak = -1e9f;
		for (const float H : Big.Height)
		{
			Peak = std::max(Peak, H);
		}
		std::printf("      %s: %s, radius %.0f m, peak %.0f m, %d x %d grid at %.1f m, built in %.0f ms\n", RoleName(S.Role),
			KindName(S.Kind), S.Radius, Peak, Big.Size, Big.Size, Big.Spacing, Ms);
		const std::vector<FScatterPoint> Plants = ScatterFoliage(Big, S);
		const std::vector<FPlace> Places = FindPlaces(Big, S);
		DrawTerrain(Big, &Plants, 1000, OutDir + "/" + (K == 1 ? "big_island.png" : "far_island.png"), &Places);
	}

	// --- Maps ---

	DrawWorld(World, 0.0, 0.0, 5000.0, 1400, OutDir + "/home_chain.png");
	DrawWorld(World, 0.0, 0.0, 14000.0, 1400, OutDir + "/cordon.png");
	DrawWorld(World, 0.0, 0.0, 60000.0, 1400, OutDir + "/region_120km.png");

	std::printf("\n%s (%d failed)\n", Failures == 0 ? "ALL PASSED" : "SOME FAILED", Failures);
	return Failures == 0 ? 0 : 1;
}
