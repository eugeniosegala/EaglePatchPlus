#include "ac2_rumble.h"
#include "ac2_image.h"
#include <xinput.h>
#include <atomic>
#include "patcher.h"
#include "../shared/logger.h"

namespace ac2_rumble
{
namespace
{
using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using ResponseCallback = uintptr_t(__thiscall*)(void*, void*);
struct Slot { uintptr_t address = 0, original = 0, replacement = 0; bool installed = false; };
Slot slots[3];
CRITICAL_SECTION lock;
bool initialized = false, controllerActive = false;
std::atomic<bool> running{false};
unsigned int controllerIndex = 0;
uintptr_t imageBase = 0, localPlayer = 0, localEntity = 0;
LARGE_INTEGER frequency;
Config settings;
Mixer mixer;
MotionTracker motion;
Motors native[4], sent[4];
SetState originalSetState = nullptr;

bool Read(uintptr_t address, void* value, size_t size)
{
	SIZE_T count = 0;
	return address >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), (void*)address, value, size, &count) && count == size;
}
uintptr_t Word(uintptr_t address)
{
	uintptr_t value = 0;
	Read(address, &value, sizeof(value));
	return value;
}
uint32_t Now()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (uint32_t)((counter.QuadPart / frequency.QuadPart) * 1000 + (counter.QuadPart % frequency.QuadPart) * 1000 / frequency.QuadPart);
}
bool Replace(Slot& slot)
{
	if (Word(slot.address) != slot.original) return false;
	DWORD old = 0;
	if (!Unprotect_internal((void*)slot.address, sizeof(uintptr_t), &old)) return false;
	*(uintptr_t*)slot.address = slot.replacement;
	if (!Protect_internal((void*)slot.address, sizeof(uintptr_t), old))
	{
		*(uintptr_t*)slot.address = slot.original;
		Protect_internal((void*)slot.address, sizeof(uintptr_t), old);
		return false;
	}
	slot.installed = true;
	return true;
}
void Restore()
{
	for (auto& slot : slots)
	{
		if (!slot.installed || Word(slot.address) != slot.replacement) continue;
		DWORD old = 0;
		if (Unprotect_internal((void*)slot.address, sizeof(uintptr_t), &old))
		{
			*(uintptr_t*)slot.address = slot.original;
			Protect_internal((void*)slot.address, sizeof(uintptr_t), old);
			slot.installed = false;
		}
	}
}
Motors Mixed(unsigned int index, uint32_t now)
{
	return controllerActive && controllerIndex == index ? mixer.Mix(native[index], now) : native[index];
}
DWORD Send(unsigned int index, Motors motors)
{
	XINPUT_VIBRATION vibration = {motors.low, motors.high};
	return originalSetState(index, &vibration);
}
DWORD WINAPI SetStateHook(DWORD index, XINPUT_VIBRATION* vibration)
{
	if (!vibration || index >= 4) return originalSetState(index, vibration);
	EnterCriticalSection(&lock);
	native[index] = {vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed};
	const Motors motors = Mixed(index, Now());
	sent[index] = motors;
	const DWORD result = Send(index, motors);
	LeaveCriticalSection(&lock);
	return result;
}
void Trigger(Effect effect, unsigned int scale, uint32_t now)
{
	unsigned int strength = settings.combat;
	if (effect == Effect::Landing || effect == Effect::SoftLanding) strength = settings.landing;
	if (effect == Effect::Climb || effect == Effect::Grab) strength = settings.climbing;
	if (mixer.Trigger(effect, strength * Percent(scale) / 100, now))
	{
		static const char* names[] = {"none", "attack", "parry", "landing", "climbing", "ledge grab", "small landing"};
		LogInfo("Enhanced rumble: %s.", names[(unsigned int)effect]);
	}
}
uintptr_t __fastcall ResponseHook(void* listener, void*, void* event)
{
	const uintptr_t owner = Word((uintptr_t)listener + 4);
	const auto callback = (ResponseCallback)Word((uintptr_t)listener + 8);
	uint32_t eventData[4] = {}, response[20] = {}, handle[3] = {};
	if (Read((uintptr_t)event, eventData, sizeof(eventData)) && Read(eventData[3], response, sizeof(response)) &&
		Read(response[12], handle, sizeof(handle)))
	{
		const uintptr_t actor = Word(owner + 4);
		EnterCriticalSection(&lock);
		if (running && controllerActive && localPlayer && localEntity)
		{
			// The committed response reports the defender's result to the attacker.
			// 3/10 are hurt/kill; 2/4/5/7 are block/parry/successful counters.
			const Effect effect = CombatEffect(eventData[2] != 0, response[8], actor == localPlayer,
				handle[0] == localEntity, handle[0] != 0 && (handle[2] & 0x80000000) != 0);
			Trigger(effect, 100, Now());
		}
		LeaveCriticalSection(&lock);
	}
	// Preserve the original bound call, including its return value and thiscall ABI.
	return callback((void*)owner, event);
}
bool Player(MotionSample& sample, uintptr_t& player, uintptr_t& entity)
{
	player = Word(Word(Word(Word(imageBase + 0x1e134b4) + 0x20) + 0x18));
	if (Word(player) != imageBase + 0x1a07898) return false;
	entity = Word(player + 8);
	const uintptr_t human = Word(player + 0x18);
	if (Word(entity) != imageBase + 0x18b3720 || Word(human) != imageBase + 0x19e6b70 || !Word(entity + 0x6c)) return false;
	float position[3];
	if (!Read(entity + 0x40, position, sizeof(position)) || !Read(human + 0x44, &sample.context, sizeof(sample.context))) return false;
	sample.player = (uint32_t)player; sample.time = Now();
	sample.x = position[0]; sample.y = position[1]; sample.z = position[2];
	return true;
}
bool Foreground()
{
	DWORD pid = 0;
	HWND window = GetForegroundWindow();
	return window && GetWindowThreadProcessId(window, &pid) && pid == GetCurrentProcessId();
}
}

bool Install(HMODULE game, Config config)
{
	if (!config.enabled || initialized) return false;
	imageBase = (uintptr_t)game;
	if (!ac2_image::IsVerifiedSteamImage(game))
	{
		LogWarn("Enhanced rumble: unsupported executable; stock rumble retained.");
		return false;
	}
	HMODULE xinput = GetModuleHandleA("xinput1_3.dll");
	originalSetState = xinput ? (SetState)GetProcAddress(xinput, "XInputSetState") : nullptr;
	if (!originalSetState || Word(imageBase + 0x18a546c) != (uintptr_t)originalSetState)
	{
		LogWarn("Enhanced rumble: XInput output is unavailable or already hooked; skipped.");
		return false;
	}
	const BYTE bridge[] = {0x55,0x8b,0xec,0x8b,0xc1,0x8b,0x48,0x04,0x8b,0x50,0x08,0x5d,0xff,0xe2};
	const uintptr_t tables[] = {0x1a312e4, 0x1a22380};
	const uintptr_t callbacks[] = {0x10199e0, 0xdcfe60};
	for (size_t i = 0; i < 2; ++i)
	{
		BYTE actual[sizeof(bridge)], getter[6];
		if (!Read(imageBase + callbacks[i], actual, sizeof(actual)) || memcmp(actual, bridge, sizeof(bridge)) ||
			Word(imageBase + tables[i] + 4) != imageBase + callbacks[i] ||
			!Read(Word(imageBase + tables[i]), getter, sizeof(getter)) || memcmp(getter, "\xb8\xe0\xf3\x59\x94\xc3", 6))
		{
			LogWarn("Enhanced rumble: combat listener signature differs or is already hooked; skipped.");
			return false;
		}
		slots[i] = {imageBase + tables[i] + 4, imageBase + callbacks[i], (uintptr_t)&ResponseHook, false};
	}
	if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return false;
	settings = config;
	settings.combat = Percent(settings.combat); settings.landing = Percent(settings.landing); settings.climbing = Percent(settings.climbing);
	InitializeCriticalSection(&lock);
	initialized = true;
	slots[2] = {imageBase + 0x18a546c, (uintptr_t)originalSetState, (uintptr_t)&SetStateHook, false};
	for (auto& slot : slots)
	{
		if (!Replace(slot))
		{
			Restore();
			LogWarn("Enhanced rumble: could not install callbacks; rolled back.");
			return false;
		}
	}
	running = true;
	LogInfo("Enhanced rumble enabled (combat %u%%, landings %u%%, climbing %u%%).", settings.combat, settings.landing, settings.climbing);
	return true;
}

void Update(bool connected, unsigned int controller, bool active)
{
	if (!running) return;
	MotionSample sample;
	uintptr_t player = 0, entity = 0;
	const bool valid = connected && active && controller < 4 && Foreground() && Player(sample, player, entity);
	EnterCriticalSection(&lock);
	if (valid && !controllerActive) LogInfo("Enhanced rumble: controller slot %u active.", controller);
	if (!valid || !controllerActive || controllerIndex != controller || localPlayer != player)
	{
		mixer.Clear(); motion.Reset();
		if (controllerActive) native[controllerIndex] = Motors{};
	}
	controllerActive = valid;
	if (valid)
	{
		controllerIndex = controller; localPlayer = player; localEntity = entity;
		const MotionEffect effect = motion.Update(sample);
		Trigger(effect.effect, effect.strength, sample.time);
	}
	else localPlayer = localEntity = 0;
	const uint32_t now = Now();
	for (unsigned int i = 0; i < 4; ++i)
	{
		const Motors desired = Mixed(i, now);
		if (desired.low != sent[i].low || desired.high != sent[i].high)
		{
			sent[i] = desired;
			// XInput does not call back into the game's event or input hooks.
			Send(i, desired);
		}
	}
	LeaveCriticalSection(&lock);
}

void Shutdown()
{
	if (!initialized) return;
	EnterCriticalSection(&lock);
	running = false;
	controllerActive = false;
	mixer.Clear(); motion.Reset();
	if (originalSetState && GetModuleHandleA("xinput1_3.dll"))
		for (unsigned int i = 0; i < 4; ++i)
			if (sent[i].low || sent[i].high) Send(i, Motors{});
	LeaveCriticalSection(&lock);
	Restore();
}
}
