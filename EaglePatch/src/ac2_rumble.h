#pragma once
#include <windows.h>
#include "ac2_rumble_effects.h"
namespace ac2_rumble
{
bool Install(HMODULE game, Config config);
void Update(bool connected, unsigned int controller, bool active);
void Shutdown();
}
