#include "pch.h"
#include "BottomlessClip.h"
#include "IMCCStateHook.h"
#include "IMessagesGUI.h"
#include "SettingsStateAndEvents.h"
#include "RuntimeExceptionHandler.h"
#include "MultilevelPointer.h"
#include "PointerDataStore.h"
#include "IMakeOrGetCheat.h"
#include "Halo2Physics.h"

template <GameState::Value gameT>
class BottomlessClipImplSetFlag : public IBottomlessClipImpl
{
private:

	// which game is this implementation for
	GameState mGame;

	// event callbacks
	ScopedCallback<ToggleEvent> mBottomlessClipCallbackHandle;

	// injected services
	std::weak_ptr<IMCCStateHook> mccStateHookWeak;
	std::weak_ptr<IMessagesGUI> messagesGUIWeak;
	std::shared_ptr<RuntimeExceptionHandler> runtimeExceptions;

	//data
	std::shared_ptr<MultilevelPointer> bottomlessClipFlag;

	// primary event callback
	void onBottomlessClipToggle(bool& newValue)
	{


		PLOG_DEBUG << "onBottomlessClipToggle called, newValue: " << newValue;
		try
		{
			lockOrThrow(mccStateHookWeak, mccStateHook);
			lockOrThrow(messagesGUIWeak, messagesGUI);

			if (mccStateHook->isGameCurrentlyPlaying(mGame) == false) return;

			byte newFlagValue = newValue ? 1 : 0;
			if (!bottomlessClipFlag->writeData(&newFlagValue)) throw HCMRuntimeException(std::format("Failed to write bottomlessClipFlag flag {}", MultilevelPointer::GetLastError()));

			messagesGUI->addMessage(newValue ? "Bottomless Clip enabled." : "Bottomless Clip disabled");
		}
		catch (HCMRuntimeException ex)
		{
			runtimeExceptions->handleMessage(ex);
		}

	}


public:
	BottomlessClipImplSetFlag(GameState gameImpl, IDIContainer& dicon)
		: mGame(gameImpl),
		mBottomlessClipCallbackHandle(dicon.Resolve<SettingsStateAndEvents>().lock()->bottomlessClipToggle->valueChangedEvent, [this](bool& n) {onBottomlessClipToggle(n); }),
		mccStateHookWeak(dicon.Resolve<IMCCStateHook>()),
		messagesGUIWeak(dicon.Resolve<IMessagesGUI>()),
		runtimeExceptions(dicon.Resolve<RuntimeExceptionHandler>())
	{

		auto ptr = dicon.Resolve<PointerDataStore>().lock();
		bottomlessClipFlag = ptr->getData<std::shared_ptr<MultilevelPointer>>(nameof(bottomlessClipFlag), mGame);
	}

	// unlike most cheats we want to make sure we set the flag back to zero when HCM closes
	~BottomlessClipImplSetFlag()
	{
		byte newFlagValue = 0;
		if (!bottomlessClipFlag->writeData(&newFlagValue)) PLOG_ERROR << (std::format("Failed to write bottomlessClipFlag flag {}", MultilevelPointer::GetLastError()));
	}
};

// Poorboys Checkpoint Manager: MCC's Halo 3, ODST and Reach have no cheat flag to set (their dev cheats are compiled out), so
// the Halo 2 physics mod does it (h3sb_clip.txt): Halo2Physics follows bottomlessClipToggle and loads the mod for it,
// with physics off too. This only ties the toggle to that service, so the checkbox shows only where it can work.
class BottomlessClipImplH3sb : public IBottomlessClipImpl
{
	std::shared_ptr<Halo2Physics> mHalo2Physics;

public:
	BottomlessClipImplH3sb(GameState game, IDIContainer& dicon)
		: mHalo2Physics(resolveDependentCheat(Halo2Physics))
	{
		if (!mHalo2Physics) throw HCMInitException("Bottomless Clip: no Halo 2 physics service for this game");
	}
};



BottomlessClip::BottomlessClip(GameState gameImpl, IDIContainer& dicon)
{
	switch (gameImpl)
	{
	case GameState::Value::Halo1:
		pimpl = std::make_unique<BottomlessClipImplSetFlag<GameState::Value::Halo1>>(gameImpl, dicon);
		break;

	case GameState::Value::Halo2:
		pimpl = std::make_unique<BottomlessClipImplSetFlag<GameState::Value::Halo2>>(gameImpl, dicon);
		break;

	case GameState::Value::Halo3:
	case GameState::Value::Halo3ODST:
	case GameState::Value::HaloReach:
		pimpl = std::make_unique<BottomlessClipImplH3sb>(gameImpl, dicon);
		break;


	default:
		throw HCMInitException("not impl yet");
	}
}

BottomlessClip::~BottomlessClip()
{
	PLOG_DEBUG << "~" << getName();
}