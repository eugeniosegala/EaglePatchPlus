#include "../EaglePatch/src/ac2_highlights.h"
#include "../EaglePatch/src/ac2_highlight_shader.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <utility>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

using Words = std::vector<uint32_t>;
static const size_t Table = 3;
static const size_t Info = Table + 7;
static const size_t Type = 48;
static const size_t Name = 64;

static Words Fixture(const char* name = "TargettingRimFXIntensity_22", unsigned int reg = 137)
{
	Words words(3 + 32, 0);
	words[0] = 0xFFFF0300;
	words[1] = 0x0021FFFE;
	words[2] = 0x42415443;
	words[Table] = 28;
	words[Table + 2] = words[0];
	words[Table + 3] = 1;
	words[Table + 4] = 28;
	words[Info] = Name;
	words[Info + 1] = 2 | (reg << 16);
	words[Info + 2] = 1;
	words[Info + 3] = Type;
	words[Table + Type / 4] = 3 << 16;
	words[Table + Type / 4 + 1] = 1 | (1 << 16);
	words[Table + Type / 4 + 2] = 1;
	strcpy(reinterpret_cast<char*>(words.data() + Table) + Name, name);
	const uint32_t body[] = {
		0x05000051, 0xA00F0000, 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000,
		0x02000001, 0x800F0800, 0xA0000000 | reg, 0x0000FFFF
	};
	words.insert(words.end(), body, body + 10);
	return words;
}

static bool Rewrite(const Words& source, Words& output)
{
	return ac2_highlights::shader::Rewrite(source.data(), source.size(), output);
}

static void CheckRewrite(const Words& source, const Words& output, unsigned int reg)
{
	const size_t insertion = 35 + 6;
	CHECK(output.size() == source.size() + 6);
	if (output.size() != source.size() + 6)
		return;
	CHECK(memcmp(output.data(), source.data(), insertion * 4) == 0);
	const uint32_t definition[] = {0x05000051, 0xA00F0000 | reg, 0, 0, 0, 0};
	CHECK(memcmp(output.data() + insertion, definition, sizeof(definition)) == 0);
	CHECK(memcmp(output.data() + insertion + 6, source.data() + insertion, (source.size() - insertion) * 4) == 0);
}

static void TestBytecode()
{
	Words output;
	for (unsigned int reg : {1u, 137u, 223u})
	{
		const Words source = Fixture("TargettingRimFXIntensity_22", reg);
		const Words before = source;
		CHECK(Rewrite(source, output));
		CheckRewrite(source, output, reg);
		CHECK(source == before);
		Words twice;
		CHECK(!Rewrite(output, twice));
		CHECK(twice.empty());
	}
	CHECK(Rewrite(Fixture("TargettingRimFXIntensity"), output));
	for (const char* name : {"g_Tint", "TargettingRimFXColor_19", "TargettingRimFXIntensityBad", "TargettingRimFXIntensity_", "TargettingRimFXIntensity_2X"})
		CHECK(!Rewrite(Fixture(name), output));
	CHECK(output.empty());
	Words source = Fixture();
	for (size_t length = 0; length < source.size(); ++length)
		CHECK(!ac2_highlights::shader::Rewrite(source.data(), length, output));
	CHECK(!ac2_highlights::shader::Rewrite(nullptr, 100, output));
	for (const auto& mutation : std::vector<std::pair<size_t, uint32_t>> {
		{0, 0xFFFE0300}, {0, 0xFFFF0200}, {1, 0x7FFFFFFE}, {Table, 27},
		{Table + 2, 0xFFFE0300}, {Table + 3, 0xFFFFFFFF}, {Table + 4, 0xFFFFFFF0},
		{Info, 0xFFFFFFFF}, {Info + 1, 2 | (224 << 16)}, {Info + 1, 1 | (137 << 16)},
		{Info + 2, 2}, {Info + 3, 0xFFFFFFFF}, {Table + Type / 4, 1 | (3 << 16)},
		{Table + Type / 4 + 1, 1 | (4 << 16)}, {Table + Type / 4 + 2, 2},
		{source.size() - 4, 0x0F000001}, {source.size() - 1, 0x00000001},
		{36, 0xA00F0000 | 137}})
	{
		Words bad = source;
		bad[mutation.first] = mutation.second;
		CHECK(!Rewrite(bad, output));
		CHECK(output.empty());
	}
	Words duplicate = source;
	duplicate.insert(duplicate.begin() + 35, source.begin() + 1, source.begin() + 35);
	CHECK(!Rewrite(duplicate, output));
	// A second uniform shares the float register: never erase its components.
	Words aliased = source;
	aliased[Table + 3] = 2;
	memset(aliased.data() + Info, 0, (35 - Info) * 4);
	aliased[Info] = 84;
	aliased[Info + 1] = 2 | (137 << 16);
	aliased[Info + 2] = 1;
	aliased[Info + 3] = 68;
	aliased[Info + 5] = 112;
	aliased[Info + 6] = 2 | (136 << 16);
	aliased[Info + 7] = 2;
	aliased[Info + 8] = 68;
	aliased[Table + 68 / 4] = 3 << 16;
	aliased[Table + 68 / 4 + 1] = 1 | (1 << 16);
	aliased[Table + 68 / 4 + 2] = 1;
	strcpy(reinterpret_cast<char*>(aliased.data() + Table) + 84, "TargettingRimFXIntensity_22");
	strcpy(reinterpret_cast<char*>(aliased.data() + Table) + 112, "g_Tint");
	CHECK(!Rewrite(aliased, output));
	aliased[Info + 6] = 2 | (140 << 16);
	CHECK(Rewrite(aliased, output));
	CheckRewrite(aliased, output, 137);
	aliased[Info + 5] = 84;
	CHECK(!Rewrite(aliased, output));
}

using CreateShader = HRESULT (WINAPI*)(void*, const DWORD*, void**);
static const uint32_t* original = nullptr;
static void* expectedDevice = nullptr;
static bool rejectModified = false;
static unsigned int modifiedCalls = 0, originalCalls = 0;
static HRESULT WINAPI Capture(void* device, const DWORD* bytecode, void** output)
{
	CHECK(device == expectedDevice);
	CHECK(output != nullptr);
	if (bytecode != reinterpret_cast<const DWORD*>(original))
	{
		++modifiedCalls;
		Words source(original, original + 45);
		Words changed(reinterpret_cast<const uint32_t*>(bytecode), reinterpret_cast<const uint32_t*>(bytecode) + 51);
		CheckRewrite(source, changed, 137);
		if (rejectModified)
			return E_FAIL;
	}
	else
		++originalCalls;
	*output = reinterpret_cast<void*>(0x11223344);
	return S_FALSE;
}

static void TestInstalledHook()
{
	unsigned char* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2190000,
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	CHECK(image != nullptr);
	if (!image)
		return;
	auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = sizeof(*dos);
	auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	nt->Signature = IMAGE_NT_SIGNATURE;
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->FileHeader.TimeDateStamp = 0x5925BA62;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	nt->OptionalHeader.SizeOfImage = 0x2190000;
	HMODULE module = reinterpret_cast<HMODULE>(image);
	unsigned char* target = image + 0x11DED3B;
	const unsigned char signature[] = {
		0x8B, 0x81, 0xA8, 0x01, 0x00, 0x00, 0xFF, 0xD0, 0x85, 0xF6, 0x8B, 0x45, 0x08
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
	CHECK(target[0] == 0xB8 && target[5] == 0x90);
	CHECK(memcmp(target + 6, signature + 6, sizeof(signature) - 6) == 0);
	CHECK(!ac2_highlights::Install(module, true));

	// Execute the patched native function load and COM call with a fake device.
	const unsigned char prefix[] = {
		0x55, 0x8B, 0xEC, 0xFF, 0x75, 0x10, 0xFF, 0x75, 0x0C,
		0x8B, 0x45, 0x08, 0x50, 0x8B, 0x08
	};
	const unsigned char suffix[] = {0x5D, 0xC2, 0x0C, 0x00};
	memcpy(target - sizeof(prefix), prefix, sizeof(prefix));
	memcpy(target + 8, suffix, sizeof(suffix));
	DWORD oldProtect;
	CHECK(VirtualProtect(image, 0x2190000, PAGE_EXECUTE_READ, &oldProtect) != 0);
	FlushInstructionCache(GetCurrentProcess(), image, 0x2190000);
	auto create = reinterpret_cast<CreateShader>(target - sizeof(prefix));
	void* vtable[107] = {};
	vtable[106] = reinterpret_cast<void*>(&Capture);
	void** device = vtable;
	expectedDevice = &device;
	Words source = Fixture();
	const Words before = source;
	original = source.data();
	void* output = nullptr;
	CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
	CHECK(output == reinterpret_cast<void*>(0x11223344));
	CHECK(modifiedCalls == 1 && originalCalls == 0);
	CHECK(source == before);
	rejectModified = true;
	CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
	CHECK(modifiedCalls == 2 && originalCalls == 1);
	CHECK(source == before);
	Words unrelated = Fixture("g_Tint");
	original = unrelated.data();
	CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
	CHECK(modifiedCalls == 2 && originalCalls == 2);
	original = nullptr;
	CHECK(create(&device, nullptr, &output) == S_FALSE);
	CHECK(modifiedCalls == 2 && originalCalls == 3);
	void* blocked = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
	CHECK(blocked != nullptr);
	if (blocked)
	{
		original = static_cast<const uint32_t*>(blocked);
		CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
		CHECK(modifiedCalls == 2 && originalCalls == 4);
		VirtualFree(blocked, 0, MEM_RELEASE);
	}
	void* readOnly = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	CHECK(readOnly != nullptr);
	if (readOnly)
	{
		memcpy(readOnly, source.data(), source.size() * 4);
		CHECK(VirtualProtect(readOnly, 4096, PAGE_READONLY, &oldProtect) != 0);
		original = static_cast<const uint32_t*>(readOnly);
		rejectModified = false;
		CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
		CHECK(modifiedCalls == 3);
		CHECK(memcmp(readOnly, source.data(), source.size() * 4) == 0);
		VirtualFree(readOnly, 0, MEM_RELEASE);
	}
	VirtualFree(image, 0, MEM_RELEASE);
}

int main()
{
	TestBytecode();
	TestInstalledHook();
	if (failures)
		return 1;
	puts("Highlight tests passed: named shader intensity, malformed input, register isolation, installed x86 COM call, fallback and signature guards.");
	return 0;
}
