#pragma once

#include <windows.h>
#include <stdint.h>

namespace ac2_fov
{
bool Install(HMODULE gameModule, float fovMultiplier);
void LogSummary();

namespace detail
{
// Exposed to the regression test, which executes the production trampoline.
extern uintptr_t continuation;
extern uintptr_t worldPointer;
extern float multiplier;
extern volatile uint32_t changes;
extern float originalFov;
extern float scaledFov;
void Hook();
}
}
