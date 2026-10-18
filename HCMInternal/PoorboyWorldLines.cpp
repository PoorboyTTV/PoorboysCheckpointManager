#include "pch.h"
#include "PoorboyWorldLines.h"
#include "MCCState.h"
#include "SettingsStateAndEvents.h"
#include "h3sb_api.h"
#include "imgui.h"

using namespace worldlines;

namespace
{
	// where each game keeps the render camera and the loaded map's name (RVAs of the MCC build the mod supports; another
	// build is not drawn). Camera layout: WorldLinesScene.h. Map name: Halo 3 / ODST map header +0xC0; Reach: the last
	// part of the scenario path ("haloreach\maps\50_panopticon"), as the mod names its world file. From re\mccgame.py.
	struct GameInfo
	{
		GameState::Value game;
		const wchar_t* module;
		const char* title;
		uint32_t stamp;                 // PE timestamp of the build
		uintptr_t camera;
		uintptr_t mapHeader;            // 0 = none (Reach)
		uintptr_t levelPath;            // 0 = none
	};
	constexpr GameInfo kGames[] = {
		{ GameState::Value::Halo3, L"halo3.dll", "Halo 3", 0x68A0EDFC, 0x2D2F688, 0x20A9058, 0 },
		{ GameState::Value::Halo3ODST, L"halo3odst.dll", "Halo 3: ODST", 0x68A0F232, 0x2D73598, 0x20EF068, 0 },
		{ GameState::Value::HaloReach, L"haloreach.dll", "Halo: Reach", 0x68A0EFE1, 0xC9FAE0, 0, 0x2A1F517 },
	};

	// the game's dll may be unloaded at any moment (between games): every read of its memory is guarded. Pages that are
	// no longer mapped are skipped without a fault (the Halo 2 physics mod's crash notes log every fault in the process)
	bool mapped(const void* src, size_t n)
	{
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(src, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
		return (const char*)src + n <= (const char*)mbi.BaseAddress + mbi.RegionSize;
	}

	bool safeCopy(void* dst, const void* src, size_t n)
	{
		if (!mapped(src, n)) return false;
		__try
		{
			memcpy(dst, src, n);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// base of the game's dll if it is loaded and the supported build, else 0 (stampOut = the build found, 0 none)
	uintptr_t gameBase(const GameInfo& g, uint32_t* stampOut = nullptr)
	{
		const HMODULE m = GetModuleHandleW(g.module);
		uint32_t pe = 0, stamp = 0;
		if (!m || !safeCopy(&pe, (const char*)m + 0x3C, 4) || pe > 0x1000 || !safeCopy(&stamp, (const char*)m + pe + 8, 4)) return 0;
		if (stampOut) *stampOut = stamp;
		return stamp == g.stamp ? (uintptr_t)m : 0;
	}

	bool readCamera(uintptr_t at, Camera& cam)
	{
		uint8_t raw[kCameraBytes];
		return at && safeCopy(raw, (const void*)at, sizeof(raw)) && cameraFromRecord(raw, cam);
	}

	std::string mapName(const GameInfo& g, uintptr_t base)
	{
		char raw[129] = {};
		std::string name;
		if (g.levelPath)
		{
			if (!safeCopy(raw, (const void*)(base + g.levelPath), 128)) return "";
			std::string path(raw);
			const size_t cut = path.find_last_of("\\/");
			name = cut == std::string::npos ? path : path.substr(cut + 1);
		}
		else
		{
			if (!safeCopy(raw, (const void*)(base + g.mapHeader + 0xC0), 32)) return "";
			raw[32] = 0;
			name = raw;
		}
		for (char& c : name)
		{
			if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return "";       // not a name (memory being changed)
			c = (char)std::tolower((unsigned char)c);
		}
		return name;
	}

	ImU32 rgb(int r, int g, int b) { return IM_COL32(r, g, b, 255); }
}

PoorboyWorldLines::PoorboyWorldLines(std::shared_ptr<IMCCStateHook> mccState, std::shared_ptr<DirectXRenderEvent> renderEvent,
	std::shared_ptr<SettingsStateAndEvents> settings, const std::string& pcmDir)
	: mccStateHook(mccState), mSettings(settings), mDir(std::filesystem::path(pcmDir) / L"h3sb"),
	mRenderCallback(renderEvent, [this](ID3D11Device* d, ID3D11DeviceContext* c, SimpleMath::Vector2 size, ID3D11RenderTargetView* t) { onRender(d, c, size, t); })
{
	mWorker = std::thread([this]() { work(); });
}

PoorboyWorldLines::~PoorboyWorldLines()
{
	mRenderCallback.removeCallback();
	{
		std::unique_lock<std::mutex> lock(mDestructionGuard); // a render in progress finishes first
		mGpu.release();
	}
	mStop = true;
	if (mWorker.joinable()) mWorker.join();
}

void PoorboyWorldLines::publish(Published p)
{
	std::scoped_lock lock(mPublishedMutex);
	mPublished = std::move(p);
}

void PoorboyWorldLines::onRender(ID3D11Device* device, ID3D11DeviceContext* context, SimpleMath::Vector2 screenSize, ID3D11RenderTargetView* target)
{
	std::unique_lock<std::mutex> lock(mDestructionGuard);
	const bool grid = mSettings->h2pWorldLinesToggle->GetValue(), spots = mSettings->h2pJumpSpotsToggle->GetValue();
	int game = -1;
	if (grid || spots)
	{
		const MCCState& state = mccStateHook->getCurrentMCCState();
		const GameState::Value now = state.currentGameState;
		if (state.currentPlayState == PlayState::Ingame)
			for (int i = 0; i < (int)std::size(kGames); i++)
				if (kGames[i].game == now) game = i;
	}
	mGame = game;
	mWantSpots = spots;
	if (game < 0 || mGpuFailed || !device || !context || !target || screenSize.x < 64 || screenSize.y < 64) return;
	Published p;
	{
		std::scoped_lock l(mPublishedMutex);
		p = mPublished;
	}
	Camera cam;
	if (!p.scene || !readCamera(p.camera, cam)) return;      // the newest camera: the lines stay put while you turn

	if (!mGpu.init(device))
	{
		mGpuFailed = true;                                   // logged once; the rest of the overlay carries on
		PLOG_ERROR << "Map grid: " << mGpu.lastError();
		return;
	}
	if (mGpu.scene() != p.scene) mGpu.setScene(p.scene);
	DrawOptions opt;
	opt.grid = grid;
	opt.spots = spots;
	opt.lineScale = screenSize.y / 1080.f;
	opt.gridWidth = mSettings->h2pGridWidth->GetValue();
	opt.gridOpacity = mSettings->h2pGridOpacity->GetValue();
	players(cam);
	opt.blockers = &mBlockers;
	if (!mGpu.draw(context, target, screenSize.x, screenSize.y, cam, opt)) return;

	if (spots && !mSettings->hideHUDToggle->GetValue())      // the labels are text: Hide HUD hides them
	{
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		for (const Label& l : p.labels)
		{
			float x, y;
			if (toScreen(cam, l.at, screenSize.x, screenSize.y, x, y) && x >= 0 && x < screenSize.x && y >= 0 && y < screenSize.y)
				dl->AddText(ImGui::GetFont(), 18.f * (std::max)(1.f, screenSize.y / 1080.f), ImVec2(x + 6, y - 9),
					rgb(kTakeOffRgb[0], kTakeOffRgb[1], kTakeOffRgb[2]), l.text.c_str());
		}
	}
}

// Every biped of the game (players, AI) as a capsule standing on its feet, carried forward by its velocity to now: a
// Spartan is ~0.7 units tall, an Elite ~0.85; one size for all, a little tall. Your own is left out while the camera is in
// it (first person); in third person, Theatre or a free camera it is kept clear like the others. The mod's list is read
// each frame (seqlock, no waiting); without the mod (or an older build) nothing is kept clear.
void PoorboyWorldLines::players(const Camera& cam)
{
	constexpr float kRadius = 0.27f, kStand = 0.86f, kCrouch = 0.62f;
	mBlockers.clear();
	HMODULE pinned = nullptr;
	if (!GetModuleHandleExW(0, L"h3sb.dll", &pinned) || !pinned) return;
	H3sbBiped list[64];
	int n = 0;
	if (auto fn = (int (*)(H3sbBiped*, int))GetProcAddress(pinned, "h3sb_bipeds")) n = fn(list, (int)std::size(list));
	FreeLibrary(pinned);
	for (int i = 0; i < n && i < (int)std::size(list); i++)
	{
		const H3sbBiped& b = list[i];
		const float ahead = std::clamp(b.age, 0.f, 0.1f);
		const V3 feet{ b.feet[0] + b.velocity[0] * ahead, b.feet[1] + b.velocity[1] * ahead, b.feet[2] + b.velocity[2] * ahead };
		const float height = kStand + (kCrouch - kStand) * std::clamp(b.crouch, 0.f, 1.f);
		const Capsule c{ feet + V3{ 0, 0, kRadius }, feet + V3{ 0, 0, height - kRadius }, kRadius };
		if (b.local)
		{
			// the camera's distance from the capsule's axis
			const V3 ab = c.b - c.a, ap = cam.pos - c.a;
			const float t = std::clamp(dot(ap, ab) / (std::max)(dot(ab, ab), 1e-6f), 0.f, 1.f);
			if (length(ap - ab * t) < kRadius + 0.2f) continue;
		}
		mBlockers.push_back(c);
	}
}

// the worker: which map, its geometry (loaded once per map, again when the mod rewrites the file: a campaign level
// loading its next part, Reach Forge changes), and with jump spots on the labels in sight (~30 times a second)
void PoorboyWorldLines::work()
{
	SetThreadDescription(GetCurrentThread(), L"PCM map grid");
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	using clock = std::chrono::steady_clock;
	std::shared_ptr<Scene> scene;
	int gameNow = -1;
	uintptr_t baseNow = 0;
	std::string wantedMap, lastProblem;
	auto nextCheck = clock::now();
	auto note = [&](const std::string& problem) {           // logged once until it changes
		if (problem != lastProblem) PLOG_INFO << "Map grid: " << problem;
		lastProblem = problem;
		};

	while (!mStop)
	{
		try
		{
			const int game = mGame.load();
			if (game < 0)
			{
				publish({});
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			const GameInfo& g = kGames[game];
			const auto now = clock::now();
			if (now >= nextCheck || game != gameNow)
			{
				nextCheck = now + std::chrono::milliseconds(500);
				uint32_t stamp = 0;
				const uintptr_t base = gameBase(g, &stamp);
				if (!base)
				{
					if (stamp) note(std::format("{} is a build the map grid does not know ({:#x}); nothing drawn", g.title, stamp));
					publish({});
					gameNow = -1;
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				if (game != gameNow || base != baseNow)
				{
					gameNow = game;
					baseNow = base;
					scene.reset();
				}
				const std::string name = mapName(g, base);
				if (!name.empty())
				{
					if (name != wantedMap)
					{
						wantedMap = name;
						scene.reset();              // the last map's lines are never drawn on this one
					}
					const auto file = mDir / std::filesystem::path("h3sb_world_" + name + ".bin");
					std::error_code ec;
					const auto time = std::filesystem::last_write_time(file, ec);
					if (!scene || (!ec && time != scene->worldTime))
					{
						try
						{
							const auto t0 = clock::now();
							auto s = Scene::load(file, mDir / L"spots", name);
							const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
							note(std::format("{} {}: {} polygons, {} map edges + {} instanced-geometry edges; {} jump-spot edges, {} take-off marks ({} ms)",
								scene ? "reloaded" : "loaded", name, s->polygons, s->edgeIndex[kStructure].size() / 2,
								s->edgeIndex[kInstance].size() / 2, s->green.size(), s->rings.size(), ms));
							scene = std::move(s);
						}
						catch (const std::exception& e)
						{
							note(std::format("{}: {}", name, e.what()));      // tried again in half a second (old lines kept on a reload)
						}
					}
				}
			}
			Published p;
			p.scene = scene;
			p.camera = baseNow + g.camera;
			Camera cam;
			if (scene && mWantSpots && !scene->rings.empty() && readCamera(p.camera, cam)) p.labels = visibleLabels(*scene, cam);
			publish(std::move(p));
		}
		catch (const std::exception& e)
		{
			note(std::string("stopped by an error: ") + e.what());
			publish({});
			scene.reset();
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
		catch (...)
		{
			note("stopped by an unknown error");
			publish({});
			scene.reset();
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	publish({});
}
