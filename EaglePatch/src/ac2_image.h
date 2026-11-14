#pragma once

#include <windows.h>
#include <stdint.h>

namespace ac2_image
{
inline bool IsReadable(const void* address, size_t size)
{
	if (!address || !size)
		return false;
	MEMORY_BASIC_INFORMATION memory = {};
	if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
		(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return false;
	const uintptr_t start = reinterpret_cast<uintptr_t>(address);
	const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
	return start <= regionEnd && size <= regionEnd - start;
}

inline bool IsVerifiedSteamImage(HMODULE module)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(module);
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (!IsReadable(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(*dos)) ||
		dos->e_lfanew > 0x1000)
		return false;
	const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
	return IsReadable(nt, sizeof(*nt)) && nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
		nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && nt->FileHeader.TimeDateStamp == 0x5925BA62 &&
		nt->OptionalHeader.SizeOfImage == 0x2190000;
}
}
