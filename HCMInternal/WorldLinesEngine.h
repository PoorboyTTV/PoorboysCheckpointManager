#pragma once
// Poorboys Checkpoint Manager: the map grid drawn inside the game's own view render (Halo 3). PoorboyWorldLines hooks
// c_player_view::render, as Jagged's H3 Collision Viewer does (its source, shared with Poorboy), and after the game has
// drawn the view this draws the grid with the game's debug drawer state: its debug shaders, its view and projection,
// its depth buffer read-only. So the lines are hidden behind everything the game drew - walls, players, vehicles and
// your first-person gun - with the game's own depth, nothing guessed.
//   1. the game's debug path is set up and given one line behind the camera (it draws nothing): that binds every bit of
//      its debug state on the device context, as its own debug lines have it
//   2. with that state, the whole map's lines from one vertex buffer made once per map (the game's vertex layout,
//      rasterizer_vertex_debug), in one draw call; the opacity is a blend factor. Each line is lifted a little off its
//      polygons (their averaged normal) so it does not flicker against the game's own surface
//   3. the input buffer, blend and rasterizer states are put back exactly, so the game's state cache stays right
//   4. wide lines near the camera and the jump-spot marks: camera-facing ribbons through the game's own
//      draw_primitive_up (Jagged's way)
#include <d3d11.h>
#include <memory>
#include "WorldLinesEmit.h"

namespace worldlines
{
	// the game's debug drawer (c_rasterizer), for the running build
	struct EngineFns
	{
		bool (*setExplicitShaders)(uint32_t explicitShader, uint32_t vertexType, uint32_t transferType, uint32_t entryPoint, void** shader) = nullptr;
		void (*setZBufferMode)(int mode) = nullptr;
		void (*drawPrimitiveUp)(uint32_t primitiveType, uint32_t primitiveCount, const void* data, uint32_t stride) = nullptr;
		int* cullMode = nullptr;            // c_rasterizer's current cull mode and its "changed" flag (Jagged's SetCullMode)
		int* cullModeChanged = nullptr;
		ID3D11DeviceContext* const* context = nullptr;  // the context the game draws with (its draw_primitive_up's): ours go
		                                                // on the same one, in order with the view it just drew
	};

	class EngineGrid
	{
	public:
		static constexpr float kLift = 0.012f;          // world units a line is lifted off its polygons
		static constexpr float kRibbonDistance = 40.f;  // wide lines are ribbons this near; one-pixel lines beyond

		~EngineGrid() { release(); }
		void release();
		// render thread, inside the game's view render, after it. false = nothing drawn (no buffer could be made, the
		// game's debug shader was refused); lastError() says which
		bool draw(ID3D11DeviceContext* ctx, const EngineFns& fns, const std::shared_ptr<const Scene>& scene, const std::shared_ptr<const EdgeCells>& cells,
			const EmitCamera& cam, const EmitOptions& opt);
		const char* lastError() const { return mError; }

	private:
		ID3D11Device* mDevice = nullptr;
		std::shared_ptr<const Scene> mScene;
		ID3D11Buffer* mVerts = nullptr;
		UINT mVertexCount = 0;
		ID3D11BlendState* mBlend = nullptr;
		ID3D11BlendState* mBlendFrom = nullptr;         // the game's blend state ours was made from
		ID3D11RasterizerState* mRaster = nullptr;
		ID3D11RasterizerState* mRasterFrom = nullptr;
		EmitScratch mScratch;
		Emitted mEmitted;
		const char* mError = "";

		bool upload(ID3D11Device* device, const Scene& s, const EmitOptions& opt);
		void drawUp(const EngineFns& fns, uint32_t type, const std::vector<DebugVertex>& verts, uint32_t perPrimitive);
	};
}
