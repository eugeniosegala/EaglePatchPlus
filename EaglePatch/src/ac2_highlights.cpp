#include "ac2_highlights.h"
#include "ac2_highlight_shader.h"
#include "ac2_image.h"

#include "patcher.h"
#include "../shared/logger.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace ac2_highlights
{
namespace
{
using CreatePixelShader = HRESULT (WINAPI*)(void*, const DWORD*, void**);
volatile LONG createdShaders = 0;
volatile LONG targetingShaders = 0;
volatile LONG patchedShaders = 0;
volatile LONG failedShaders = 0;

bool PowerOfTwo(LONG value)
{
	return value > 0 && (value & (value - 1)) == 0;
}

void Report(const char* event)
{
	LogInfo("DisableWhiteHighlights: %s (%ld pixel shaders created, %ld with the targeting rim, %ld patched).", event,
		createdShaders, targetingShaders, patchedShaders);
}

// Copies the caller's bytecode into aligned words. The engine's buffers need not
// be 4-byte aligned; the parse never reads past the committed region.
bool ReadBytecode(const DWORD* bytecode, std::vector<uint32_t>& words)
{
	words.clear();
	MEMORY_BASIC_INFORMATION memory = {};
	if (!bytecode || VirtualQuery(bytecode, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
		(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return false;
	const uintptr_t end = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
	size_t bytes = end - reinterpret_cast<uintptr_t>(bytecode);
	if (bytes > 65536 * sizeof(uint32_t))
		bytes = 65536 * sizeof(uint32_t);
	const size_t length = shader::Length(reinterpret_cast<const unsigned char*>(bytecode), bytes);
	if (!length)
		return false;
	words.resize(length);
	memcpy(words.data(), bytecode, length * sizeof(uint32_t));
	return true;
}

HRESULT WINAPI CreateWithoutTargetingRim(void* device, const DWORD* bytecode, void** pixelShader)
{
	const auto create = reinterpret_cast<CreatePixelShader>((*static_cast<void***>(device))[106]);
	std::vector<uint32_t> replacement;
	bool targeting = false;
	try
	{
		std::vector<uint32_t> words;
		if (ReadBytecode(bytecode, words))
		{
			targeting = shader::HasTargeting(words.data(), words.size());
			if (targeting)
				shader::Rewrite(words.data(), words.size(), replacement);
		}
	}
	catch (...)
	{
		replacement.clear();
	}
	const LONG created = InterlockedIncrement(&createdShaders);
	if (targeting)
	{
		const LONG found = InterlockedIncrement(&targetingShaders);
		if (replacement.empty())
		{
			if (InterlockedIncrement(&failedShaders) <= 4)
				LogWarn("DisableWhiteHighlights: a targeting-rim shader could not be rewritten; using the original.");
		}
		else
		{
			const HRESULT result = create(device, reinterpret_cast<const DWORD*>(replacement.data()), pixelShader);
			if (SUCCEEDED(result))
			{
				const LONG patched = InterlockedIncrement(&patchedShaders);
				if (patched <= 2 || PowerOfTwo(patched))
					Report("targeting-rim shader patched");
				return result;
			}
			LogWarn("DisableWhiteHighlights: modified shader rejected (0x%08lX); using the original.", static_cast<unsigned long>(result));
		}
		if (found <= 2)
			Report("targeting-rim shader left unchanged");
	}
	else if (created >= 64 && PowerOfTwo(created))
		Report("progress");
	return create(device, bytecode, pixelShader);
}

// Stencil highlight palette (glow and blurred-outline masks). Each of the eight
// stencil channels has a color; the pass draws it where objects wrote that bit.
struct Color
{
	float red, green, blue, alpha;
};

using DrawMask = void (__thiscall*)(void*, void*, unsigned int, const Color*, const Color*, void*);
DrawMask originalDrawMask = nullptr;
CRITICAL_SECTION paletteLock;
struct SeenPalette
{
	void* caller;
	unsigned int mask;
	short colors[9][4];
};
SeenPalette seenPalettes[32];
unsigned int seenCount = 0;

bool IsFinite(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return (bits & 0x7F800000u) != 0x7F800000u;
}

float Larger(float a, float b) { return a > b ? a : b; }
float Smaller(float a, float b) { return a < b ? a : b; }

void LogPalette(void* caller, unsigned int mask, const Color* colors, const Color& unmasked, unsigned int neutral)
{
	mask &= 0xFF;
	SeenPalette entry = {caller, mask, {}};
	for (unsigned int index = 0; index < 9; ++index)
	{
		const Color& color = index < 8 ? colors[index] : unmasked;
		const float values[4] = {color.red, color.green, color.blue, color.alpha};
		for (unsigned int channel = 0; channel < 4; ++channel)
		{
			const float value = IsFinite(values[channel]) ? values[channel] : -99.0f;
			entry.colors[index][channel] = (index < 8 && !(mask & (1u << index))) ? 0 :
				static_cast<short>(Larger(-32000.0f, Smaller(32000.0f, value * 100.0f)));
		}
	}
	EnterCriticalSection(&paletteLock);
	bool known = false;
	for (unsigned int index = 0; index < seenCount && !known; ++index)
		known = memcmp(&seenPalettes[index], &entry, sizeof(entry)) == 0;
	const bool report = !known && seenCount < sizeof(seenPalettes) / sizeof(seenPalettes[0]);
	if (report)
		seenPalettes[seenCount++] = entry;
	LeaveCriticalSection(&paletteLock);
	if (!report)
		return;
	char text[900];
	int used = _snprintf(text, sizeof(text), "DisableWhiteHighlights: highlight palette from %p, channels 0x%02X:", caller, mask);
	for (unsigned int index = 0; index < 9 && used > 0 && used < static_cast<int>(sizeof(text)); ++index)
	{
		if (index < 8 && !(mask & (1u << index)))
			continue;
		const short* value = entry.colors[index];
		const int written = _snprintf(text + used, sizeof(text) - used, " %s%u=(%.2f %.2f %.2f %.2f)",
			index < 8 ? "ch" : "unmasked", index < 8 ? index : 0u,
			value[0] / 100.0, value[1] / 100.0, value[2] / 100.0, value[3] / 100.0);
		used = written < 0 ? -1 : used + written;
	}
	text[sizeof(text) - 1] = 0;
	LogInfo("%s; neutralised 0x%02X.", text, neutral);
}

void __fastcall DrawWithoutNeutralHighlights(void* owner, void*, void* texture,
	unsigned int mask, const Color* colors, const Color* unmaskedColor, void* context)
{
#ifdef _MSC_VER
	void* caller = _ReturnAddress();
#else
	void* caller = __builtin_return_address(0);
#endif
	if (!colors || !unmaskedColor)
	{
		originalDrawMask(owner, texture, mask, colors, unmaskedColor, context);
		return;
	}
	__declspec(align(16)) Color filtered[8];
	memcpy(filtered, colors, sizeof(filtered));
	unsigned int neutral = 0;
	for (unsigned int index = 0; index < 8; ++index)
	{
		if ((mask & (1u << index)) && shader::IsNeutralHighlight(filtered[index].red, filtered[index].green,
			filtered[index].blue, unmaskedColor->red, unmaskedColor->green, unmaskedColor->blue))
		{
			// Match the untouched background exactly, whether the mask is additive
			// (black) or an overlay around grey, so the channel draws nothing.
			filtered[index] = *unmaskedColor;
			neutral |= 1u << index;
		}
	}
	LogPalette(caller, mask, colors, *unmaskedColor, neutral);
	originalDrawMask(owner, texture, mask, filtered, unmaskedColor, context);
}

bool InstallPaletteFilter(unsigned char* image)
{
	unsigned char* target = image + 0x12CAEE0;
	static const unsigned char expected[] = {
		0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04,
		0x55, 0x8B, 0x6B, 0x04, 0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x6A, 0xFF
	};
	if (!ac2_image::IsReadable(target, sizeof(expected)) || memcmp(target, expected, sizeof(expected)) != 0)
	{
		LogWarn("DisableWhiteHighlights: highlight palette code changed or already hooked; palette filter skipped.");
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
	InitializeCriticalSection(&paletteLock);
	originalDrawMask = reinterpret_cast<DrawMask>(trampoline);
	unsigned char jump[displaced] = {0xE9, 0, 0, 0, 0, 0x90};
	offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&DrawWithoutNeutralHighlights) -
		reinterpret_cast<uintptr_t>(target + 5));
	memcpy(jump + 1, &offset, sizeof(offset));
	memcpy(target, jump, sizeof(jump));
	if (!Protect_internal(target, displaced, oldProtect))
	{
		memcpy(target, expected, displaced);
		Protect_internal(target, displaced, oldProtect);
		originalDrawMask = nullptr;
		VirtualFree(trampoline, 0, MEM_RELEASE);
		LogWarn("DisableWhiteHighlights: palette protection restore failed; original code restored.");
		return false;
	}
	return true;
}

bool InstallShaderFilter(unsigned char* image)
{
	// The engine's sole CreatePixelShader call: preserve its native allocation,
	// ownership and COM ABI, substituting only the function loaded into EAX.
	unsigned char* target = image + 0x11DED3B;
	static const unsigned char expected[] = {
		0x8B, 0x81, 0xA8, 0x01, 0x00, 0x00, 0xFF, 0xD0, 0x85, 0xF6, 0x8B, 0x45, 0x08
	};
	if (!ac2_image::IsReadable(target, sizeof(expected)) || memcmp(target, expected, sizeof(expected)) != 0)
	{
		LogWarn("DisableWhiteHighlights: pixel shader creation code changed or already hooked; shader filter skipped.");
		return false;
	}
	unsigned char replacement[] = {0xB8, 0, 0, 0, 0, 0x90};
	const uint32_t function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&CreateWithoutTargetingRim));
	memcpy(replacement + 1, &function, sizeof(function));
	DWORD oldProtect = 0;
	if (!Unprotect_internal(target, sizeof(replacement), &oldProtect))
		return false;
	memcpy(target, replacement, sizeof(replacement));
	if (!Protect_internal(target, sizeof(replacement), oldProtect))
	{
		memcpy(target, expected, sizeof(replacement));
		Protect_internal(target, sizeof(replacement), oldProtect);
		LogWarn("DisableWhiteHighlights: shader protection restore failed; original code restored.");
		return false;
	}
	return true;
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
	unsigned char* image = reinterpret_cast<unsigned char*>(gameModule);
	const bool shaders = InstallShaderFilter(image);
	const bool palette = InstallPaletteFilter(image);
	if (!shaders && !palette)
		return false;
	LogInfo("DisableWhiteHighlights: enabled (targeting-rim shaders %s; white/silver highlight palette %s).",
		shaders ? "on" : "off", palette ? "on" : "off");
	return true;
}
}
