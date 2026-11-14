#include <stdio.h>
#include <limits>
#include <wchar.h>
#include "../EaglePatch/src/ac2_ui_scale.cpp"

using namespace ac2_ui::detail;
static unsigned failures = 0;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "Line %u: %s\n", __LINE__, #expr); ++failures; } } while (0)
static double px, py, sx, sy;
static unsigned opens, closes, writes;
static bool available, rejectScale;
static uintptr_t item[40];
static Root* __fastcall OpenRoot(Root* root, void*, void* player, const wchar_t* path)
{
	CHECK(player == reinterpret_cast<void*>(0x1234));
	CHECK(!wcscmp(path, L"_root") || !wcscmp(path, L"_level0"));
	root->value = available ? 1 : 0; ++opens; return root;
}
static void __fastcall CloseRoot(Root* root, void*) { root->value = 0; ++closes; }
static int __fastcall ValidRoot(Root* root, void*) { return root->value != 0; }
static int* __fastcall Position(Root*, void*, int* status, double* x, double* y) { *status = 0; *x = px; *y = py; return status; }
static int* __fastcall Scale(Root*, void*, int* status, double* x, double* y) { *status = 0; *x = sx; *y = sy; return status; }
static int* __fastcall SetPosition(Root*, void*, int* status, double x, double y) { ++writes; *status = 0; px = x; py = y; return status; }
static int* __fastcall SetScale(Root*, void*, int* status, double x, double y)
{
	++writes; *status = rejectScale && x == 50 ? -1 : 0;
	if (*status >= 0) { sx = x; sy = y; }
	return status;
}
static void Reset()
{
	api = {reinterpret_cast<Open>(&OpenRoot), reinterpret_cast<Close>(&CloseRoot), reinterpret_cast<Valid>(&ValidRoot),
		reinterpret_cast<GetPair>(&Position), reinterpret_cast<GetPair>(&Scale), reinterpret_cast<SetPair>(&SetPosition), reinterpret_cast<SetPair>(&SetScale)};
	multiplier = 0.5f; px = py = 0; sx = sy = 100;
	opens = closes = writes = 0; available = true; rejectScale = false;
	memset(item, 0, sizeof(item)); item[0x64/4] = 0x1234;
}
static void TransformTests()
{
	Reset(); CHECK(Apply(item, "MenuManager"));
	CHECK(sx == 50 && sy == 50 && px == 320 && py == 180);
	CHECK(opens == 1 && closes == 1);
	CHECK(!Apply(item, "MenuManager")); CHECK(sx == 50 && px == 320 && writes == 2);
	Reset(); CHECK(Apply(item, "HudMiniMap")); CHECK(px == 640 && py == 360);
	CHECK(px + 1280 * 0.5 == 1280 && py + 720 * 0.5 == 720);
	Reset(); CHECK(Apply(item, "HudControlsManager")); CHECK(px == 640 && py == 0);
	Reset(); CHECK(Apply(item, "HudHealthBar")); CHECK(px == 0 && py == 0);
	Reset(); CHECK(Apply(item, "HudMoneyComponent")); CHECK(px == 0 && py == 360);
	Reset(); CHECK(!Apply(item, "HudIconManager")); CHECK(writes == 0 && opens == 0);
	Reset(); px = 20; py = 40; CHECK(Apply(item, "MenuManager")); CHECK(px == 330 && py == 200);
	Reset(); available = false; CHECK(!Apply(item, "MenuManager")); CHECK(opens == 2 && closes == 2 && writes == 0);
	Reset(); sx = 75; CHECK(!Apply(item, "MenuManager")); CHECK(sx == 75 && writes == 0 && closes == 1);
	Reset(); px = std::numeric_limits<double>::quiet_NaN(); CHECK(!Apply(item, "MenuManager")); CHECK(writes == 0 && closes == 1);
	Reset(); rejectScale = true; CHECK(!Apply(item, "MenuManager")); CHECK(px == 0 && py == 0 && sx == 100 && sy == 100 && closes == 1);
}
static void GuardTests()
{
	CHECK(!ac2_ui::Install(nullptr, 1));
	CHECK(!ac2_ui::Install(nullptr, 0.5f));
	CHECK(!ac2_ui::Install(nullptr, 0.49f));
	CHECK(!ac2_ui::Install(nullptr, 1.1f));
	CHECK(!ac2_ui::Install(nullptr, std::numeric_limits<float>::quiet_NaN()));
	CHECK(!ac2_ui::Install(nullptr, std::numeric_limits<float>::infinity()));
}
static void InstallationTests()
{
	auto* game = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x2190000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	CHECK(game != nullptr); if (!game) return;
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game);
	dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(game + 0x80);
	nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
	nt->FileHeader.TimeDateStamp = 0x5925ba62;
	nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC; nt->OptionalHeader.SizeOfImage = 0x2190000;
	const uintptr_t offsets[] = {0x10566c0,0x13ae6a0,0x13aff30,0x13ae1e0,0x13af2f0,0x13af460,0x13ae820,0x13ae9a0};
	const char* bytes[] = {"\x55\x8b\xec\x6a\xff\x68\x20\xcd", "\x8b\x44\x24\x04\x85\xc0\x57\x8b\xf9",
		"\x51\x53\x8b\xd9\x8b\x03\x85\xc0", "\x8b\x01\x85\xc0\x74\x0c\x83\x78\x1c\x00",
		"\x57\x8b\xf9\x8b\x07\x85\xc0\x75\x0e", "\x83\xec\x14\x57\x8b\xf9\x8b\x07\x85\xc0",
		"\x57\x8b\xf9\x8b\x07\x85\xc0\x75\x0e", "\x57\x8b\xf9\x8b\x07\x85\xc0\x75\x0e"};
	const unsigned lengths[] = {8,9,8,10,9,10,9,9};
	for (unsigned i = 0; i < 8; ++i) memcpy(game + offsets[i], bytes[i], lengths[i]);
	HMODULE module = reinterpret_cast<HMODULE>(game);
	for (unsigned i = 0; i < 8; ++i)
	{
		game[offsets[i]] ^= 1;
		CHECK(!ac2_ui::Install(module, 0.5f));
		game[offsets[i]] ^= 1;
		CHECK(game[offsets[0]] == 0x55);
	}
	nt->FileHeader.TimeDateStamp ^= 1; CHECK(!ac2_ui::Install(module, 0.5f)); nt->FileHeader.TimeDateStamp ^= 1;
	// Complete the displaced prologue with a synthetic original function epilogue.
	memcpy(game + offsets[0] + 8, "\xc7\x01\x8b\xe5\x5d\xc3", 6);
	CHECK(ac2_ui::Install(module, 0.5f));
	CHECK(game[offsets[0]] == 0xe9);
	CHECK(!ac2_ui::Install(module, 0.5f));
	Reset();
	uintptr_t table[8] = {}, type[9] = {};
	const uintptr_t getter = reinterpret_cast<uintptr_t>(game + 0x1000), slot = reinterpret_cast<uintptr_t>(game + 0x1c6b000);
	game[0x1000] = 0xa1; memcpy(game + 0x1001, &slot, 4); game[0x1005] = 0xc3;
	table[4] = getter; item[0] = reinterpret_cast<uintptr_t>(table);
	type[8] = reinterpret_cast<uintptr_t>("MenuManager");
	const uintptr_t typePointer = reinterpret_cast<uintptr_t>(type);
	memcpy(reinterpret_cast<void*>(slot), &typePointer, 4);
	char name[64] = {};
	CHECK(TypeName(item, name) && !strcmp(name, "MenuManager"));
	// The real installed x86 jump and trampoline must return with the original thiscall stack intact.
	reinterpret_cast<Initialize>(game + offsets[0])(item);
	CHECK(sx == 50 && sy == 50 && px == 320 && py == 180 && opens == closes);
	VirtualFree(reinterpret_cast<void*>(initialize), 0, MEM_RELEASE);
	initialize = nullptr;
	VirtualFree(game, 0, MEM_RELEASE);
}
int main()
{
	TransformTests(); GuardTests(); InstallationTests();
	if (failures) return 1;
	puts("UI scale tests passed: menu/corner transforms, native API calling convention, handle lifetime, idempotence, rollback, configuration guards and installed x86 hook.");
	return 0;
}
