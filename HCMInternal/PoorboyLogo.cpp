#include "pch.h"
#include "PoorboyLogo.h"
#include "PoorboyLogoArt.h"
#include "MCCState.h"
#include "imgui.h"

void PoorboyLogo::release()
{
	if (mView) mView->Release();
	mView = nullptr;
	mDevice = nullptr;
	mWidth = mHeight = 0;
}

void PoorboyLogo::onRender(ID3D11Device* pDevice, SimpleMath::Vector2 screenSize)
{
	std::unique_lock<std::mutex> lock(mDestructionGuard);
	if (mFailed || !pDevice || screenSize.x < 64 || screenSize.y < 64) return;
	const MCCState& state = mccStateHook->getCurrentMCCState();
	if (state.currentPlayState != PlayState::Ingame) return;
	const GameState::Value game = state.currentGameState;
	if (game != GameState::Value::Halo3 && game != GameState::Value::Halo3ODST && game != GameState::Value::HaloReach) return;

	const poorboy_logo::Rect at = poorboy_logo::place(screenSize.x, screenSize.y);
	if (pDevice != mDevice || at.width != mWidth || at.height != mHeight)
	{
		release();
		mView = poorboy_logo::makeView(pDevice, at.width, at.height);
		if (!mView)
		{
			mFailed = true;
			PLOG_ERROR << "Poorboy logo: could not make its texture (" << at.width << " x " << at.height << ")";
			return;
		}
		mDevice = pDevice;
		mWidth = at.width;
		mHeight = at.height;
		PLOG_INFO << "Poorboy logo: " << mWidth << " x " << mHeight << " for a " << screenSize.x << " x " << screenSize.y << " screen";
	}
	ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(uintptr_t)mView, ImVec2(at.x, at.y), ImVec2(at.x + mWidth, at.y + mHeight),
		ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, poorboy_logo::kOpacity));
}
