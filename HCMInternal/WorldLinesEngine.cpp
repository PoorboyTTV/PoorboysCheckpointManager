// Poorboys Checkpoint Manager: the map grid inside the game's view render (see WorldLinesEngine.h)
#include "WorldLinesEngine.h"
#include <algorithm>
#include <cmath>

namespace worldlines
{
	namespace
	{
		constexpr uint32_t kExplicitShaderDebug = 0, kVertexTypeDebug = 8;
		constexpr uint32_t kLineList = 1, kTriangleList = 3;
		constexpr int kCullNone = 1, kCullClockwise = 2;
		constexpr uint32_t kBatch = 1024;               // primitives per draw_primitive_up call (Jagged's default)

		template <typename T> void drop(T*& p)
		{
			if (p) p->Release();
			p = nullptr;
		}

		// Jagged's SetCullMode: c_rasterizer keeps the mode in bits 2-3 and a "changed" flag it applies on the next draw
		void setCull(const EngineFns& fns, int mode)
		{
			if (!fns.cullMode || !fns.cullModeChanged) return;
			const int value = (*fns.cullMode & (int)0xFFFFFFF3) | (4 * mode);
			if (value != *fns.cullMode)
			{
				*fns.cullMode = value;
				*fns.cullModeChanged |= 1;
			}
		}

		uint8_t degamma(float c)
		{
			return (uint8_t)std::lround(std::pow(std::clamp(c, 0.f, 1.f), 2.2f) * 255.f);
		}
	}

	void EngineGrid::release()
	{
		drop(mVerts);
		drop(mBlend);
		drop(mBlendFrom);
		drop(mRaster);
		drop(mRasterFrom);
		mDevice = nullptr;
		mScene.reset();
		mVertexCount = 0;
	}

	// every edge of the map as two of the game's debug vertices, lifted off its polygons, in its kind's colour (opaque:
	// the opacity is the blend factor)
	bool EngineGrid::upload(ID3D11Device* device, const Scene& s, const EmitOptions& opt)
	{
		drop(mVerts);
		mVertexCount = 0;
		std::vector<DebugVertex> v;
		size_t edges = 0;
		for (int k = 0; k < kEdgeKinds; k++) edges += s.edgeIndex[k].size() / 2;
		if (!edges) return true;
		v.reserve(edges * 2);
		for (int k = 0; k < kEdgeKinds; k++)
		{
			const float* c = k == kInstance ? opt.instance : opt.structure;
			const uint8_t r = degamma(c[0]), g = degamma(c[1]), b = degamma(c[2]);
			const auto& idx = s.edgeIndex[k];
			const auto& nrm = s.edgeNormal[k];
			for (size_t e = 0; e + 1 < idx.size(); e += 2)
			{
				if (idx[e] >= s.verts.size() || idx[e + 1] >= s.verts.size()) continue;
				const V3 lift = e / 2 < nrm.size() ? nrm[e / 2] * kLift : V3{};
				for (uint32_t i : { idx[e], idx[e + 1] })
				{
					const V3 p = s.verts[i] + lift;
					v.push_back({ p.x, p.y, p.z, b, g, r, 255 });
				}
			}
		}
		if (v.size() * sizeof(DebugVertex) > 0x7FFFFFFF)
		{
			mError = "the map has too many lines for one buffer";
			return false;
		}
		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = (UINT)(v.size() * sizeof(DebugVertex));
		bd.Usage = D3D11_USAGE_IMMUTABLE;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA init{ v.data(), 0, 0 };
		if (FAILED(device->CreateBuffer(&bd, &init, &mVerts)))
		{
			mError = "the map's line buffer could not be made";
			return false;
		}
		mVertexCount = (UINT)v.size();
		return true;
	}

	void EngineGrid::drawUp(const EngineFns& fns, uint32_t type, const std::vector<DebugVertex>& verts, uint32_t perPrimitive)
	{
		const size_t primitives = verts.size() / perPrimitive;
		for (size_t first = 0; first < primitives; first += kBatch)
		{
			const uint32_t n = (uint32_t)(std::min)((size_t)kBatch, primitives - first);
			fns.drawPrimitiveUp(type, n, verts.data() + first * perPrimitive, (uint32_t)sizeof(DebugVertex));
		}
	}

	bool EngineGrid::draw(ID3D11DeviceContext* ctx, const EngineFns& fns, const std::shared_ptr<const Scene>& scene, const std::shared_ptr<const EdgeCells>& cells,
		const EmitCamera& cam, const EmitOptions& opt)
	{
		if (!ctx || !scene || !fns.setExplicitShaders || !fns.setZBufferMode || !fns.drawPrimitiveUp) return false;
		ID3D11Device* device = nullptr;
		ctx->GetDevice(&device);
		if (!device) return false;
		device->Release();                              // the context holds it
		if (device != mDevice)
		{
			release();
			mDevice = device;
		}
		if (opt.grid && mScene != scene)
		{
			mScene = scene;
			if (!upload(device, *scene, opt)) return false;
		}

		// 1. the game's debug state, flushed onto the context by one line behind the camera (it draws nothing)
		if (!fns.setExplicitShaders(kExplicitShaderDebug, kVertexTypeDebug, 0, 0, nullptr))
		{
			mError = "the game's debug shader was refused";
			return false;
		}
		setCull(fns, kCullNone);
		fns.setZBufferMode(1);                          // depth-tested, not written
		const V3 behind = cam.pos - cam.fwd * 4.f, behind2 = behind + cam.up * 0.01f;
		const DebugVertex flush[2] = { { behind.x, behind.y, behind.z, 0, 0, 0, 0 }, { behind2.x, behind2.y, behind2.z, 0, 0, 0, 0 } };
		fns.drawPrimitiveUp(kLineList, 1, flush, (uint32_t)sizeof(DebugVertex));

		// 2. the whole map's lines with that state; the input buffer, topology, blend and rasterizer states put back after
		if (opt.grid && mVerts && mVertexCount)
		{
			ID3D11Buffer* oldVb = nullptr;
			UINT oldStride = 0, oldOffset = 0;
			ctx->IAGetVertexBuffers(0, 1, &oldVb, &oldStride, &oldOffset);
			D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			ctx->IAGetPrimitiveTopology(&oldTopology);
			ID3D11BlendState* oldBlend = nullptr;
			FLOAT oldFactor[4] = {};
			UINT oldMask = 0;
			ctx->OMGetBlendState(&oldBlend, oldFactor, &oldMask);
			ID3D11RasterizerState* oldRaster = nullptr;
			ctx->RSGetState(&oldRaster);

			// ours: the game's blend with the opacity as the blend factor; its rasterizer state with smoothed lines
			if (oldBlend != mBlendFrom || !mBlend)
			{
				drop(mBlend);
				drop(mBlendFrom);
				D3D11_BLEND_DESC bd{};
				if (oldBlend) oldBlend->GetDesc(&bd);
				else bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL, bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE,
					bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO, bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				bd.IndependentBlendEnable = FALSE;
				D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
				rt.BlendEnable = TRUE;
				rt.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
				rt.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
				rt.BlendOp = D3D11_BLEND_OP_ADD;
				if (rt.RenderTargetWriteMask == 0) rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
				if (SUCCEEDED(device->CreateBlendState(&bd, &mBlend)))
				{
					mBlendFrom = oldBlend;
					if (mBlendFrom) mBlendFrom->AddRef();
				}
			}
			if (oldRaster != mRasterFrom || !mRaster)
			{
				drop(mRaster);
				drop(mRasterFrom);
				D3D11_RASTERIZER_DESC rd{};
				rd.FillMode = D3D11_FILL_SOLID;
				rd.CullMode = D3D11_CULL_NONE;
				rd.DepthClipEnable = TRUE;
				if (oldRaster) oldRaster->GetDesc(&rd);
				rd.CullMode = D3D11_CULL_NONE;
				rd.AntialiasedLineEnable = rd.MultisampleEnable ? FALSE : TRUE;     // smoothed one-pixel lines
				if (SUCCEEDED(device->CreateRasterizerState(&rd, &mRaster)))
				{
					mRasterFrom = oldRaster;
					if (mRasterFrom) mRasterFrom->AddRef();
				}
			}

			const float o = std::clamp(opt.gridOpacity, 0.f, 1.f);
			const FLOAT factor[4] = { o, o, o, o };
			if (mBlend) ctx->OMSetBlendState(mBlend, factor, oldMask);
			if (mRaster) ctx->RSSetState(mRaster);
			const UINT stride = sizeof(DebugVertex), offset = 0;
			ctx->IASetVertexBuffers(0, 1, &mVerts, &stride, &offset);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			ctx->Draw(mVertexCount, 0);

			ctx->IASetVertexBuffers(0, 1, &oldVb, &oldStride, &oldOffset);
			ctx->IASetPrimitiveTopology(oldTopology);
			ctx->OMSetBlendState(oldBlend, oldFactor, oldMask);
			ctx->RSSetState(oldRaster);
			if (oldVb) oldVb->Release();
			if (oldBlend) oldBlend->Release();
			if (oldRaster) oldRaster->Release();
		}

		// 3. wide lines near the camera and the jump-spot marks: ribbons through the game's own path (vertex alpha)
		const float scale = (std::max)(cam.heightPx, 1.f) / 1080.f;
		const bool ribbons = opt.grid && opt.gridWidth * scale > 1.05f;
		if (ribbons || opt.spots)
		{
			EmitOptions e = opt;
			e.grid = ribbons;
			e.maxDistance = kRibbonDistance;
			e.depthPull = 0.0005f;
			if (cells) emit(*scene, *cells, cam, e, mScratch, mEmitted);
			else mEmitted.clear();
			drawUp(fns, kTriangleList, mEmitted.tris, 3);
			drawUp(fns, kLineList, mEmitted.lines, 2);
		}
		setCull(fns, kCullClockwise);
		mError = "";
		return true;
	}
}
