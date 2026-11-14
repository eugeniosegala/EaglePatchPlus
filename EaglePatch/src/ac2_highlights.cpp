#include "ac2_highlights.h"
#include "ac2_image.h"

#include <algorithm>
#include <stdint.h>
#include <string.h>

#include "patcher.h"
#include "../shared/logger.h"

namespace ac2_highlights
{
namespace
{
struct Color
{
	float red, green, blue, alpha;
};

using DrawMask = void (__thiscall*)(void*, void*, unsigned int, const Color*, const Color*, void*);
DrawMask originalDrawMask = nullptr;

bool IsFinite(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return (bits & 0x7F800000u) != 0x7F800000u;
}

bool IsWhite(const Color& color)
{
	if (!IsFinite(color.red) || !IsFinite(color.green) || !IsFinite(color.blue))
		return false;
	const float maximum = std::max(color.red, std::max(color.green, color.blue));
	const float minimum = std::min(color.red, std::min(color.green, color.blue));
	return maximum > 0.0f && minimum >= maximum * 0.9f;
}

void __fastcall DrawWithoutWhiteHighlights(void* owner, void*, void* texture,
	unsigned int mask, const Color* colors, const Color* unmaskedColor, void* context)
{
	if (!colors)
	{
		originalDrawMask(owner, texture, mask, colors, unmaskedColor, context);
		return;
	}
	__declspec(align(16)) Color filtered[8];
	memcpy(filtered, colors, sizeof(filtered));
	for (unsigned int index = 0; index < 8; ++index)
	{
		if ((mask & (1u << index)) && IsWhite(filtered[index]))
			filtered[index] = {};
	}
	// Both glow and blurred outlines use this palette. Keep the native clear,
	// draw and resource transitions running so old masks cannot persist on screen.
	originalDrawMask(owner, texture, mask, filtered, unmaskedColor, context);
}
}

bool Install(HMODULE gameModule, bool disableWhiteHighlights)
{
	if (!disableWhiteHighlights)
		return false;
	if (!ac2_image::IsVerifiedSteamImage(gameModule))
	{
		LogWarn("White highlight removal: this executable layout is not verified; skipped.");
		return false;
	}
	unsigned char* target = reinterpret_cast<unsigned char*>(gameModule) + 0x12CAEE0;
	static const unsigned char expected[] = {
		0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04,
		0x55, 0x8B, 0x6B, 0x04, 0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x6A, 0xFF
	};
	if (!ac2_image::IsReadable(target, sizeof(expected)) || memcmp(target, expected, sizeof(expected)) != 0)
	{
		LogWarn("White highlight removal: mask renderer code changed or already hooked; skipped.");
		return false;
	}
	const size_t displaced = 6;
	unsigned char* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, displaced + 5,
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!trampoline)
		return false;
	memcpy(trampoline, target, displaced);
	trampoline[displaced] = 0xE9;
	uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target + displaced) -
		reinterpret_cast<uintptr_t>(trampoline + displaced + 5));
	memcpy(trampoline + displaced + 1, &offset, sizeof(offset));
	DWORD oldProtect = 0;
	if (!VirtualProtect(trampoline, displaced + 5, PAGE_EXECUTE_READ, &oldProtect))
	{
		VirtualFree(trampoline, 0, MEM_RELEASE);
		return false;
	}
	FlushInstructionCache(GetCurrentProcess(), trampoline, displaced + 5);
	if (!Unprotect_internal(target, displaced, &oldProtect))
	{
		VirtualFree(trampoline, 0, MEM_RELEASE);
		return false;
	}
	originalDrawMask = reinterpret_cast<DrawMask>(trampoline);
	unsigned char jump[displaced] = {0xE9, 0, 0, 0, 0, 0x90};
	offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&DrawWithoutWhiteHighlights) -
		reinterpret_cast<uintptr_t>(target + 5));
	memcpy(jump + 1, &offset, sizeof(offset));
	memcpy(target, jump, sizeof(jump));
	if (!Protect_internal(target, displaced, oldProtect))
	{
		memcpy(target, expected, displaced);
		Protect_internal(target, displaced, oldProtect);
		originalDrawMask = nullptr;
		VirtualFree(trampoline, 0, MEM_RELEASE);
		LogWarn("White highlight removal: protection restore failed; original code restored.");
		return false;
	}
	LogInfo("DisableWhiteHighlights: enabled (white/grey glow and outline palettes; matching object highlights included).");
	return true;
}
}
