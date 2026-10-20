// Offline check of the map grid (HCMInternal\WorldLinesScene.cpp + WorldLinesGpu.cpp) without the game: loads a map's
// world file, prints what it holds, and renders frames with Direct3D's software device (WARP): the collision polygons
// shaded grey stand in for the game's picture, then the map grid is drawn over them exactly as in the overlay.
//   worldlines_test <world file> <spots dir> <map> <out dir> [grid|spots|both] [cameras.txt]
// cameras.txt: lines "x y z  yaw_deg pitch_deg" (eye position; yaw 0 = +x, pitch down negative). Without it the
// cameras stand behind the first jump-spot take-off marks, or at a few floor points.
#define _CRT_SECURE_NO_WARNINGS
#include "../../HCMInternal/WorldLinesGpu.h"
#include "../../HCMInternal/WorldLinesEmit.h"
#include "../../HCMInternal/WorldLinesEngine.h"

#include <d3dcompiler.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <unordered_map>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace worldlines;

namespace
{
	const char* kShadeHlsl = R"(
cbuffer F : register(b0) { float4 rowX, rowY, rowZ, rowW; float4 camPos; float4 viewport; float4 colour; float4 limits; };
struct V { float4 pos : SV_Position; float3 w : TEXCOORD0; };
V vs(float3 p : POSITION) { V o; float4 h = float4(p, 1); o.pos = float4(dot(h, rowX), dot(h, rowY), dot(h, rowZ), dot(h, rowW)); o.w = p; return o; }
float4 ps(V i) : SV_Target {
    float3 n = normalize(cross(ddx(i.w), ddy(i.w)));
    float l = 0.30 + 0.50 * abs(dot(n, normalize(float3(0.4, 0.3, 0.85))));
    float fog = saturate(length(i.w - camPos.xyz) / 90.0);
    return float4(lerp(float3(l, l, l * 1.04) * 0.42, float3(0.10, 0.10, 0.11), fog), 1);
}
)";

	struct Consts
	{
		float rowX[4], rowY[4], rowZ[4], rowW[4], camPos[4], viewport[4], colour[4], limits[4];
	};

	Camera makeCamera(V3 eye, float yawDeg, float pitchDeg, float vfov, float aspect)
	{
		const float y = yawDeg * 3.14159265f / 180.f, p = pitchDeg * 3.14159265f / 180.f;
		Camera c;
		c.pos = eye;
		c.fwd = { std::cos(y) * std::cos(p), std::sin(y) * std::cos(p), std::sin(p) };
		V3 up{ 0, 0, 1 };
		up = up - c.fwd * dot(up, c.fwd);
		c.up = up * (1.f / length(up));
		c.right = cross(c.fwd, c.up);
		c.ty = std::tan(vfov / 2);
		c.tx = c.ty * aspect;
		return c;
	}


	// ---- a stand-in for Halo 3's debug drawer (c_rasterizer), for WorldLinesEngine offline -------------------------
	namespace fake
	{
		ID3D11DeviceContext* ctx = nullptr;
		ID3D11VertexShader* vs = nullptr;
		ID3D11PixelShader* ps = nullptr;
		ID3D11InputLayout* layout = nullptr;
		ID3D11Buffer* cb = nullptr;
		ID3D11Buffer* vb = nullptr;                 // its "draw_primitive_up" buffer
		ID3D11DepthStencilState* read = nullptr;    // z mode 1: tested, not written
		ID3D11DepthStencilState* write = nullptr;   // the scene's own drawing (the gun)
		ID3D11BlendState* blend = nullptr;          // alpha blending
		ID3D11RasterizerState* raster = nullptr;
		int cullMode = 4 * 2, cullChanged = 0;
		int zMode = 0;
		const char* hlsl = R"(
cbuffer View : register(b0) { float4 camPos; float4 right; float4 up; float4 fwd; float4 tn; };
struct VIn { float3 p : POSITION; float4 c : COLOR; };
struct VOut { float4 pos : SV_Position; float4 c : COLOR; };
VOut vs(VIn i) {
    float3 d = i.p - camPos.xyz;
    float vz = dot(d, fwd.xyz);
    VOut o;
    o.pos = float4(dot(d, right.xyz) / tn.x, dot(d, up.xyz) / tn.y, vz * tn.w / (tn.w - tn.z) - tn.z * tn.w / (tn.w - tn.z), vz);
    o.c = i.c;
    return o;
}
float4 ps(VOut i) : SV_Target { return i.c; }
)";
		bool setExplicitShaders(uint32_t, uint32_t, uint32_t, uint32_t, void**)
		{
			ctx->VSSetShader(vs, nullptr, 0);
			ctx->PSSetShader(ps, nullptr, 0);
			ctx->IASetInputLayout(layout);
			ctx->VSSetConstantBuffers(0, 1, &cb);
			ctx->RSSetState(raster);
			const float f[4] = { 0, 0, 0, 0 };
			ctx->OMSetBlendState(blend, f, 0xFFFFFFFF);
			return true;
		}
		void setZBufferMode(int mode) { zMode = mode; }
		void drawPrimitiveUp(uint32_t type, uint32_t count, const void* data, uint32_t stride)
		{
			ctx->OMSetDepthStencilState(zMode == 1 ? read : write, 0);
			const uint32_t per = type == 3 ? 3 : 2;
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(ctx->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
			std::memcpy(m.pData, data, (size_t)count * per * stride);
			ctx->Unmap(vb, 0);
			const UINT off = 0;
			ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
			ctx->IASetPrimitiveTopology(type == 3 ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			ctx->Draw(count * per, 0);
		}
		bool init(ID3D11Device* dev, ID3D11DeviceContext* c)
		{
			ctx = c;
			ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
			if (FAILED(D3DCompile(hlsl, std::strlen(hlsl), "fake", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
				FAILED(D3DCompile(hlsl, std::strlen(hlsl), "fake", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err)))
			{
				if (err) std::printf("%s\n", (const char*)err->GetBufferPointer());
				return false;
			}
			dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
			dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
			const D3D11_INPUT_ELEMENT_DESC el[2] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
			dev->CreateInputLayout(el, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout);
			D3D11_BUFFER_DESC bd{};
			bd.ByteWidth = 80;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			dev->CreateBuffer(&bd, nullptr, &cb);
			bd.ByteWidth = 1024 * 3 * 16;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			dev->CreateBuffer(&bd, nullptr, &vb);
			D3D11_DEPTH_STENCIL_DESC dd{};
			dd.DepthEnable = TRUE;
			dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			dev->CreateDepthStencilState(&dd, &read);
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dev->CreateDepthStencilState(&dd, &write);
			D3D11_BLEND_DESC bl{};
			bl.RenderTarget[0].BlendEnable = TRUE;
			bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			dev->CreateBlendState(&bl, &blend);
			D3D11_RASTERIZER_DESC rd{};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_NONE;
			rd.DepthClipEnable = TRUE;
			dev->CreateRasterizerState(&rd, &raster);
			return vs && ps && layout && cb && vb && read && write && blend && raster;
		}
		void setView(const Camera& cam)
		{
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
			const float k[20] = { cam.pos.x, cam.pos.y, cam.pos.z, 0, cam.right.x, cam.right.y, cam.right.z, 0, cam.up.x, cam.up.y, cam.up.z, 0,
				cam.fwd.x, cam.fwd.y, cam.fwd.z, 0, cam.tx, cam.ty, 0.0078125f, 4096.f };
			std::memcpy(m.pData, k, sizeof(k));
			ctx->Unmap(cb, 0);
		}
	}

	// ---- a stand-in for the game's own depth buffer (the frame path's gun mask) ---------------------------------------
	// the game's picture of the map (the collision polygons again) and a first-person gun, drawn into a depth buffer the
	// way the MCC games keep theirs: float depth reversed with the camera's near / far planes (near 1/128, far 10240 as
	// Reach's render_near/far_clip_distance), the gun with Halo 3's first-person near plane (x2.4, 0x279BEC); 24-bit
	// depth the usual way round
	namespace gamedepth
	{
		const char* hlsl = R"(
cbuffer G : register(b0) { float4 camPos; float4 right; float4 up; float4 fwd; float4 p; float4 q; };
float4 vs(float3 x : POSITION) : SV_Position {
    float3 d = x - camPos.xyz;
    float vz = dot(d, fwd.xyz);
    float n = p.z, f = p.w;
    float z = q.x > 0.5 ? n * f / (f - n) - n * vz / (f - n) : f * vz / (f - n) - n * f / (f - n);
    return float4(dot(d, right.xyz) / p.x, dot(d, up.xyz) / p.y, z, vz);
}
)";
		ID3D11VertexShader* vs = nullptr;
		ID3D11InputLayout* layout = nullptr;
		ID3D11Buffer* cb = nullptr;
		ID3D11Buffer* quad = nullptr;
		ID3D11DepthStencilState* greater = nullptr, * less = nullptr, * always = nullptr;
		ID3D11RasterizerState* raster = nullptr;
		constexpr float kNearPlane = 0.0078125f, kFarPlane = 10240.f;

		bool init(ID3D11Device* dev)
		{
			ID3DBlob* b = nullptr, * err = nullptr;
			if (FAILED(D3DCompile(hlsl, std::strlen(hlsl), "gamedepth", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &b, &err)))
			{
				if (err) std::printf("%s\n", (const char*)err->GetBufferPointer());
				return false;
			}
			dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs);
			const D3D11_INPUT_ELEMENT_DESC el{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
			dev->CreateInputLayout(&el, 1, b->GetBufferPointer(), b->GetBufferSize(), &layout);
			D3D11_BUFFER_DESC bd{};
			bd.ByteWidth = 96;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			dev->CreateBuffer(&bd, nullptr, &cb);
			bd.ByteWidth = 6 * 12;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			dev->CreateBuffer(&bd, nullptr, &quad);
			D3D11_DEPTH_STENCIL_DESC dd{};
			dd.DepthEnable = TRUE;
			dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			dd.DepthFunc = D3D11_COMPARISON_GREATER;
			dev->CreateDepthStencilState(&dd, &greater);
			dd.DepthFunc = D3D11_COMPARISON_LESS;
			dev->CreateDepthStencilState(&dd, &less);
			dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
			dev->CreateDepthStencilState(&dd, &always);
			D3D11_RASTERIZER_DESC rd{};
			rd.FillMode = D3D11_FILL_SOLID;
			rd.CullMode = D3D11_CULL_NONE;
			rd.DepthClipEnable = TRUE;
			dev->CreateRasterizerState(&rd, &raster);
			return vs && layout && cb && quad && greater && less && always && raster;
		}

		void setView(ID3D11DeviceContext* ctx, const Camera& cam, float nearPlane, bool reversed)
		{
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
			const float k[24] = { cam.pos.x, cam.pos.y, cam.pos.z, 0, cam.right.x, cam.right.y, cam.right.z, 0, cam.up.x, cam.up.y, cam.up.z, 0,
				cam.fwd.x, cam.fwd.y, cam.fwd.z, 0, cam.tx, cam.ty, nearPlane, kFarPlane, reversed ? 1.f : 0.f, 0, 0, 0 };
			std::memcpy(m.pData, k, sizeof(k));
			ctx->Unmap(cb, 0);
		}

		// the game's frame into dsv (w x h): the map, then the gun over the bottom-right quarter, 0.3 units ahead
		void render(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv, UINT w, UINT h, bool reversed, const Camera& cam, ID3D11Buffer* verts,
			ID3D11Buffer* tris, UINT triCount)
		{
			ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, reversed ? 0.f : 1.f, 0);
			ctx->OMSetRenderTargets(0, nullptr, dsv);
			const D3D11_VIEWPORT vp{ 0, 0, (float)w, (float)h, 0, 1 };
			ctx->RSSetViewports(1, &vp);
			ctx->RSSetState(raster);
			ctx->IASetInputLayout(layout);
			ctx->VSSetShader(vs, nullptr, 0);
			ctx->VSSetConstantBuffers(0, 1, &cb);
			ctx->GSSetShader(nullptr, nullptr, 0);
			ctx->PSSetShader(nullptr, nullptr, 0);
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			const UINT stride = 12, offset = 0;
			setView(ctx, cam, kNearPlane, reversed);
			ctx->OMSetDepthStencilState(reversed ? greater : less, 0);
			ctx->IASetVertexBuffers(0, 1, &verts, &stride, &offset);
			ctx->IASetIndexBuffer(tris, DXGI_FORMAT_R32_UINT, 0);
			ctx->DrawIndexed(triCount, 0, 0);
			const V3 c0 = cam.pos + cam.fwd * 0.3f;
			const V3 r = cam.right * (0.3f * cam.tx * 1.02f), u = cam.up * (0.3f * cam.ty * 1.02f);
			const V3 q[6] = { c0, c0 + r, c0 + r - u, c0, c0 + r - u, c0 - u };
			D3D11_MAPPED_SUBRESOURCE m;
			if (SUCCEEDED(ctx->Map(quad, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			{
				std::memcpy(m.pData, q, sizeof(q));
				ctx->Unmap(quad, 0);
			}
			setView(ctx, cam, kNearPlane * 2.4f, reversed);
			ctx->OMSetDepthStencilState(always, 0);
			ctx->IASetVertexBuffers(0, 1, &quad, &stride, &offset);
			ctx->Draw(6, 0);
			ctx->OMSetDepthStencilState(nullptr, 0);
		}
	}

	bool writeBmp(const char* path, const uint8_t* rgba, UINT pitch, int w, int h)
	{
		FILE* f = std::fopen(path, "wb");
		if (!f) return false;
		const int row = (w * 3 + 3) & ~3;
		const uint32_t size = 54 + row * h;
		uint8_t hdr[54] = { 'B', 'M' };
		auto put32 = [&](int at, uint32_t v) { std::memcpy(hdr + at, &v, 4); };
		put32(2, size);
		put32(10, 54);
		put32(14, 40);
		put32(18, (uint32_t)w);
		put32(22, (uint32_t)h);
		hdr[26] = 1;
		hdr[28] = 24;
		put32(34, row * h);
		std::fwrite(hdr, 1, 54, f);
		std::vector<uint8_t> line(row, 0);
		for (int y = h - 1; y >= 0; y--)
		{
			const uint8_t* src = rgba + (size_t)y * pitch;
			for (int x = 0; x < w; x++)
			{
				line[x * 3 + 0] = src[x * 4 + 2];
				line[x * 3 + 1] = src[x * 4 + 1];
				line[x * 3 + 2] = src[x * 4 + 0];
			}
			std::fwrite(line.data(), 1, row, f);
		}
		std::fclose(f);
		return true;
	}
}

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		std::fprintf(stderr, "usage: worldlines_test <world file> <spots dir> <map> <out dir> [grid|spots|both] [cameras.txt]\n");
		return 2;
	}
	using clock = std::chrono::steady_clock;
	const auto t0 = clock::now();
	std::shared_ptr<Scene> scene;
	try
	{
		scene = Scene::load(argv[1], argv[2], argv[3]);
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "load failed: %s\n", e.what());
		return 1;
	}
	const double loadMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
	std::printf("%s: %zu polygons -> %zu vertices, %zu triangles, edges %zu structure + %zu instanced = %zu; green %zu, yellow %zu, "
		"rings %zu; load %.0f ms\n", scene->map.c_str(), scene->polygons, scene->verts.size(), scene->triIndex.size() / 3,
		scene->edgeIndex[kStructure].size() / 2, scene->edgeIndex[kInstance].size() / 2,
		(scene->edgeIndex[0].size() + scene->edgeIndex[1].size()) / 2, scene->green.size(), scene->yellow.size(), scene->rings.size(), loadMs);
	for (int k = 0; k < kEdgeKinds; k++)
		for (uint32_t i : scene->edgeIndex[k])
			if (i >= scene->verts.size())
			{
				std::fprintf(stderr, "FAIL: edge index out of range\n");
				return 1;
			}

	const std::string mode = argc > 5 ? argv[5] : "grid";
	DrawOptions opt;
	opt.grid = mode != "spots";
	opt.spots = mode != "grid";
	const int W = 1920, H = 1080;
	const float vfov = 1.43f;      // 114 degrees wide at 16:9

	// cameras
	struct Pose { V3 eye; float yaw, pitch; };
	std::vector<Pose> poses;
	if (argc > 6)
	{
		std::ifstream cf(argv[6]);
		Pose p;
		while (cf >> p.eye.x >> p.eye.y >> p.eye.z >> p.yaw >> p.pitch) poses.push_back(p);
	}
	else
	{
		for (size_t i = 0; i < scene->rings.size() && poses.size() < 3; i++)
		{
			const Ring& r = scene->rings[i];
			const float yaw = std::atan2(r.dir.y, r.dir.x) * 180.f / 3.14159265f;
			poses.push_back({ r.pos + V3{ 0, 0, 0.62f } - r.dir * 1.5f, yaw, -35.f });
		}
		for (size_t i = 0; poses.size() < 4 && i < scene->triIndex.size() / 3; i += scene->triIndex.size() / 3 / 7 + 1)
		{
			const V3 a = scene->verts[scene->triIndex[3 * i]];
			poses.push_back({ a + V3{ 0, 0, 2.5f }, 45.f * (float)i, -30.f });
		}
	}

	// the software device and a 1080p target
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
	if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
	{
		std::fprintf(stderr, "no WARP device\n");
		return 1;
	}
	D3D11_TEXTURE2D_DESC td{};
	td.Width = W;
	td.Height = H;
	td.MipLevels = td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET;
	ID3D11Texture2D* target = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	dev->CreateTexture2D(&td, nullptr, &target);
	dev->CreateRenderTargetView(target, nullptr, &rtv);
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D* staging = nullptr;
	dev->CreateTexture2D(&td, nullptr, &staging);
	td.Format = DXGI_FORMAT_D32_FLOAT;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	td.CPUAccessFlags = 0;
	ID3D11Texture2D* bgDepthTex = nullptr;
	ID3D11DepthStencilView* bgDepth = nullptr;
	dev->CreateTexture2D(&td, nullptr, &bgDepthTex);
	dev->CreateDepthStencilView(bgDepthTex, nullptr, &bgDepth);

	// the stand-in for the game's picture: the collision polygons, shaded
	ID3DBlob* vsb = nullptr, * psb = nullptr, * err = nullptr;
	if (FAILED(D3DCompile(kShadeHlsl, std::strlen(kShadeHlsl), "shade", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsb, &err)) ||
		FAILED(D3DCompile(kShadeHlsl, std::strlen(kShadeHlsl), "shade", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psb, &err)))
	{
		std::fprintf(stderr, "shade shader: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
		return 1;
	}
	ID3D11VertexShader* svs = nullptr;
	ID3D11PixelShader* sps = nullptr;
	ID3D11InputLayout* slayout = nullptr;
	dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &svs);
	dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &sps);
	const D3D11_INPUT_ELEMENT_DESC el{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
	dev->CreateInputLayout(&el, 1, vsb->GetBufferPointer(), vsb->GetBufferSize(), &slayout);
	D3D11_BUFFER_DESC bd{};
	bd.ByteWidth = (UINT)(scene->verts.size() * sizeof(V3));
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA sd{ scene->verts.data() };
	ID3D11Buffer* vb = nullptr, * ib = nullptr, * cb = nullptr;
	dev->CreateBuffer(&bd, &sd, &vb);
	bd.ByteWidth = (UINT)(scene->triIndex.size() * 4);
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	sd.pSysMem = scene->triIndex.data();
	dev->CreateBuffer(&bd, &sd, &ib);
	bd.ByteWidth = sizeof(Consts);
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	dev->CreateBuffer(&bd, nullptr, &cb);
	D3D11_DEPTH_STENCIL_DESC dsd{};
	dsd.DepthEnable = TRUE;
	dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsd.DepthFunc = D3D11_COMPARISON_GREATER;
	ID3D11DepthStencilState* dss = nullptr;
	dev->CreateDepthStencilState(&dsd, &dss);
	D3D11_RASTERIZER_DESC rsd{};
	rsd.FillMode = D3D11_FILL_SOLID;
	rsd.CullMode = D3D11_CULL_NONE;
	rsd.DepthClipEnable = TRUE;
	ID3D11RasterizerState* rs = nullptr;
	dev->CreateRasterizerState(&rsd, &rs);

	Gpu gpu;
	if (!gpu.init(dev))
	{
		std::fprintf(stderr, "FAIL: %s\n", gpu.lastError());
		return 1;
	}
	gpu.setScene(scene);
	opt.lineScale = H / 1080.f;
	for (size_t i = 0; i < poses.size(); i++)
	{
		const Camera cam = makeCamera(poses[i].eye, poses[i].yaw, poses[i].pitch, vfov, (float)W / H);
		const float bg[4] = { 0.1f, 0.1f, 0.11f, 1 };
		ctx->ClearRenderTargetView(rtv, bg);
		ctx->ClearDepthStencilView(bgDepth, D3D11_CLEAR_DEPTH, 0.f, 0);
		Consts k{};
		const float rx[4] = { cam.right.x / cam.tx, cam.right.y / cam.tx, cam.right.z / cam.tx, -dot(cam.right, cam.pos) / cam.tx };
		const float ry[4] = { cam.up.x / cam.ty, cam.up.y / cam.ty, cam.up.z / cam.ty, -dot(cam.up, cam.pos) / cam.ty };
		const float rz[4] = { 0, 0, 0, kNear };
		const float rw[4] = { cam.fwd.x, cam.fwd.y, cam.fwd.z, -dot(cam.fwd, cam.pos) };
		std::memcpy(k.rowX, rx, 16);
		std::memcpy(k.rowY, ry, 16);
		std::memcpy(k.rowZ, rz, 16);
		std::memcpy(k.rowW, rw, 16);
		k.camPos[0] = cam.pos.x, k.camPos[1] = cam.pos.y, k.camPos[2] = cam.pos.z;
		ctx->UpdateSubresource(cb, 0, nullptr, &k, 0, 0);
		const D3D11_VIEWPORT vp{ 0, 0, (float)W, (float)H, 0, 1 };
		ctx->RSSetViewports(1, &vp);
		ctx->RSSetState(rs);
		ctx->OMSetRenderTargets(1, &rtv, bgDepth);
		ctx->OMSetDepthStencilState(dss, 0);
		ctx->IASetInputLayout(slayout);
		const UINT stride = 12, offset = 0;
		ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx->VSSetShader(svs, nullptr, 0);
		ctx->VSSetConstantBuffers(0, 1, &cb);
		ctx->PSSetShader(sps, nullptr, 0);
		ctx->PSSetConstantBuffers(0, 1, &cb);
		ctx->DrawIndexed((UINT)scene->triIndex.size(), 0, 0);
		// the overlay draws with the game's target bound and no depth (ImGuiManager); the grid restores that after itself
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		const auto t1 = clock::now();
		if (!gpu.draw(ctx, rtv, (float)W, (float)H, cam, opt))
		{
			std::fprintf(stderr, "FAIL: draw: %s\n", gpu.lastError());
			return 1;
		}
		ctx->Flush();
		// the state must be what it was: the game's target, no depth view
		ID3D11RenderTargetView* now = nullptr;
		ID3D11DepthStencilView* nowDepth = nullptr;
		ctx->OMGetRenderTargets(1, &now, &nowDepth);
		const bool restored = now == rtv && nowDepth == nullptr;
		if (now) now->Release();
		if (nowDepth) nowDepth->Release();
		ctx->CopyResource(staging, target);
		D3D11_MAPPED_SUBRESOURCE m;
		ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
		const double ms = std::chrono::duration<double, std::milli>(clock::now() - t1).count();
		char path[1024];
		std::snprintf(path, sizeof path, "%s\\%s_%s_%zu.bmp", argv[4], scene->map.c_str(), mode.c_str(), i);
		writeBmp(path, (const uint8_t*)m.pData, m.RowPitch, W, H);
		ctx->Unmap(staging, 0);
		const std::vector<Label> labels = visibleLabels(*scene, cam);
		std::printf("  camera %zu at (%.2f %.2f %.2f) yaw %.0f pitch %.0f -> %s (WARP %.0f ms, state restored %s, %zu labels)\n", i, cam.pos.x,
			cam.pos.y, cam.pos.z, poses[i].yaw, poses[i].pitch, path, ms, restored ? "yes" : "NO", labels.size());
		if (!restored) return 1;
	}
	// a player in the middle of the view (a capsule 2 units ahead): no grid line may be drawn over it
	{
		int behind = 0, over = 0, tried = 0;
		// level views from points on the map (eye height), 8 directions each
		std::vector<Pose> views;
		for (size_t i = 0; views.size() < 400 && i < scene->triIndex.size() / 3; i += scene->triIndex.size() / 3 / 50 + 1)
		{
			const V3 a = scene->verts[scene->triIndex[i * 3]], b = scene->verts[scene->triIndex[i * 3 + 1]], c = scene->verts[scene->triIndex[i * 3 + 2]];
			for (int k = 0; k < 8; k++) views.push_back({ (a + b + c) * (1.f / 3) + V3{ 0, 0, 0.7f }, 45.f * (float)k, 0.f });
		}
		for (size_t i = 0; i < views.size() && tried < 40; i++)
		{
			const Camera cam = makeCamera(views[i].eye, views[i].yaw, views[i].pitch, vfov, (float)W / H);
			const V3 mid = cam.pos + cam.fwd * 2.f;
			const std::vector<Capsule> blockers{ { mid - V3{ 0, 0, 0.35f }, mid + V3{ 0, 0, 0.35f }, 0.35f } };
			// only views where nothing of the map is between the camera and the capsule (a map edge in front of a
			// player is drawn over it, rightly): rays to the capsule's middle and around it
			bool clear = true;
			for (int k = 0; k < 17 * 17 && clear; k++)
			{
				// the test box (120 px = 0.39 units across at 2 units) and a little more, up to the capsule's front
				const float ox = (float)(k % 17 - 8) * 0.03f, oz = (float)(k / 17 - 8) * 0.03f;
				const V3 to = mid + cam.right * ox + cam.up * oz;
				float t = 0;
				if (scene->firstHit(cam.pos, cam.pos + (to - cam.pos) * 0.85f, t)) clear = false;
			}
			if (!clear) continue;
			int counts[2] = {};
			for (int with = 0; with < 2; with++)
			{
				const float bg[4] = { 0.1f, 0.1f, 0.11f, 1 };
				ctx->ClearRenderTargetView(rtv, bg);
				ctx->OMSetRenderTargets(1, &rtv, nullptr);
				DrawOptions o = opt;
				o.grid = true;
				o.spots = false;
				o.blockers = with ? &blockers : nullptr;
				gpu.draw(ctx, rtv, (float)W, (float)H, cam, o);
				ctx->CopyResource(staging, target);
				D3D11_MAPPED_SUBRESOURCE m;
				ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
				// the capsule's middle on screen: a 120 x 120 box around the centre (the capsule is ~220 px wide there)
				for (int y = H / 2 - 60; y < H / 2 + 60; y++)
					for (int x = W / 2 - 60; x < W / 2 + 60; x++)
					{
						const uint8_t* p = (const uint8_t*)m.pData + (size_t)y * m.RowPitch + (size_t)x * 4;
						if (p[0] > 60 || p[1] > 60 || p[2] > 60) counts[with]++;
					}
				ctx->Unmap(staging, 0);
			}
			if (counts[0] > 0)
			{
				tried++;
				behind += counts[0];
				over += counts[1];
			}
		}
		std::printf("  blocker: %d line pixels in the middle without it, %d with a player capsule there (%d views)\n", behind, over, tried);
		if (tried < 3 || over != 0)
		{
			std::printf("FAIL: lines drawn over a player\n");
			return 1;
		}
	}
	// the Halo 3 path (WorldLinesEmit: the lines the game's own debug drawer gets): every map edge with an end on screen
	// and within the draw distance comes out, from the cells near the camera; and how long a frame's lines take
	{
		EdgeCells cells;
		const auto c0 = std::chrono::steady_clock::now();
		cells.build(*scene);
		const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
		size_t total = scene->edgeIndex[kStructure].size() / 2 + scene->edgeIndex[kInstance].size() / 2;
		EmitScratch scratch;
		Emitted out;
		EmitOptions eo;
		eo.depthPull = 0.f;
		eo.maxDistance = 80.f;
		int missed = 0, views = 0;
		size_t emittedMax = 0, mustMax = 0;
		double emitMs = 0;
		std::vector<Pose> views2 = poses;
		for (size_t i = 0; views2.size() < 24 && i < scene->triIndex.size() / 3; i += scene->triIndex.size() / 3 / 20 + 1)
		{
			const V3 a = scene->verts[scene->triIndex[i * 3]], b = scene->verts[scene->triIndex[i * 3 + 1]], c = scene->verts[scene->triIndex[i * 3 + 2]];
			views2.push_back({ (a + b + c) * (1.f / 3) + V3{ 0, 0, 0.7f }, 37.f * (float)i, -10.f });
		}
		auto key = [](V3 p) {
			uint32_t x, y, z;
			std::memcpy(&x, &p.x, 4), std::memcpy(&y, &p.y, 4), std::memcpy(&z, &p.z, 4);
			return ((uint64_t)x * 0x9E3779B1u) ^ ((uint64_t)y << 21) ^ ((uint64_t)z * 0x85EBCA77u);
			};
		auto emitCam = [&](const Camera& cam) {
			EmitCamera ec;
			ec.pos = cam.pos, ec.fwd = cam.fwd, ec.up = cam.up, ec.left = cross(cam.up, cam.fwd);
			ec.vfov = vfov, ec.hfov = 2.f * std::atan(std::tan(vfov / 2) * (float)W / H), ec.heightPx = (float)H, ec.aspect = (float)W / H;
			return ec;
			};
		for (const Pose& pose : views2)
		{
			const Camera cam = makeCamera(pose.eye, pose.yaw, pose.pitch, vfov, (float)W / H);
			const auto te = std::chrono::steady_clock::now();
			emit(*scene, cells, emitCam(cam), eo, scratch, out);
			emitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - te).count();
			views++;
			// the emitted lines, as point pairs (no pull: they are the scene's own points)
			std::unordered_map<uint64_t, int> got;
			for (size_t v = 0; v + 1 < out.lines.size(); v += 2)
			{
				const V3 a{ out.lines[v].x, out.lines[v].y, out.lines[v].z }, b{ out.lines[v + 1].x, out.lines[v + 1].y, out.lines[v + 1].z };
				got[key(a) ^ (key(b) * 3)]++;
				got[key(b) ^ (key(a) * 3)]++;
			}
			size_t must = 0;
			for (int k = 0; k < kEdgeKinds; k++)
				for (size_t e = 0; e + 1 < scene->edgeIndex[k].size(); e += 2)
				{
					const V3 a = scene->verts[scene->edgeIndex[k][e]], b = scene->verts[scene->edgeIndex[k][e + 1]];
					if (length(a - cam.pos) > 80.f && length(b - cam.pos) > 80.f) continue;
					float x, y;
					const bool onA = toScreen(cam, a, (float)W, (float)H, x, y) && x >= 0 && x < W && y >= 0 && y < H;
					const bool onB = toScreen(cam, b, (float)W, (float)H, x, y) && x >= 0 && x < W && y >= 0 && y < H;
					if (!onA && !onB) continue;
					must++;
					if (!got.count(key(a) ^ (key(b) * 3))) missed++;
				}
			emittedMax = (std::max)(emittedMax, out.lines.size() / 2);
			mustMax = (std::max)(mustMax, must);
		}
		// whole map (draw distance 0): the worst case
		eo.maxDistance = 0.f;
		const Camera cam0 = makeCamera(views2[0].eye, views2[0].yaw, views2[0].pitch, vfov, (float)W / H);
		const auto t1 = std::chrono::steady_clock::now();
		emit(*scene, cells, emitCam(cam0), eo, scratch, out);
		const double wholeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
		const size_t wholeLines = out.lines.size() / 2;
		// wide lines: two triangles per line
		eo.maxDistance = 80.f;
		eo.gridWidth = 2.5f;
		emit(*scene, cells, emitCam(cam0), eo, scratch, out);
		const bool ribbons = out.lines.empty() && !out.tris.empty() && out.tris.size() % 6 == 0;
		std::printf("  game's own drawer: %zu edges, cells built in %.1f ms; %d views: %d on-screen edges missing (most on screen %zu, emitted %zu), %.3f ms a frame at 80 units; whole map %.2f ms (%zu lines); wide lines as ribbons %d\n",
			total, buildMs, views, missed, mustMax, emittedMax, emitMs / (std::max)(views, 1), wholeMs, wholeLines, ribbons);
		if (missed != 0 || !ribbons)
		{
			std::printf("FAIL: the game's-drawer lines\n");
			return 1;
		}
	}
	// WorldLinesEngine with the stand-in drawer: the map's lines are drawn, never over a "first-person gun" already in
	// the depth buffer (the bottom-right quarter, 0.3 units from the camera), and the drawer's buffer, topology, blend
	// and rasterizer states are as it left them
	{
		if (!fake::init(dev, ctx))
		{
			std::printf("FAIL: the stand-in drawer could not be made\n");
			return 1;
		}
		ID3D11Texture2D* dtex = nullptr;
		ID3D11DepthStencilView* dsv = nullptr;
		D3D11_TEXTURE2D_DESC dd{};
		dd.Width = W, dd.Height = H, dd.MipLevels = 1, dd.ArraySize = 1, dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT, dd.SampleDesc.Count = 1;
		dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		dev->CreateTexture2D(&dd, nullptr, &dtex);
		dev->CreateDepthStencilView(dtex, nullptr, &dsv);
		auto cellsPtr = std::make_shared<EdgeCells>();
		cellsPtr->build(*scene);
		std::shared_ptr<const Scene> sceneC = scene;
		std::shared_ptr<const EdgeCells> cellsC = cellsPtr;
		EngineFns fns;
		fns.setExplicitShaders = &fake::setExplicitShaders;
		fns.setZBufferMode = &fake::setZBufferMode;
		fns.drawPrimitiveUp = &fake::drawPrimitiveUp;
		fns.cullMode = &fake::cullMode;
		fns.cullModeChanged = &fake::cullChanged;
		EngineGrid grid;
		int lit = 0, overGun = 0, restored = 0, tried = 0, gunViews = 0;
		const char* why = "";
		for (const Pose& pose : poses)
		{
			const Camera cam = makeCamera(pose.eye, pose.yaw, pose.pitch, vfov, (float)W / H);
			const float bg[4] = { 0, 0, 0, 1 };
			ctx->ClearRenderTargetView(rtv, bg);
			ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
			ctx->OMSetRenderTargets(1, &rtv, dsv);
			D3D11_VIEWPORT vp{ 0, 0, (float)W, (float)H, 0, 1 };
			ctx->RSSetViewports(1, &vp);
			fake::setView(cam);
			// the gun: a black quad in the bottom-right quarter, depth written (the game's scene before the hook)
			fake::setExplicitShaders(0, 8, 0, 0, nullptr);
			fake::setZBufferMode(0);
			const V3 c0 = cam.pos + cam.fwd * 0.05f;
			const V3 r = cam.right * (0.05f * cam.tx), u = cam.up * (0.05f * cam.ty);
			const V3 q[4] = { c0, c0 + r, c0 + r - u, c0 - u };
			const worldlines::DebugVertex gun[6] = { { q[0].x, q[0].y, q[0].z, 0, 0, 0, 255 }, { q[1].x, q[1].y, q[1].z, 0, 0, 0, 255 },
				{ q[2].x, q[2].y, q[2].z, 0, 0, 0, 255 }, { q[0].x, q[0].y, q[0].z, 0, 0, 0, 255 }, { q[2].x, q[2].y, q[2].z, 0, 0, 0, 255 },
				{ q[3].x, q[3].y, q[3].z, 0, 0, 0, 255 } };
			fake::drawPrimitiveUp(3, 2, gun, 16);
			// what the drawer's state is before ours
			ID3D11Buffer* vbBefore = nullptr;
			UINT sb = 0, ob = 0;
			ctx->IAGetVertexBuffers(0, 1, &vbBefore, &sb, &ob);
			EmitCamera ec;
			ec.pos = cam.pos, ec.fwd = cam.fwd, ec.up = cam.up, ec.left = cross(cam.up, cam.fwd), ec.vfov = vfov;
			ec.hfov = 2.f * std::atan(std::tan(vfov / 2) * (float)W / H), ec.heightPx = (float)H, ec.aspect = (float)W / H;
			EmitOptions eo;
			eo.grid = true;
			eo.gridOpacity = 1.f;
			if (!grid.draw(ctx, fns, sceneC, cellsC, ec, eo))
			{
				why = grid.lastError();
				break;
			}
			tried++;
			ID3D11Buffer* vbAfter = nullptr;
			UINT sa = 0, oa = 0;
			ctx->IAGetVertexBuffers(0, 1, &vbAfter, &sa, &oa);
			ID3D11BlendState* bs = nullptr;
			FLOAT bf[4];
			UINT bm = 0;
			ctx->OMGetBlendState(&bs, bf, &bm);
			ID3D11RasterizerState* rsNow = nullptr;
			ctx->RSGetState(&rsNow);
			D3D11_PRIMITIVE_TOPOLOGY topo{};
			ctx->IAGetPrimitiveTopology(&topo);
			if (vbAfter == fake::vb && vbBefore == fake::vb && bs == fake::blend && rsNow == fake::raster && topo == D3D11_PRIMITIVE_TOPOLOGY_LINELIST && fake::cullMode == 4 * 2)
				restored++;
			for (auto* x : { vbBefore, vbAfter }) if (x) x->Release();
			if (bs) bs->Release();
			if (rsNow) rsNow->Release();
			// a map edge nearer than the gun is rightly in front of it: such views are not counted for the gun
			bool clearAhead = true;
			for (int k = 0; k < kEdgeKinds && clearAhead; k++)
				for (size_t e = 0; e + 1 < scene->edgeIndex[k].size() && clearAhead; e += 2)
				{
					const V3 a = scene->verts[scene->edgeIndex[k][e]], b = scene->verts[scene->edgeIndex[k][e + 1]], ab = b - a, ap = cam.pos - a;
					const float t = std::clamp(dot(ap, ab) / (std::max)(dot(ab, ab), 1e-9f), 0.f, 1.f);
					if (length(ap - ab * t) < 0.1f) clearAhead = false;
				}
			if (clearAhead) gunViews++;
			ctx->CopyResource(staging, target);
			D3D11_MAPPED_SUBRESOURCE m;
			ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++)
				{
					const uint8_t* px = (const uint8_t*)m.pData + (size_t)y * m.RowPitch + (size_t)x * 4;
					const bool on = px[0] > 40 || px[1] > 40 || px[2] > 40;
					if (!on) continue;
					if (x > W / 2 + 4 && y > H / 2 + 4) { if (clearAhead) overGun++; }
					else lit++;
				}
			{
				static int last = 0;
				if (overGun != last) { std::printf("    view %d (eye %.2f %.2f %.2f yaw %.0f pitch %.0f): %d over the gun|", tried, pose.eye.x, pose.eye.y, pose.eye.z, pose.yaw, pose.pitch, overGun - last);
					char path[512]; std::snprintf(path, sizeof(path), "%s/engine_gun_%d.bmp", argv[4], tried); writeBmp(path, (const uint8_t*)m.pData, m.RowPitch, W, H); }
				last = overGun;
			}
			ctx->Unmap(staging, 0);
		}
		std::printf("  in the game's view render (stand-in drawer): %d views, %d line pixels, %d over the first-person gun, state put back on %d%s%s\n", tried, lit, overGun,
			restored, *why ? ": " : "", why);
		if (tried == 0 || lit == 0 || gunViews == 0 || overGun != 0 || restored != tried)
		{
			std::printf("FAIL: the game's-drawer path\n");
			return 1;
		}
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		dsv->Release();
		dtex->Release();
	}
	// the frame path over the game's own depth (Halo 3 without bloom, ODST, Reach): no line over the first-person gun
	// (the bottom-right quarter, 0.3 units ahead) and the map's lines elsewhere as without it, in each depth layout the
	// games use or might (float reversed at the frame's size and at another size, 24-bit the usual way round); the
	// context's targets and shader resource put back
	{
		if (!gamedepth::init(dev))
		{
			std::printf("FAIL: the stand-in game depth could not be made\n");
			return 1;
		}
		struct Layout
		{
			const char* name;
			DXGI_FORMAT tex, view;
			UINT w, h;
			bool reversed;
		};
		const Layout layouts[] = {
			{ "D32_FLOAT_S8X24 at the frame's size (the MCC games)", DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, (UINT)W, (UINT)H, true },
			{ "D32_FLOAT at 1280 x 720", DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, 1280, 720, true },
			{ "D24_UNORM_S8 the usual way round", DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D24_UNORM_S8_UINT, (UINT)W, (UINT)H, false },
		};
		// level views from points on the map with no map edge within half a unit of the camera
		std::vector<Pose> views;
		for (size_t i = 0; views.size() < 48 && i < scene->triIndex.size() / 3; i += scene->triIndex.size() / 3 / 40 + 1)
		{
			const V3 a = scene->verts[scene->triIndex[i * 3]], b = scene->verts[scene->triIndex[i * 3 + 1]], c = scene->verts[scene->triIndex[i * 3 + 2]];
			const V3 eye = (a + b + c) * (1.f / 3) + V3{ 0, 0, 0.7f };
			bool clear = true;
			for (int k = 0; k < kEdgeKinds && clear; k++)
				for (size_t e = 0; e + 1 < scene->edgeIndex[k].size() && clear; e += 2)
				{
					const V3 p = scene->verts[scene->edgeIndex[k][e]], q = scene->verts[scene->edgeIndex[k][e + 1]], pq = q - p, pe = eye - p;
					const float t = std::clamp(dot(pe, pq) / (std::max)(dot(pq, pq), 1e-9f), 0.f, 1.f);
					if (length(pe - pq * t) < 0.5f) clear = false;
				}
			if (clear)
				for (int k = 0; k < 4; k++) views.push_back({ eye, 90.f * (float)k + 20.f, -15.f });
		}
		// a shader resource of the "game's" in slot 0, to see it put back
		ID3D11Texture2D* dummyTex = nullptr;
		ID3D11ShaderResourceView* dummy = nullptr;
		{
			D3D11_TEXTURE2D_DESC d{};
			d.Width = d.Height = 4, d.MipLevels = d.ArraySize = 1, d.Format = DXGI_FORMAT_R8G8B8A8_UNORM, d.SampleDesc.Count = 1;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			dev->CreateTexture2D(&d, nullptr, &dummyTex);
			dev->CreateShaderResourceView(dummyTex, nullptr, &dummy);
		}
		// line pixels of one frame: in the gun's quarter, and elsewhere
		auto count = [&](const Camera& cam, ID3D11Texture2D* game, ID3D11DepthStencilView* bound, int& inGun, int& elsewhere, bool& restored, double& ms) {
			const float bg[4] = { 0, 0, 0, 1 };
			ctx->ClearRenderTargetView(rtv, bg);
			ctx->OMSetRenderTargets(1, &rtv, bound);
			ctx->PSSetShaderResources(0, 1, &dummy);
			DrawOptions o = opt;
			o.grid = true;
			o.spots = false;
			o.gridOpacity = 1.f;
			o.blockers = nullptr;
			o.sceneDepth = game;
			o.sceneNear = gamedepth::kNearPlane;
			o.sceneFar = gamedepth::kFarPlane;
			const auto t = clock::now();
			if (!gpu.draw(ctx, rtv, (float)W, (float)H, cam, o)) return false;
			ctx->Flush();
			ms += std::chrono::duration<double, std::milli>(clock::now() - t).count();
			ID3D11RenderTargetView* now = nullptr;
			ID3D11DepthStencilView* nowDepth = nullptr;
			ID3D11ShaderResourceView* nowView = nullptr;
			ctx->OMGetRenderTargets(1, &now, &nowDepth);
			ctx->PSGetShaderResources(0, 1, &nowView);
			restored = restored && now == rtv && nowDepth == bound && nowView == dummy;
			for (IUnknown* x : { (IUnknown*)now, (IUnknown*)nowDepth, (IUnknown*)nowView }) if (x) x->Release();
			ctx->CopyResource(staging, target);
			D3D11_MAPPED_SUBRESOURCE m;
			ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
			if (const char* dump = std::getenv("WORLDLINES_DUMP"))      // a folder: every frame as a picture
			{
				static int n = 0;
				char path[1024];
				std::snprintf(path, sizeof path, "%s\\gunmask_%03d_%s.bmp", dump, n++ / 2, game ? "with" : "without");
				writeBmp(path, (const uint8_t*)m.pData, m.RowPitch, W, H);
			}
			inGun = elsewhere = 0;
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++)
				{
					const uint8_t* px = (const uint8_t*)m.pData + (size_t)y * m.RowPitch + (size_t)x * 4;
					if (px[0] <= 40 && px[1] <= 40 && px[2] <= 40) continue;
					if (x > W / 2 + 4 && y > H / 2 + 4) inGun++;
					else if (x < W / 2 - 4 || y < H / 2 - 4) elsewhere++;
				}
			ctx->Unmap(staging, 0);
			return true;
		};
		bool ok = !views.empty();
		for (const Layout& L : layouts)
		{
			ID3D11Texture2D* tex = nullptr;
			ID3D11DepthStencilView* dsv = nullptr;
			D3D11_TEXTURE2D_DESC d{};
			d.Width = L.w, d.Height = L.h, d.MipLevels = d.ArraySize = 1, d.Format = L.tex, d.SampleDesc.Count = 1;
			d.BindFlags = D3D11_BIND_DEPTH_STENCIL | (L.tex != L.view ? D3D11_BIND_SHADER_RESOURCE : 0);
			D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
			vd.Format = L.view;
			vd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex)) || FAILED(dev->CreateDepthStencilView(tex, &vd, &dsv)))
			{
				std::printf("FAIL: %s: the stand-in depth could not be made\n", L.name);
				return 1;
			}
			// the game's depth view stays bound while the grid draws when it is the frame's size (the overlay may
			// find it so); another size cannot be bound with the frame's target
			ID3D11DepthStencilView* bound = L.w == (UINT)W && L.h == (UINT)H ? dsv : nullptr;
			int gunWithout = 0, gunWith = 0, restWithout = 0, restWith = 0, gunViews = 0;
			bool restored = true, used = true;
			double msWithout = 0, msWith = 0;
			for (const Pose& pose : views)
			{
				const Camera cam = makeCamera(pose.eye, pose.yaw, pose.pitch, vfov, (float)W / H);
				gamedepth::render(ctx, dsv, L.w, L.h, L.reversed, cam, vb, ib, (UINT)scene->triIndex.size());
				int g0 = 0, r0 = 0, g1 = 0, r1 = 0;
				if (!count(cam, nullptr, bound, g0, r0, restored, msWithout) || !count(cam, tex, bound, g1, r1, restored, msWith))
				{
					std::printf("FAIL: %s: draw: %s\n", L.name, gpu.lastError());
					return 1;
				}
				used = used && gpu.sceneUsed();
				if (g0 > 0) gunViews++;
				gunWithout += g0, gunWith += g1, restWithout += r0, restWith += r1;
			}
			const bool pass = used && restored && gunViews >= 3 && gunWith == 0 && restWith >= restWithout * 0.97;
			std::printf("  over the game's depth, %s: %zu views (%d with lines behind the gun): gun quarter %d -> %d line pixels, elsewhere %d -> %d (%.1f %%); "
				"%.1f -> %.1f ms a frame (WARP); state put back %s%s%s\n", L.name, views.size(), gunViews, gunWithout, gunWith, restWithout, restWith,
				restWithout ? 100.0 * restWith / restWithout : 0.0, msWithout / views.size(), msWith / views.size(), restored ? "yes" : "NO",
				used ? "" : "; the depth was not used: ", used ? "" : gpu.sceneProblem());
			ok = ok && pass;
			dsv->Release();
			tex->Release();
		}
		// without a known near plane, or with a depth the grid cannot read, the lines are drawn as before
		{
			const Camera cam = makeCamera(views.empty() ? V3{} : views[0].eye, 20.f, -15.f, vfov, (float)W / H);
			DrawOptions o = opt;
			o.sceneDepth = target;      // a colour texture: not a depth
			o.sceneNear = 0.01f;
			const bool drew = gpu.draw(ctx, rtv, (float)W, (float)H, cam, o);
			const bool refused = drew && !gpu.sceneUsed();
			std::printf("  a colour texture given as the game's depth: drawn %s, not used %s (%s)\n", drew ? "yes" : "NO", refused ? "yes" : "NO", gpu.sceneProblem());
			ok = ok && refused;
		}
		ID3D11ShaderResourceView* none = nullptr;
		ctx->PSSetShaderResources(0, 1, &none);
		dummy->Release();
		dummyTex->Release();
		if (!ok)
		{
			std::printf("FAIL: the gun mask over the game's depth\n");
			return 1;
		}
	}
	std::printf("PASS\n");
	return 0;
}
