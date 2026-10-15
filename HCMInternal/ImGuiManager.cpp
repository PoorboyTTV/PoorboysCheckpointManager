#include "pch.h"
#include "ImGuiManager.h"
#include "PCMTheme.h"
#include "GlobalKill.h"
#include "ProggyVectorRegularFont.h"
#include "imgui_internal.h"





ImGuiManager* ImGuiManager::instance = nullptr;

WNDPROC ImGuiManager::mOldWndProc = nullptr;
IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall ImGuiManager::mNewWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//https://www.unknowncheats.me/forum/2488829-post5.html
	ImGuiIO& io = ImGui::GetIO();
	LRESULT res = ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);


	switch (uMsg)
	{
	//case WM_CLOSE:
	case WM_DESTROY:
	case WM_NCDESTROY:
		GlobalKill::killMe();
		break;
	default:
		break;
	}

	if (io.WantCaptureMouse == false)
	{
		// ImGui didn't handle the click so let MCC do it
		return CallWindowProc(ImGuiManager::mOldWndProc, hWnd, uMsg, wParam, lParam);
	}
	else
	{
		switch (uMsg)
		{
			// Certain messages we want to let MCC know about too
		case WM_ACTIVATE:
		case WM_ACTIVATEAPP:
		case WM_NCACTIVATE:
			return CallWindowProc(ImGuiManager::mOldWndProc, hWnd, uMsg, wParam, lParam);
			break;
		default:
			return true; // otherwise we just tell MCC to not worry about it
		}
	}





}

void ImGuiManager::initializeImGuiResources(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext, IDXGISwapChain* pSwapChain, ID3D11RenderTargetView* pMainRenderTargetView)
{
	PLOG_VERBOSE << "initializeImGuiResources";
	// Use swap chain description to get MCC window handle
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	pSwapChain->GetDesc(&swapChainDesc);
	if (!&swapChainDesc) throw HCMInitException("Failed to get swap chain description");
	m_windowHandle = swapChainDesc.OutputWindow;

	// Setup the imgui WndProc callback
	mOldWndProc = (WNDPROC)SetWindowLongPtrW(m_windowHandle, GWLP_WNDPROC, (LONG_PTR)&mNewWndProc);


	// Setup ImGui stuff
	PLOG_DEBUG << "Initializing ImGui";
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;// | ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

	if (!ImGui_ImplWin32_Init(m_windowHandle))
	{
		throw HCMInitException(std::format("ImGui_ImplWin32_Init failed w/ {} ", (uint64_t)m_windowHandle).c_str());
	};
	if (!ImGui_ImplDX11_Init(pDevice, pDeviceContext))
	{
		throw HCMInitException(std::format("ImGui_ImplDX11_Init failed w/ {}, {} ", (uint64_t)pDevice, (uint64_t)pDeviceContext).c_str());
	};

	PLOG_DEBUG << "ImGui Initialized";

	// Setup Dear ImGui style: Poorboys Checkpoint Manager's themes (PCMTheme.cpp; HCM's own maroon is one of them)
	PCMTheme::applyPending();

	// setup font
	io.Fonts->AddFontDefault();
	mRescalableMonospacedFont = io.Fonts->AddFontFromMemoryCompressedTTF(ProggyVectorRegularFont_compressed_data, ProggyVectorRegularFont_compressed_size, 15.f * 2);

	//int my_image_width = 0;
	//int my_image_height = 0;

	//bool ret = LoadTextureFromMemory(103, pDevice, &my_texture, &my_image_width, &my_image_height);
	//// todo; overlay bypass stuff based on return value
	//IM_ASSERT(ret);


}


ImGuiManager::~ImGuiManager()
{
	PLOG_DEBUG << "~ImGuiManager()";
	//presentEventCallback.~ScopedCallback();
	presentEventCallback.removeCallback(); // no new callback invokes
	if (mOldWndProc) 		// restore the original wndProc
	{
		SetWindowLongPtrW(m_windowHandle, GWLP_WNDPROC, (LONG_PTR)mOldWndProc);
		mOldWndProc = nullptr;
	}

	std::unique_lock<std::mutex> lock(mDestructionGuard); // block until callbacks finish executing



	// Cleanup
	if (m_isImguiInitialized)
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		m_isImguiInitialized = false;
	}

	instance = nullptr;
}



//void ImGuiManager::lapuaTest(SimpleMath::Vector2 ss)
//{
//	constexpr auto strength = 0x01FFFFFF;
//	constexpr auto zero = ImVec2(0, 0);
//	constexpr auto one = ImVec2(1, 1);
//	ImGui::GetBackgroundDrawList()->AddImage(my_texture, zero, ss, zero, one, strength);
//
//}

void ImGuiManager::onPresentHookEvent(ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext, IDXGISwapChain* pSwapChain, ID3D11RenderTargetView* pMainRenderTargetView)
{
	LOG_ONCE(PLOG_DEBUG << "ImGuiManager::onPresentHookEvent running");
	std::unique_lock<std::mutex> lock(mDestructionGuard);
#pragma region init


	if (!instance->m_isImguiInitialized)
	{
		PLOG_INFO << "Initializing ImGuiManager";
		try
		{
			instance->initializeImGuiResources(pDevice, pDeviceContext, pSwapChain, pMainRenderTargetView);
			instance->m_isImguiInitialized = true;
		}
		catch (HCMInitException& ex)
		{
			PLOG_FATAL << "Failed to initialize ImGui, info: " << std::endl
				<< ex.what() << std::endl
				<< "HCM will now automatically close down";
			GlobalKill::killMe();
			return;
		}
	}
#pragma endregion init

	//TODO: check if this is necessary
	MSG msg;
	while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);

	}


	// Start ImGui frame
	PCMTheme::applyPending();       // a theme picked since the last frame
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	GImGui->NavWindowingTarget = nullptr; // prevent navinputs altogether

	// I don't 100% understand what this does, but it must be done before we try to render
	pDeviceContext->OMSetRenderTargets(1, &pMainRenderTargetView, NULL);
	auto screenSize = getScreenSize();


	// invoke callback of anything that wants to render with ImGui
	BackgroundRenderEvent->operator()(screenSize); // for overlays.
	MidgroundRenderEvent->operator()(screenSize);
	ForegroundRenderEvent->operator()(screenSize);
	ForegroundDirectXRenderEvent->operator()(pDevice, pDeviceContext, screenSize, pMainRenderTargetView);

	// Finish ImGui frame
	ImGui::EndFrame();
	ImGui::Render();

	// Render it !
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}


