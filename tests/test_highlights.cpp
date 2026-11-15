#include "../EaglePatch/src/ac2_highlights.h"
#include "../EaglePatch/src/ac2_highlight_shader.h"

#include <stdio.h>
#include <string.h>
#include <vector>
#include <utility>
#include <limits>
#include <initializer_list>

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
	// Length and detection work in place at any byte alignment.
	{
		const Words shader = Fixture();
		std::vector<unsigned char> bytes(shader.size() * 4 + 3);
		for (size_t shift = 0; shift < 4; ++shift)
		{
			memcpy(bytes.data() + shift, shader.data(), shader.size() * 4);
			CHECK(ac2_highlights::shader::Length(bytes.data() + shift, shader.size() * 4) == shader.size());
			CHECK(ac2_highlights::shader::Length(bytes.data() + shift, shader.size() * 4 - 4) == 0);
		}
		CHECK(ac2_highlights::shader::Length(nullptr, 100) == 0);
		Words legacy = shader;
		legacy[0] = 0xFFFF0104;
		CHECK(ac2_highlights::shader::Length(reinterpret_cast<const unsigned char*>(legacy.data()), legacy.size() * 4) == 0);
		CHECK(ac2_highlights::shader::HasTargeting(shader.data(), shader.size()));
		CHECK(ac2_highlights::shader::HasTargeting(Fixture("TargettingRimFXColor_19").data(), shader.size()));
		CHECK(!ac2_highlights::shader::HasTargeting(Fixture("g_Tint").data(), shader.size()));
		CHECK(!ac2_highlights::shader::HasTargeting(shader.data(), 2));
	}
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
	// Engine buffers may start at any byte offset; the copy is still rewritten.
	std::vector<unsigned char> unaligned(source.size() * 4 + 1);
	memcpy(unaligned.data() + 1, source.data(), source.size() * 4);
	original = reinterpret_cast<const uint32_t*>(unaligned.data() + 1);
	rejectModified = false;
	CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
	CHECK(modifiedCalls == 3 && originalCalls == 4);
	CHECK(memcmp(unaligned.data() + 1, source.data(), source.size() * 4) == 0);
	void* readOnly = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	CHECK(readOnly != nullptr);
	if (readOnly)
	{
		memcpy(readOnly, source.data(), source.size() * 4);
		CHECK(VirtualProtect(readOnly, 4096, PAGE_READONLY, &oldProtect) != 0);
		original = static_cast<const uint32_t*>(readOnly);
		rejectModified = false;
		CHECK(create(&device, reinterpret_cast<const DWORD*>(original), &output) == S_FALSE);
		CHECK(modifiedCalls == 4);
		CHECK(memcmp(readOnly, source.data(), source.size() * 4) == 0);
		VirtualFree(readOnly, 0, MEM_RELEASE);
	}
	VirtualFree(image, 0, MEM_RELEASE);
}

static IMAGE_NT_HEADERS32* FakeImage(unsigned char* image)
{
	auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	dos->e_lfanew = sizeof(*dos);
	auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	nt->Signature = IMAGE_NT_SIGNATURE;
	nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->FileHeader.TimeDateStamp = 0x5925BA62;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	nt->OptionalHeader.SizeOfImage = 0x2190000;
	return nt;
}

static void TestNeutralColors()
{
	using ac2_highlights::shader::IsNeutralHighlight;
	CHECK(IsNeutralHighlight(1, 1, 1, 0, 0, 0));
	CHECK(IsNeutralHighlight(0.4f, 0.4f, 0.4f, 0, 0, 0));
	CHECK(IsNeutralHighlight(0.75f, 0.8f, 0.95f, 0, 0, 0));
	CHECK(IsNeutralHighlight(1, 1, 1, 0.5f, 0.5f, 0.5f));
	CHECK(!IsNeutralHighlight(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f));
	CHECK(!IsNeutralHighlight(1, 0, 0, 0, 0, 0));
	CHECK(!IsNeutralHighlight(0, 0.4f, 1, 0, 0, 0));
	CHECK(!IsNeutralHighlight(1, 0.8f, 0.2f, 0, 0, 0));
	CHECK(!IsNeutralHighlight(0.6f, 0.75f, 1, 0, 0, 0));
	CHECK(!IsNeutralHighlight(0.02f, 0.02f, 0.02f, 0, 0, 0));
	CHECK(!IsNeutralHighlight(std::numeric_limits<float>::quiet_NaN(), 1, 1, 0, 0, 0));
	CHECK(!IsNeutralHighlight(1, 1, 1, std::numeric_limits<float>::infinity(), 0, 0));
}

struct Color { float red, green, blue, alpha; };
using DrawMask = void (__thiscall*)(void*, void*, unsigned int, const Color*, const Color*, void*);
static Color captured[8];
static Color unmasked = {0.5f, 0.5f, 0.5f, 1.0f};
static const Color* inputPalette = nullptr;
static unsigned int expectedMask = 0, paletteCalls = 0;

static void __cdecl CapturePalette(void* owner, void* texture, unsigned int mask,
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
	++paletteCalls;
}

static void TestPaletteHook()
{
	unsigned char* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2190000,
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	CHECK(image != nullptr);
	if (!image)
		return;
	FakeImage(image);
	HMODULE module = reinterpret_cast<HMODULE>(image);
	unsigned char* target = image + 0x12CAEE0;
	const unsigned char signature[] = {
		0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4, 0xF0, 0x83, 0xC4, 0x04,
		0x55, 0x8B, 0x6B, 0x04, 0x89, 0x6C, 0x24, 0x04, 0x8B, 0xEC, 0x6A, 0xFF
	};
	memcpy(target, signature, sizeof(signature));
	for (size_t index = 0; index < sizeof(signature); ++index)
	{
		target[index] ^= 1;
		CHECK(!ac2_highlights::Install(module, true));
		target[index] ^= 1;
	}
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
	const uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&CapturePalette) -
		reinterpret_cast<uintptr_t>(target + 6 + 21));
	memcpy(body + 17, &offset, sizeof(offset));
	memcpy(target + 6, body, sizeof(body));
	DWORD oldProtect;
	CHECK(VirtualProtect(target, 4096, PAGE_EXECUTE_READ, &oldProtect) != 0);
	FlushInstructionCache(GetCurrentProcess(), target, 4096);
	DrawMask draw = reinterpret_cast<DrawMask>(target);
	Color colors[8] = {
		{1, 1, 1, 1}, {0.4f, 0.4f, 0.4f, 0.7f}, {1, 0, 0, 1}, {0, 0, 1, 1},
		{1, 0.8f, 0, 1}, {0, 0, 0, 1}, {0.75f, 0.8f, 0.95f, 1}, {-1, -1, -1, 1}
	};
	Color before[8];
	memcpy(before, colors, sizeof(colors));
	inputPalette = colors;
	for (unsigned int mask : {0u, 1u, 2u, 4u, 0x40u, 255u})
	{
		expectedMask = mask;
		draw(reinterpret_cast<void*>(0x11223344), reinterpret_cast<void*>(0x55667788),
			mask, colors, &unmasked, reinterpret_cast<void*>(0x12345678));
		CHECK(memcmp(colors, before, sizeof(colors)) == 0);
		for (unsigned int index = 0; index < 8; ++index)
		{
			const bool removed = (index == 0 || index == 1 || index == 6) && (mask & (1u << index));
			CHECK(memcmp(&captured[index], removed ? &unmasked : &before[index], sizeof(Color)) == 0);
		}
	}
	colors[0].red = std::numeric_limits<float>::quiet_NaN();
	colors[1].red = std::numeric_limits<float>::infinity();
	expectedMask = 255;
	draw(reinterpret_cast<void*>(0x11223344), reinterpret_cast<void*>(0x55667788),
		255, colors, &unmasked, reinterpret_cast<void*>(0x12345678));
	CHECK(memcmp(captured, colors, 2 * sizeof(Color)) == 0);
	CHECK(paletteCalls == 7);
	VirtualFree(image, 0, MEM_RELEASE);
}

int main()
{
	TestBytecode();
	TestNeutralColors();
	TestInstalledHook();
	TestPaletteHook();
	if (failures)
		return 1;
	puts("Highlight tests passed: named shader intensity, unaligned bytecode, malformed input, register isolation, installed x86 COM call, neutral palette channels, palette trampoline, fallback and signature guards.");
	return 0;
}
