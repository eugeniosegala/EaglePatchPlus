#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits>

#include "../EaglePatch/src/ac2_shadow_cascades.h"
#include "../EaglePatch/shared/logger.h"

static int failures = 0;
#define CHECK(expression) Check((expression), #expression, __LINE__)
static void Check(bool passed, const char* expression, int line)
{
	if (!passed)
	{
		fprintf(stderr, "Line %d: %s\n", line, expression);
		++failures;
	}
}

static uint32_t engineStub[6] = {0, 0, 0, 0, 0x11223344, 0x55667788};
static uint32_t xmmSeed[8] = {0x10203040, 0x50607080, 0x90a0b0c0, 0xd0e0f001, 0x12345678, 0x87654321, 0xabcdef01, 0x10fedcba};
static uint32_t registers[9];
static uint32_t xmmSnapshot[8];
static uintptr_t savedEsp;
using ac2_shadows::detail::continuation;
using ac2_shadows::detail::Hook;
static void (*hookEntry)() = &Hook;

__declspec(naked) static void CaptureHook()
{
	__asm
	{
		mov registers, eax
		mov registers[4], esi
		mov registers[8], ebx
		mov registers[12], ecx
		mov registers[16], edx
		mov registers[20], edi
		mov registers[24], ebp
		mov registers[28], esp
		pushfd
		pop registers[32]
		movdqu xmmSnapshot, xmm0
		movdqu xmmSnapshot[16], xmm1
		popfd
		pop edi
		pop esi
		pop ebx
		pop ebp
		ret
	}
}

// Present the real hook with the engine's EBP-relative locals and EBX object.
// Record its output before any compiler-generated code can change registers.
__declspec(naked) static void RunHook(float* frame)
{
	__asm
	{
		push ebp
		push ebx
		push esi
		push edi
		pushfd
		mov ebp, [esp + 24]
		add ebp, 0x60
		lea ebx, engineStub
		mov ecx, 0x11335577
		mov edx, 0x22446688
		mov edi, 0x33557799
		mov esi, 0x88776655
		movdqu xmm0, xmmSeed
		movdqu xmm1, xmmSeed[16]
		mov savedEsp, esp
		push 0x246
		popfd
		jmp hookEntry
	}
}

static bool Near(float a, float b)
{
	return fabsf(a - b) < 0.001f;
}

static void TestHook(float first, float second, float third, float farDistance, float multiplier, bool shouldChange, float expectedFirst, float expectedSecond, float expectedThird)
{
	float frame[24];
	for (unsigned int i = 0; i < 24; ++i)
		frame[i] = 1234.0f + i;
	frame[3] = first;
	frame[2] = second;
	frame[1] = third;
	frame[0] = farDistance;
	float before[24];
	memcpy(before, frame, sizeof(frame));
	ac2_shadows::detail::calls = 0;
	ac2_shadows::detail::changes = 0;
	ac2_shadows::detail::multiplier = multiplier;
	continuation = reinterpret_cast<uintptr_t>(&CaptureHook);
	RunHook(frame);
	CHECK(ac2_shadows::detail::calls == 1);
	CHECK(ac2_shadows::detail::changes == (shouldChange ? 1u : 0u));
	CHECK(memcmp(frame + 4, before + 4, sizeof(frame) - 4 * sizeof(float)) == 0);
	CHECK(memcmp(frame, before, sizeof(float)) == 0);
	if (shouldChange)
	{
		CHECK(Near(frame[3], expectedFirst));
		CHECK(Near(frame[2], expectedSecond));
		CHECK(Near(frame[1], expectedThird));
		CHECK(frame[3] < frame[2] && frame[2] < frame[1] && frame[1] < frame[0]);
		CHECK(Near(ac2_shadows::detail::effectiveMultiplier, expectedFirst / first));
		const float original[4] = {first, second, third, farDistance};
		for (unsigned int i = 0; i < 4; ++i)
		{
			CHECK(ac2_shadows::detail::originalDistances[i] == original[i]);
			CHECK(ac2_shadows::detail::scaledDistances[i] == frame[3 - i]);
		}
	}
	else
		CHECK(memcmp(frame, before, sizeof(frame)) == 0);

	// EAX/ESI must reflect the displaced instructions; every other tested
	// register, status flag, XMM lane and the stack pointer must survive.
	CHECK(registers[0] == engineStub[4]);
	CHECK(registers[1] == engineStub[5]);
	CHECK(registers[2] == reinterpret_cast<uintptr_t>(engineStub));
	CHECK(registers[3] == 0x11335577);
	CHECK(registers[4] == 0x22446688);
	CHECK(registers[5] == 0x33557799);
	CHECK(registers[6] == reinterpret_cast<uintptr_t>(frame + 24));
	CHECK(registers[7] == savedEsp);
	CHECK((registers[8] & 0xCD5) == 0x44);
	CHECK(memcmp(xmmSeed, xmmSnapshot, sizeof(xmmSeed)) == 0);
}

static const unsigned char signature[] = {
	0x8B, 0x73, 0x14, 0x8B, 0x43, 0x10, 0x8D, 0x8E, 0xE0, 0x00, 0x00, 0x00,
	0x33, 0xFF, 0xF3, 0x0F, 0x11, 0x45, 0xD4, 0x89, 0x45, 0xD8, 0x89, 0x4D
};

static void TestInstallation()
{
	unsigned char* image = static_cast<unsigned char*>(VirtualAlloc(NULL, 0x2190000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	CHECK(image != NULL);
	if (!image)
		return;
	IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = sizeof(*dos);
	IMAGE_NT_HEADERS32* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	nt->Signature = IMAGE_NT_SIGNATURE;
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->FileHeader.TimeDateStamp = 0x5925BA62;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	nt->OptionalHeader.SizeOfImage = 0x2190000;
	unsigned char* target = image + 0x118C83E;
	memcpy(target, signature, sizeof(signature));
	HMODULE module = reinterpret_cast<HMODULE>(image);
	CHECK(!ac2_shadows::Install(NULL, 6));
	CHECK(!ac2_shadows::Install(module, 1));
	CHECK(!ac2_shadows::Install(module, 0));
	CHECK(!ac2_shadows::Install(module, 9));
	CHECK(!ac2_shadows::Install(module, UINT32_MAX));
	CHECK(memcmp(target, signature, sizeof(signature)) == 0);

	nt->FileHeader.TimeDateStamp ^= 1;
	CHECK(!ac2_shadows::Install(module, 6));
	nt->FileHeader.TimeDateStamp ^= 1;
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
	CHECK(!ac2_shadows::Install(module, 6));
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
	CHECK(!ac2_shadows::Install(module, 6));
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	nt->OptionalHeader.SizeOfImage -= 1;
	CHECK(!ac2_shadows::Install(module, 6));
	nt->OptionalHeader.SizeOfImage += 1;
	dos->e_lfanew = -1;
	CHECK(!ac2_shadows::Install(module, 6));
	dos->e_lfanew = sizeof(*dos);
	CHECK(memcmp(target, signature, sizeof(signature)) == 0);

	for (unsigned int i = 0; i < sizeof(signature); ++i)
	{
		target[i] ^= 1;
		unsigned char before[sizeof(signature)];
		memcpy(before, target, sizeof(before));
		CHECK(!ac2_shadows::Install(module, 6));
		CHECK(memcmp(target, before, sizeof(before)) == 0);
		target[i] ^= 1;
	}
	target[0] = 0xE9; // A standalone radius patch already owns the site.
	CHECK(!ac2_shadows::Install(module, 6));
	memcpy(target, signature, sizeof(signature));
	CHECK(ac2_shadows::Install(module, 8));
	CHECK(target[0] == 0xE9 && target[5] == 0x90);
	CHECK(memcmp(target + 6, signature + 6, sizeof(signature) - 6) == 0);
	uint32_t displacement = 0;
	memcpy(&displacement, target + 1, sizeof(displacement));
	CHECK(reinterpret_cast<uintptr_t>(target + 5) + displacement == reinterpret_cast<uintptr_t>(&Hook));
	CHECK(continuation == reinterpret_cast<uintptr_t>(target + 6));
	CHECK(ac2_shadows::detail::multiplier == 8.0f);
	MEMORY_BASIC_INFORMATION memory = {};
	CHECK(VirtualQuery(target, &memory, sizeof(memory)) == sizeof(memory));
	CHECK(memory.Protect == PAGE_READWRITE);
	CHECK(!ac2_shadows::Install(module, 3));
	CHECK(ac2_shadows::detail::multiplier == 8.0f);
	VirtualFree(image, 0, MEM_RELEASE);
}

int main()
{
	InitLogging(NULL, false, false);
	TestHook(4, 16, 64, 400, 3, true, 12, 48, 192);
	TestHook(4, 16, 64, 400, 6, true, 24, 96, 384);
	TestHook(4, 16, 64, 400, 8, true, 32, 128, 392);
	TestHook(8, 80, 90, 400, 8, true, 64, 384.16f, 392);
	TestHook(390, 391, 392, 400, 8, false, 0, 0, 0);
	TestHook(8, 30, 150, 400, 6, true, 48, 180, 392);
	TestHook(4, 16, 64, 400, 1, false, 0, 0, 0);
	TestHook(0, 16, 64, 400, 6, false, 0, 0, 0);
	TestHook(-4, 16, 64, 400, 6, false, 0, 0, 0);
	TestHook(4, 64, 16, 400, 6, false, 0, 0, 0);
	TestHook(4, 16, 399, 400, 6, false, 0, 0, 0);
	TestHook(std::numeric_limits<float>::quiet_NaN(), 16, 64, 400, 6, false, 0, 0, 0);
	TestHook(4, 16, 64, std::numeric_limits<float>::infinity(), 6, false, 0, 0, 0);
	TestInstallation();
	ShutdownLogging();
	if (failures)
		return 1;
	puts("Shadow cascade tests passed: real x86 hook, split ordering, register preservation and installation guards.");
	return 0;
}
