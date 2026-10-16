#include "pch.h"
#include "Halo2Physics.h"
#include "SettingsStateAndEvents.h"
#include "IMessagesGUI.h"
#include "DirPathContainer.h"
#include "RuntimeExceptionHandler.h"
#include "IMCCStateHook.h"
#include "IMakeOrGetCheat.h"
#include "GetPlayerViewAngle.h"
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
		bool fly;            // h3sb_fly.txt; works with physics off too (the mod is then loaded for it alone, mode 0)
		float flySpeed;
	};

	// The one mod in the process, whichever game's cheat instance asks.
	class H3sbHost
	{
		std::mutex mMutex;
		std::wstring mDir;                          // <PCM folder>\h3sb\ (with the trailing backslash)
		std::atomic<bool> mUnloadAsked = false;     // mode 9 written: the dll takes itself out within a quarter second
		PROCESS_INFORMATION mLive{}, mWorld{};      // helper windows started from here
		std::string mLastError;
		bool mSaidPhysics = false, mSaidFly = false; // what the message log was last told (both games' instances call apply)
		bool mSpotInUse = false;                    // Save spot was pressed: the mod stays loaded for it (observe mode when nothing else is on)

		void writeSwitch(const wchar_t* name, int value)
		{
			std::ofstream f(std::filesystem::path(mDir + name), std::ios::trunc);
			f << value;
		}

		void writeFly(bool on, float speed)
		{
			std::ofstream f(std::filesystem::path(mDir + L"h3sb_fly.txt"), std::ios::trunc);
			f.imbue(std::locale::classic());
			f << (on ? 1 : 0) << ' ' << std::clamp(speed, 0.5f, 100.f);
		}

		// a line for the message log when physics or fly changed since the last call ("" = nothing new)
		std::string news(const H3sbOptions& o)
		{
			std::string line;
			if (o.physics != mSaidPhysics) line = o.physics ? "Halo 2 physics on" : "Halo 2 physics off: the game's own physics again";
			if (o.fly != mSaidFly) line += std::string(line.empty() ? "" : ". ") + (o.fly ? "Fly on: stick where you look, jump up, crouch down" : "Fly off");
			mSaidPhysics = o.physics;
			mSaidFly = o.fly;
			return line;
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

		// a file of ours that was unzipped but is gone: antivirus programs quarantine the helper (and could the dll)
		static constexpr const char* kAntivirusFix = "Restore it in Windows Security > Virus & threat protection > Protection history "
			"(or your antivirus's quarantine) and allow it, or unzip Poorboys Checkpoint Manager again.";

		static std::string missingFile(const std::wstring& path)
		{
			return std::format("Halo 2 physics: {} is missing. Your antivirus probably removed it (a false alarm). {}",
				std::filesystem::path(path).string(), kAntivirusFix);
		}

		void startHelper(PROCESS_INFORMATION& pi, const std::wstring& args)
		{
			if (running(pi)) return;            // already open (both games' cheat instances get the same button press)
			closeHandles(pi);
			const std::wstring exe = mDir + L"helper\\h3sb_helper.exe";
			if (!std::filesystem::exists(exe)) throw HCMRuntimeException(fail(missingFile(exe)));
			std::wstring cmd = L"\"" + exe + L"\" " + args;
			STARTUPINFOW si{};
			si.cb = sizeof(si);
			if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, mDir.c_str(), &si, &pi))
			{
				const DWORD error = GetLastError();
				pi = PROCESS_INFORMATION{};
				if (error == ERROR_VIRUS_INFECTED || error == ERROR_VIRUS_DELETED)
					throw HCMRuntimeException(fail(std::format("Halo 2 physics: your antivirus blocked {} (a false alarm). {}", std::filesystem::path(exe).string(), kAntivirusFix)));
				throw HCMRuntimeException(fail(std::format("Halo 2 physics: could not start the helper (error {})", error)));
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
			if (mDir.empty()) mDir = (std::filesystem::path(pcmDir) / L"h3sb").wstring() + L"\\";     // pcmDir may end in a slash
		}

		// returns a line for the message log ("" = nothing worth saying)
		std::string apply(const H3sbOptions& o)
		{
			std::scoped_lock lock(mMutex);
			if (mDir.empty()) return "";
			if (!o.physics && !o.fly && !mSpotInUse)
			{
				if (loaded() && !mUnloadAsked)
				{
					writeFly(false, o.flySpeed);
					writeSwitch(L"h3sb_mode.txt", 9);
					mUnloadAsked = true;
				}
				return news(o);
			}
			const std::wstring dll = mDir + L"h3sb.dll";
			if (!std::filesystem::exists(dll)) throw HCMRuntimeException(fail(missingFile(dll)));
			if (loaded() && mUnloadAsked)
			{
				// still on its way out from a moment ago: let it finish, then load it again
				for (int i = 0; i < 40 && loaded(); i++) Sleep(100);
				if (loaded()) throw HCMRuntimeException(fail("Halo 2 physics: the mod did not unload; switch it on again in a moment"));
			}
			mUnloadAsked = false;
			// fly or the spot alone (physics off): the mod in observe mode with the game's own barriers, tick rate, melee and falls
			writeSwitch(L"h3sb_tick.txt", o.physics && o.tick30 ? 30 : 60);
			writeSwitch(L"h3sb_barriers.txt", o.physics ? std::clamp(o.barriers, 0, 3) : 0);
			writeSwitch(L"h3sb_combos.txt", o.physics && o.combos ? 1 : 0);
			writeSwitch(L"h3sb_falls.txt", o.physics && o.noFallDeaths ? 1 : 0);
			writeFly(o.fly, o.flySpeed);
			writeSwitch(L"h3sb_mode.txt", o.physics ? 3 : 0);
			if (loaded()) return news(o);      // it reads the switches four times a second
			// a new session: the mod records every tick (about 120 MB per half hour), clear the last session's first
			std::error_code ec;
			for (auto& e : std::filesystem::directory_iterator(mDir, ec))
			{
				const std::wstring name = e.path().filename().wstring();
				if (name.starts_with(L"h3sb_ticks_") && e.path().extension() == L".bin") std::filesystem::remove(e.path(), ec);
			}
			if (!LoadLibraryW(dll.c_str())) throw HCMRuntimeException(fail(std::format("Halo 2 physics: could not load h3sb.dll (error {})", GetLastError())));
			mLastError.clear();
			return news(o);
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

		// from the first Save spot on the mod stays loaded for the spot
		void useSpot()
		{
			std::scoped_lock lock(mMutex);
			mSpotInUse = true;
		}

		// Save spot (1) / Back to spot (2): 1 queued for the next tick the player is on foot, 0 the mod refused it,
		// -1 the mod is not loaded
		int spot(int what)
		{
			if (mUnloadAsked) return -1;
			HMODULE pinned = nullptr;
			if (!GetModuleHandleExW(0, L"h3sb.dll", &pinned) || !pinned) return -1;
			int ok = 0;
			if (auto fn = (int (*)(int))GetProcAddress(pinned, "h3sb_spot")) ok = fn(what);
			FreeLibrary(pinned);
			return ok == 1 ? 1 : 0;
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

	ScopedCallback<ToggleEvent> mPhysicsCallback, mTick30Callback, mCombosCallback, mNoFallDeathsCallback, mFlyCallback;
	ScopedCallback<eventpp::CallbackList<void(float&)>> mFlySpeedCallback;
	ScopedCallback<eventpp::CallbackList<void(int&)>> mBarriersCallback;
	ScopedCallback<ActionEvent> mArmCallback, mMapWindowCallback, mWorldLinesCallback, mCloseHelpersCallback;
	ScopedCallback<ActionEvent> mSpotSaveCallback, mSpotBackCallback;
	// the view direction goes with the spot: the mod keeps the player's place, speed and armed state, this the camera
	std::weak_ptr<GetPlayerViewAngle> mViewAngleWeak;
	std::optional<SimpleMath::Vector2> mSpotView;

	H3sbOptions options()
	{
		auto s = mSettingsWeak.lock();
		if (!s) throw HCMRuntimeException("Halo 2 physics: settings went away");
		return H3sbOptions{ s->h2pPhysicsToggle->GetValue(), s->h2pBarriers->GetValue(), s->h2pTick30Toggle->GetValue(), s->h2pCombosToggle->GetValue(),
			s->h2pNoFallDeathsToggle->GetValue(), s->h2pFlyToggle->GetValue(), s->h2pFlySpeed->GetValue() };
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

	// Save spot: works with Halo 2 physics off too (the mod is loaded for it in observe mode)
	void onSpotSave()
	{
		auto mcc = mccStateHookWeak.lock();
		if (!mcc || !mcc->isGameCurrentlyPlaying(mGame)) return;
		guarded([this]() {
			host().useSpot();
			if (!host().isLoaded()) say(host().apply(options()));      // throws when it cannot be loaded
			H3sbStatus s{};
			s.size = (int32_t)sizeof(H3sbStatus);
			const bool armed = host().readStatus(&s) == 1 && s.have_body && s.armed;
			if (host().spot(1) != 1)
			{
				say("Spot not saved: the Halo 2 physics mod is not running");
				return;
			}
			mSpotView.reset();
			if (auto view = mViewAngleWeak.lock())
			{
				try
				{
					mSpotView = view->getPlayerViewAngle();
				}
				catch (HCMRuntimeException ex)
				{
					PLOG_ERROR << "Save spot: the view direction could not be read, the spot keeps the place only: " << ex.what();
				}
			}
			say(armed ? "Spot saved (armed)" : "Spot saved");
		});
	}

	void onSpotBack()
	{
		auto mcc = mccStateHookWeak.lock();
		if (!mcc || !mcc->isGameCurrentlyPlaying(mGame)) return;
		guarded([this]() {
			H3sbStatus s{};
			s.size = (int32_t)sizeof(H3sbStatus);
			if (host().readStatus(&s) != 1 || s.size < (int32_t)sizeof(H3sbStatus) || !s.spot)
			{
				say("No spot saved on this map yet: Save spot first");
				return;
			}
			if (host().spot(2) != 1)
			{
				say("Back to spot: the Halo 2 physics mod is not running");
				return;
			}
			if (mSpotView)
				if (auto view = mViewAngleWeak.lock()) view->setPlayerViewAngle(*mSpotView);
			say(s.spot_armed ? "Back to spot (armed)" : "Back to spot");
		});
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
		mFlyCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pFlyToggle->valueChangedEvent, [this](bool&) { onChange(); }),
		mFlySpeedCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pFlySpeed->valueChangedEvent, [this](float&) { onChange(); }),
		mArmCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pArmEvent, [this]() { onArm(); }),
		mMapWindowCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pOpenMapWindowEvent, [this]() {
			guarded([this]() { host().openMapWindow(mSettingsWeak.lock()->h2pCalloutsToggle->GetValue()); });
			}),
		mWorldLinesCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pOpenWorldLinesEvent, [this]() { guarded([]() { host().openWorldLines(); }); }),
		mCloseHelpersCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->h2pCloseHelpersEvent, [this]() { host().closeHelpers(); }),
		mSpotSaveCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->spotSaveEvent, [this]() { onSpotSave(); }),
		mSpotBackCallback(dicon.Resolve<SettingsStateAndEvents>().lock()->spotBackEvent, [this]() { onSpotBack(); })
	{
		host().setDir(dicon.Resolve<DirPathContainer>().lock()->dirPath);
		// HCM's own view-angle reader: without it the spot still works, it just does not turn the camera back
		try
		{
			mViewAngleWeak = resolveDependentCheat(GetPlayerViewAngle);
		}
		catch (HCMInitException ex)
		{
			PLOG_ERROR << "Save spot: no view angle for this game, Back to spot keeps the camera where it is: " << ex.what();
		}
		catch (std::bad_weak_ptr)
		{
			PLOG_ERROR << "Save spot: no view angle for this game (bad_weak_ptr)";
		}
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
