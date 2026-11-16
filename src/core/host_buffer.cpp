// SPDX-License-Identifier: MIT
#include "core/host_buffer.h"

#include <cstring>

namespace genonline_eos
{

bool CopyToHostBuffer(const char* value, char* buffer, uint32_t bufferSize)
{
	if (value == nullptr || value[0] == '\0' || buffer == nullptr)
	{
		return false;
	}
	const size_t length = std::strlen(value);
	if (length >= bufferSize)
	{
		return false;
	}
	std::memcpy(buffer, value, length + 1);
	return true;
}

} // namespace genonline_eos
