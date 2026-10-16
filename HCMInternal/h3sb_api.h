// What the mod (h3sb.dll) offers a host program inside the same process, e.g. Poorboys Checkpoint Manager's
// overlay. Plain C layout, so the host only needs this header:
//     typedef int (*H3sbStatusFn)(H3sbStatus *);
//     auto fn = (H3sbStatusFn)GetProcAddress(GetModuleHandleW(L"h3sb.dll"), "h3sb_status");
//     H3sbStatus s{sizeof(H3sbStatus)};  if (fn && fn(&s)) ...
// The switches stay files next to h3sb.dll, read 4 times a second while a game runs (once a second before):
//     h3sb_mode.txt      0 observe only, 1-3 the Halo 2 step (3 = with Halo 2's own movement), 9 = unload the dll
//     h3sb_barriers.txt  0 the game's, 1 soft ceilings off, 2 and the out-of-bounds kill off, 3 and kill volumes off
//     h3sb_tick.txt      30 or 60 (MCC's own)
//     h3sb_combos.txt    1 Halo 2's button combos, 0 the game's own
//     h3sb_falls.txt     1 no falling damage or falling deaths, 0 the game's own
//     h3sb_fly.txt       "1 <speed>" fly (any mode, 0 too), speed in world units a second; "0" or no file = off
//     h3sb_clip.txt      1 bottomless clip for this machine's player (any mode, 0 too), 0 the game's own
#pragma once

#include <stdint.h>

struct H3sbStatus {
    int32_t size;           // in: sizeof(H3sbStatus); out: the size the dll filled in
    int32_t mode;           // h3sb_mode.txt as the dll took it
    int32_t game;           // 0 none yet, 1 Halo 3, 2 Halo 3: ODST
    int32_t have_body;      // the Halo 2 body drives the local player this tick
    int32_t level;          // the body's backstep level (-3 = armed)
    int32_t armed;          // level == -3: a fall onto the right edge bounces
    int32_t asleep;         // Halo 2's sleep rule put the body to sleep (standing still loses the armed state)
    int32_t airborne;
    float speed;            // |velocity| of the Halo 2 body, world units a second
    float sweep_speed;      // the speed to pass for a step: Halo 2's sweep at 30 Hz, 5.4465
    float velocity[3];
    uint64_t updated_ms;    // GetTickCount64() of the tick these come from (old: menu, paused or loading)
    char map[32];           // the loaded map, e.g. "040_voi"
    int32_t flying;         // fly is moving the player (h3sb_fly.txt); added after map: a host with the shorter struct
                            // still gets everything up to map
    int32_t spot;           // 1 = a spot is saved on the map that is loaded (h3sb_spot)
    int32_t spot_armed;     // the Halo 2 body was armed when it was saved (it is armed again there)
    float spot_pos[3];
};

// h3sb_arm(): the armed state (level -3) on the next tick the Halo 2 body is driven, as if it had stood up inside
// geometry. A sleeping body is woken first. 1 = taken, 0 = no Halo 2 body right now (mode 0, no game, in a vehicle).
//     typedef int (*H3sbArmFn)();  GetProcAddress(h3sb, "h3sb_arm")
// h3sb_spot(what): 1 = save where this machine's player is (position, velocity, crouch and the Halo 2 body's armed
// state), 2 = go back there. Taken on the next tick the player is on foot, within 3 seconds; any mode, flying too. Only
// this player moves (nothing is rolled back), so it is safe online. 1 = queued, -1 = what is not 1 or 2. Whether there
// is a spot to go back to: H3sbStatus.spot. The view direction is the host's to save and put back.
//     typedef int (*H3sbSpotFn)(int);  GetProcAddress(h3sb, "h3sb_spot")

#ifdef H3SB_EXPORTS
extern "C" __declspec(dllexport) int h3sb_status(H3sbStatus *out);
extern "C" __declspec(dllexport) int h3sb_arm();
extern "C" __declspec(dllexport) int h3sb_spot(int what);
#endif
