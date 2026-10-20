#pragma once
#include "pch.h"
#include "IMCCStateHook.h"
#include "DirectXRenderEvent.h"
#include "BinarySetting.h"
#include "WorldLinesScene.h"
#include "WorldLinesGpu.h"
#include "WorldLinesEmit.h"
#include "WorldLinesEngine.h"

class SettingsStateAndEvents;

// Poorboys Checkpoint Manager: the map's collision drawn into the game view by the overlay itself (Poorboy's Mods):
//   "Map grid"    every collision edge of the whole map: the map itself (structure BSP) cyan, instanced geometry yellow,
//                 hidden behind walls (the collision goes into a depth buffer first; WorldLinesGpu)
//   "Jump spots"  Halo 3's stock maps: green landing edges, magenta take-off marks with their labels, orange fast-drop seams
// Halo 3: drawn inside the game's own view render (WorldLinesEngine: c_player_view::render hooked, as Jagged's H3
// Collision Viewer does), so the game's depth buffer hides the lines behind walls, people, vehicles and your
// first-person gun. ODST and Reach (and Halo 3 if the hook cannot go in, or with "Grid without bloom"): drawn over the
// finished frame (WorldLinesGpu), kept off the first-person gun and arms by a copy of the game's own depth buffer, people
// kept clear by capsules (h3sb_bipeds; your own too unless the camera is in it: first person).
// It replaced the old helper window (h3sb_world.py: a full-screen see-through window redrawn ~115 times a second, which
// made the game lag). The polygons are the ones the Halo 2 physics mod read from the game (h3sb\h3sb_world_<map>.bin),
// so the mod is kept loaded while either is on. A thread of its own finds and loads the map; the render hook uploads it
// once and then draws a handful of calls a frame with the newest camera.
class PoorboyWorldLines
{
	std::shared_ptr<IMCCStateHook> mccStateHook;
	std::shared_ptr<SettingsStateAndEvents> mSettings;  // Map grid, Jump spots, grid width / opacity, Hide HUD
	std::vector<worldlines::Capsule> mBlockers;         // render thread: the players this frame
	std::filesystem::path mDir;                     // <PCM folder>\h3sb
	ScopedCallback<DirectXRenderEvent> mRenderCallback;
	std::mutex mDestructionGuard;
	worldlines::Gpu mGpu;                           // render thread only
	bool mGpuFailed = false;
	// render thread: the game's own depth buffer, which keeps the frame path's lines off its first-person gun and arms
	// (found in the game's surface table every half second, one reference held between) and what was last logged of it
	ID3D11Texture2D* mGameDepth = nullptr;
	uint64_t mGameDepthMs = 0;
	std::string mGameDepthNote;
	const char* gameDepth(int game, ID3D11Device* device, uintptr_t camera, worldlines::DrawOptions& opt);    // "" = given

	std::atomic<int> mGame = -1;                    // render thread -> worker: index of the game being played, -1 = draw nothing
	std::atomic<bool> mWantSpots = false;
	std::atomic<bool> mStop = false;

	struct Published
	{
		std::shared_ptr<const worldlines::Scene> scene;
		std::vector<worldlines::Label> labels;      // take-off marks' labels in sight (jump spots on)
		std::shared_ptr<const worldlines::EdgeCells> cells;     // the scene's lines by cell (the game's drawer's ribbons)
		uintptr_t camera = 0;                       // address of the game's camera record
	};
	std::mutex mPublishedMutex;
	Published mPublished;                           // worker -> render thread
	std::thread mWorker;

	void onRender(ID3D11Device* device, ID3D11DeviceContext* context, SimpleMath::Vector2 screenSize, ID3D11RenderTargetView* target);

	// the game's view render hook (Halo 3): installed by the worker, drawn from on the game's render thread
	worldlines::EngineGrid mEngine;                 // render thread
	worldlines::EngineFns mFns;
	std::atomic<ID3D11DeviceContext*> mContext = nullptr;   // the game's immediate context (from the frame callback)
	std::atomic<bool> mEngineReady = false;         // the hook is in for the running module
	std::atomic<int> mEngineGame = -1;              // kGames index it is in for
	std::atomic<uint64_t> mEngineDrawnMs = 0;       // the last view the game's drawer drew (the frame path stands aside)
	uintptr_t mViewCamera = 0;                      // c_view::m_render_camera
	void** mHookSlot = nullptr;                     // worker thread: the vtable slot, the module it is in, and a module
	uintptr_t mHookBase = 0, mHookRefusedBase = 0;  // refused (another tool already hooks it; tried again after 30 s)
	uint64_t mHookRefusedMs = 0;
	std::atomic<int> mEngineDraws = 0, mFrameDraws = 0;     // for the status line (logged when the way changes)
	std::atomic<int> mFrameSkip = 0;                // why the last frame drew no grid over it (FrameSkip, PoorboyWorldLines.cpp)
	bool mEngineErrorLogged = false, mEngineContextLogged = false;
	void hookView(int game, uintptr_t base);
	void unhookView();
	void players(const worldlines::Camera& cam);    // mBlockers from the Halo 2 physics mod's biped list
	void publish(Published p);
	void work();

public:
	PoorboyWorldLines(std::shared_ptr<IMCCStateHook> mccState, std::shared_ptr<DirectXRenderEvent> renderEvent,
		std::shared_ptr<SettingsStateAndEvents> settings, const std::string& pcmDir);
	~PoorboyWorldLines();

	// called by the hook on the game's render thread after it has rendered a player view
	void onViewRendered(void* view);

	PoorboyWorldLines(const PoorboyWorldLines&) = delete;
	PoorboyWorldLines& operator=(const PoorboyWorldLines&) = delete;
};
