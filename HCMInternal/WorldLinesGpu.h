#pragma once
// Poorboys Checkpoint Manager: draws the map grid (every collision edge of the map: the structure BSP cyan, instanced
// geometry yellow) and the jump-spot marks into the game's frame with Direct3D 11. The collision triangles go into a
// depth buffer of our own first, so lines behind walls are hidden; a copy of the game's own depth keeps them off its
// first-person gun and arms (drawn after the game's frame, the lines would be over them). The map is uploaded once (per map / world-file
// change); a frame is a handful of draw calls, nothing per line on the CPU. Plain D3D11 + WorldLinesScene, so
// tools\worldlines_test renders the same frames offline (WARP).
#include <d3d11.h>
#include <memory>
#include "WorldLinesScene.h"

namespace worldlines
{
	struct Rgba
	{
		float r, g, b, a;
	};

	// the take-off marks' colour (their labels use it too)
	constexpr int kTakeOffRgb[3] = { 255, 90, 230 };

	// something the lines must not be drawn over (a player): a capsule from a to b with radius r, into the depth pass
	struct Capsule
	{
		V3 a, b;
		float r;
	};

	struct DrawOptions
	{
		bool grid = true;               // every collision edge
		bool spots = false;             // jump-spot marks (Halo 3's stock maps)
		float lineScale = 1.f;          // line widths are for 1080 lines; scaled by this (screen height / 1080)
		float gridWidth = 1.f;          // the grid's line width in pixels (at 1080 lines)
		float gridOpacity = 0.75f;      // 0..1
		Rgba structure{ 0.25f, 0.88f, 0.94f, 1.f };   // the map (structure BSP)
		Rgba instance{ 0.92f, 0.90f, 0.16f, 1.f };    // instanced geometry
		const std::vector<Capsule>* blockers = nullptr;
		// the game's own depth buffer (its first-person gun and arms are in it): a piece of line with something of the
		// game's within sceneRange in front of it is not drawn. Copied each draw; any size (the frame's picture
		// stretched over it), one sample, a depth format (float = reversed, as the MCC games keep it). Null = not used
		ID3D11Texture2D* sceneDepth = nullptr;
		float sceneNear = 0.f, sceneFar = 0.f;        // the game camera's clip planes (its depth's projection)
		float sceneRange = 2.f;                       // world units
	};

	class Gpu
	{
	public:
		~Gpu() { release(); }
		// the device the next draw uses; resources are (re)made when it changes. false = shaders could not be made
		bool init(ID3D11Device* device);
		void release();
		// the scene to draw (render thread); its buffers are made on the first draw that needs them
		void setScene(std::shared_ptr<const Scene> scene);
		const std::shared_ptr<const Scene>& scene() const { return mScene; }
		// draws into rtv (width x height); restores the context's state afterwards. false = nothing drawn (no scene,
		// a buffer could not be made)
		bool draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, float width, float height, const Camera& cam, const DrawOptions& opt);
		const char* lastError() const { return mError; }
		// whether the last draw kept the lines off the game's depth (opt.sceneDepth), and why not
		bool sceneUsed() const { return mSceneUsed; }
		const char* sceneProblem() const { return mSceneProblem; }

	private:
		ID3D11Device* mDevice = nullptr;
		ID3D11VertexShader* mVsDepth = nullptr;
		ID3D11VertexShader* mVsLine = nullptr;
		ID3D11GeometryShader* mGsLine = nullptr;
		ID3D11PixelShader* mPsLine = nullptr;
		ID3D11InputLayout* mLayout = nullptr;
		ID3D11Buffer* mFrameCb = nullptr;
		ID3D11BlendState* mNoColour = nullptr;
		ID3D11BlendState* mAlpha = nullptr;
		ID3D11DepthStencilState* mDepthWrite = nullptr;
		ID3D11DepthStencilState* mDepthTest = nullptr;
		ID3D11RasterizerState* mRaster = nullptr;
		ID3D11RasterizerState* mRasterDepth = nullptr;  // the depth pass: pushed back by its slope (lines on steep faces)
		// the depth buffer, made at the target's size and sample count
		ID3D11Texture2D* mDepthTex = nullptr;
		ID3D11DepthStencilView* mDepthView = nullptr;
		UINT mDepthW = 0, mDepthH = 0, mDepthSamples = 0;
		// the scene's buffers
		std::shared_ptr<const Scene> mScene, mUploaded;
		ID3D11Buffer* mVerts = nullptr;
		ID3D11Buffer* mTris = nullptr;
		ID3D11Buffer* mEdges[kEdgeKinds] = {};
		ID3D11Buffer* mSpotVerts = nullptr;     // green, yellow, cyan pieces as plain line vertices
		ID3D11Buffer* mBlockerVerts = nullptr;  // the capsules' triangles, rewritten every frame
		static constexpr UINT kBlockerVertexCap = 64 * 1024;
		UINT mTriCount = 0, mEdgeCount[kEdgeKinds] = {}, mGreen = 0, mYellow = 0, mCyan = 0;
		const char* mError = "";
		// the copy of the game's depth, made at its size and format group
		ID3D11Texture2D* mSceneCopy = nullptr;
		ID3D11ShaderResourceView* mSceneView = nullptr;
		D3D11_TEXTURE2D_DESC mSceneDesc{};
		bool mSceneReversed = false, mSceneUsed = false;
		const char* mSceneProblem = "";

		bool upload();
		void releaseScene();
		bool depthFor(ID3D11RenderTargetView* rtv, UINT width, UINT height);
		bool sceneFor(ID3D11DeviceContext* ctx, ID3D11Texture2D* game);
	};
}
