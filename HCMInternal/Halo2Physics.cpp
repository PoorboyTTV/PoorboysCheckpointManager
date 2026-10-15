#include "pch.h"
#include "Halo2Physics.h"
#include "SettingsStateAndEvents.h"
#include "IMessagesGUI.h"
#include "DirPathContainer.h"
#include "RuntimeExceptionHandler.h"
#include "IMCCStateHook.h"
#include <filesystem>
#include <fstream>

namespace
{
	struct H3sbOptions
	{
		bool physics;        // the master switch: off = the mod is not loaded, the game is untouched
		int barriers;        // 0-3, h3sb_barriers.txt
		bool tick30;
		bool combos;
		bool noFallDeaths;
	};

	// The one mod in the process, whichever game's cheat instance asks.
	class H3sbHost
	{
		std::mutex mMutex;
		std::wstring mDir;                          // <PCM folder>\h3sb\ (with the trailing backslash)
		std::atomic<bool> mUnloadAsked = false;     // mode 9 written: the dll takes itself out within a quarter second
		PROCESS_INFORMATION mLive{}, mWorld{};      // helper windows started from here
		std::string mLastError;

		void writeSwitch(const wchar_t* name, int value)
		{
			std::ofstream f(std::filesystem::path(mDir + name), std::ios::trunc);
			f << value;
		}

		static bool loaded() { return GetModuleHandleW(L"h3sb.dll") != nullptr; }

		static bool running(PROCESS_INFORMATION& pi) { return pi.hProcess && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT; }

		static void closeHandles(PROCESS_INFORMATION& pi)
		{
			if (pi.hProcess) CloseHandle(pi.hProcess);
			if (pi.hThread) CloseHandle(pi.hThread);
			pi = PROCESS_INFORMATION{};
		}

		std::string fail(std::string message)
		{
			mLastError = message;
			return message;
		}

		void startHelper(PROCESS_INFORMATION& pi, const std::wstring& args)
		{
			if (running(pi)) return;            // already open (both games' cheat instances get the same button press)
			closeHandles(pi);
			const std::wstring exe = mDir + L"helper\\h3sb_helper.exe";
			if (!std::filesystem::exists(exe)) throw HCMRuntimeException(fail(std::format("Halo 2 physics: the helper is missing at {}", std::filesystem::path(exe).string())));
			std::wstring cmd = L"\"" + exe + L"\" " + args;
			STARTUPINFOW si{};
			si.cb = sizeof(si);
			if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, mDir.c_str(), &si, &pi))
			{
				pi = PROCESS_INFORMATION{};
				throw HCMRuntimeException(fail(std::format("Halo 2 physics: could not start the helper (error {})", GetLastError())));
			}
		}

		// the helpers close themselves when h3sb_helpers.stop is newer than they are (the Stop script's way: they may
		// run as administrator, a closer that is not one cannot end them)
		void stopHelpersLocked()
		{
			if (!mDir.empty())
			{
				std::ofstream f(std::filesystem::path(mDir + L"h3sb_helpers.stop"), std::ios::trunc);
				f << "stop asked by Poorboys Checkpoint Manager";
			}
			closeHandles(mLive);
			closeHandles(mWorld);
		}

	public:
		void setDir(const std::string& pcmDir)
		{
			std::scoped_lock lock(mMutex);
			if (mDir.empty()) mDir = std::filesystem::path(pcmDir).wstring() + L"\\h3sb\\";
		}

		// returns a line for the message log ("" = nothing worth saying)
		std::string apply(const H3sbOptions& o)
		{
			std::scoped_lock lock(mMutex);
			if (mDir.empty()) return "";
			if (!o.physics)
			{
				if (loaded() && !mUnloadAsked)
				{
					writeSwitch(L"h3sb_mode.txt", 9);
					mUnloadAsked = true;
					return "Halo 2 physics off: the game's own physics again";
				}
				return "";
			}
			const std::wstring dll = mDir + L"h3sb.dll";
			if (!std::filesystem::exists(dll)) throw HCMRuntimeException(fail(std::format("Halo 2 physics: h3sb.dll is missing at {}", std::filesystem::path(dll).string())));
			if (loaded() && mUnloadAsked)
			{
				// still on its way out from a moment ago: let it finish, then load it again
				for (int i = 0; i < 40 && loaded(); i++) Sleep(100);
				if (loaded()) throw HCMRuntimeException(fail("Halo 2 physics: the mod did not unload; switch it on again in a moment"));
			}
			mUnloadAsked = false;
			writeSwitch(L"h3sb_tick.txt", o.tick30 ? 30 : 60);
			writeSwitch(L"h3sb_barriers.txt", std::clamp(o.barriers, 0, 3));
			writeSwitch(L"h3sb_combos.txt", o.combos ? 1 : 0);
			writeSwitch(L"h3sb_falls.txt", o.noFallDeaths ? 1 : 0);
			writeSwitch(L"h3sb_mode.txt", 3);
			if (loaded()) return "";           // it reads the switches four times a second
			// a new session: the mod records every tick (about 120 MB per half hour), clear the last session's first
			std::error_code ec;
			for (auto& e : std::filesystem::directory_iterator(mDir, ec))
			{
				const std::wstring name = e.path().filename().wstring();
				if (name.starts_with(L"h3sb_ticks_") && e.path().extension() == L".bin") std::filesystem::remove(e.path(), ec);
			}
			if (!LoadLibraryW(dll.c_str())) throw HCMRuntimeException(fail(std::format("Halo 2 physics: could not load h3sb.dll (error {})", GetLastError())));
			mLastError.clear();
			return "Halo 2 physics on";
		}

		void openMapWindow(bool callouts)
		{
			std::scoped_lock lock(mMutex);
			if (mDir.empty()) return;
			startHelper(mLive, L"live \"" + mDir + L".\" --with-game --data \"" + mDir + L"spots\"" + (callouts ? L"" : L" --no-sound"));
		}

		void openWorldLines()
		{
			std::scoped_lock lock(mMutex);
			if (mDir.empty()) return;
			startHelper(mWorld, L"world --folder \"" + mDir + L".\" --data \"" + mDir + L"spots\"");
		}

		void closeHelpers()
		{
			std::scoped_lock lock(mMutex);
			stopHelpersLocked();
		}

		// PCM is closing: the mod and its windows go too
		void shutdown()
		{
			std::scoped_lock lock(mMutex);
			if (mDir.empty()) return;
			if (loaded() && !mUnloadAsked)
			{
				writeSwitch(L"h3sb_mode.txt", 9);
				mUnloadAsked = true;
			}
			stopHelpersLocked();
		}

		int readStatus(H3sbStatus* out)
		{
			if (mUnloadAsked) return 0;
			// holds the dll while it is called, so it cannot go away under the call (it unloads itself from its own thread)
			HMODULE pinned = nullptr;
			if (!GetModuleHandleExW(0, L"h3sb.dll", &pinned) || !pinned) return 0;
			int ok = 0;
			if (auto fn = (int (*)(H3sbStatus*))GetProcAddress(pinned, "h3sb_status")) ok = fn(out);
			FreeLibrary(pinned);
			return ok;
		}

		bool isLoaded() { return loaded() && !mUnloadAsked; }

		// 1 armed on the next tick, 0 no Halo 2 body to arm, -1 the mod is not loaded
		int arm()
		{
			if (mUnloadAsked) return -1;
			HMODULE pinned = nullptr;
			if (!GetModuleHandleExW(0, L"h3sb.dll", &pinned) || !pinned) return -1;
			int ok = 0;
			if (auto fn = (int (*)())GetProcAddress(pinned, "h3sb_arm")) ok = fn();
			FreeLibrary(pinned);
			return ok;
		}

		std::string lastError()
		{
			std::scoped_lock lock(mMutex);
			return mLastError;
		}
	};

	H3sbHost& host()
	{
		static H3sbHost h;
		return h;
	}
}

class Halo2Physics::Halo2PhysicsImpl
{
private:
	GameState mGame;
	std::weak_ptr<SettingsStateAndEvents> mSettingsWeak;
	std::weak_ptr<IMessagesGUI> messagesGUIWeak;
	std::shared_ptr<RuntimeExceptionHandler> runtimeExceptions;
	std::weak_ptr<IMCCStateHook> mccStateHookWeak;

	ScopedCallback<ToggleEvent> mPhysicsCallback, mTick30Callback, mCombosCallback, mNoFallDeathsCallback;
	ScopedCallback<eventpp::CallbackList<void(int&)>> mBarriersCallback;
	ScopedCallback<ActionEvent> mArmCallback, mMapWindowCallback, mWorldLinesCallback, mCloseHelpersCallback;

	H3sbOptions options()
	{
		auto s = mSettingsWeak.lock();
		if (!s) throw HCMRuntimeException("Halo 2 physics: settings went away");
		return H3sbOptions{ s->h2pPhysicsToggle->GetValue(), s->h2pBarriers->GetValue(), s->h2pTick30Toggle->GetValue(), s->h2pCombosToggle->GetValue(),
			s->h2pNoFallDeathsToggle->GetValue() };
	}

	void say(const std::string& line)
	{
		if (line.empty()) return;
		if (auto messages = messagesGUIWeak.lock()) messages->addMessage(line);
		PLOG_INFO << line;
	}

	template <typename F>
	void guarded(F f)
	{
		try
		{
			f();
		}
		catch (HCMRuntimeException ex)
		{
			runtimeExceptions->handleMessage(ex);
		}
	}

	void onChange() { guarded([this]() { say(host().apply(options())); }); }

	// both games' instances get the press: only the one whose game is running answers it
	void onArm()
	{
		auto mcc = mccStateHookWeak.lock();
		if (!mcc || !mcc->isGameCurrentlyPlaying(mGame)) return;
		const int r = host().arm();
		say(r == 1 ? "Armed" : r == 0 ? "Not armed: the Halo 2 body is not driving the player right now (in a vehicle, dead or loading?)"
			: "Not armed: turn Halo 2 physics on first");
	}

public:
	Halo2PhysicsImpl(GameState game, IDIContainer& dicon) :
		mGame(game),
		mSettingsWeak(dicon.Resolve<SettingsStateAndEvents>()),
		messagesGUIWeak(dicon.Resolve<IMessagesGUI>()),
		runtimeExceptions(dicon.Resolve<RuntimeExceptionHandler>()),
		mccStateHookWeak(dicon.Resolve<IMCCStateHook>()),
		mPhysicsCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pPhysicsToggle->valueChangedEvent, [this](bool&) { onChange(); }),
		mTick30Callback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pTick30Toggle->valueChangedEvent, [this](bool&) { onChange(); }),
		mCombosCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pCombosToggle->valueChangedEvent, [this](bool&) { onChange(); }),
		mNoFallDeathsCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pNoFallDeathsToggle->valueChangedEvent, [this](bool&) { onChange(); }),
		mBarriersCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pBarriers->valueChangedEvent, [this](int&) { onChange(); }),
		mArmCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pArmEvent, [this]() { onArm(); }),
		mMapWindowCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pOpenMapWindowEvent, [this]() {
			guarded([this]() { host().openMapWindow(mSettingsWeak.lock()->h2pCalloutsToggle->GetValue()); });
			}),
		mWorldLinesCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pOpenWorldLinesEvent, [this]() { guarded([]() { host().openWorldLines(); }); }),
		mCloseHelpersCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pCloseHelpersEvent, [this]() { host().closeHelpers(); })
	{
		host().setDir(dicon.Resolve<DirPathContainer>().lock()->dirPath);
		// switched on last time PCM ran: on again (a missing dll is reported, it does not stop PCM)
		try
		{
			say(host().apply(options()));
		}
		catch (HCMRuntimeException ex)
		{
			PLOG_ERROR << ex.what();
		}
	}

	~Halo2PhysicsImpl()
	{
		host().shutdown();
	}
};

Halo2Physics::Halo2Physics(GameState gameImpl, IDIContainer& dicon)
	: pimpl(std::make_unique<Halo2PhysicsImpl>(gameImpl, dicon))
{
}

Halo2Physics::~Halo2Physics()
{
	PLOG_VERBOSE << "~" << getName();
}

int Halo2Physics::readStatus(H3sbStatus* out) { return host().readStatus(out); }

bool Halo2Physics::isLoaded() { return host().isLoaded(); }

std::string Halo2Physics::lastError() { return host().lastError(); }
