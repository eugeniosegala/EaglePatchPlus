#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

namespace ac2_highlights
{
namespace shader
{
// Shader-local DEF constants override the game's later constant uploads. Only
// the named targeting intensity is disabled; lighting and other effects remain.
inline bool IsIntensity(const unsigned char* table, size_t bytes, size_t offset)
{
	static const char name[] = "TargettingRimFXIntensity";
	const size_t length = sizeof(name) - 1;
	if (offset >= bytes || bytes - offset <= length || memcmp(table + offset, name, length) != 0)
		return false;
	offset += length;
	if (table[offset] == 0)
		return true;
	if (table[offset++] != '_' || offset >= bytes || table[offset] < '0' || table[offset] > '9')
		return false;
	while (offset < bytes && table[offset] >= '0' && table[offset] <= '9')
		++offset;
	return offset < bytes && table[offset] == 0;
}

inline uint16_t Read16(const unsigned char* data)
{
	uint16_t value;
	memcpy(&value, data, sizeof(value));
	return value;
}

inline uint32_t Read32(const unsigned char* data)
{
	uint32_t value;
	memcpy(&value, data, sizeof(value));
	return value;
}

inline bool FindIntensity(const unsigned char* table, size_t bytes, unsigned int& reg)
{
	if (bytes < 28 || Read32(table) != 28 || Read32(table + 8) != 0xFFFF0300)
		return false;
	const size_t count = Read32(table + 12);
	const size_t info = Read32(table + 16);
	if (count > 512 || info > bytes || count > (bytes - info) / 20)
		return false;
	bool found = false;
	for (size_t index = 0; index < count; ++index)
	{
		const unsigned char* constant = table + info + index * 20;
		if (!IsIntensity(table, bytes, Read32(constant)))
			continue;
		const size_t type = Read32(constant + 12);
		if (found || Read16(constant + 4) != 2 || Read16(constant + 8) != 1 ||
			type > bytes || bytes - type < 16 || Read16(table + type) != 0 ||
			Read16(table + type + 2) != 3 || Read16(table + type + 4) != 1 ||
			Read16(table + type + 6) != 1 || Read16(table + type + 8) != 1 ||
			Read16(table + type + 10) != 0)
			return false;
		reg = Read16(constant + 6);
		if (reg >= 224)
			return false;
		found = true;
	}
	if (!found)
		return false;
	// Packed or aliased uniforms must not lose an unrelated component.
	for (size_t index = 0; index < count; ++index)
	{
		const unsigned char* constant = table + info + index * 20;
		const unsigned int start = Read16(constant + 6);
		const unsigned int length = Read16(constant + 8);
		if (Read16(constant + 4) == 2 && reg >= start && reg - start < length &&
			!IsIntensity(table, bytes, Read32(constant)))
			return false;
	}
	return true;
}

// A palette channel is a white/grey/silver highlight when it is finite, visibly
// lit and nearly colourless (a slight blue or warm tint included). Saturated
// channels such as red, blue or gold Eagle Vision highlights are kept.
inline bool IsNeutralHighlight(float red, float green, float blue, float baseRed, float baseGreen, float baseBlue)
{
	const float values[6] = {red, green, blue, baseRed, baseGreen, baseBlue};
	for (float value : values)
	{
		uint32_t bits;
		memcpy(&bits, &value, sizeof(bits));
		if ((bits & 0x7F800000u) == 0x7F800000u)
			return false;
	}
	const float maximum = red > green ? (red > blue ? red : blue) : (green > blue ? green : blue);
	const float minimum = red < green ? (red < blue ? red : blue) : (green < blue ? green : blue);
	if (maximum < 0.05f || maximum - minimum > maximum * 0.3f)
		return false;
	return red != baseRed || green != baseGreen || blue != baseBlue;
}

// Measures a shader model 2+ token stream in place (any alignment). Returns the
// word count through the end token, or 0 when the stream is not complete.
inline size_t Length(const unsigned char* data, size_t availableBytes)
{
	const size_t available = availableBytes / sizeof(uint32_t);
	if (!data || available < 2)
		return 0;
	const uint32_t version = Read32(data);
	if ((version & 0xFFFF0000) != 0xFFFF0000 || ((version >> 8) & 0xFF) < 2)
		return 0;
	for (size_t index = 1; index < available;)
	{
		const uint32_t token = Read32(data + index * sizeof(uint32_t));
		if (token == 0x0000FFFF)
			return index + 1;
		const size_t length = (token & 0xFFFF) == 0xFFFE ? (token >> 16) & 0x7FFF : (token >> 24) & 0xF;
		if (length > available - index - 1)
			return 0;
		index += length + 1;
	}
	return 0;
}

// True when the shader's constant table names any targeting-rim parameter.
inline bool HasTargeting(const uint32_t* words, size_t availableWords)
{
	static const char name[] = "TargettingRimFX";
	if (!words || availableWords < 2 || (words[0] & 0xFFFF0000) != 0xFFFF0000)
		return false;
	for (size_t index = 1; index < availableWords;)
	{
		const uint32_t token = words[index];
		if ((token & 0xFFFF) != 0xFFFE)
			return false;
		const size_t length = (token >> 16) & 0x7FFF;
		if (length > availableWords - index - 1)
			return false;
		if (length && words[index + 1] == 0x42415443)
		{
			const unsigned char* table = reinterpret_cast<const unsigned char*>(words + index + 2);
			const size_t bytes = (length - 1) * sizeof(uint32_t);
			for (size_t offset = 0; offset + sizeof(name) - 1 <= bytes; ++offset)
				if (memcmp(table + offset, name, sizeof(name) - 1) == 0)
					return true;
			return false;
		}
		index += length + 1;
	}
	return false;
}

inline bool Rewrite(const uint32_t* words, size_t availableWords, std::vector<uint32_t>& output)
{
	output.clear();
	if (!words || availableWords < 2 || words[0] != 0xFFFF0300)
		return false;
	const unsigned char* table = nullptr;
	size_t tableBytes = 0;
	size_t end = 0;
	size_t insertion = 1;
	bool declarations = true;
	for (size_t index = 1; index < availableWords;)
	{
		const uint32_t token = words[index];
		const unsigned int opcode = token & 0xFFFF;
		if (token == 0x0000FFFF)
		{
			end = index + 1;
			break;
		}
		const size_t length = opcode == 0xFFFE ? (token >> 16) & 0x7FFF : (token >> 24) & 0xF;
		if (length > availableWords - index - 1 || opcode == 0xFFFF ||
			(opcode != 0xFFFE && length == 0 && opcode != 0 && opcode != 28 && opcode != 29 &&
				opcode != 39 && opcode != 42 && opcode != 43 && opcode != 44))
			return false;
		if (opcode == 0xFFFE && length && words[index + 1] == 0x42415443)
		{
			if (table)
				return false;
			table = reinterpret_cast<const unsigned char*>(words + index + 2);
			tableBytes = (length - 1) * sizeof(uint32_t);
		}
		if (opcode != 0xFFFE && opcode != 31 && opcode != 47 && opcode != 48 && opcode != 81)
			declarations = false;
		index += length + 1;
		if (declarations)
			insertion = index;
	}
	unsigned int reg = 0;
	if (!end || !table || !FindIntensity(table, tableBytes, reg))
		return false;
	for (size_t index = 1; index < end - 1;)
	{
		const unsigned int opcode = words[index] & 0xFFFF;
		const size_t length = opcode == 0xFFFE ? (words[index] >> 16) & 0x7FFF : (words[index] >> 24) & 0xF;
		if (opcode == 81)
		{
			if (length != 5)
				return false;
			const uint32_t dest = words[index + 1];
			const unsigned int type = ((dest >> 28) & 7) | ((dest >> 8) & 24);
			if (type == 2 && (dest & 0x7FF) == reg)
				return false;
		}
		index += length + 1;
	}
	const uint32_t definition[] = {0x05000051, 0xA00F0000 | reg, 0, 0, 0, 0};
	output.reserve(end + 6);
	output.insert(output.end(), words, words + insertion);
	output.insert(output.end(), definition, definition + 6);
	output.insert(output.end(), words + insertion, words + end);
	return true;
}
}
}
