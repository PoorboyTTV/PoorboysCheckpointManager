#pragma once
#include "Lapua.h"
#include "RuntimeExceptionHandler.h"
#include "BinarySetting.h"


class HideWatermarkManager
{
private:

	// injected services
	std::shared_ptr<RuntimeExceptionHandler> runtimeExceptions;
	std::weak_ptr<HCMInternalGUI> hcmInternalGUIWeak;

	ScopedCallback<ToggleEvent> HideWatermarkToggleEventCallback;
	ScopedCallback<ToggleEvent> HideHUDToggleEventCallback;
	std::shared_ptr<BinarySetting<bool>> HideWatermarkToggle;
	std::shared_ptr<BinarySetting<bool>> HideHUDToggle;

	// Hide HUD hides the overlay's text too (messages, the collapsed tab): the same marker as a hidden watermark
	void onHideHUDToggleEvent(bool& newValue)
	{
		Lapua::shouldRunForWatermark = (newValue || HideWatermarkToggle->GetValue()) && Lapua::lapuaGood && Lapua::lapuaGood2;
	}

	void onHideWatermarkToggleEvent(bool& newValue)
	{
		PLOGV << "onHideWatermarkToggleEvent firing, newValue: " << newValue;
		try
		{

			if (newValue)
			{
				// todo fix this up
				if (Lapua::lapuaGood == false) throw HCMRuntimeException("Lapua service one failure");
				if (Lapua::lapuaGood2 == false) throw HCMRuntimeException("Lapua service two failure");
				Lapua::shouldRunForWatermark = true;
			}
			else
			{
				Lapua::shouldRunForWatermark = HideHUDToggle && HideHUDToggle->GetValue() && Lapua::lapuaGood && Lapua::lapuaGood2;
			}
		}
		catch (HCMRuntimeException ex)
		{
			runtimeExceptions->handleMessage(ex);
			HideWatermarkToggle->GetValueDisplay() = false;
			HideWatermarkToggle->UpdateValueWithInput();
		}

	}


public:
	HideWatermarkManager(std::shared_ptr<BinarySetting<bool>> toggle, std::shared_ptr<BinarySetting<bool>> hideHUD, std::shared_ptr<RuntimeExceptionHandler> exp)
		:
		HideWatermarkToggle(toggle),
		HideHUDToggle(hideHUD),
		runtimeExceptions(exp),
		HideWatermarkToggleEventCallback(toggle->valueChangedEvent, [this](bool& n) { onHideWatermarkToggleEvent(n); }),
		HideHUDToggleEventCallback(hideHUD->valueChangedEvent, [this](bool& n) { onHideHUDToggleEvent(n); })
	{}

};