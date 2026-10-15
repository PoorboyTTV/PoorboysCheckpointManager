#pragma once
#include "pch.h"
#include "IOptionalCheat.h"
#include "GameState.h"
#include "DIContainer.h"
#include "h3sb_api.h"

// Poorboys Checkpoint Manager: Poorboy's Halo 2 physics for Halo 3 and ODST.
// The mod stays its own dll (h3sb.dll, in <PCM folder>\h3sb\ with its helper windows and spot data). This cheat loads it
// into MCC when any of its switches is on, drives it through the switch files it reads next to itself (h3sb_api.h) and
// lets it unload itself when they are all back to the game's own, or when PCM closes. One host for the whole process:
// the Halo 3 and the ODST instance of this cheat share it.
class Halo2Physics : public IOptionalCheat
{
private:
	class Halo2PhysicsImpl;
	std::unique_ptr<Halo2PhysicsImpl> pimpl;

public:
	Halo2Physics(GameState gameImpl, IDIContainer& dicon);
	~Halo2Physics();

	std::string_view getName() override { return nameof(Halo2Physics); }

	// for the overlay's status line, any thread: 1 = the mod is loaded and reported a tick (out filled in)
	static int readStatus(H3sbStatus* out);
	static bool isLoaded();
	// the last thing that went wrong (dll missing, could not load, helper missing), empty when all is well
	static std::string lastError();
};
