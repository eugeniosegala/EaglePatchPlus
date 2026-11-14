#include "ac2_ui_scale.h"
#include "ac2_image.h"
#include "patcher.h"
#include "../shared/logger.h"
#include <string.h>
#include <math.h>

namespace ac2_ui
{
namespace detail
{
struct Root { uintptr_t value = 0; };
using Open = Root*(__thiscall*)(Root*, void*, const wchar_t*);
using Close = void(__thiscall*)(Root*);
using Valid = int(__thiscall*)(Root*);
using GetPair = int*(__thiscall*)(Root*, int*, double*, double*);
using SetPair = int*(__thiscall*)(Root*, int*, double, double);
using Initialize = void(__thiscall*)(void*);
struct Api { Open open; Close close; Valid valid; GetPair position, scale; SetPair setPosition, setScale; };
Api api = {};
Initialize initialize = nullptr;
uintptr_t image = 0;
float multiplier = 1.0f;
volatile LONG applied = 0, skipped = 0;

bool Finite(double value)
{
	uint64_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}
bool Read(uintptr_t address, void* out, size_t size)
{
	if (!ac2_image::IsReadable(reinterpret_cast<const void*>(address), size)) return false;
	memcpy(out, reinterpret_cast<const void*>(address), size);
	return true;
}
bool TypeName(void* item, char (&name)[64])
{
	uintptr_t table = 0, getter = 0, slot = 0, type = 0, text = 0;
	unsigned char code[6];
	if (!Read(reinterpret_cast<uintptr_t>(item), &table, 4) || !Read(table + 0x10, &getter, 4) ||
		getter < image + 0x1000 || getter >= image + 0x18a5000 || !Read(getter, code, sizeof(code)) ||
		code[0] != 0xa1 || code[5] != 0xc3) return false;
	memcpy(&slot, code + 1, 4);
	if (slot < image + 0x1c6b000 || slot >= image + 0x2190000 || !Read(slot, &type, 4) || !Read(type + 0x20, &text, 4)) return false;
	for (size_t i = 0; i < sizeof(name); ++i)
	{
		if (!Read(text + i, name + i, 1)) return false;
		if (!name[i]) return i != 0;
		if (name[i] < 32 || name[i] > 126) return false;
	}
	name[0] = 0;
	return false;
}
void Anchor(const char* name, double& x, double& y)
{
	x = 640; y = 360;
	if (strstr(name, "HudMiniMap")) { x = 1280; y = 720; }
	else if (strstr(name, "HudControlsManager")) { x = 1280; y = 0; }
	else if (strstr(name, "HudHealthBar") || strstr(name, "HudMissionState")) { x = 0; y = 0; }
	else if (strstr(name, "HudMoney")) { x = 0; y = 720; }
}
bool Apply(void* item, const char* name)
{
	// World markers must retain their projected positions; these are not HUD panels.
	if (strstr(name, "HudIconManager")) return false;
	uintptr_t player = 0;
	if (!Read(reinterpret_cast<uintptr_t>(item) + 0x64, &player, 4) || !player) return false;
	Root root;
	api.open(&root, reinterpret_cast<void*>(player), L"_root");
	if (!api.valid(&root))
	{
		api.close(&root);
		api.open(&root, reinterpret_cast<void*>(player), L"_level0");
	}
	if (!api.valid(&root)) { api.close(&root); return false; }
	int status = -1;
	double x = 0, y = 0, sx = 0, sy = 0;
	api.position(&root, &status, &x, &y);
	bool ok = status >= 0 && Finite(x) && Finite(y);
	api.scale(&root, &status, &sx, &sy);
	ok = ok && status >= 0 && Finite(sx) && Finite(sy);
	const double target = 100.0 * multiplier;
	// Roots normally have a 100% transform. Do not compound on reload, or replace a different mod's transform.
	if (!ok || (sx == target && sy == target) || sx != 100.0 || sy != 100.0)
	{ api.close(&root); return false; }
	double ax, ay;
	Anchor(name, ax, ay);
	const double nx = x * multiplier + ax * (1.0 - multiplier);
	const double ny = y * multiplier + ay * (1.0 - multiplier);
	api.setPosition(&root, &status, nx, ny);
	ok = status >= 0;
	if (ok) { api.setScale(&root, &status, target, target); ok = status >= 0; }
	if (ok)
	{
		double actualX = 0, actualY = 0, actualSx = 0, actualSy = 0;
		api.position(&root, &status, &actualX, &actualY);
		ok = status >= 0 && Finite(actualX) && Finite(actualY) && fabs(actualX - nx) < 0.1 && fabs(actualY - ny) < 0.1;
		api.scale(&root, &status, &actualSx, &actualSy);
		ok = ok && status >= 0 && Finite(actualSx) && Finite(actualSy) && fabs(actualSx - target) < 0.01 && fabs(actualSy - target) < 0.01;
	}
	if (!ok)
	{
		api.setPosition(&root, &status, x, y);
		api.setScale(&root, &status, sx, sy);
	}
	api.close(&root);
	return ok;
}
void __fastcall InitializeHook(void* item, void*)
{
	initialize(item);
	char name[64] = {};
	if (!TypeName(item, name)) return;
	if (Apply(item, name))
	{
		if (InterlockedIncrement(&applied) <= 8) LogInfo("UIScale: scaled %s to %.0f%%.", name[0] ? name : "Flash interface", multiplier * 100);
	}
	else if (InterlockedIncrement(&skipped) <= 3) LogInfo("UIScale: left %s unchanged (root unavailable, already scaled or custom transform).", name[0] ? name : "Flash interface");
}
}

bool Install(HMODULE game, float scale)
{
	if (!detail::Finite(scale) || scale < 0.5f || scale > 1.0f)
	{ LogWarn("UIScale must be between 0.5 and 1.0; UI scaling skipped."); return false; }
	if (scale == 1.0f) return false;
	if (!ac2_image::IsVerifiedSteamImage(game))
	{ LogWarn("UIScale: executable layout is not verified; skipped."); return false; }
	const uintptr_t base = reinterpret_cast<uintptr_t>(game);
	// Steam Digital UPlay: FireItem initialization and native Flash MovieClip APIs.
	struct Signature { uintptr_t rva; const char* bytes; size_t size; };
	const Signature signatures[] = {
		{0x10566c0, "\x55\x8b\xec\x6a\xff\x68\x20\xcd", 8},
		{0x13ae6a0, "\x8b\x44\x24\x04\x85\xc0\x57\x8b\xf9", 9},
		{0x13aff30, "\x51\x53\x8b\xd9\x8b\x03\x85\xc0", 8},
		{0x13ae1e0, "\x8b\x01\x85\xc0\x74\x0c\x83\x78\x1c\x00", 10},
		{0x13af2f0, "\x57\x8b\xf9\x8b\x07\x85\xc0\x75\x0e", 9},
		{0x13af460, "\x83\xec\x14\x57\x8b\xf9\x8b\x07\x85\xc0", 10},
		{0x13ae820, "\x57\x8b\xf9\x8b\x07\x85\xc0\x75\x0e", 9},
		{0x13ae9a0, "\x57\x8b\xf9\x8b\x07\x85\xc0\x75\x0e", 9}
	};
	for (const auto& signature : signatures)
	{
		const void* address = reinterpret_cast<const void*>(base + signature.rva);
		if (!ac2_image::IsReadable(address, signature.size) || memcmp(address, signature.bytes, signature.size))
		{ LogWarn("UIScale: UI code differs or is already hooked; skipped."); return false; }
	}
	unsigned char* target = reinterpret_cast<unsigned char*>(base + 0x10566c0);
	unsigned char* original = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!original) return false;
	memcpy(original, target, 5);
	original[5] = 0xe9;
	const uint32_t back = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target + 5) - reinterpret_cast<uintptr_t>(original + 10));
	memcpy(original + 6, &back, 4);
	DWORD protect = 0, old = 0;
	if (!VirtualProtect(original, 16, PAGE_EXECUTE_READ, &protect) || !FlushInstructionCache(GetCurrentProcess(), original, 16) || !Unprotect_internal(target, 5, &old))
	{ VirtualFree(original, 0, MEM_RELEASE); return false; }
	detail::image = base;
	detail::multiplier = scale;
	detail::initialize = reinterpret_cast<detail::Initialize>(original);
	detail::api = {reinterpret_cast<detail::Open>(base + 0x13ae6a0), reinterpret_cast<detail::Close>(base + 0x13aff30),
		reinterpret_cast<detail::Valid>(base + 0x13ae1e0), reinterpret_cast<detail::GetPair>(base + 0x13af2f0),
		reinterpret_cast<detail::GetPair>(base + 0x13af460), reinterpret_cast<detail::SetPair>(base + 0x13ae820),
		reinterpret_cast<detail::SetPair>(base + 0x13ae9a0)};
	unsigned char jump[5] = {0xe9};
	const uint32_t displacement = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&detail::InitializeHook) - reinterpret_cast<uintptr_t>(target + 5));
	memcpy(jump + 1, &displacement, 4);
	memcpy(target, jump, 5);
	if (!Protect_internal(target, 5, old))
	{
		memcpy(target, original, 5);
		Protect_internal(target, 5, old);
		VirtualFree(original, 0, MEM_RELEASE);
		detail::initialize = nullptr;
		LogWarn("UIScale: could not restore code protection; original code restored.");
		return false;
	}
	LogInfo("UIScale: %.2fx (native Flash transform; restart to change).", scale);
	return true;
}
}
