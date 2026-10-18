// Poorboys Checkpoint Manager: the map grid's Direct3D 11 drawing (see WorldLinesGpu.h). Built without the precompiled
// header (HCMInternal.vcxproj: NotUsing) so tools\worldlines_test compiles the same file.
#include "WorldLinesGpu.h"
#include "WorldLinesShaders.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

namespace worldlines
{
	namespace
	{
		template <typename T> void drop(T*& p)
		{
			if (p) p->Release();
			p = nullptr;
		}

		// the shaders' constant buffer (WorldLinesShaders.hlsl: Frame)
		struct FrameConstants
		{
			float rowX[4], rowY[4], rowZ[4], rowW[4];
			float camPos[4];
			float viewport[4];      // width, height, half line width (pixels), near
			float colour[4];
			float limits[4];        // x: lines further than this along the view are not drawn
		};

		Rgba rgb(int r, int g, int b) { return { r / 255.f, g / 255.f, b / 255.f, 1.f }; }
		constexpr float kSlopeBias = 3.f;
		const Rgba kLandingColour = rgb(60, 255, 110), kTakeOffColour = rgb(kTakeOffRgb[0], kTakeOffRgb[1], kTakeOffRgb[2]),
			kFastDropColour = rgb(255, 140, 20);

		// the context state this draw changes, put back afterwards (the game and the overlay draw after us)
		struct StateBackup
		{
			UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			ID3D11RasterizerState* raster = nullptr;
			ID3D11BlendState* blend = nullptr;
			FLOAT blendFactor[4];
			UINT sampleMask = 0;
			ID3D11DepthStencilState* depth = nullptr;
			UINT stencilRef = 0;
			ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
			ID3D11DepthStencilView* depthView = nullptr;
			ID3D11VertexShader* vs = nullptr;
			ID3D11GeometryShader* gs = nullptr;
			ID3D11PixelShader* ps = nullptr;
			ID3D11ClassInstance* vsInst[256], * gsInst[256], * psInst[256];
			UINT vsInstCount = 256, gsInstCount = 256, psInstCount = 256;
			ID3D11Buffer* vsCb = nullptr, * gsCb = nullptr, * psCb = nullptr;
			D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			ID3D11Buffer* indexBuffer = nullptr;
			DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
			UINT indexOffset = 0;
			ID3D11Buffer* vertexBuffer = nullptr;
			UINT vertexStride = 0, vertexOffset = 0;
			ID3D11InputLayout* layout = nullptr;

			void save(ID3D11DeviceContext* c)
			{
				c->RSGetViewports(&viewportCount, viewports);
				c->RSGetState(&raster);
				c->OMGetBlendState(&blend, blendFactor, &sampleMask);
				c->OMGetDepthStencilState(&depth, &stencilRef);
				c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depthView);
				c->VSGetShader(&vs, vsInst, &vsInstCount);
				c->GSGetShader(&gs, gsInst, &gsInstCount);
				c->PSGetShader(&ps, psInst, &psInstCount);
				c->VSGetConstantBuffers(0, 1, &vsCb);
				c->GSGetConstantBuffers(0, 1, &gsCb);
				c->PSGetConstantBuffers(0, 1, &psCb);
				c->IAGetPrimitiveTopology(&topology);
				c->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
				c->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
				c->IAGetInputLayout(&layout);
			}

			void restore(ID3D11DeviceContext* c)
			{
				c->RSSetViewports(viewportCount, viewports);
				c->RSSetState(raster);
				c->OMSetBlendState(blend, blendFactor, sampleMask);
				c->OMSetDepthStencilState(depth, stencilRef);
				UINT n = 0;
				while (n < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && targets[n]) n++;
				c->OMSetRenderTargets(n, targets, depthView);
				c->VSSetShader(vs, vsInst, vsInstCount);
				c->GSSetShader(gs, gsInst, gsInstCount);
				c->PSSetShader(ps, psInst, psInstCount);
				c->VSSetConstantBuffers(0, 1, &vsCb);
				c->GSSetConstantBuffers(0, 1, &gsCb);
				c->PSSetConstantBuffers(0, 1, &psCb);
				c->IASetPrimitiveTopology(topology);
				c->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
				c->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
				c->IASetInputLayout(layout);
				drop(raster);
				drop(blend);
				drop(depth);
				for (auto& t : targets) drop(t);
				drop(depthView);
				drop(vs);
				drop(gs);
				drop(ps);
				for (UINT i = 0; i < vsInstCount; i++) drop(vsInst[i]);
				for (UINT i = 0; i < gsInstCount; i++) drop(gsInst[i]);
				for (UINT i = 0; i < psInstCount; i++) drop(psInst[i]);
				drop(vsCb);
				drop(gsCb);
				drop(psCb);
				drop(indexBuffer);
				drop(vertexBuffer);
				drop(layout);
			}
		};

		// a capsule's surface as triangles (12 around, 3 rings per end cap), appended to out
		void capsuleTriangles(const Capsule& c, std::vector<V3>& out)
		{
			constexpr int kAround = 12, kRings = 3;
			V3 axis = c.b - c.a;
			const float len = length(axis);
			axis = len > 1e-5f ? axis * (1.f / len) : V3{ 0, 0, 1 };
			const V3 side0 = std::abs(axis.z) < 0.9f ? cross(axis, V3{ 0, 0, 1 }) : cross(axis, V3{ 1, 0, 0 });
			const V3 u = side0 * (1.f / length(side0)), v = cross(axis, u);
			// rings from the bottom pole to the top pole: (centre, radius) pairs
			std::vector<std::pair<V3, float>> rings;
			for (int k = 0; k <= kRings; k++)
			{
				const float t = 3.14159265f / 2 * (float)k / kRings;      // 0 = bottom pole
				rings.push_back({ c.a - axis * (c.r * std::cos(t)), c.r * std::sin(t) });
			}
			for (int k = kRings; k >= 0; k--)
			{
				const float t = 3.14159265f / 2 * (float)k / kRings;
				rings.push_back({ c.b + axis * (c.r * std::cos(t)), c.r * std::sin(t) });
			}
			auto at = [&](size_t ring, int j) {
				const float a = 2.f * 3.14159265f * (float)j / kAround;
				return rings[ring].first + (u * std::cos(a) + v * std::sin(a)) * rings[ring].second;
				};
			for (size_t ring = 0; ring + 1 < rings.size(); ring++)
				for (int j = 0; j < kAround; j++)
				{
					const V3 p00 = at(ring, j), p01 = at(ring, j + 1), p10 = at(ring + 1, j), p11 = at(ring + 1, j + 1);
					out.insert(out.end(), { p00, p10, p11, p00, p11, p01 });
				}
		}

		ID3D11Buffer* makeBuffer(ID3D11Device* dev, const void* data, size_t bytes, UINT bind)
		{
			if (!bytes || bytes > 0x7FFFFFFF) return nullptr;
			D3D11_BUFFER_DESC d{};
			d.ByteWidth = (UINT)bytes;
			d.Usage = D3D11_USAGE_IMMUTABLE;
			d.BindFlags = bind;
			D3D11_SUBRESOURCE_DATA init{};
			init.pSysMem = data;
			ID3D11Buffer* b = nullptr;
			return SUCCEEDED(dev->CreateBuffer(&d, &init, &b)) ? b : nullptr;
		}
	}

	bool Gpu::init(ID3D11Device* device)
	{
		if (device == mDevice && mVsLine) return true;
		release();
		if (!device) return false;
		mDevice = device;
		bool ok = SUCCEEDED(device->CreateVertexShader(kVSDepth, sizeof(kVSDepth), nullptr, &mVsDepth)) &&
			SUCCEEDED(device->CreateVertexShader(kVSLine, sizeof(kVSLine), nullptr, &mVsLine)) &&
			SUCCEEDED(device->CreateGeometryShader(kGSLine, sizeof(kGSLine), nullptr, &mGsLine)) &&
			SUCCEEDED(device->CreatePixelShader(kPSLine, sizeof(kPSLine), nullptr, &mPsLine));
		const D3D11_INPUT_ELEMENT_DESC position{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
		ok = ok && SUCCEEDED(device->CreateInputLayout(&position, 1, kVSLine, sizeof(kVSLine), &mLayout));
		D3D11_BUFFER_DESC cb{};
		cb.ByteWidth = sizeof(FrameConstants);
		cb.Usage = D3D11_USAGE_DYNAMIC;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ok = ok && SUCCEEDED(device->CreateBuffer(&cb, nullptr, &mFrameCb));
		D3D11_BLEND_DESC bd{};
		bd.RenderTarget[0].RenderTargetWriteMask = 0;
		ok = ok && SUCCEEDED(device->CreateBlendState(&bd, &mNoColour));
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		ok = ok && SUCCEEDED(device->CreateBlendState(&bd, &mAlpha));
		D3D11_DEPTH_STENCIL_DESC dd{};
		dd.DepthEnable = TRUE;
		dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;            // reversed depth: closer = greater
		ok = ok && SUCCEEDED(device->CreateDepthStencilState(&dd, &mDepthWrite));
		dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		ok = ok && SUCCEEDED(device->CreateDepthStencilState(&dd, &mDepthTest));
		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = TRUE;
		ok = ok && SUCCEEDED(device->CreateRasterizerState(&rd, &mRaster));
		// a face seen nearly edge-on changes depth a lot from one pixel to the next and would cover half of the lines
		// lying on it: the triangles are pushed back by a few pixels' worth of their own slope (reversed depth: minus)
		rd.SlopeScaledDepthBias = -kSlopeBias;
		ok = ok && SUCCEEDED(device->CreateRasterizerState(&rd, &mRasterDepth));
		D3D11_BUFFER_DESC vb{};
		vb.ByteWidth = kBlockerVertexCap * sizeof(V3);
		vb.Usage = D3D11_USAGE_DYNAMIC;
		vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		ok = ok && SUCCEEDED(device->CreateBuffer(&vb, nullptr, &mBlockerVerts));
		if (!ok)
		{
			release();
			mError = "the map grid's shaders or states could not be made";
		}
		return ok;
	}

	void Gpu::releaseScene()
	{
		drop(mVerts);
		drop(mTris);
		for (auto& e : mEdges) drop(e);
		drop(mSpotVerts);
		mUploaded.reset();
		mTriCount = mGreen = mYellow = mCyan = 0;
		for (auto& c : mEdgeCount) c = 0;
	}

	void Gpu::release()
	{
		releaseScene();
		drop(mDepthView);
		drop(mDepthTex);
		mDepthW = mDepthH = mDepthSamples = 0;
		drop(mVsDepth);
		drop(mVsLine);
		drop(mGsLine);
		drop(mPsLine);
		drop(mLayout);
		drop(mFrameCb);
		drop(mNoColour);
		drop(mAlpha);
		drop(mDepthWrite);
		drop(mDepthTest);
		drop(mRaster);
		drop(mRasterDepth);
		drop(mBlockerVerts);
		mDevice = nullptr;
	}

	void Gpu::setScene(std::shared_ptr<const Scene> scene) { mScene = std::move(scene); }

	bool Gpu::upload()
	{
		releaseScene();
		const Scene& s = *mScene;
		mVerts = makeBuffer(mDevice, s.verts.data(), s.verts.size() * sizeof(V3), D3D11_BIND_VERTEX_BUFFER);
		mTris = makeBuffer(mDevice, s.triIndex.data(), s.triIndex.size() * 4, D3D11_BIND_INDEX_BUFFER);
		if (!mVerts || !mTris)
		{
			releaseScene();
			mError = "the map is too big for a buffer";
			return false;
		}
		mTriCount = (UINT)s.triIndex.size();
		for (int k = 0; k < kEdgeKinds; k++)
		{
			mEdges[k] = makeBuffer(mDevice, s.edgeIndex[k].data(), s.edgeIndex[k].size() * 4, D3D11_BIND_INDEX_BUFFER);
			mEdgeCount[k] = mEdges[k] ? (UINT)s.edgeIndex[k].size() : 0;
		}
		std::vector<V3> spot;
		for (const auto* group : { &s.green, &s.yellow, &s.cyan })
			for (const Seg& g : *group)
			{
				spot.push_back(g.a);
				spot.push_back(g.b);
			}
		mSpotVerts = makeBuffer(mDevice, spot.data(), spot.size() * sizeof(V3), D3D11_BIND_VERTEX_BUFFER);
		if (mSpotVerts)
		{
			mGreen = (UINT)s.green.size() * 2;
			mYellow = (UINT)s.yellow.size() * 2;
			mCyan = (UINT)s.cyan.size() * 2;
		}
		mUploaded = mScene;
		return true;
	}

	bool Gpu::depthFor(ID3D11RenderTargetView* rtv, UINT width, UINT height)
	{
		ID3D11Resource* res = nullptr;
		rtv->GetResource(&res);
		ID3D11Texture2D* tex = nullptr;
		UINT samples = 1, quality = 0;
		if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)))
		{
			D3D11_TEXTURE2D_DESC d;
			tex->GetDesc(&d);
			samples = d.SampleDesc.Count;
			quality = d.SampleDesc.Quality;
			tex->Release();
		}
		if (res) res->Release();
		if (mDepthView && width == mDepthW && height == mDepthH && samples == mDepthSamples) return true;
		drop(mDepthView);
		drop(mDepthTex);
		D3D11_TEXTURE2D_DESC td{};
		td.Width = width;
		td.Height = height;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_D32_FLOAT;
		td.SampleDesc.Count = samples;
		td.SampleDesc.Quality = quality;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(mDevice->CreateTexture2D(&td, nullptr, &mDepthTex))) return false;
		D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
		vd.Format = DXGI_FORMAT_D32_FLOAT;
		vd.ViewDimension = samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(mDevice->CreateDepthStencilView(mDepthTex, &vd, &mDepthView)))
		{
			drop(mDepthTex);
			return false;
		}
		mDepthW = width;
		mDepthH = height;
		mDepthSamples = samples;
		return true;
	}

	bool Gpu::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, float width, float height, const Camera& cam, const DrawOptions& opt)
	{
		if (!mDevice || !mVsLine || !mScene || !ctx || !rtv || width < 16 || height < 16 || (!opt.grid && !opt.spots)) return false;
		if (mUploaded != mScene && !upload()) return false;
		if (!depthFor(rtv, (UINT)width, (UINT)height))
		{
			mError = "the map grid's depth buffer could not be made";
			return false;
		}

		StateBackup saved;
		saved.save(ctx);

		FrameConstants k{};
		const float rx[4] = { cam.right.x / cam.tx, cam.right.y / cam.tx, cam.right.z / cam.tx, -dot(cam.right, cam.pos) / cam.tx };
		const float ry[4] = { cam.up.x / cam.ty, cam.up.y / cam.ty, cam.up.z / cam.ty, -dot(cam.up, cam.pos) / cam.ty };
		const float rz[4] = { 0, 0, 0, kNear };
		const float rw[4] = { cam.fwd.x, cam.fwd.y, cam.fwd.z, -dot(cam.fwd, cam.pos) };
		std::memcpy(k.rowX, rx, sizeof(rx));
		std::memcpy(k.rowY, ry, sizeof(ry));
		std::memcpy(k.rowZ, rz, sizeof(rz));
		std::memcpy(k.rowW, rw, sizeof(rw));
		k.camPos[0] = cam.pos.x;
		k.camPos[1] = cam.pos.y;
		k.camPos[2] = cam.pos.z;
		k.viewport[0] = width;
		k.viewport[1] = height;
		k.viewport[3] = kNear;
		auto setConstants = [&](float halfWidth, Rgba colour, float maxDepth) {
			k.viewport[2] = halfWidth;
			k.colour[0] = colour.r;
			k.colour[1] = colour.g;
			k.colour[2] = colour.b;
			k.colour[3] = colour.a;
			k.limits[0] = maxDepth;
			D3D11_MAPPED_SUBRESOURCE m;
			if (SUCCEEDED(ctx->Map(mFrameCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			{
				std::memcpy(m.pData, &k, sizeof(k));
				ctx->Unmap(mFrameCb, 0);
			}
			};

		const D3D11_VIEWPORT vp{ 0, 0, width, height, 0, 1 };
		ctx->RSSetViewports(1, &vp);
		ctx->RSSetState(mRaster);
		ctx->OMSetRenderTargets(1, &rtv, mDepthView);
		ctx->ClearDepthStencilView(mDepthView, D3D11_CLEAR_DEPTH, 0.f, 0);     // reversed depth: 0 = nothing there
		ctx->IASetInputLayout(mLayout);
		ctx->VSSetConstantBuffers(0, 1, &mFrameCb);
		ctx->GSSetConstantBuffers(0, 1, &mFrameCb);
		ctx->PSSetConstantBuffers(0, 1, &mFrameCb);
		const UINT stride = sizeof(V3), offset = 0;

		// 1. the collision triangles into the depth buffer only
		setConstants(1.f, rgb(0, 0, 0), 1e9f);
		ctx->IASetVertexBuffers(0, 1, &mVerts, &stride, &offset);
		ctx->IASetIndexBuffer(mTris, DXGI_FORMAT_R32_UINT, 0);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(mVsDepth, nullptr, 0);
		ctx->GSSetShader(nullptr, nullptr, 0);
		ctx->PSSetShader(nullptr, nullptr, 0);
		ctx->OMSetBlendState(mNoColour, nullptr, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(mDepthWrite, 0);
		ctx->RSSetState(mRasterDepth);
		ctx->DrawIndexed(mTriCount, 0, 0);
		ctx->RSSetState(mRaster);
		// players: into the depth buffer too, so no line is drawn over them
		if (opt.blockers && !opt.blockers->empty() && mBlockerVerts)
		{
			std::vector<V3> tris;
			for (const Capsule& c : *opt.blockers) capsuleTriangles(c, tris);
			const UINT n = (UINT)(std::min)(tris.size(), (size_t)kBlockerVertexCap) / 3 * 3;
			D3D11_MAPPED_SUBRESOURCE m;
			if (n && SUCCEEDED(ctx->Map(mBlockerVerts, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			{
				std::memcpy(m.pData, tris.data(), n * sizeof(V3));
				ctx->Unmap(mBlockerVerts, 0);
				ctx->IASetVertexBuffers(0, 1, &mBlockerVerts, &stride, &offset);
				ctx->Draw(n, 0);
				ctx->IASetVertexBuffers(0, 1, &mVerts, &stride, &offset);
			}
		}

		// 2. lines, hidden where the triangles are in front
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		ctx->VSSetShader(mVsLine, nullptr, 0);
		ctx->GSSetShader(mGsLine, nullptr, 0);
		ctx->PSSetShader(mPsLine, nullptr, 0);
		ctx->OMSetBlendState(mAlpha, nullptr, 0xFFFFFFFF);
		ctx->OMSetDepthStencilState(mDepthTest, 0);
		const float s = (std::max)(opt.lineScale, 0.5f);
		if (opt.grid)
			for (int kind = 0; kind < kEdgeKinds; kind++)
				if (mEdgeCount[kind])
				{
					Rgba colour = kind == kInstance ? opt.instance : opt.structure;
					colour.a *= std::clamp(opt.gridOpacity, 0.f, 1.f);
					setConstants(std::clamp(opt.gridWidth, 0.25f, 8.f) * 0.5f * s, colour, 1e9f);
					ctx->IASetIndexBuffer(mEdges[kind], DXGI_FORMAT_R32_UINT, 0);
					ctx->DrawIndexed(mEdgeCount[kind], 0, 0);
				}
		if (opt.spots && mSpotVerts)
		{
			// the old helper's pen widths (4 / 2 / 3 pixels), within 25 world units; its cyan and yellow are the grid's
			// colours now, so take-off marks are magenta and fast-drop seams orange (landing edges stay green)
			ctx->IASetVertexBuffers(0, 1, &mSpotVerts, &stride, &offset);
			if (mYellow) setConstants(1.0f * s, kFastDropColour, 25.f), ctx->Draw(mYellow, mGreen);
			if (mGreen) setConstants(2.0f * s, kLandingColour, 25.f), ctx->Draw(mGreen, 0);
			if (mCyan) setConstants(1.5f * s, kTakeOffColour, 25.f), ctx->Draw(mCyan, mGreen + mYellow);
		}

		saved.restore(ctx);
		return true;
	}
}
