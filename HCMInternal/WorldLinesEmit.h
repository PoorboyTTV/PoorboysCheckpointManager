#pragma once
// Poorboys Checkpoint Manager: the map grid drawn by the game itself (Halo 3). Every line near the camera becomes the
// game's own debug vertices (rasterizer_vertex_debug), which PoorboyWorldLines hands to the game's debug drawer inside
// its view render: the game's depth buffer then hides the lines behind everything the game drew - walls, players,
// vehicles and your first-person gun - which a picture drawn over the finished frame cannot know about. This is how
// Jagged's H3 Collision Viewer draws (its source, shared with Poorboy: the c_player_view render hook, c_rasterizer's
// debug path, the culling, depth pull and ribbon lines below follow it).
// Plain C++ (no game, no Direct3D), so tools\worldlines_test checks it offline.
#include <cstdint>
#include <vector>
#include "WorldLinesScene.h"

namespace worldlines
{
	// rasterizer_vertex_debug: a point and a byte colour (b, g, r, a)
	struct DebugVertex
	{
		float x, y, z;
		uint8_t b, g, r, a;
	};
	static_assert(sizeof(DebugVertex) == 16, "rasterizer_vertex_debug is 16 bytes");

	// a scene's edges and jump-spot pieces on a grid of cells, so a frame only looks at the cells near the camera.
	// Built once per scene (the worker thread); read-only afterwards.
	class EdgeCells
	{
	public:
		static constexpr float kCell = 16.f;
		struct Item
		{
			uint32_t a, b;          // vertex indices (grid edges) or 0xFFFFFFFF (a jump-spot piece: index in piece)
			uint32_t piece;         // jump-spot piece: group << 24 | index (group 0 green, 1 yellow, 2 cyan)
			uint8_t kind;           // EdgeKind, or 255 for a jump-spot piece
		};
		void build(const Scene& s);
		// calls f(item) for every item whose cells overlap the sphere (an item may come once per cell it is in: the
		// caller drops repeats with seen())
		template <typename F> void forNear(V3 c, float radius, F f) const;
		size_t items() const { return mItems.size(); }
		const std::vector<Item>& all() const { return mItems; }
	private:
		V3 mOrigin{};
		int mDims[3] = {};
		std::vector<uint32_t> mStarts;      // per cell: first entry in mCellItems (one extra at the end)
		std::vector<uint32_t> mCellItems;   // item indices
		std::vector<uint32_t> mBig;         // items spanning very many cells: every query gets them
		std::vector<Item> mItems;
		friend struct EmitScratch;
	};

	// the camera of the view the game is rendering (c_view::m_render_camera)
	struct EmitCamera
	{
		V3 pos, fwd, up, left;
		float vfov = 1.2f, hfov = 1.8f;     // radians
		float heightPx = 1080.f, aspect = 16.f / 9.f;
	};

	struct EmitOptions
	{
		bool grid = true, spots = false;
		float gridWidth = 1.f;              // pixels at 1080 lines (1 = the game's one-pixel lines)
		float gridOpacity = 0.6f;
		float maxDistance = 80.f;           // the grid's draw distance in world units (0 = the whole map)
		float spotsDistance = 25.f;         // jump spots: as the old helper, within 25 units
		float depthPull = 0.0005f;          // fraction of the distance a point is moved toward the camera (lines on
		                                    // the collision surface would otherwise lose to the render mesh's depth)
		float structure[3] = { 0.25f, 0.88f, 0.94f };   // the map (structure BSP), cyan
		float instance[3] = { 0.92f, 0.90f, 0.16f };    // instanced geometry, yellow
	};

	struct Emitted
	{
		std::vector<DebugVertex> lines;     // line list (2 per line)
		std::vector<DebugVertex> tris;      // triangle list (wide lines: 2 triangles each)
		uint32_t considered = 0, culled = 0;
		void clear() { lines.clear(), tris.clear(), considered = culled = 0; }
	};

	// scratch kept between frames (no allocation per frame once warm)
	struct EmitScratch
	{
		std::vector<uint32_t> stamp;        // per item: the frame it was last taken in
		uint32_t frame = 0;
	};

	void emit(const Scene& scene, const EdgeCells& cells, const EmitCamera& cam, const EmitOptions& opt, EmitScratch& scratch, Emitted& out);

	// ---- template body --------------------------------------------------------------------------------------------
	template <typename F> void EdgeCells::forNear(V3 c, float radius, F f) const
	{
		for (uint32_t i : mBig) f(i);
		if (mDims[0] == 0) return;
		int lo[3], hi[3];
		const float cc[3] = { c.x, c.y, c.z }, oo[3] = { mOrigin.x, mOrigin.y, mOrigin.z };
		for (int k = 0; k < 3; k++)
		{
			lo[k] = (int)std::floor((cc[k] - radius - oo[k]) / kCell);
			hi[k] = (int)std::floor((cc[k] + radius - oo[k]) / kCell);
			if (lo[k] < 0) lo[k] = 0;
			if (hi[k] > mDims[k] - 1) hi[k] = mDims[k] - 1;
			if (lo[k] > hi[k]) return;
		}
		for (int z = lo[2]; z <= hi[2]; z++)
			for (int y = lo[1]; y <= hi[1]; y++)
				for (int x = lo[0]; x <= hi[0]; x++)
				{
					const size_t cell = ((size_t)z * mDims[1] + y) * mDims[0] + x;
					for (uint32_t j = mStarts[cell]; j < mStarts[cell + 1]; j++) f(mCellItems[j]);
				}
	}
}
