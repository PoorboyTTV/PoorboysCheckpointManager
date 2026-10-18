#pragma once
// Poorboys Checkpoint Manager: one map's geometry for the map grid and the jump-spot marks (PoorboyWorldLines,
// drawn by WorldLinesGpu), built from:
//   h3sb\h3sb_world_<map>.bin    the collision polygons the mod (h3sb.dll) read from the game ("H3WD" v1/v2)
//   h3sb\spots\det_<map>.json    jump spots (green landing edges, cyan take-off marks)    - Halo 3's stock maps only
//   h3sb\spots\spots_<map>.json  the old scan's fast-drop seams (yellow)                   - Halo 3's stock maps only
// Plain C++ (no HCM types) so it can be tested on its own (tools\worldlines_test).
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace worldlines
{
	struct V3
	{
		float x = 0, y = 0, z = 0;
	};
	inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	float length(V3 a);

	struct Seg
	{
		V3 a, b;
	};

	// a take-off mark: ring on the floor, arrow in the direction to run, cross at the landing point, its label
	struct Ring
	{
		V3 pos, dir, at;
		std::string label;
	};

	// boxes on a uniform grid; boxes spanning very many cells go in a list that every query gets (as h3map.Grid)
	class BoxGrid
	{
		V3 mOrigin;
		float mCell = 1.f;
		std::unordered_map<uint64_t, uint32_t> mCellIndex;   // cell key -> index into mStarts
		std::vector<uint32_t> mStarts;                      // per cell: first item in mItems (one extra at the end)
		std::vector<uint32_t> mItems;
		std::vector<uint32_t> mBig;
		bool cellOf(V3 p, int64_t out[3]) const;
	public:
		void build(const std::vector<V3>& lo, const std::vector<V3>& hi, float cell);
		// every box index whose cells overlap [lo, hi] (may repeat an index; big boxes always)
		void query(V3 lo, V3 hi, std::vector<uint32_t>& out) const;
	};

	enum EdgeKind { kStructure = 0, kInstance = 1, kEdgeKinds = 2 };

	class Scene
	{
	public:
		std::string map;
		std::filesystem::path worldFile;
		std::filesystem::file_time_type worldTime{};
		size_t polygons = 0;

		// the whole map for the GPU: welded vertices, every polygon as fan triangles (the depth pass) and every polygon
		// edge once, by where its polygon comes from (the structure BSP, or instanced geometry)
		std::vector<V3> verts;
		std::vector<uint32_t> triIndex;
		std::vector<uint32_t> edgeIndex[kEdgeKinds];        // pairs of vertex indices
		std::vector<V3> edgeNormal[kEdgeKinds];             // one per edge: the average normal of the polygons sharing it

		std::vector<Seg> green;         // landing edges of the jump spots (launch >= +9 on enough take-offs), lifted 0.01
		std::vector<Seg> yellow;        // fast-drop seams, lifted 0.01
		std::vector<Ring> rings;        // at most 60, at least 1.2 apart
		std::vector<Seg> cyan;          // kCyanPerRing pieces per ring, in ring order
		std::vector<V3> ringMid;
		static constexpr int kCyanPerRing = 21;

		// throws std::runtime_error (missing file, file still being written, bad data)
		static std::shared_ptr<Scene> load(const std::filesystem::path& worldFile, const std::filesystem::path& spotsDir, const std::string& mapName);

		// the first polygon the segment p->q crosses: t in [0, 1] along it (false = none)
		bool firstHit(V3 p, V3 q, float& t) const;
		// nothing between p and 98.5 % of the way to q
		bool lineOfSight(V3 p, V3 q) const;

	private:
		BoxGrid mTriGrid;
		bool pieceHit(V3 a, V3 b, bool any, float& t, std::vector<uint32_t>& scratch) const;
		void loadSpots(const std::filesystem::path& spotsDir);
	};

	// the game's render camera record (Halo 3 halo3.dll + 0x2D2F688, ODST + 0x2D73598, Reach + 0xC9FAE0): +0x00 position,
	// +0x0C forward, +0x18 up, +0x28 vertical field of view (radians), +0x38 window bounds s16 (y0, x0, y1, x1)
	struct Camera
	{
		V3 pos, fwd, up, right;
		float tx = 1, ty = 1;           // tangents of the half field of view, sideways and up
	};
	constexpr size_t kCameraBytes = 0x40;
	constexpr float kNear = 0.06f;
	// false = not a usable camera (menu, loading, garbage)
	bool cameraFromRecord(const uint8_t* raw, Camera& out);
	// a world point on the screen (W x H pixels); false = behind the camera
	bool toScreen(const Camera& cam, V3 p, float W, float H, float& x, float& y);

	struct Label
	{
		V3 at;                          // 0.35 above the take-off mark
		std::string text;
	};
	// the take-off marks' labels to show: within 14 world units, in front, with a line of sight to the mark
	std::vector<Label> visibleLabels(const Scene& scene, const Camera& cam);
}
