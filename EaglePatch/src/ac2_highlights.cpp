#include "ac2_highlights.h"
#include "ac2_highlight_shader.h"
#include "ac2_image.h"

#include "patcher.h"
#include "../shared/logger.h"

namespace ac2_highlights
{
namespace
{
using CreatePixelShader = HRESULT (WINAPI*)(void*, const DWORD*, void**);
volatile LONG patchedShaders = 0;

HRESULT WINAPI CreateWithoutTargetingRim(void* device, const DWORD* bytecode, void** pixelShader)
{
	const auto create = reinterpret_cast<CreatePixelShader>((*static_cast<void***>(device))[106]);
	std::vector<uint32_t> replacement;
	try
	{
		MEMORY_BASIC_INFORMATION memory = {};
		if (bytecode && (reinterpret_cast<uintptr_t>(bytecode) & 3) == 0 &&
			VirtualQuery(bytecode, &memory, sizeof(memory)) == sizeof(memory) && memory.State == MEM_COMMIT &&
			!(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		{
			const uintptr_t end = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
			const size_t readableWords = (end - reinterpret_cast<uintptr_t>(bytecode)) / sizeof(uint32_t);
			const size_t limit = readableWords < 65536 ? readableWords : 65536;
			shader::Rewrite(reinterpret_cast<const uint32_t*>(bytecode), limit, replacement);
		}
	}
	catch (...)
	{
		replacement.clear();
	}
	if (!replacement.empty())
	{
		const HRESULT result = create(device, reinterpret_cast<const DWORD*>(replacement.data()), pixelShader);
		if (SUCCEEDED(result))
		{
			if (InterlockedIncrement(&patchedShaders) == 1)
				LogInfo("DisableWhiteHighlights: character targeting-rim shader patched successfully.");
			return result;
		}
		LogWarn("DisableWhiteHighlights: modified shader rejected (0x%08lX); using original.", static_cast<unsigned long>(result));
	}
	return create(device, bytecode, pixelShader);
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
	// The engine's sole CreatePixelShader call: preserve its native allocation,
	// ownership and COM ABI, substituting only the function loaded into EAX.
	unsigned char* target = reinterpret_cast<unsigned char*>(gameModule) + 0x11DED3B;
	static const unsigned char expected[] = {
		0x8B, 0x81, 0xA8, 0x01, 0x00, 0x00, 0xFF, 0xD0, 0x85, 0xF6, 0x8B, 0x45, 0x08
	};
	if (!ac2_image::IsReadable(target, sizeof(expected)) || memcmp(target, expected, sizeof(expected)) != 0)
	{
		LogWarn("White highlight removal: pixel shader creation code changed or already hooked; skipped.");
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
		LogWarn("White highlight removal: protection restore failed; original code restored.");
		return false;
	}
	LogInfo("DisableWhiteHighlights: enabled (character targeting-rim intensity; shader creation only).");
	return true;
}
}
