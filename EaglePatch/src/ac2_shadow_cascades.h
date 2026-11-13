#pragma once

#include <windows.h>
#include <stdint.h>

namespace ac2_shadows
{
// Installs only on the verified Digital UPlay layout. Other EaglePatch features
// remain available when this optional patch is disabled or cannot be installed.
bool Install(HMODULE gameModule, unsigned int distanceMultiplier);
void LogSummary();

namespace detail
{
// Shared with the regression test so it executes the production trampoline.
extern uintptr_t continuation;
extern float multiplier;
extern volatile uint32_t calls;
extern volatile uint32_t changes;
extern float originalDistances[4];
extern float scaledDistances[4];
extern float effectiveMultiplier;
void Hook();
}
}
