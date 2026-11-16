// SPDX-License-Identifier: MIT
// Copies a string into a buffer the host owns. Pure logic without EOS.
#pragma once

#include <cstdint>

namespace genonline_eos
{

// Writes value with its NUL into buffer and returns true. Returns false and leaves buffer untouched when value is null or empty, buffer is null, or value and its NUL do not fit in bufferSize.
bool CopyToHostBuffer(const char* value, char* buffer, uint32_t bufferSize);

} // namespace genonline_eos
