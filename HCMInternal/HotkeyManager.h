#pragma once
#include "HotkeyDefinitions.h"
#include "IMessagesGUI.h"
#include "SharedRequestProvider.h"

class HotkeyManager
{
private:
	// callbacks
	ScopedCallback<RenderEvent> mImGuiRenderCallbackHandle;

	// services
	std::shared_ptr<HotkeyDefinitions> mHotkeyDefinitions;
	std::shared_ptr<TokenSharedRequestProvider> mHotkeyDisabler;


	// data 
	std::string mHotkeyConfigPath;
	std::vector<std::thread> mFireEventThreads;

	void pollInput();

	// "Hold the Fly key to fly": whether the Fly hotkey was down last frame, and the newest wish for the fly setting
	// (set off the render thread; the newest wish wins)
	struct FlyHold
	{
		std::mutex mutex;
		std::atomic<bool> want = false;
	};
	bool mFlyHeld = false;
	std::shared_ptr<FlyHold> mFlyHold = std::make_shared<FlyHold>();
	void pollFlyHold(bool disabled);


public:
	// Hotkey manager doesn't render, but it does want to poll for inputs every frame
	HotkeyManager(std::shared_ptr<RenderEvent> pRenderEvent, std::shared_ptr<HotkeyDefinitions> pHotkeyDefinitions, std::shared_ptr<IMessagesGUI> messagesGUI, std::string dirPath, std::shared_ptr<TokenSharedRequestProvider> hotkeyDisabler);
	~HotkeyManager();

	void saveHotkeysToFile();

};

