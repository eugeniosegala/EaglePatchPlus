#include "../EaglePatch/src/ac2_highlights.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits>
#include <initializer_list>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

struct Color { float red, green, blue, alpha; };
using DrawMask = void (__thiscall*)(void*, void*, unsigned int, const Color*, const Color*, void*);
static Color captured[8];
static Color unmasked = {0.2f, 0.3f, 0.4f, 0.5f};
static const Color* inputPalette = nullptr;
static unsigned int expectedMask = 0;
static unsigned int calls = 0;

static void __cdecl Capture(void* owner, void* texture, unsigned int mask,
	const Color* colors, const Color* unmaskedColor, void* context)
{
	CHECK(owner == reinterpret_cast<void*>(0x11223344));
	CHECK(texture == reinterpret_cast<void*>(0x55667788));
	CHECK(mask == expectedMask);
	CHECK(context == reinterpret_cast<void*>(0x12345678));
	CHECK(unmaskedColor == &unmasked);
	CHECK(colors != inputPalette);
	CHECK((reinterpret_cast<uintptr_t>(colors) & 15) == 0);
	memcpy(captured, colors, sizeof(captured));
	++calls;
}

int main()
{
	unsigned char* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2190000,
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!image)
		return 1;
	IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = sizeof(*dos);
	IMAGE_NT_HEADERS32* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	nt->Signature = IMAGE_NT_SIGNATURE;
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->FileHeader.TimeDateStamp = 0x5925BA62;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	nt->OptionalHeader.SizeOfImage = 0x2190000;
	HMODULE module = reinterpret_cast<HMODULE>(image);
	unsigned char* target = image + 0x12CAEE0;
	const unsigned char signature[] = {
		0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04,
		0x55, 0x8B, 0x6B, 0x04, 0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x6A, 0xFF
	};
	memcpy(target, signature, sizeof(signature));
	CHECK(!ac2_highlights::Install(module, false));
	CHECK(!ac2_highlights::Install(nullptr, true));
	nt->FileHeader.TimeDateStamp ^= 1;
	CHECK(!ac2_highlights::Install(module, true));
	nt->FileHeader.TimeDateStamp ^= 1;
	for (size_t index = 0; index < sizeof(signature); ++index)
	{
		target[index] ^= 1;
		CHECK(!ac2_highlights::Install(module, true));
		target[index] ^= 1;
	}
	CHECK(memcmp(target, signature, sizeof(signature)) == 0);
	CHECK(ac2_highlights::Install(module, true));
	CHECK(target[0] == 0xE9 && target[5] == 0x90);
	CHECK(memcmp(target + 6, signature + 6, sizeof(signature) - 6) == 0);
	CHECK(!ac2_highlights::Install(module, true));

	// Run the installed jump, production wrapper and relocated engine prologue.
	// The fixture after the prologue records all five native arguments, then
	// performs the engine's EBX/stack restoration and ret 20.
	unsigned char body[] = {
		0xFF, 0x73, 0x18, 0xFF, 0x73, 0x14, 0xFF, 0x73, 0x10,
		0xFF, 0x73, 0x0C, 0xFF, 0x73, 0x08, 0x51,
		0xE8, 0, 0, 0, 0, 0x83, 0xC4, 0x18, 0x8B, 0xE3, 0x5B, 0xC2, 0x14, 0x00
	};
	uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Capture) -
		reinterpret_cast<uintptr_t>(target + 6 + 21));
	memcpy(body + 17, &offset, sizeof(offset));
	memcpy(target + 6, body, sizeof(body));
	DWORD oldProtect;
	CHECK(VirtualProtect(target, 4096, PAGE_EXECUTE_READ, &oldProtect) != 0);
	FlushInstructionCache(GetCurrentProcess(), target, 4096);
	DrawMask draw = reinterpret_cast<DrawMask>(target);
	Color colors[8] = {
		{1, 1, 1, 1}, {0.4f, 0.4f, 0.4f, 0.7f}, {1, 0, 0, 1}, {0, 0, 1, 1},
		{1, 0.8f, 0, 1}, {0, 0, 0, 1}, {1, 1, 1, 0}, {-1, -1, -1, 1}
	};
	Color before[8];
	memcpy(before, colors, sizeof(colors));
	inputPalette = colors;
	const Color zero = {};
	for (unsigned int mask : {0u, 1u, 2u, 4u, 255u})
	{
		expectedMask = mask;
		draw(reinterpret_cast<void*>(0x11223344), reinterpret_cast<void*>(0x55667788),
			mask, colors, &unmasked, reinterpret_cast<void*>(0x12345678));
		CHECK(memcmp(colors, before, sizeof(colors)) == 0);
		for (unsigned int index = 0; index < 8; ++index)
		{
			bool removed = (index == 0 || index == 1 || index == 6) && (mask & (1u << index));
			CHECK(memcmp(&captured[index], removed ? &zero : &before[index], sizeof(Color)) == 0);
		}
	}
	colors[0].red = std::numeric_limits<float>::quiet_NaN();
	colors[1].red = std::numeric_limits<float>::infinity();
	expectedMask = 255;
	draw(reinterpret_cast<void*>(0x11223344), reinterpret_cast<void*>(0x55667788),
		255, colors, &unmasked, reinterpret_cast<void*>(0x12345678));
	CHECK(memcmp(captured, colors, 2 * sizeof(Color)) == 0);
	CHECK(calls == 6);
	VirtualFree(image, 0, MEM_RELEASE);
	if (failures)
		return 1;
	puts("Highlight tests passed: installed x86 trampoline, native arguments, palette isolation, color preservation and signature guards.");
	return 0;
}
