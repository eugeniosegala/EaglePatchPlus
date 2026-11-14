#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits>

#include "../EaglePatch/src/ac2_fov.h"
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

static uint32_t registers[9];
static uint32_t xmmSeed[32];
static uint32_t xmmSnapshot[32];
static uintptr_t savedEsp;
static uintptr_t world[14];
static uintptr_t handle[3];
static uintptr_t worldAddress;

__declspec(naked) static void ReturnFromHook()
{
	__asm { ret }
}

__declspec(naked) static void CaptureHook()
{
	__asm
	{
		mov registers, eax
		mov registers[4], ebx
		mov registers[8], ecx
		mov registers[12], edx
		mov registers[16], esi
		mov registers[20], edi
		mov registers[24], ebp
		mov registers[28], esp
		pushfd
		pop registers[32]
		movdqu xmmSnapshot, xmm0
		movdqu xmmSnapshot[16], xmm1
		movdqu xmmSnapshot[32], xmm2
		movdqu xmmSnapshot[48], xmm3
		movdqu xmmSnapshot[64], xmm4
		movdqu xmmSnapshot[80], xmm5
		movdqu xmmSnapshot[96], xmm6
		movdqu xmmSnapshot[112], xmm7
		popfd
		pop edi
		pop esi
		pop ebx
		pop ebp
		ret
	}
}

static uintptr_t hookEntry;
static void (*captureEntry)() = &CaptureHook;

__declspec(naked) static void RunCamera(void* camera)
{
	__asm
	{
		push ebp
		push ebx
		push esi
		push edi
		pushfd
		mov ecx, [esp + 24]
		mov eax, 0x10203040
		mov ebx, 0x11223344
		mov edx, 0x22334455
		mov esi, 0x33445566
		mov edi, 0x44556677
		mov ebp, 0x55667788
		movdqu xmm0, xmmSeed
		movdqu xmm1, xmmSeed[16]
		movdqu xmm2, xmmSeed[32]
		movdqu xmm3, xmmSeed[48]
		movdqu xmm4, xmmSeed[64]
		movdqu xmm5, xmmSeed[80]
		movdqu xmm6, xmmSeed[96]
		movdqu xmm7, xmmSeed[112]
		mov savedEsp, esp
		push 0x246
		popfd
		call hookEntry
		jmp captureEntry
	}
}

static uint32_t Bits(float value)
{
	uint32_t bits = 0;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void PrepareWorld(uint32_t* camera)
{
	memset(world, 0, sizeof(world));
	worldAddress = reinterpret_cast<uintptr_t>(world);
	world[0x34 / 4] = reinterpret_cast<uintptr_t>(handle);
	handle[0] = reinterpret_cast<uintptr_t>(camera);
	handle[1] = 0;
	handle[2] = 0x80000000;
}

static void CheckRegisters(void* camera, float expected, bool installed)
{
	CHECK(registers[0] == (installed ? 1u : 0x10203040));
	CHECK(registers[1] == 0x11223344);
	CHECK(registers[2] == reinterpret_cast<uintptr_t>(camera));
	CHECK(registers[3] == (installed ? static_cast<uint32_t*>(camera)[0x3C / 4] : 0x22334455));
	CHECK(registers[4] == 0x33445566);
	CHECK(registers[5] == 0x44556677);
	CHECK(registers[6] == 0x55667788);
	CHECK(registers[7] == savedEsp);
	CHECK((registers[8] & 0xCD5) == 0x44);
	for (unsigned int i = 0; i < 32; ++i)
	{
		if (i == 4)
			CHECK(xmmSnapshot[i] == Bits(expected));
		else if (i > 4 && i < 8)
			CHECK(xmmSnapshot[i] == 0); // Displaced MOVSS clears the upper lanes.
		else
			CHECK(xmmSnapshot[i] == xmmSeed[i]);
	}
}

enum CameraState { Active, OtherCamera, NullWorld, NullHandle, InvalidHandle };

static void TestHook(float input, float multiplier, float expected, bool changed, CameraState state = Active)
{
	uint32_t camera[128];
	for (unsigned int i = 0; i < 128; ++i)
		camera[i] = 0x12340000 + i;
	camera[0x30 / 4] = Bits(input);
	uint32_t before[128];
	memcpy(before, camera, sizeof(camera));
	PrepareWorld(camera);
	if (state == OtherCamera)
		handle[0] += 16;
	else if (state == NullWorld)
		worldAddress = 0;
	else if (state == NullHandle)
		world[0x34 / 4] = 0;
	else if (state == InvalidHandle)
		handle[2] = 0x40000000;
	ac2_fov::detail::worldPointer = reinterpret_cast<uintptr_t>(&worldAddress);
	ac2_fov::detail::continuation = reinterpret_cast<uintptr_t>(&ReturnFromHook);
	ac2_fov::detail::multiplier = multiplier;
	ac2_fov::detail::changes = 0;
	hookEntry = reinterpret_cast<uintptr_t>(&ac2_fov::detail::Hook);
	for (unsigned int repeat = 1; repeat <= 2; ++repeat)
	{
		RunCamera(camera);
		CheckRegisters(camera, expected, false);
		CHECK(memcmp(camera, before, sizeof(camera)) == 0);
		CHECK(ac2_fov::detail::changes == (changed ? repeat : 0));
	}
	if (changed)
	{
		CHECK(ac2_fov::detail::originalFov == input);
		CHECK(ac2_fov::detail::scaledFov == expected);
	}
}

static const unsigned char signature[] = {
	0xF3, 0x0F, 0x10, 0x49, 0x30, 0x8B, 0x51, 0x3C, 0x89, 0x81, 0xEC, 0x01, 0x00, 0x00,
	0xB8, 0x01, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x11, 0x89, 0x60, 0x01, 0x00, 0x00
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
	unsigned char* target = image + 0xC71CB;
	memcpy(target, signature, sizeof(signature));
	target[sizeof(signature)] = 0xC3;
	unsigned char getter[] = {0xA1, 0, 0, 0, 0, 0x8B, 0x40, 0x34, 0x8B, 0x48, 0x08, 0x8B, 0x00, 0xC1, 0xF9, 0x1F, 0x23, 0xC1, 0xC3};
	const uintptr_t worldSlot = reinterpret_cast<uintptr_t>(image + 0x1E10B84);
	memcpy(getter + 1, &worldSlot, sizeof(worldSlot));
	unsigned char* reader = image + 0xC7360;
	memcpy(reader, getter, sizeof(getter));
	HMODULE module = reinterpret_cast<HMODULE>(image);
	CHECK(!ac2_fov::Install(NULL, 1.2f));
	CHECK(!ac2_fov::Install(module, 1.0f));
	CHECK(!ac2_fov::Install(module, 0.0f));
	CHECK(!ac2_fov::Install(module, -1.0f));
	CHECK(!ac2_fov::Install(module, 0.49f));
	CHECK(!ac2_fov::Install(module, 2.01f));
	CHECK(!ac2_fov::Install(module, std::numeric_limits<float>::quiet_NaN()));
	CHECK(!ac2_fov::Install(module, std::numeric_limits<float>::infinity()));
	CHECK(!ac2_fov::Install(module, -std::numeric_limits<float>::infinity()));
	nt->FileHeader.TimeDateStamp ^= 1;
	CHECK(!ac2_fov::Install(module, 1.2f));
	nt->FileHeader.TimeDateStamp ^= 1;
	CHECK(memcmp(target, signature, sizeof(signature)) == 0);
	for (unsigned int i = 0; i < sizeof(signature); ++i)
	{
		target[i] ^= 1;
		CHECK(!ac2_fov::Install(module, 1.2f));
		CHECK(target[i] == static_cast<unsigned char>(signature[i] ^ 1));
		target[i] ^= 1;
	}
	for (unsigned int i = 0; i < sizeof(getter); ++i)
	{
		reader[i] ^= 1;
		CHECK(!ac2_fov::Install(module, 1.2f));
		CHECK(memcmp(target, signature, sizeof(signature)) == 0);
		reader[i] ^= 1;
	}
	target[0] = 0xE9;
	CHECK(!ac2_fov::Install(module, 1.2f));
	memcpy(target, signature, sizeof(signature));
	DWORD oldProtect = 0;
	CHECK(VirtualProtect(target, 5, PAGE_EXECUTE_READ, &oldProtect) != 0);
	CHECK(ac2_fov::Install(module, 1.2f));
	CHECK(target[0] == 0xE9);
	CHECK(memcmp(target + 5, signature + 5, sizeof(signature) - 5) == 0);
	uint32_t displacement = 0;
	memcpy(&displacement, target + 1, sizeof(displacement));
	CHECK(reinterpret_cast<uintptr_t>(target + 5) + displacement == reinterpret_cast<uintptr_t>(&ac2_fov::detail::Hook));
	CHECK(ac2_fov::detail::continuation == reinterpret_cast<uintptr_t>(target + 5));
	MEMORY_BASIC_INFORMATION memory = {};
	CHECK(VirtualQuery(target, &memory, sizeof(memory)) == sizeof(memory));
	CHECK(memory.Protect == PAGE_EXECUTE_READ);
	CHECK(!ac2_fov::Install(module, 1.5f));
	CHECK(ac2_fov::detail::multiplier == 1.2f);

	// Execute the installed jump and the following real engine instructions,
	// not only a direct call to the hook, with an active and inactive camera.
	uint32_t camera[128] = {};
	camera[0x30 / 4] = Bits(0.768f);
	PrepareWorld(camera);
	memcpy(image + 0x1E10B84, &worldAddress, sizeof(worldAddress));
	ac2_fov::detail::changes = 0;
	hookEntry = reinterpret_cast<uintptr_t>(target);
	const float expected = 0.768f * 1.2f;
	for (unsigned int i = 0; i < 2; ++i)
	{
		RunCamera(camera);
		CheckRegisters(camera, expected, true);
		CHECK(camera[0x30 / 4] == Bits(0.768f));
		CHECK(camera[0x160 / 4] == Bits(expected));
		CHECK(camera[0x1EC / 4] == 0x10203040);
	}
	handle[0] += 16;
	RunCamera(camera);
	CheckRegisters(camera, 0.768f, true);
	CHECK(camera[0x160 / 4] == Bits(0.768f));
	CHECK(ac2_fov::detail::changes == 2);
	VirtualFree(image, 0, MEM_RELEASE);
}

int main()
{
	InitLogging(NULL, false, false);
	for (unsigned int i = 0; i < 32; ++i)
		xmmSeed[i] = 0x10203040 + i;
	TestHook(0.768f, 1.2f, 0.768f * 1.2f, true);
	TestHook(0.768f, 1.0f, 0.768f, true);
	TestHook(0.768f, 0.5f, 0.384f, true);
	TestHook(1.4f, 2.0f, 1.5f, true);
	TestHook(0.06f, 0.5f, 0.05f, true);
	TestHook(0.768f, 1.2f, 0.768f, false, OtherCamera);
	TestHook(0.768f, 1.2f, 0.768f, false, NullWorld);
	TestHook(0.768f, 1.2f, 0.768f, false, NullHandle);
	TestHook(0.768f, 1.2f, 0.768f, false, InvalidHandle);
	TestHook(0, 1.2f, 0, false);
	TestHook(-0.7f, 1.2f, -0.7f, false);
	TestHook(1.6f, 1.2f, 1.6f, false);
	TestHook(std::numeric_limits<float>::infinity(), 1.2f, std::numeric_limits<float>::infinity(), false);
	TestHook(std::numeric_limits<float>::quiet_NaN(), 1.2f, std::numeric_limits<float>::quiet_NaN(), false);
	TestInstallation();
	ShutdownLogging();
	if (failures)
		return 1;
	puts("FOV tests passed: active camera filtering, real x86 hook and installed jump, CPU state, angle limits and executable guards.");
	return 0;
}
