#pragma once

#include <stdint.h>
#include <math.h>
#include <string.h>

namespace ac2_rumble
{
enum class Effect : unsigned int { None, Attack, Parry, Landing, Climb, Grab, SoftLanding, Count };
struct Motors { uint16_t low = 0, high = 0; };
struct Config
{
	bool enabled = true;
	unsigned int combat = 70, landing = 80, climbing = 35;
};
inline unsigned int Percent(unsigned int value) { return value > 100 ? 100 : value; }
inline Effect CombatEffect(bool committed, unsigned int response, bool ownerIsPlayer, bool targetIsPlayer, bool validTarget)
{
	if (!committed || !validTarget) return Effect::None;
	if (ownerIsPlayer && !targetIsPlayer && (response == 3 || response == 10)) return Effect::Attack;
	if (!ownerIsPlayer && targetIsPlayer && (response == 2 || response == 4 || response == 5 || response == 7)) return Effect::Parry;
	return Effect::None;
}
class Mixer
{
	struct Pulse { uint32_t start = 0, duration = 0; Motors motors; bool active = false, emitted = false; };
	Pulse pulses[(unsigned int)Effect::Count];
public:
	void Clear() { for (auto& p : pulses) p = Pulse{}; }
	bool Trigger(Effect effect, unsigned int strength, uint32_t now)
	{
		const unsigned int i = (unsigned int)effect;
		if (!strength || !i || i >= (unsigned int)Effect::Count) return false;
		static const uint32_t durations[] = {0, 85, 65, 160, 35, 55, 80};
		static const uint32_t cooldowns[] = {0, 80, 100, 250, 170, 170, 150};
		static const Motors levels[] = {{0,0}, {26000,41000}, {32000,50000}, {54000,22000}, {10000,28000}, {18000,36000}, {12000,50000}};
		auto& p = pulses[i];
		if (p.emitted && now - p.start < cooldowns[i]) return false;
		strength = Percent(strength);
		p.start = now; p.duration = durations[i]; p.active = p.emitted = true;
		p.motors = {(uint16_t)(levels[i].low * strength / 100), (uint16_t)(levels[i].high * strength / 100)};
		return true;
	}
	Motors Mix(Motors native, uint32_t now)
	{
		for (auto& p : pulses)
		{
			if (!p.active) continue;
			const uint32_t age = now - p.start;
			if (age >= p.duration) { p.active = false; continue; }
			const uint16_t low = (uint16_t)((uint32_t)p.motors.low * (p.duration - age) / p.duration);
			const uint16_t high = (uint16_t)((uint32_t)p.motors.high * (p.duration - age) / p.duration);
			if (low > native.low) native.low = low;
			if (high > native.high) native.high = high;
		}
		return native;
	}
};
struct MotionSample { uint32_t player = 0, time = 0, context = 0; float x = 0, y = 0, z = 0; };
struct MotionEffect { Effect effect = Effect::None; unsigned int strength = 100; };
class MotionTracker
{
	MotionSample previous;
	bool ready = false, falling = false, settling = false;
	float peak = 0, speed = 0, climbed = 0;
	uint32_t fallStart = 0, settleStart = 0;
	static bool Finite(float value)
	{
		uint32_t bits; memcpy(&bits, &value, sizeof(bits));
		return (bits & 0x7f800000) != 0x7f800000;
	}
	static bool Climbing(uint32_t context) { return context == 5 || context == 7 || context == 8 || context == 9; }
	static bool Ground(uint32_t context) { return context == 4 || context == 10 || context == 19; }
public:
	void Reset() { ready = falling = settling = false; climbed = speed = 0; }
	MotionEffect Update(MotionSample sample)
	{
		MotionEffect result;
		if (!sample.player || !Finite(sample.x) || !Finite(sample.y) || !Finite(sample.z)) { Reset(); return result; }
		if ((sample.context < 4 || sample.context > 10) && sample.context != 19) { Reset(); return result; }
		if (!ready || previous.player != sample.player || sample.time - previous.time > 250)
		{ Reset(); previous = sample; ready = true; return result; }
		const uint32_t elapsed = sample.time - previous.time;
		if (!elapsed) return result;
		const float dx = sample.x - previous.x, dy = sample.y - previous.y, dz = sample.z - previous.z;
		const float distanceSquared = dx * dx + dy * dy + dz * dz;
		if (distanceSquared > 400.0f) { Reset(); previous = sample; ready = true; return result; }
		const float verticalSpeed = dz * (1000.0f / elapsed);
		if ((sample.context == 6 || previous.context == 6) && !falling)
		{
			falling = true; peak = previous.z; speed = 0; fallStart = previous.time;
		}
		if (falling)
		{
			if (sample.z > peak) peak = sample.z;
			if (-verticalSpeed > speed) speed = -verticalSpeed;
		}
		if (Climbing(sample.context))
		{
			if (sample.context != 9 || verticalSpeed >= -0.8f) falling = settling = false;
			if (!Climbing(previous.context) || (sample.context == 7 && previous.context != 7))
			{ result.effect = Effect::Grab; climbed = 0; }
			else
			{
				climbed += sqrtf(distanceSquared);
				if (climbed >= 0.55f) { result.effect = Effect::Climb; climbed = 0; }
			}
		}
		else
		{
			climbed = 0;
			// Narrow-object jumps can leave the airborne context before contact.
			// Keep tracking until descent actually stops on a ground context.
			if (falling && Ground(sample.context) && fabsf(verticalSpeed) < 0.8f)
			{
				const float drop = peak - sample.z;
				if (!settling) { settling = true; settleStart = sample.time; }
				if ((drop >= 0.10f && speed >= 0.5f) || sample.time - settleStart >= 300)
				{
					if (drop >= 1.3f && speed >= 3.0f && sample.time - fallStart >= 40)
					{
						result.effect = Effect::Landing;
						result.strength = Percent(50 + (unsigned int)(drop * 8.0f));
					}
					else if (drop >= 0.10f && speed >= 0.5f && sample.time - fallStart >= 40)
					{
						result.effect = Effect::SoftLanding;
						result.strength = 65;
					}
					falling = settling = false;
				}
			}
			else settling = false;
		}
		previous = sample;
		return result;
	}
};
}
