#pragma once
#include "pch.h"
#include "IMCCStateHook.h"
#include "DirectXRenderEvent.h"

// Poorboys Checkpoint Manager: Poorboy's logo, drawn solid in Halo 3 and ODST games left of the grenade counts, so a clip
// shows the game was played with Poorboy's mods. It is not a setting (Halo Checkpoint Manager's own watermark can be
// hidden; this cannot). The overlay draws it, so it is in what MCC presents: recordings see it, unless Halo Checkpoint
// Manager's OBS bypass keeps the whole overlay out of OBS.
class PoorboyLogo
{
	std::shared_ptr<IMCCStateHook> mccStateHook;
	ScopedCallback<DirectXRenderEvent> mRenderCallback;
	std::mutex mDestructionGuard;

	// the texture, made at the size it is drawn (PoorboyLogoArt.h), and the device it belongs to
	ID3D11Device* mDevice = nullptr;
	ID3D11ShaderResourceView* mView = nullptr;
	int mWidth = 0, mHeight = 0;
	bool mFailed = false;

	void onRender(ID3D11Device* pDevice, SimpleMath::Vector2 screenSize);
	void release();

public:
	PoorboyLogo(std::shared_ptr<IMCCStateHook> mccState, std::shared_ptr<DirectXRenderEvent> renderEvent)
		: mccStateHook(mccState),
		mRenderCallback(renderEvent, [this](ID3D11Device* a, ID3D11DeviceContext*, SimpleMath::Vector2 c, ID3D11RenderTargetView*) { onRender(a, c); })
	{}

	~PoorboyLogo()
	{
		mRenderCallback.removeCallback();
		std::unique_lock<std::mutex> lock(mDestructionGuard); // block until a render finishes
		release();
	}

	PoorboyLogo(const PoorboyLogo&) = delete;
	PoorboyLogo& operator=(const PoorboyLogo&) = delete;
};
