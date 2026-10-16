#pragma once
// Poorboy's logo for the overlay (PoorboyLogo.cpp): where it goes on the screen and its texture at that size. Header
// only, with nothing from the rest of the overlay, so a test can draw it the same way.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <d3d11.h>
#include "PoorboyLogoData.h"

namespace poorboy_logo
{
	// where Halo 3's HUD leaves room left of the grenade counts, on a 1920 x 1080 screen: the logo 62 high, its top-left
	// corner at (22, 54), just under the collapsed PCM bar and level with the grenade panel
	constexpr float kLeft = 22.f, kTop = 54.f, kHeight = 62.f;

	struct Rect { float x, y; int width, height; };

	// Halo 3's HUD scales with a 16:9 screen; on a wider one it moves out by part of the extra width (the same rule the
	// PCM window uses to sit over the HUD)
	inline Rect place(float screenWidth, float screenHeight)
	{
		const float scale = std::min(screenHeight / 1080.f, screenWidth / 1920.f);
		constexpr float ratio16to9 = 16.f / 9.f;
		const float extra = screenWidth / screenHeight > ratio16to9 ? (screenWidth - screenHeight * ratio16to9) * 0.5f * 0.6f : 0.f;
		const int height = std::max(8, (int)std::lround(kHeight * scale));
		const int width = std::max(8, (int)std::lround(height * (double)kPoorboyLogoWidth / kPoorboyLogoHeight));
		return { std::round(kLeft * scale + extra), std::round(kTop * scale), width, height };
	}

	// shrink one axis by area: each output covers [i * n / m, (i + 1) * n / m) of the input
	struct Tap { int index; float weight; };
	inline std::vector<std::vector<Tap>> areaTaps(int n, int m)
	{
		std::vector<std::vector<Tap>> taps(m);
		const double step = (double)n / m;
		for (int i = 0; i < m; i++)
		{
			const double lo = i * step, hi = (i + 1) * step;
			for (int j = (int)lo; j < n && j < hi; j++)
			{
				const double cover = std::min(hi, (double)j + 1) - std::max(lo, (double)j);
				if (cover > 0) taps[i].push_back({ j, (float)(cover / step) });
			}
		}
		return taps;
	}

	// the logo at width x height, white with straight alpha (RGBA in memory): the embedded logo shrunk by area, since
	// ImGui's sampler has no mips and drawing it 1:1 keeps it sharp
	inline std::vector<uint32_t> pixels(int width, int height)
	{
		const auto across = areaTaps(kPoorboyLogoWidth, width), down = areaTaps(kPoorboyLogoHeight, height);
		std::vector<float> rows((size_t)kPoorboyLogoHeight * width);
		for (int y = 0; y < kPoorboyLogoHeight; y++)
			for (int x = 0; x < width; x++)
			{
				float a = 0;
				for (const Tap& t : across[x]) a += t.weight * kPoorboyLogoAlpha[(size_t)y * kPoorboyLogoWidth + t.index];
				rows[(size_t)y * width + x] = a;
			}
		std::vector<uint32_t> out((size_t)width * height);
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
			{
				float a = 0;
				for (const Tap& t : down[y]) a += t.weight * rows[(size_t)t.index * width + x];
				out[(size_t)y * width + x] = 0x00FFFFFFu | ((uint32_t)std::clamp((int)std::lround(a), 0, 255) << 24);
			}
		return out;
	}

	// the texture ImGui draws (nullptr if the device refuses it)
	inline ID3D11ShaderResourceView* makeView(ID3D11Device* device, int width, int height)
	{
		const std::vector<uint32_t> px = pixels(width, height);
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = (UINT)width;
		desc.Height = (UINT)height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data{ px.data(), (UINT)(width * 4), 0 };
		ID3D11Texture2D* texture = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, &data, &texture)) || !texture) return nullptr;
		ID3D11ShaderResourceView* view = nullptr;
		const HRESULT hr = device->CreateShaderResourceView(texture, nullptr, &view);
		texture->Release();
		return SUCCEEDED(hr) ? view : nullptr;
	}
}
