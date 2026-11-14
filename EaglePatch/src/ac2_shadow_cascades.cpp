#include "ac2_shadow_cascades.h"
#include "ac2_image.h"

#include <string.h>

#include "patcher.h"
#include "../shared/logger.h"

namespace ac2_shadows
{
namespace detail
{
uintptr_t continuation = 0;
float multiplier = 1.0f;
volatile uint32_t calls = 0;
volatile uint32_t changes = 0;
float originalDistances[4] = {};
float scaledDistances[4] = {};
float effectiveMultiplier = 1.0f;
static float zero = 0.0f;
static float one = 1.0f;
static float maxFar = 1000000.0f;
static float farFraction = 0.98f;

// At this point the engine has read the map's split distances into its frame.
// The same locals subsequently build all four projections/caster frusta and
// g_ShadowCascadesRangesMin/Max. Changing only the uploaded shader ranges would
// sample the wrong projections. The outer split and light depth range stay stock.
NAKED void Hook()
{
	__asm
	{
		pushfd
		pushad
		sub esp, 48
		movdqu [esp], xmm0
		movdqu [esp + 16], xmm1
		inc calls

		// Refuse unordered, nonpositive, NaN or unbounded map data.
		movss xmm0, [ebp - 0x54]
		ucomiss xmm0, zero
		jbe hook_done
		movss xmm1, [ebp - 0x58]
		ucomiss xmm1, xmm0
		jbe hook_done
		movss xmm0, [ebp - 0x5c]
		ucomiss xmm0, xmm1
		jbe hook_done
		movss xmm1, [ebp - 0x60]
		ucomiss xmm1, xmm0
		jbe hook_done
		ucomiss xmm1, maxFar
		ja hook_done

		movss xmm1, multiplier
		ucomiss xmm1, one
		jbe hook_done

		// Cap each split separately, preserving the outer limit and room
		// between cascades without limiting the nearby split to the far one.
		movss xmm0, [ebp - 0x60]
		mulss xmm0, farFraction
		movss xmm1, [ebp - 0x5c]
		mulss xmm1, multiplier
		minss xmm1, xmm0
		ucomiss xmm1, [ebp - 0x5c]
		jb hook_done
		movss [esp + 40], xmm1

		mulss xmm1, farFraction
		movss xmm0, [ebp - 0x58]
		mulss xmm0, multiplier
		minss xmm0, xmm1
		ucomiss xmm0, [ebp - 0x58]
		jb hook_done
		movss [esp + 36], xmm0

		mulss xmm0, farFraction
		movss xmm1, [ebp - 0x54]
		mulss xmm1, multiplier
		minss xmm1, xmm0
		ucomiss xmm1, [ebp - 0x54]
		jbe hook_done
		movss [esp + 32], xmm1
		divss xmm1, [ebp - 0x54]

		cmp changes, 0
		jne hook_scale
		movss effectiveMultiplier, xmm1
		movss xmm0, [ebp - 0x54]
		movss originalDistances, xmm0
		movss xmm0, [ebp - 0x58]
		movss originalDistances[4], xmm0
		movss xmm0, [ebp - 0x5c]
		movss originalDistances[8], xmm0
		movss xmm0, [ebp - 0x60]
		movss originalDistances[12], xmm0

	hook_scale:
		movss xmm0, [esp + 32]
		movss [ebp - 0x54], xmm0
		movss xmm0, [esp + 36]
		movss [ebp - 0x58], xmm0
		movss xmm0, [esp + 40]
		movss [ebp - 0x5c], xmm0

		cmp changes, 0
		jne hook_count
		movss xmm0, [ebp - 0x54]
		movss scaledDistances, xmm0
		movss xmm0, [ebp - 0x58]
		movss scaledDistances[4], xmm0
		movss xmm0, [ebp - 0x5c]
		movss scaledDistances[8], xmm0
		movss xmm0, [ebp - 0x60]
		movss scaledDistances[12], xmm0
	hook_count:
		inc changes
	hook_done:
		movdqu xmm0, [esp]
		movdqu xmm1, [esp + 16]
		add esp, 48
		popad
		popfd

		// Replay the two displaced instructions, without clobbering a register
		// to branch back to the engine.
		mov esi, [ebx + 0x14]
		mov eax, [ebx + 0x10]
		jmp continuation
	}
}
}

bool Install(HMODULE gameModule, unsigned int distanceMultiplier)
{
	if (distanceMultiplier == 1)
	{
		LogInfo("Shadow cascade distances: game's own (multiplier 1).");
		return false;
	}
	if (distanceMultiplier < 2 || distanceMultiplier > 8)
	{
		LogWarn("ShadowCascadeDistanceMultiplier must be 1 through 8; cascade patch skipped.");
		return false;
	}

	if (!ac2_image::IsVerifiedSteamImage(gameModule))
	{
		LogWarn("Shadow cascade patch: this executable layout is not verified; other EaglePatch fixes remain enabled.");
		return false;
	}
	const uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);

	// Current Steam executable (Digital UPlay layout), inspected after Steam's
	// loader has initialized it. No unverified Retail address or stack layout.
	unsigned char* target = reinterpret_cast<unsigned char*>(base + 0x118C83E);
	static const unsigned char expected[] = {
		0x8B, 0x73, 0x14, 0x8B, 0x43, 0x10, 0x8D, 0x8E, 0xE0, 0x00, 0x00, 0x00,
		0x33, 0xFF, 0xF3, 0x0F, 0x11, 0x45, 0xD4, 0x89, 0x45, 0xD8, 0x89, 0x4D
	};
	if (!ac2_image::IsReadable(target, sizeof(expected)) || memcmp(target, expected, sizeof(expected)) != 0)
	{
		LogWarn("Shadow cascade patch: code signature changed or already hooked; skipped. Remove AC2ShadowRadius.asi if installed.");
		return false;
	}

	DWORD oldProtect = 0;
	if (!Unprotect_internal(target, 6, &oldProtect))
	{
		LogWarn("Shadow cascade patch: could not prepare code page; skipped.");
		return false;
	}
	detail::multiplier = static_cast<float>(distanceMultiplier);
	detail::continuation = reinterpret_cast<uintptr_t>(target + 6);
	unsigned char jump[6] = {0xE9, 0, 0, 0, 0, 0x90};
	const uint32_t displacement = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&detail::Hook) - reinterpret_cast<uintptr_t>(target + 5));
	memcpy(jump + 1, &displacement, sizeof(displacement));
	memcpy(target, jump, sizeof(jump));
	if (!Protect_internal(target, sizeof(jump), oldProtect))
	{
		memcpy(target, expected, sizeof(jump));
		Protect_internal(target, sizeof(jump), oldProtect);
		detail::continuation = 0;
		LogWarn("Shadow cascade patch: protection restore failed; original instructions restored.");
		return false;
	}
	LogInfo("Shadow cascade distance multiplier: %ux (verified layout and code signature; outer shadow limit preserved).", distanceMultiplier);
	return true;
}

void LogSummary()
{
	if (!detail::changes)
		return;
	LogInfo("Shadow cascade distances: %.3f / %.3f / %.3f / %.3f -> %.3f / %.3f / %.3f / %.3f (nearest cascade multiplier %.3f).",
		detail::originalDistances[0], detail::originalDistances[1], detail::originalDistances[2], detail::originalDistances[3],
		detail::scaledDistances[0], detail::scaledDistances[1], detail::scaledDistances[2], detail::scaledDistances[3], detail::effectiveMultiplier);
}
}
