#include "pch.h"
#include "PCMTheme.h"

namespace
{
	std::atomic<int> gRequested = PCMTheme::Halo2Menu;
	std::atomic<int> gApplied = -1;

	struct Palette
	{
		ImVec4 window, child, popup, text, textDisabled, border;
		ImVec4 frame, frameHovered, frameActive;
		ImVec4 button, buttonHovered, buttonActive;
		ImVec4 header, headerHovered, headerActive;
		ImVec4 accent;          // check marks, slider grabs, the speed bar, separators
		ImVec4 title, titleCollapsed;
		float rounding;
	};

	Palette palette(int style)
	{
		switch (style)
		{
		case PCMTheme::Competitive:        // B: near-black, sharp corners, neon green
			return Palette{
				{0.02f, 0.02f, 0.02f, 0.94f}, {0.06f, 0.06f, 0.06f, 0.60f}, {0.03f, 0.03f, 0.03f, 0.97f},
				{0.95f, 0.97f, 0.95f, 1.00f}, {0.55f, 0.60f, 0.55f, 1.00f}, {0.22f, 1.00f, 0.08f, 0.50f},
				{0.08f, 0.08f, 0.08f, 1.00f}, {0.12f, 0.20f, 0.10f, 1.00f}, {0.16f, 0.32f, 0.10f, 1.00f},
				{0.09f, 0.09f, 0.09f, 1.00f}, {0.14f, 0.26f, 0.10f, 1.00f}, {0.20f, 0.45f, 0.10f, 1.00f},
				{0.07f, 0.07f, 0.07f, 1.00f}, {0.10f, 0.18f, 0.08f, 1.00f}, {0.13f, 0.30f, 0.08f, 1.00f},
				{0.22f, 1.00f, 0.08f, 1.00f},
				{0.25f, 1.00f, 0.10f, 1.00f}, {0.20f, 0.85f, 0.10f, 1.00f},
				0.0f };
		case PCMTheme::MilitaryHUD:        // C: charcoal graphite, warm amber, hairlines
			return Palette{
				{0.07f, 0.07f, 0.075f, 0.94f}, {0.11f, 0.11f, 0.12f, 0.60f}, {0.08f, 0.08f, 0.085f, 0.97f},
				{0.93f, 0.91f, 0.86f, 1.00f}, {0.55f, 0.53f, 0.48f, 1.00f}, {0.55f, 0.45f, 0.25f, 0.45f},
				{0.10f, 0.10f, 0.11f, 1.00f}, {0.18f, 0.15f, 0.09f, 1.00f}, {0.28f, 0.21f, 0.06f, 1.00f},
				{0.13f, 0.13f, 0.14f, 1.00f}, {0.24f, 0.19f, 0.08f, 1.00f}, {0.40f, 0.29f, 0.04f, 1.00f},
				{0.12f, 0.12f, 0.13f, 1.00f}, {0.20f, 0.16f, 0.08f, 1.00f}, {0.30f, 0.22f, 0.05f, 1.00f},
				{1.00f, 0.69f, 0.00f, 1.00f},
				{1.00f, 0.69f, 0.00f, 1.00f}, {0.85f, 0.58f, 0.05f, 1.00f},
				2.0f };
		case PCMTheme::HCMClassic:         // Halo Checkpoint Manager's own maroon
			return Palette{
				{0.08f, 0.04f, 0.08f, 1.00f}, {0.18f, 0.10f, 0.18f, 0.50f}, {0.08f, 0.04f, 0.08f, 0.97f},
				{0.80f, 0.90f, 0.90f, 1.00f}, {0.24f, 0.29f, 0.29f, 1.00f}, {0.40f, 0.50f, 0.50f, 0.38f},
				{0.35f, 0.09f, 0.12f, 1.00f}, {0.55f, 0.23f, 0.29f, 1.00f}, {0.81f, 0.23f, 0.29f, 1.00f},
				{0.30f, 0.09f, 0.12f, 1.00f}, {0.50f, 0.23f, 0.29f, 1.00f}, {0.75f, 0.23f, 0.29f, 1.00f},
				{0.26f, 0.59f, 0.98f, 0.31f}, {0.26f, 0.59f, 0.98f, 0.80f}, {0.26f, 0.59f, 0.98f, 1.00f},
				{1.00f, 0.90f, 0.60f, 0.70f},
				{1.00f, 0.80f, 0.40f, 1.00f}, {1.00f, 0.60f, 0.25f, 1.00f},
				0.0f };
		default:                           // A: the Halo 2 menus, navy and steel blue, glowing cyan
			return Palette{
				{0.03f, 0.07f, 0.14f, 0.92f}, {0.05f, 0.11f, 0.20f, 0.60f}, {0.04f, 0.08f, 0.16f, 0.97f},
				{0.90f, 0.95f, 1.00f, 1.00f}, {0.45f, 0.60f, 0.72f, 1.00f}, {0.25f, 0.65f, 0.90f, 0.55f},
				{0.06f, 0.14f, 0.26f, 1.00f}, {0.10f, 0.24f, 0.40f, 1.00f}, {0.14f, 0.36f, 0.58f, 1.00f},
				{0.08f, 0.20f, 0.36f, 1.00f}, {0.12f, 0.32f, 0.52f, 1.00f}, {0.16f, 0.46f, 0.70f, 1.00f},
				{0.08f, 0.22f, 0.38f, 0.85f}, {0.10f, 0.32f, 0.52f, 0.90f}, {0.12f, 0.42f, 0.65f, 1.00f},
				{0.35f, 0.85f, 1.00f, 1.00f},
				{0.45f, 0.88f, 1.00f, 1.00f}, {0.35f, 0.75f, 0.95f, 1.00f},
				3.0f };
		}
	}

	void apply(int style)
	{
		const Palette p = palette(style);
		ImGuiStyle& s = ImGui::GetStyle();
		ImGui::StyleColorsDark(&s);
		s.WindowBorderSize = style == PCMTheme::HCMClassic ? 0.0f : 1.0f;
		s.FrameBorderSize = 1.0f;
		s.WindowRounding = p.rounding;
		s.FrameRounding = p.rounding;
		s.GrabRounding = p.rounding;
		s.PopupRounding = p.rounding;
		s.TabRounding = p.rounding;
		s.ScrollbarRounding = p.rounding;
		s.IndentSpacing = 10.f;
		ImVec4* c = s.Colors;
		c[ImGuiCol_WindowBg] = p.window;
		c[ImGuiCol_ChildBg] = p.child;
		c[ImGuiCol_PopupBg] = p.popup;
		c[ImGuiCol_Text] = p.text;
		c[ImGuiCol_TextDisabled] = p.textDisabled;
		c[ImGuiCol_Border] = p.border;
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = p.frame;
		c[ImGuiCol_FrameBgHovered] = p.frameHovered;
		c[ImGuiCol_FrameBgActive] = p.frameActive;
		c[ImGuiCol_Button] = p.button;
		c[ImGuiCol_ButtonHovered] = p.buttonHovered;
		c[ImGuiCol_ButtonActive] = p.buttonActive;
		c[ImGuiCol_Header] = p.header;
		c[ImGuiCol_HeaderHovered] = p.headerHovered;
		c[ImGuiCol_HeaderActive] = p.headerActive;
		c[ImGuiCol_CheckMark] = p.accent;
		c[ImGuiCol_SliderGrab] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.60f);
		c[ImGuiCol_SliderGrabActive] = p.accent;
		c[ImGuiCol_PlotHistogram] = p.accent;
		c[ImGuiCol_PlotHistogramHovered] = p.accent;
		c[ImGuiCol_Separator] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.45f);
		c[ImGuiCol_ScrollbarGrab] = p.button;
		c[ImGuiCol_ScrollbarGrabHovered] = p.buttonHovered;
		c[ImGuiCol_ScrollbarGrabActive] = p.buttonActive;
		c[ImGuiCol_TextSelectedBg] = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.35f);
		c[ImGuiCol_TitleBg] = p.window;
		c[ImGuiCol_TitleBgActive] = ImVec4(p.headerActive.x, p.headerActive.y, p.headerActive.z, 0.70f);
		c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.f, 0.f, 0.f, 0.f);
		c[ImGuiCol_ModalWindowDimBg] = ImVec4(p.window.x, p.window.y, p.window.z, 0.35f);
	}
}

namespace PCMTheme
{
	const char* name(int style)
	{
		switch (style)
		{
		case Competitive: return "B: Competitive (black and neon green)";
		case MilitaryHUD: return "C: Military HUD (charcoal and amber)";
		case HCMClassic: return "Halo Checkpoint Manager's own";
		default: return "A: Halo 2 menu (navy and cyan)";
		}
	}

	void request(int style)
	{
		if (style < 0 || style >= Count) style = Halo2Menu;
		gRequested = style;
	}

	void applyPending()
	{
		const int want = gRequested;
		if (want == gApplied) return;
		apply(want);
		gApplied = want;
	}

	ImVec4 titleColor(bool windowOpen)
	{
		const Palette p = palette(gApplied < 0 ? gRequested.load() : gApplied.load());
		return windowOpen ? p.title : p.titleCollapsed;
	}
}
