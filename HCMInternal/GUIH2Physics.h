#pragma once
#include "IGUIElement.h"
#include "SettingsStateAndEvents.h"
#include "Halo2Physics.h"
#include "PCMTheme.h"

// Poorboys Checkpoint Manager: the widgets HCM's own ones do not cover (the Halo 2 Physics section, the style picker).

// The live line from the mod: armed or not, the body's speed against the 5.45 a step has to pass (bar), the map.
class GUIH2PhysicsStatus : public IGUIElement
{
public:
	GUIH2PhysicsStatus(GameState implGame, ToolTipCollection tooltip)
		: IGUIElement(implGame, std::nullopt, tooltip)
	{
		this->currentHeight = GUIFrameHeightWithSpacing * 3;
	}

	void render(HotkeyRenderer&) override
	{
		H3sbStatus s{};
		s.size = (int32_t)sizeof(H3sbStatus);
		const bool have = Halo2Physics::readStatus(&s) == 1 && GetTickCount64() - s.updated_ms < 1500;
		this->currentHeight = GUIFrameHeightWithSpacing * 3;
		if (!Halo2Physics::isLoaded())
		{
			const std::string err = Halo2Physics::lastError();
			if (!err.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", err.c_str());
			else
				ImGui::TextDisabled("Halo 2 physics is off");
			this->currentHeight = GUIFrameHeightWithSpacing * 2;
		}
		else if (!have)
		{
			// HCM pauses the game while this window is open (Control > GUI settings), and the mod only hooks into a
			// game whose clock runs: say so, or it looks like nothing is happening
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "On - takes hold once the game runs.");
			ImGui::TextDisabled("The game is paused while this window is open: close it with its toggle key.");
		}
		else if (!s.have_body)
		{
			ImGui::TextDisabled("Waiting for the player (%s)", s.map);
			ImGui::Dummy(ImVec2(0, (float)GUIFrameHeight));
		}
		else
		{
			if (s.armed)
				ImGui::TextColored(ImVec4(0.25f, 1.0f, 0.35f, 1.0f), "ARMED");
			else
				ImGui::TextDisabled(s.asleep ? "asleep" : "not armed");
			ImGui::SameLine();
			ImGui::Text("  speed %.2f   pass %.2f", s.speed, s.sweep_speed);
			ImGui::SameLine();
			ImGui::TextDisabled("  %s", s.map);
			const float fraction = s.sweep_speed > 0 ? std::clamp(s.speed / s.sweep_speed, 0.0f, 1.0f) : 0.0f;
			ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, (float)GUIFrameHeight * 0.6f), "");
		}
		renderTooltip();
		ImGui::TextDisabled("Halo 2 physics by Poorboy");
	}

	std::string_view getName() override { return "Halo 2 physics status"; }
};

// Barriers: the mod's four steps by name (HCM's GUIComboEnum shows enum identifiers), with the cycle hotkey in front.
class GUIH2PhysicsBarriers : public IGUIElement
{
	std::weak_ptr<BinarySetting<int>> mSettingWeak;
	std::vector<std::thread> mUpdateSettingThreads;
	static constexpr const char* kLabels[4] = {
		"The game's own",
		"Soft ceilings off",
		"+ out-of-bounds kill off",
		"+ kill volumes (pits) off",
	};

public:
	GUIH2PhysicsBarriers(GameState implGame, ToolTipCollection tooltip, std::optional<RebindableHotkeyEnum> hotkey, std::shared_ptr<BinarySetting<int>> setting)
		: IGUIElement(implGame, hotkey, tooltip), mSettingWeak(setting)
	{
		this->currentHeight = GUIFrameHeightWithSpacing;
	}

	void render(HotkeyRenderer& hotkeyRenderer) override
	{
		auto setting = mSettingWeak.lock();
		if (!setting) return;
		hotkeyRenderer.renderHotkey(mHotkey);
		ImGui::SameLine();
		const int current = std::clamp(setting->GetValueDisplay(), 0, 3);
		ImGui::SetNextItemWidth(230.0f);
		if (ImGui::BeginCombo("Barriers##h2p", kLabels[current]))
		{
			for (int n = 0; n < 4; n++)
			{
				if (ImGui::Selectable(kLabels[n], n == current) && n != current)
				{
					setting->GetValueDisplay() = n;
					auto& t = mUpdateSettingThreads.emplace_back(std::thread([setting]() { setting->UpdateValueWithInput(); }));
					t.detach();
				}
				if (n == current) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		renderTooltip();
	}

	~GUIH2PhysicsBarriers()
	{
		for (auto& thread : mUpdateSettingThreads)
			if (thread.joinable()) thread.join();
	}

	std::string_view getName() override { return "Barriers##h2p"; }
};

// The overlay's theme (PCMTheme.h), saved with the settings; switching takes effect on the next frame.
class GUIPCMThemePicker : public IGUIElement
{
	std::weak_ptr<BinarySetting<int>> mSettingWeak;
	std::vector<std::thread> mUpdateSettingThreads;

public:
	GUIPCMThemePicker(GameState implGame, ToolTipCollection tooltip, std::shared_ptr<BinarySetting<int>> setting)
		: IGUIElement(implGame, std::nullopt, tooltip), mSettingWeak(setting)
	{
		this->currentHeight = GUIFrameHeightWithSpacing;
		PCMTheme::request(setting->GetValue());       // the one saved last time
	}

	void render(HotkeyRenderer&) override
	{
		auto setting = mSettingWeak.lock();
		if (!setting) return;
		const int current = std::clamp(setting->GetValueDisplay(), 0, (int)PCMTheme::Count - 1);
		ImGui::SetNextItemWidth(300.0f);
		if (ImGui::BeginCombo("Style##pcm", PCMTheme::name(current)))
		{
			for (int n = 0; n < PCMTheme::Count; n++)
			{
				if (ImGui::Selectable(PCMTheme::name(n), n == current) && n != current)
				{
					setting->GetValueDisplay() = n;
					PCMTheme::request(n);
					auto& t = mUpdateSettingThreads.emplace_back(std::thread([setting]() { setting->UpdateValueWithInput(); }));
					t.detach();
				}
				if (n == current) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		renderTooltip();
	}

	~GUIPCMThemePicker()
	{
		for (auto& thread : mUpdateSettingThreads)
			if (thread.joinable()) thread.join();
	}

	std::string_view getName() override { return "Style##pcm"; }
};
