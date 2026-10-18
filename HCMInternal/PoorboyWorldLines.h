#pragma once
#include "pch.h"
#include "IMCCStateHook.h"
#include "DirectXRenderEvent.h"
#include "BinarySetting.h"
#include "WorldLinesScene.h"
#include "WorldLinesGpu.h"

class SettingsStateAndEvents;

// Poorboys Checkpoint Manager: the map's collision drawn into the game view by the overlay itself (Poorboy's Mods):
//   "Map grid"    every collision edge of the whole map: the map itself (structure BSP) cyan, instanced geometry yellow,
//                 hidden behind walls (the collision goes into a depth buffer first; WorldLinesGpu)
//   "Jump spots"  Halo 3's stock maps: green landing edges, magenta take-off marks with their labels, orange fast-drop seams
// People are kept clear of the lines: every biped the mod lists (h3sb_bipeds) goes into the depth pass as a capsule (your
// own too, unless the camera is in it: first person).
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

	std::atomic<int> mGame = -1;                    // render thread -> worker: index of the game being played, -1 = draw nothing
	std::atomic<bool> mWantSpots = false;
	std::atomic<bool> mStop = false;

	struct Published
	{
		std::shared_ptr<const worldlines::Scene> scene;
		std::vector<worldlines::Label> labels;      // take-off marks' labels in sight (jump spots on)
		uintptr_t camera = 0;                       // address of the game's camera record
	};
	std::mutex mPublishedMutex;
	Published mPublished;                           // worker -> render thread
	std::thread mWorker;

	void onRender(ID3D11Device* device, ID3D11DeviceContext* context, SimpleMath::Vector2 screenSize, ID3D11RenderTargetView* target);
	void players(const worldlines::Camera& cam);    // mBlockers from the Halo 2 physics mod's biped list
	void publish(Published p);
	void work();

public:
	PoorboyWorldLines(std::shared_ptr<IMCCStateHook> mccState, std::shared_ptr<DirectXRenderEvent> renderEvent,
		std::shared_ptr<SettingsStateAndEvents> settings, const std::string& pcmDir);
	~PoorboyWorldLines();

	PoorboyWorldLines(const PoorboyWorldLines&) = delete;
	PoorboyWorldLines& operator=(const PoorboyWorldLines&) = delete;
};
