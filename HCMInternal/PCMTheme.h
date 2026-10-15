#pragma once
#include "imgui.h"

// Poorboys Checkpoint Manager: the overlay's look. The three styles the user was shown as ChatGPT mockups
// plus Halo Checkpoint Manager's own, picked in the Control section and saved with the
// settings. A pick is applied on the render thread between two frames (ImGuiManager calls applyPending).
namespace PCMTheme
{
	enum Style : int { Halo2Menu = 0, Competitive = 1, MilitaryHUD = 2, HCMClassic = 3, Count = 4 };

	const char* name(int style);
	void request(int style);        // any thread
	void applyPending();            // render thread, before ImGui::NewFrame
	ImVec4 titleColor(bool windowOpen);
}
