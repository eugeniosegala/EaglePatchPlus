#include "ac2_fov.h"
#include "ac2_image.h"

#include <string.h>

#include "patcher.h"
#include "../shared/logger.h"

namespace ac2_fov
{
namespace detail
{
uintptr_t continuation = 0;
uintptr_t worldPointer = 0;
float multiplier = 1.0f;
volatile uint32_t changes = 0;
float originalFov = 0.0f;
float scaledFov = 0.0f;
static bool locked = false;
// Measured active-camera input during normal gameplay on the verified Steam build.
static const float gameplayFov = 0.768f;
static float minFov = 0.05f;
static float maxFov = 1.5f;

// The engine copies this value into both FOV inputs at camera+0x160/0x16c,
// then builds the projection and culling data from those inputs. The game
// retains its requested angle at +0x30 in both camera modes.
NAKED void Hook()
{
	__asm
	{
		movss xmm1, [ecx + 0x30]
		pushfd
		push eax

		// Follow the same tagged camera handle as the engine's active-camera
		// getter. Leave reflection, shadow and other inactive cameras alone.
		mov eax, worldPointer
		mov eax, [eax]
		test eax, eax
		jz fov_done
		mov eax, [eax + 0x34]
		test eax, eax
		jz fov_done
		test dword ptr [eax + 8], 0x80000000
		jz fov_done
		cmp ecx, [eax]
		jne fov_done

		cmp locked, 0
		je fov_dynamic
		// A scripted zoom can cross the dynamic mode's normal range. Accept
		// any positive finite request, preserving invalid camera inputs.
		movd eax, xmm1
		test eax, eax
		jle fov_done
		cmp eax, 0x7F800000
		jae fov_done
		movss xmm1, gameplayFov
		jmp fov_scale

	fov_dynamic:
		ucomiss xmm1, minFov
		jb fov_done
		ucomiss xmm1, maxFov
		ja fov_done
	fov_scale:
		mulss xmm1, multiplier
		maxss xmm1, minFov
		minss xmm1, maxFov
		cmp changes, 0
		jne fov_count
		mov eax, [ecx + 0x30]
		mov originalFov, eax
		movss scaledFov, xmm1
	fov_count:
		inc changes
	fov_done:
		pop eax
		popfd
		jmp continuation
	}
}
}

bool Install(HMODULE gameModule, float fovMultiplier, bool lockFov)
{
	// Check the exponent explicitly: MSVC builds with /fp:fast, where isfinite
	// and NaN comparisons alone are not a reliable configuration guard.
	uint32_t bits = 0;
	memcpy(&bits, &fovMultiplier, sizeof(bits));
	if ((bits & 0x7F800000) == 0x7F800000 || fovMultiplier < 0.5f || fovMultiplier > 2.0f)
	{
		LogWarn("FOVMultiplier must be a finite value from 0.5 through 2.0; FOV patch skipped.");
		return false;
	}
	if (!lockFov && fovMultiplier == 1.0f)
	{
		LogInfo("Camera FOV: game's own (multiplier 1.0).");
		return false;
	}
	if (!ac2_image::IsVerifiedSteamImage(gameModule))
	{
		LogWarn("FOV patch: this executable layout is not verified; other EaglePatch fixes remain enabled.");
		return false;
	}

	const uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);
	unsigned char* target = reinterpret_cast<unsigned char*>(base + 0xC71CB);
	static const unsigned char expected[] = {
		0xF3, 0x0F, 0x10, 0x49, 0x30, 0x8B, 0x51, 0x3C, 0x89, 0x81, 0xEC, 0x01, 0x00, 0x00,
		0xB8, 0x01, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x11, 0x89, 0x60, 0x01, 0x00, 0x00
	};
	// Verify the getter as well as the patched instruction: it owns the world
	// global and camera handle offsets used by the active-camera filter.
	unsigned char getter[] = {
		0xA1, 0, 0, 0, 0, 0x8B, 0x40, 0x34, 0x8B, 0x48, 0x08, 0x8B, 0x00, 0xC1, 0xF9, 0x1F, 0x23, 0xC1, 0xC3
	};
	const uintptr_t world = base + 0x1E10B84;
	memcpy(getter + 1, &world, sizeof(world));
	const void* cameraGetter = reinterpret_cast<const void*>(base + 0xC7360);
	if (!ac2_image::IsReadable(target, sizeof(expected)) || memcmp(target, expected, sizeof(expected)) != 0 ||
		!ac2_image::IsReadable(cameraGetter, sizeof(getter)) || memcmp(cameraGetter, getter, sizeof(getter)) != 0 ||
		!ac2_image::IsReadable(reinterpret_cast<const void*>(world), sizeof(uintptr_t)))
	{
		LogWarn("FOV patch: camera code changed or already hooked; skipped.");
		return false;
	}
	DWORD oldProtect = 0;
	if (!Unprotect_internal(target, 5, &oldProtect))
	{
		LogWarn("FOV patch: could not prepare code page; skipped.");
		return false;
	}
	detail::multiplier = fovMultiplier;
	detail::locked = lockFov;
	detail::worldPointer = world;
	detail::continuation = reinterpret_cast<uintptr_t>(target + 5);
	unsigned char jump[5] = {0xE9, 0, 0, 0, 0};
	const uint32_t displacement = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&detail::Hook) - reinterpret_cast<uintptr_t>(target + 5));
	memcpy(jump + 1, &displacement, sizeof(displacement));
	memcpy(target, jump, sizeof(jump));
	if (!Protect_internal(target, sizeof(jump), oldProtect))
	{
		memcpy(target, expected, sizeof(jump));
		Protect_internal(target, sizeof(jump), oldProtect);
		detail::continuation = 0;
		LogWarn("FOV patch: protection restore failed; original instruction restored.");
		return false;
	}
	LogInfo("FOVMultiplier: %.3f (active camera only; %s).", fovMultiplier, lockFov ? "fixed gameplay FOV" : "dynamic FOV");
	return true;
}

void LogSummary()
{
	if (detail::changes)
		LogInfo("Camera FOV input: %.4f -> %.4f.", detail::originalFov, detail::scaledFov);
}
}
