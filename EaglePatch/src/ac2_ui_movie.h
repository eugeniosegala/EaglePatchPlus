#pragma once

#include <stdint.h>
#include <string.h>
#include <vector>

namespace ac2_ui
{
namespace movie
{
using Bytes = std::vector<unsigned char>;
static const char marker[] = "EaglePatchPlus_UIScale_v1";
inline uint16_t U16(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t U32(const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline void Put16(Bytes& b, uint16_t v) { b.push_back((unsigned char)v); b.push_back((unsigned char)(v >> 8)); }
inline void Put32(Bytes& b, uint32_t v) { for (unsigned i = 0; i < 4; ++i) b.push_back((unsigned char)(v >> (i * 8))); }
inline bool ValidScale(float scale)
{
	uint32_t bits; memcpy(&bits, &scale, 4);
	return (bits & 0x7f800000) != 0x7f800000 && scale >= 0.5f && scale <= 1.0f;
}
inline void PushString(Bytes& actions, const char* text)
{
	const size_t length = strlen(text) + 1;
	actions.push_back(0x96); Put16(actions, (uint16_t)(length + 1)); actions.push_back(0);
	actions.insert(actions.end(), text, text + length);
}
inline void PushNumber(Bytes& actions, float value)
{
	actions.push_back(0x96); Put16(actions, 5); actions.push_back(1);
	uint32_t bits; memcpy(&bits, &value, 4); Put32(actions, bits);
}
inline void Variable(Bytes& actions, const char* name) { PushString(actions, name); actions.push_back(0x1c); }
inline void Root(Bytes& actions) { Variable(actions, "_root"); }
inline void Property(Bytes& actions, const char* key, float scale, float offset, const char* object = "_root")
{
	Variable(actions, object); PushString(actions, key);
	Variable(actions, object); PushString(actions, key); actions.push_back(0x4e);
	PushNumber(actions, scale); actions.push_back(0x0c);
	if (offset != 0) { PushNumber(actions, offset); actions.push_back(0x0a); }
	actions.push_back(0x4f);
}
inline Bytes ComponentActions(float scale, float width, float height)
{
	// The master loads these full-screen HUD movies asynchronously into separate clips.
	// Compensate their shared root's centered transform in the parent's coordinates.
	struct Component { const char* name; float x, y; };
	const Component components[] = {{"HUD_Controls",1,0},{"HUD_Equipment",0,1},{"HUD_Money",0,1},{"HUD_Subtitles",0.5f,1}};
	Bytes body, actions;
	for (const auto& component : components)
	{
		Bytes adjust;
		Property(adjust, "_x", 1, (component.x - 0.5f) * width * (1 - scale) / scale, "target");
		Property(adjust, "_y", 1, (component.y - 0.5f) * height * (1 - scale) / scale, "target");
		Variable(body, "target"); PushString(body, "_name"); body.push_back(0x4e);
		PushString(body, component.name); body.push_back(0x49); body.push_back(0x12);
		body.push_back(0x9d); Put16(body, 2); Put16(body, (uint16_t)adjust.size());
		body.insert(body.end(), adjust.begin(), adjust.end());
	}
	Variable(body, "target"); PushString(body, marker); PushNumber(body, scale); body.push_back(0x4f);
	Variable(actions, "target"); PushString(actions, marker); actions.push_back(0x4e);
	actions.push_back(0x9d); Put16(actions, 2); Put16(actions, (uint16_t)body.size());
	actions.insert(actions.end(), body.begin(), body.end()); actions.push_back(0);
	return actions;
}
inline Bytes ComponentListener(float scale, float width, float height)
{
	Bytes actions, function = ComponentActions(scale, width, height), header;
	// Add a load listener alongside the game's listener, after its initialization completes.
	PushString(actions, "onLoadInit");
	header.push_back(0); Put16(header, 1);
	const char parameter[] = "target"; header.insert(header.end(), parameter, parameter + sizeof(parameter));
	Put16(header, (uint16_t)function.size());
	actions.push_back(0x9b); Put16(actions, (uint16_t)header.size());
	actions.insert(actions.end(), header.begin(), header.end()); actions.insert(actions.end(), function.begin(), function.end());
	PushNumber(actions, 1); actions.push_back(0x43); // {onLoadInit: function(target) {...}}
	PushNumber(actions, 1); Root(actions); PushString(actions, "hudMasterScript"); actions.push_back(0x4e);
	PushString(actions, "m_MovieClipLoader"); actions.push_back(0x4e);
	PushString(actions, "addListener"); actions.push_back(0x52); actions.push_back(0x17);
	return actions;
}
inline Bytes Actions(float scale, float x, float y, const Bytes& extras = Bytes())
{
	// AVM1 GetVariable/GetMember/SetMember; see Adobe's SWF 10 format specification.
	// The marker prevents repeated first-frame actions from compounding the scale.
	Bytes body, actions;
	Property(body, "_xscale", scale, 0); Property(body, "_yscale", scale, 0);
	Property(body, "_x", scale, x * (1 - scale)); Property(body, "_y", scale, y * (1 - scale));
	body.insert(body.end(), extras.begin(), extras.end());
	Root(body); PushString(body, marker); PushNumber(body, scale); body.push_back(0x4f);
	Root(actions); PushString(actions, marker); actions.push_back(0x4e);
	actions.push_back(0x9d); Put16(actions, 2); Put16(actions, (uint16_t)body.size());
	actions.insert(actions.end(), body.begin(), body.end()); actions.push_back(0);
	return actions;
}
inline void Anchor(const char* name, float& x, float& y)
{
	x = y = 0.5f;
	if (strstr(name, "HudMiniMap")) { x = 1; y = 1; }
	else if (strstr(name, "HudControlsManager")) { x = 1; y = 0; }
	else if (strstr(name, "HudHealthBar") || strstr(name, "HudMissionState")) { x = 0; y = 0; }
	else if (strstr(name, "HudMoney")) { x = 0; y = 1; }
}
inline bool Patch(const unsigned char* data, size_t size, float scale, const char* type, Bytes& output)
{
	output.clear();
	if (!data || !type || !ValidScale(scale) || scale == 1 || size < 16 || size > 16 * 1024 * 1024 ||
		(memcmp(data, "UEF", 3) && memcmp(data, "FWS", 3)) || data[3] < 5 || data[3] > 10 || U32(data + 4) != size ||
		strstr(type, "HudIconManager")) return false;
	const unsigned bits = data[8] >> 3;
	if (!bits || bits > 30) return false;
	const size_t rectBytes = (5 + 4 * bits + 7) / 8;
	if (8 + rectBytes + 4 > size) return false;
	int32_t bounds[4] = {};
	unsigned bit = 5;
	for (auto& value : bounds)
	{
		uint32_t raw = 0;
		for (unsigned i = 0; i < bits; ++i, ++bit) raw = (raw << 1) | ((data[8 + bit / 8] >> (7 - bit % 8)) & 1);
		if (raw & (uint32_t(1) << (bits - 1))) raw |= ~((uint32_t(1) << bits) - 1);
		value = (int32_t)raw;
	}
	if (bounds[1] <= bounds[0] || bounds[3] <= bounds[2]) return false;
	size_t position = 8 + rectBytes + 4, frame = 0;
	bool ended = false;
	while (position + 2 <= size)
	{
		const size_t start = position;
		const unsigned header = U16(data + position), tag = header >> 6;
		size_t length = header & 63; position += 2;
		if (length == 63)
		{
			if (size - position < 4) return false;
			length = U32(data + position); position += 4;
		}
		if (length > size - position) return false;
		if (tag == 69 && (length != 4 || (U32(data + position) & 8))) return false;
		if (tag == 12 && !frame)
		{
			for (size_t i = 0; i + sizeof(marker) <= length; ++i)
				if (!memcmp(data + position + i, marker, sizeof(marker))) return false;
		}
		if (tag == 1 && !frame) { if (length) return false; frame = start; }
		position += length;
		if (tag == 0) { if (length) return false; ended = true; break; }
	}
	if (!ended || !frame) return false;
	for (; position < size; ++position) if (data[position]) return false;
	float ax, ay; Anchor(type, ax, ay);
	ax = (bounds[0] + (bounds[1] - bounds[0]) * ax) / 20.0f;
	ay = (bounds[2] + (bounds[3] - bounds[2]) * ay) / 20.0f;
	const Bytes extras = !strcmp(type, "MultiComponentShowingFireItem") ?
		ComponentListener(scale, (bounds[1] - bounds[0]) / 20.0f, (bounds[3] - bounds[2]) / 20.0f) : Bytes();
	Bytes actions = Actions(scale, ax, ay, extras), tag;
	Put16(tag, (12 << 6) | 63); Put32(tag, (uint32_t)actions.size()); tag.insert(tag.end(), actions.begin(), actions.end());
	output.reserve(size + tag.size());
	output.insert(output.end(), data, data + frame); output.insert(output.end(), tag.begin(), tag.end()); output.insert(output.end(), data + frame, data + size);
	const uint32_t newSize = (uint32_t)output.size();
	for (unsigned i = 0; i < 4; ++i) output[4 + i] = (unsigned char)(newSize >> (8 * i));
	return true;
}
}
}
