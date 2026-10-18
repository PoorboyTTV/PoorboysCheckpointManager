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

	// ---- Halo 3: the game's own view render (Jagged's H3 Collision Viewer, Overlay\Engine.h + dllmain.cpp) ----------
	// c_player_view::x_global_player_views[4] sit back to back (0x2820 each) in halo3.dll's writable data and share one
	// read-only vtable whose 4 slots (render, render_setup, compute_visibility, render_submit_visibility) point into code;
	// slot 0 is swapped for ours. c_rasterizer's debug path: set_explicit_shaders, set_z_buffer_mode, draw_primitive_up,
	// and the cull mode globals. c_view::m_render_camera at +0x158 (render_camera: position, forward, up, vertical field
	// of view +0x28, +0x2C, render_pixel_bounds +0x44 s16 top, left, bottom, right). The same build only (PE stamp).
	struct EngineGame
	{
		GameState::Value game;
		uint32_t stamp;
		uintptr_t viewSize, setExplicitShaders, setZBufferMode, drawPrimitiveUp, cullMode, cullModeChanged, viewCamera;
		uintptr_t context;      // ID3D11DeviceContext* the rasterizer draws with (draw_primitive_up: Map, Unmap, Draw on it)
	};
	constexpr EngineGame kEngineGames[] = {
		{ GameState::Value::Halo3, 0x68A0EDFC, 0x2820, 0x278C34, 0x277F90, 0x2A32B4, 0x968DC0, 0x8AC580, 0x158, 0x46BB828 },
	};

	using ViewRender = void (*)(void*, void*, void*, void*);
	std::atomic<PoorboyWorldLines*> gViewOwner = nullptr;
	std::atomic<ViewRender> gViewOriginal = nullptr;
	std::atomic<int> gViewInside = 0;
	std::atomic<int> gViewFaults = 0;

	void guardedViewRendered(PoorboyWorldLines* owner, void* view)
	{
		__try
		{
			owner->onViewRendered(view);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			gViewFaults++;
		}
	}

	// all four integer argument registers go to the game's own render untouched, then the grid is drawn
	void hookedViewRender(void* self, void* a, void* b, void* c)
	{
		gViewInside++;
		if (ViewRender original = gViewOriginal.load()) original(self, a, b, c);
		static thread_local bool busy = false;
		if (!busy)
		{
			busy = true;
			if (PoorboyWorldLines* owner = gViewOwner.load()) guardedViewRendered(owner, self);
			busy = false;
		}
		gViewInside--;
	}

	struct Range
	{
		uintptr_t lo, hi;
	};

	// the vtable shared by 4 consecutive objects of viewSize in the module's writable data, its 4 slots in code
	// (Jagged's FindPlayerViewVtable: the first match is c_player_view's on 1.3528); 0 = none
	uintptr_t findViewVtable(uintptr_t base, uintptr_t viewSize, uintptr_t& imageEnd, Range& code)
	{
		__try
		{
			auto dos = (IMAGE_DOS_HEADER*)base;
			auto nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
			imageEnd = base + nt->OptionalHeader.SizeOfImage;
			auto sec = IMAGE_FIRST_SECTION(nt);
			Range text[16], rdata[16], data[16];
			int nt_ = 0, nr = 0, nd = 0;
			for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
			{
				const DWORD ch = sec[i].Characteristics;
				const uintptr_t lo = base + sec[i].VirtualAddress, hi = lo + sec[i].Misc.VirtualSize;
				if (!sec[i].Misc.VirtualSize || (ch & IMAGE_SCN_MEM_DISCARDABLE)) continue;
				if (ch & IMAGE_SCN_MEM_EXECUTE) { if (nt_ < 16) text[nt_++] = { lo, hi }; }
				else if (ch & IMAGE_SCN_MEM_WRITE) { if (nd < 16) data[nd++] = { lo, hi }; }
				else if (ch & IMAGE_SCN_MEM_READ) { if (nr < 16) rdata[nr++] = { lo, hi }; }
			}
			auto in = [](const Range* r, int n, uintptr_t v) {
				for (int i = 0; i < n; i++)
					if (v >= r[i].lo && v < r[i].hi) return true;
				return false;
				};
			code = nt_ ? text[0] : Range{ 0, 0 };
			for (int d = 0; d < nd; d++)
			{
				const uintptr_t start = (data[d].lo + 7) & ~(uintptr_t)7;
				if (data[d].hi < start + 4 * viewSize) continue;
				const uintptr_t end = data[d].hi - 4 * viewSize;
				for (uintptr_t p = start; p <= end; p += 8)
				{
					const uintptr_t v = *(uintptr_t*)p;
					if (!in(rdata, nr, v)) continue;
					if (*(uintptr_t*)(p + viewSize) != v || *(uintptr_t*)(p + 2 * viewSize) != v || *(uintptr_t*)(p + 3 * viewSize) != v) continue;
					if (p >= data[d].lo + viewSize && *(uintptr_t*)(p - viewSize) == v) continue;
					bool slots = true;
					for (int s = 0; s < 4 && slots; s++)
					{
						const uintptr_t fn = *(uintptr_t*)(v + s * 8);
						// slot 0 may already be someone's hook (outside the module): the caller decides
						if (!in(text, nt_, fn) && !(s == 0 && (fn < base || fn >= imageEnd))) slots = false;
					}
					if (slots) return v;
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
		return 0;
	}

	bool writeSlot(void** slot, void* value)
	{
		DWORD old = 0, unused = 0;
		__try
		{
			if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
			*slot = value;
			VirtualProtect(slot, sizeof(void*), old, &unused);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// c_view::m_render_camera as the emitter's camera (read under the hook's guard: the view is the game's own object)
	bool viewCamera(const uint8_t* cam, EmitCamera& out)
	{
		float f[12];
		std::memcpy(f, cam, sizeof(f));
		for (float x : f)
			if (!std::isfinite(x)) return false;
		int16_t bounds[4];
		std::memcpy(bounds, cam + 0x44, sizeof(bounds));
		out.pos = { f[0], f[1], f[2] };
		out.fwd = { f[3], f[4], f[5] };
		out.up = { f[6], f[7], f[8] };
		out.vfov = f[10];
		out.hfov = f[11];
		const float fl = length(out.fwd), ul = length(out.up);
		if (fl < 0.5f || fl > 1.5f || ul < 0.5f || ul > 1.5f || !(out.vfov > 0.05f && out.vfov < 3.1f)) return false;
		out.fwd = out.fwd * (1.f / fl);
		out.up = out.up * (1.f / ul);
		V3 left = cross(out.up, out.fwd);
		const float ll = length(left);
		if (ll < 1e-4f) return false;
		out.left = left * (1.f / ll);
		const float h = (float)(bounds[2] - bounds[0]), w = (float)(bounds[3] - bounds[1]);
		out.heightPx = h > 16.f ? h : 1080.f;
		out.aspect = h > 16.f && w > 16.f ? w / h : 16.f / 9.f;
		if (!(out.hfov > 0.05f && out.hfov < 3.1f)) out.hfov = 2.f * std::atan(std::tan(out.vfov * 0.5f) * out.aspect);
		return true;
	}
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
	mStop = true;
	if (mWorker.joinable()) mWorker.join();
	// the game's view render back to its own, then no call of ours may still be running
	unhookView();
	gViewOwner = nullptr;
	for (int i = 0; i < 200 && gViewInside.load() > 0; i++) Sleep(10);
	{
		std::unique_lock<std::mutex> lock(mDestructionGuard); // a render in progress finishes first
		mGpu.release();
		mEngine.release();
	}
}

// worker thread: the game's view render hooked for kGames[game] at base (once per module load)
void PoorboyWorldLines::hookView(int game, uintptr_t base)
{
	const EngineGame* eg = nullptr;
	for (const EngineGame& e : kEngineGames)
		if (e.game == kGames[game].game && e.stamp == kGames[game].stamp) eg = &e;
	if (!eg) return;
	uintptr_t imageEnd = 0;
	Range code{};
	const uintptr_t vtable = findViewVtable(base, eg->viewSize, imageEnd, code);
	void** slot = (void**)vtable;
	void* current = vtable ? *slot : nullptr;
	if (!vtable || !current)
	{
		mHookRefusedBase = base, mHookRefusedMs = GetTickCount64();
		PLOG_ERROR << "Map grid: " << kGames[game].title << "'s view render was not found; the grid is drawn over the frame instead";
		return;
	}
	if (current != (void*)&hookedViewRender && ((uintptr_t)current < base || (uintptr_t)current >= imageEnd))
	{
		// another tool hooks it already (Jagged's H3 Collision Viewer does): two hooks re-installing over each other
		// would call each other for ever, so this one stays out
		mHookRefusedBase = base, mHookRefusedMs = GetTickCount64();
		PLOG_INFO << "Map grid: another tool already hooks " << kGames[game].title << "'s view render; the grid is drawn over the frame instead";
		return;
	}
	if (current != (void*)&hookedViewRender) gViewOriginal = (ViewRender)current;
	mFns.setExplicitShaders = (decltype(mFns.setExplicitShaders))(base + eg->setExplicitShaders);
	mFns.setZBufferMode = (decltype(mFns.setZBufferMode))(base + eg->setZBufferMode);
	mFns.drawPrimitiveUp = (decltype(mFns.drawPrimitiveUp))(base + eg->drawPrimitiveUp);
	mFns.cullMode = (int*)(base + eg->cullMode);
	mFns.cullModeChanged = (int*)(base + eg->cullModeChanged);
	mFns.context = (ID3D11DeviceContext* const*)(base + eg->context);
	mViewCamera = eg->viewCamera;
	gViewOwner = this;
	if (current != (void*)&hookedViewRender && !writeSlot(slot, (void*)&hookedViewRender))
	{
		mHookRefusedBase = base, mHookRefusedMs = GetTickCount64();
		PLOG_ERROR << "Map grid: could not hook " << kGames[game].title << "'s view render; the grid is drawn over the frame instead";
		return;
	}
	mHookSlot = slot;
	mHookBase = base;
	mEngineGame = game;
	mEngineReady = true;
	PLOG_INFO << "Map grid: drawn inside " << kGames[game].title << "'s own view render (vtable " << std::hex << (vtable - base) << ")";
}

// worker thread / destructor: the slot back to the game's own render, if the module it is in is still the one hooked
void PoorboyWorldLines::unhookView()
{
	mEngineReady = false;
	if (!mHookSlot) return;
	const int game = mEngineGame.load();
	if (game >= 0 && (uintptr_t)GetModuleHandleW(kGames[game].module) == mHookBase)
	{
		void* now = nullptr;
		safeCopy(&now, mHookSlot, sizeof(now));
		if (now == (void*)&hookedViewRender) writeSlot(mHookSlot, (void*)gViewOriginal.load());
	}
	mHookSlot = nullptr;
	mHookBase = 0;
}

// the game's render thread, after it rendered a player view (Halo 3)
void PoorboyWorldLines::onViewRendered(void* view)
{
	if (!mEngineReady || gViewFaults.load() > 0) return;
	const int game = mGame.load();
	if (game < 0 || game != mEngineGame.load()) return;
	const bool grid = mSettings->h2pWorldLinesToggle->GetValue(), spots = mSettings->h2pJumpSpotsToggle->GetValue();
	// the game's own context: its view's drawing is on it (it may not be the frame's immediate context)
	ID3D11DeviceContext* ctx = mFns.context ? *mFns.context : nullptr;
	if ((!grid && !spots) || !ctx || !view) return;
	if (!mEngineContextLogged)
	{
		mEngineContextLogged = true;
		ID3D11RenderTargetView* rtv = nullptr;
		ID3D11DepthStencilView* dsv = nullptr;
		ctx->OMGetRenderTargets(1, &rtv, &dsv);
		PLOG_INFO << "Map grid: the game's context " << (void*)ctx << " (" << (ctx->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? "deferred" : "immediate")
			<< ", the frame's " << (void*)mContext.load() << "), target " << (void*)rtv << ", depth " << (void*)dsv;
		if (rtv) rtv->Release();
		if (dsv) dsv->Release();
	}
	std::shared_ptr<const Scene> scene;
	std::shared_ptr<const EdgeCells> cells;
	{
		std::scoped_lock l(mPublishedMutex);
		scene = mPublished.scene;
		cells = mPublished.cells;
	}
	if (!scene) return;
	EmitCamera cam;
	if (!viewCamera((const uint8_t*)view + mViewCamera, cam)) return;
	EmitOptions opt;
	opt.grid = grid;
	opt.spots = spots;
	opt.gridWidth = mSettings->h2pGridWidth->GetValue();
	opt.gridOpacity = mSettings->h2pGridOpacity->GetValue();
	if (mEngine.draw(ctx, mFns, scene, cells, cam, opt))
	{
		mEngineDrawnMs = GetTickCount64();
		mEngineDraws++;
	}
	else if (!mEngineErrorLogged)
	{
		mEngineErrorLogged = true;
		PLOG_ERROR << "Map grid: the game's drawer: " << mEngine.lastError() << "; the grid is drawn over the frame instead";
	}
}

void PoorboyWorldLines::publish(Published p)
{
	std::scoped_lock lock(mPublishedMutex);
	mPublished = std::move(p);
}

void PoorboyWorldLines::onRender(ID3D11Device* device, ID3D11DeviceContext* context, SimpleMath::Vector2 screenSize, ID3D11RenderTargetView* target)
{
	std::unique_lock<std::mutex> lock(mDestructionGuard);
	if (context) mContext = context;                     // the game's own view render (Halo 3) draws with it
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

	// Halo 3 with the view render hooked: the game's drawer drew the lines this frame; only the labels are left
	const bool engineLive = mEngineReady && GetTickCount64() - mEngineDrawnMs.load() < 300;
	if (!engineLive && !mGpu.init(device))
	{
		mGpuFailed = true;                                   // logged once; the rest of the overlay carries on
		PLOG_ERROR << "Map grid: " << mGpu.lastError();
		return;
	}
	if (!engineLive && mGpu.scene() != p.scene) mGpu.setScene(p.scene);
	DrawOptions opt;
	opt.grid = grid;
	opt.spots = spots;
	opt.lineScale = screenSize.y / 1080.f;
	opt.gridWidth = mSettings->h2pGridWidth->GetValue();
	opt.gridOpacity = mSettings->h2pGridOpacity->GetValue();
	if (!engineLive)
	{
		players(cam);
		opt.blockers = &mBlockers;
		if (!mGpu.draw(context, target, screenSize.x, screenSize.y, cam, opt)) return;
		mFrameDraws++;
	}

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
	std::shared_ptr<EdgeCells> cells;
	int gameNow = -1;
	auto nextStatus = clock::now() + std::chrono::seconds(10);
	std::string lastStatus;
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
					scene.reset(), cells.reset();
				}
				// Halo 3: the game's own view render, hooked once per module load. A module loaded again at the same
				// address (MCC does that between maps) has a fresh vtable without the hook: hooked again
				if (mEngineReady && mHookSlot)
				{
					void* inSlot = nullptr;
					if (!safeCopy(&inSlot, mHookSlot, sizeof(inSlot)) || inSlot != (void*)&hookedViewRender)
					{
						PLOG_INFO << "Map grid: " << g.title << " was loaded again; hooking its view render again";
						mEngineReady = false;
						mHookSlot = nullptr;
						mHookBase = 0;
					}
				}
				if (mHookBase && mHookBase != base) unhookView();
				if (!mEngineReady && (base != mHookRefusedBase || GetTickCount64() - mHookRefusedMs > 30000)) hookView(game, base);
				const std::string name = mapName(g, base);
				if (!name.empty())
				{
					if (name != wantedMap)
					{
						wantedMap = name;
						scene.reset(), cells.reset();              // the last map's lines are never drawn on this one
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
							auto c = std::make_shared<EdgeCells>();
							c->build(*s);
							scene = std::move(s);
							cells = std::move(c);
						}
						catch (const std::exception& e)
						{
							note(std::format("{}: {}", name, e.what()));      // tried again in half a second (old lines kept on a reload)
						}
					}
				}
			}
			if (clock::now() >= nextStatus)
			{
				// how the grid was drawn in the last 10 s (the game's renderer, or over the frame): logged when it changes
				nextStatus = clock::now() + std::chrono::seconds(10);
				void* inSlot = nullptr;
				const bool hooked = mEngineReady && mHookSlot && safeCopy(&inSlot, mHookSlot, sizeof(inSlot)) && inSlot == (void*)&hookedViewRender;
				const int engine = mEngineDraws.exchange(0), frame = mFrameDraws.exchange(0);
				const std::string status = std::string(engine > 0 ? "drawn by the game's renderer" : frame > 0 ? "drawn over the frame" : "not drawn") +
					"; hook " + (hooked ? "in place" : "not in place") + ", faults " + std::to_string(gViewFaults.load());
				if (status != lastStatus)
					PLOG_INFO << "Map grid on " << (scene ? scene->map : std::string("(no map yet)")) << ": " << status << " (" << engine << " views, " << frame
					<< " frames in the last 10 s)";
				lastStatus = status;
			}
			Published p;
			p.scene = scene;
			p.cells = cells;
			p.camera = baseNow + g.camera;
			Camera cam;
			if (scene && mWantSpots && !scene->rings.empty() && readCamera(p.camera, cam)) p.labels = visibleLabels(*scene, cam);
			publish(std::move(p));
		}
		catch (const std::exception& e)
		{
			note(std::string("stopped by an error: ") + e.what());
			publish({});
			scene.reset(), cells.reset();
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
		catch (...)
		{
			note("stopped by an unknown error");
			publish({});
			scene.reset(), cells.reset();
			std::this_thread::sleep_for(std::chrono::seconds(2));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	publish({});
}
