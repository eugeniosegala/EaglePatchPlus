#include <stdio.h>
#include <limits>
#include <initializer_list>
#include "../EaglePatch/src/ac2_rumble_effects.h"
using namespace ac2_rumble;
static unsigned int failures = 0;
static void Check(bool passed, const char* text, unsigned int line)
{
	if (!passed) { fprintf(stderr, "Line %u: %s\n", line, text); ++failures; }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
static bool Silent(Motors value) { return value.low == 0 && value.high == 0; }

static void Combat()
{
	// Confirmed outgoing hits and kills, incoming damage, and defensive results.
	CHECK(CombatEffect(true, 3, true, false, true) == Effect::Attack);
	CHECK(CombatEffect(true, 10, true, false, true) == Effect::Attack);
	CHECK(CombatEffect(true, 3, false, true, true) == Effect::None);
	CHECK(CombatEffect(true, 3, false, false, true) == Effect::None);
	CHECK(CombatEffect(false, 3, true, false, true) == Effect::None);
	CHECK(CombatEffect(true, 3, true, false, false) == Effect::None);
	for (unsigned int type : {2u, 4u, 5u, 7u})
	{
		CHECK(CombatEffect(true, type, false, true, true) == Effect::Parry);
		CHECK(CombatEffect(true, type, true, false, true) == Effect::None);
	}
	for (unsigned int type : {0u, 1u, 6u, 8u, 9u, 99u})
		CHECK(CombatEffect(true, type, false, true, true) == Effect::None);
}
static void Mixing()
{
	Mixer mixer;
	CHECK(mixer.Trigger(Effect::Attack, 70, 100));
	CHECK(!mixer.Trigger(Effect::Attack, 70, 117)); // duplicate notifications from one contact
	CHECK(!Silent(mixer.Mix({}, 100)));
	const Motors stock = {60000, 65535};
	const Motors combined = mixer.Mix(stock, 101);
	CHECK(combined.low == stock.low && combined.high == stock.high);
	const Motors faded = mixer.Mix({}, 150);
	const Motors initial = mixer.Mix({}, 100);
	CHECK(faded.low < initial.low && faded.high < initial.high);
	CHECK(Silent(mixer.Mix({}, 185)));
	CHECK(mixer.Mix(stock, 200).high == stock.high);
	CHECK(!mixer.Trigger(Effect::Parry, 0, 200));
	CHECK(mixer.Trigger(Effect::Landing, 100, 200));
	mixer.Clear();
	CHECK(Silent(mixer.Mix({}, 201)));
	CHECK(mixer.Trigger(Effect::Grab, 100, UINT32_MAX - 10));
	CHECK(!Silent(mixer.Mix({}, 20)));
	CHECK(Silent(mixer.Mix({}, 45)));
	CHECK(Percent(500) == 100);
}
static MotionSample At(uint32_t time, uint32_t context, float z, float x = 0, uint32_t player = 1)
{
	return {player, time, context, x, 0, z};
}
static void Movement()
{
	MotionTracker tracker;
	CHECK(tracker.Update(At(0, 4, 10)).effect == Effect::None);
	tracker.Update(At(100, 6, 10));
	tracker.Update(At(200, 6, 9));
	tracker.Update(At(300, 6, 7));
	CHECK(tracker.Update(At(400, 4, 6)).effect == Effect::Landing);
	CHECK(tracker.Update(At(500, 4, 6)).effect == Effect::None);
	// Small hops produce a distinct light contact, rather than a heavy landing.
	tracker.Update(At(600, 6, 6.4f));
	tracker.Update(At(700, 6, 6.1f));
	CHECK(tracker.Update(At(800, 4, 6)).effect == Effect::SoftLanding);
	CHECK(tracker.Update(At(900, 4, 6)).effect == Effect::None);
	CHECK(tracker.Update(At(1000, 7, 6)).effect == Effect::Grab);
	CHECK(tracker.Update(At(1100, 8, 6)).effect == Effect::None);
	CHECK(tracker.Update(At(1200, 8, 6.7f)).effect == Effect::Climb);
	for (uint32_t t = 1300; t < 2000; t += 100)
		CHECK(tracker.Update(At(t, 8, 6.7f)).effect == Effect::None);
	tracker.Update(At(2000, 6, 6.7f));
	CHECK(tracker.Update(At(2100, 7, 5)).effect == Effect::Grab); // catching a fall is a grab, not ground contact
	// Loading, teleports, lost samples, invalid positions and player changes reset tracking.
	CHECK(tracker.Update(At(2200, 4, -1000)).effect == Effect::None);
	CHECK(tracker.Update(At(3200, 7, 5)).effect == Effect::None);
	CHECK(tracker.Update(At(3300, 7, 6, 0, 2)).effect == Effect::None);
	CHECK(tracker.Update(At(3400, 8, std::numeric_limits<float>::quiet_NaN())).effect == Effect::None);
	CHECK(tracker.Update(At(3500, 8, 6)).effect == Effect::None);
	CHECK(tracker.Update(At(3600, 25, 6)).effect == Effect::None);
	CHECK(tracker.Update(At(3700, 7, 6)).effect == Effect::None);
	tracker.Reset();
	tracker.Update(At(UINT32_MAX - 50, 7, 6));
	CHECK(tracker.Update(At(49, 8, 6.7f)).effect == Effect::Climb);
	tracker.Reset();
	tracker.Update(At(0, 4, 10));
	tracker.Update(At(100, 4, 9));
	tracker.Update(At(200, 4, 7));
	tracker.Update(At(300, 4, 6));
	CHECK(tracker.Update(At(400, 4, 6)).effect == Effect::None); // walking downstairs is not a fall
	tracker.Reset();
	tracker.Update(At(0, 10, 10));
	tracker.Update(At(100, 6, 10.5f));
	CHECK(tracker.Update(At(200, 10, 11)).effect == Effect::None); // airborne context ends early
	CHECK(tracker.Update(At(300, 10, 10)).effect == Effect::None);
	CHECK(tracker.Update(At(400, 10, 8)).effect == Effect::None);
	CHECK(tracker.Update(At(500, 10, 7)).effect == Effect::Landing);
	CHECK(tracker.Update(At(600, 10, 7)).effect == Effect::None);
}
static void LandingRegressions()
{
	MotionTracker rolling;
	rolling.Update(At(0, 4, 5));
	rolling.Update(At(100, 6, 5));
	rolling.Update(At(200, 6, 3));
	CHECK(rolling.Update(At(300, 4, 2.7f, 1)).effect == Effect::Landing);
	CHECK(rolling.Update(At(400, 4, 2.4f, 2)).effect == Effect::None);

	MotionTracker narrow;
	narrow.Update(At(0, 10, 5));
	narrow.Update(At(100, 6, 5));
	CHECK(narrow.Update(At(200, 10, 3)).effect == Effect::None);
	CHECK(narrow.Update(At(300, 10, 2.7f)).effect == Effect::Landing);
	CHECK(narrow.Update(At(400, 10, 2.4f)).effect == Effect::None);

	MotionTracker drop;
	drop.Update(At(0, 7, 4));
	CHECK(drop.Update(At(16, 4, 2)).effect == Effect::Landing);
	CHECK(drop.Update(At(32, 4, 2)).effect == Effect::None);

	MotionTracker hitch;
	hitch.Update(At(0, 4, 5));
	hitch.Update(At(100, 6, 5));
	CHECK(hitch.Update(At(500, 4, 2)).effect == Effect::Landing);

	// An upward climb-out, stairs and long interruptions must remain silent.
	drop.Reset();
	drop.Update(At(0, 7, 4));
	CHECK(drop.Update(At(16, 4, 4.3f)).effect == Effect::None);
	hitch.Reset();
	hitch.Update(At(0, 4, 5));
	hitch.Update(At(100, 6, 5));
	CHECK(hitch.Update(At(1500, 4, 2)).effect == Effect::None);
	drop.Reset();
	drop.Update(At(0, 7, 4));
	CHECK(drop.Update(At(100, 4, -100)).effect == Effect::None);

	Mixer pulse;
	CHECK(pulse.Trigger(Effect::Landing, 80, 100));
	const Motors start = pulse.Mix({}, 100), held = pulse.Mix({}, 120);
	CHECK(held.low == start.low && held.high == start.high);
	CHECK(pulse.Mix({}, 180).low < held.low);
	CHECK(Silent(pulse.Mix({}, 260)));
}
int main()
{
	Combat(); Mixing(); Movement(); LandingRegressions();
	if (failures) return 1;
	puts("Rumble tests passed: confirmed combat outcomes, native mixing, expiration, movement contacts and reset safety.");
	return 0;
}
