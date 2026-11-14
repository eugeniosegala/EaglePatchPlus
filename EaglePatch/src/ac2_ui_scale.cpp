#include "ac2_ui_scale.h"
#include "ac2_ui_movie.h"
#include "ac2_image.h"
#include "patcher.h"
#include "../shared/logger.h"
#include <new>

namespace ac2_ui
{
namespace detail
{
using Load = int*(__thiscall*)(void*, int*, const void*, uint32_t, uint32_t);
using Destroy = void(__thiscall*)(void*);
Load load = nullptr;
Destroy destroy = nullptr;
uintptr_t image = 0;
float multiplier = 1.0f;
CRITICAL_SECTION storageLock;
struct Buffer { void* state; movie::Bytes data; Buffer* next = nullptr; };
Buffer* buffers = nullptr;
size_t retained = 0;
volatile LONG changes = 0;
bool Read(uintptr_t address, void* out, size_t size)
{
	if (!ac2_image::IsReadable(reinterpret_cast<const void*>(address), size)) return false;
	memcpy(out, reinterpret_cast<const void*>(address), size); return true;
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
	name[0] = 0; return false;
}
bool Reserve(Buffer* value)
{
	EnterCriticalSection(&storageLock);
	const bool fits = value->data.size() <= 64 * 1024 * 1024 - retained;
	if (fits) retained += value->data.size();
	LeaveCriticalSection(&storageLock); return fits;
}
void Store(void* state, Buffer* replacement, bool retire)
{
	Buffer* garbage = nullptr;
	EnterCriticalSection(&storageLock);
	if (retire)
	{
		Buffer** link = &buffers;
		while (*link)
		{
			Buffer* old = *link;
			if (old->state != state) { link = &old->next; continue; }
			*link = old->next; retained -= old->data.size(); old->next = garbage; garbage = old;
		}
	}
	if (replacement) { replacement->next = buffers; buffers = replacement; }
	LeaveCriticalSection(&storageLock);
	while (garbage) { Buffer* next = garbage->next; delete garbage; garbage = next; }
}
int* __fastcall LoadHook(void* state, void*, int* status, const void* data, uint32_t size, uint32_t flags)
{
	Buffer* pending = nullptr;
	uintptr_t player = 0, bridge = 0, itemPlayer = 0;
	char name[64] = {};
	// The engine holds its per-player lock throughout this internal load, including deferred menus.
	if (Read(reinterpret_cast<uintptr_t>(state), &player, 4) && Read(reinterpret_cast<uintptr_t>(state) + 0x50, &bridge, 4) && bridge >= 8 &&
		Read(bridge - 8 + 0x64, &itemPlayer, 4) && itemPlayer == player && TypeName(reinterpret_cast<void*>(bridge - 8), name) &&
		size <= 16 * 1024 * 1024 && ac2_image::IsReadable(data, size))
	{
		pending = new(std::nothrow) Buffer{state, {}, nullptr};
		if (pending)
		{
			try
			{
				if (!movie::Patch(static_cast<const unsigned char*>(data), size, multiplier, name, pending->data) || !Reserve(pending))
				{ delete pending; pending = nullptr; }
			}
			catch (const std::bad_alloc&) { delete pending; pending = nullptr; }
		}
	}
	int* result = load(state, status, pending ? pending->data.data() : data, pending ? (uint32_t)pending->data.size() : size, flags);
	// Flash may retain the stream. Retire old bytes only after successful replacement or destruction.
	Store(state, pending, result && *result >= 0);
	if (pending && InterlockedIncrement(&changes) <= 12)
		LogInfo("UIScale: %s movie prepared at %.0f%% (load status %d).", name, multiplier * 100, result ? *result : -1);
	return result;
}
void __fastcall DestroyHook(void* state, void*)
{
	destroy(state);
	Store(state, nullptr, true);
}
}

bool Install(HMODULE game, float scale)
{
	if (!movie::ValidScale(scale)) { LogWarn("UIScale must be between 0.5 and 1.0; skipped."); return false; }
	if (scale == 1) return false;
	if (!ac2_image::IsVerifiedSteamImage(game)) { LogWarn("UIScale: executable layout is not verified; skipped."); return false; }
	const uintptr_t base = reinterpret_cast<uintptr_t>(game);
	struct Hook { uintptr_t rva; const char* bytes; size_t size; uintptr_t entry; unsigned char* original; DWORD protection; };
	Hook hooks[] = {
		{0x13b2dd0, "\x83\xec\x0c\x53\x56\x8d\x44\x24\x0c", 9, reinterpret_cast<uintptr_t>(&detail::LoadHook), nullptr, 0},
		{0x13b28b0, "\x51\x53\x55\x56\x57", 5, reinterpret_cast<uintptr_t>(&detail::DestroyHook), nullptr, 0}
	};
	const void* bridge = reinterpret_cast<const void*>(base + 0x13ac405);
	if (!ac2_image::IsReadable(bridge, 10) || memcmp(bridge, "\x8b\x76\x04\x8b\x44\x24\x10\x83\xc6\x50", 10))
	{ LogWarn("UIScale: UI owner callback differs; skipped."); return false; }
	for (const auto& hook : hooks)
		if (!ac2_image::IsReadable(reinterpret_cast<const void*>(base + hook.rva), hook.size) || memcmp(reinterpret_cast<const void*>(base + hook.rva), hook.bytes, hook.size))
		{ LogWarn("UIScale: movie loader differs or is already hooked; skipped."); return false; }
	for (auto& hook : hooks)
	{
		hook.original = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (hook.original)
		{
			memcpy(hook.original, reinterpret_cast<void*>(base + hook.rva), hook.size);
			hook.original[hook.size] = 0xe9;
			const uint32_t jump = (uint32_t)(base + hook.rva + hook.size - reinterpret_cast<uintptr_t>(hook.original + hook.size + 5));
			memcpy(hook.original + hook.size + 1, &jump, 4);
			DWORD old = 0;
			if (VirtualProtect(hook.original, 16, PAGE_EXECUTE_READ, &old) && FlushInstructionCache(GetCurrentProcess(), hook.original, 16)) continue;
		}
		for (auto& allocated : hooks) if (allocated.original) VirtualFree(allocated.original, 0, MEM_RELEASE);
		return false;
	}
	InitializeCriticalSection(&detail::storageLock);
	detail::image = base; detail::multiplier = scale;
	detail::load = reinterpret_cast<detail::Load>(hooks[0].original);
	detail::destroy = reinterpret_cast<detail::Destroy>(hooks[1].original);
	unsigned patched = 0; bool success = true;
	for (auto& hook : hooks)
	{
		auto* target = reinterpret_cast<unsigned char*>(base + hook.rva);
		if (!Unprotect_internal(target, hook.size, &hook.protection)) { success = false; break; }
		unsigned char jump[9]; memset(jump, 0x90, sizeof(jump)); jump[0] = 0xe9;
		const uint32_t offset = (uint32_t)(hook.entry - reinterpret_cast<uintptr_t>(target + 5));
		memcpy(jump + 1, &offset, 4); memcpy(target, jump, hook.size); ++patched;
		if (!Protect_internal(target, hook.size, hook.protection)) { success = false; break; }
	}
	if (!success)
	{
		bool restored = true;
		for (unsigned i = 0; i < patched; ++i)
		{
			auto* target = reinterpret_cast<unsigned char*>(base + hooks[i].rva); DWORD old;
			if (Unprotect_internal(target, hooks[i].size, &old))
			{ memcpy(target, hooks[i].original, hooks[i].size); Protect_internal(target, hooks[i].size, hooks[i].protection); }
			else restored = false;
		}
		// Never free a trampoline that a remaining jump can still reach.
		if (!restored) { detail::multiplier = 1; LogWarn("UIScale: rollback incomplete; scaling disabled, hook storage retained."); return false; }
		for (auto& hook : hooks) VirtualFree(hook.original, 0, MEM_RELEASE);
		DeleteCriticalSection(&detail::storageLock); detail::load = nullptr; detail::destroy = nullptr;
		LogWarn("UIScale: could not install both hooks; rolled back."); return false;
	}
	LogInfo("UIScale: %.2fx (UI movie load patch; restart to change).", scale);
	return true;
}
}
