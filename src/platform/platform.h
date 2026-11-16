// SPDX-License-Identifier: MIT
// The only place that talks to the operating system: the path of this module and loading a shared library.
#pragma once

#include <string>

namespace genonline_eos
{
namespace platform
{

// The full path of the plugin library or program that holds this code, or an empty string when unknown.
std::string ThisModulePath();

// True when the path of this module is known and short enough for the EOS SDK, which loads its parts from that folder.
bool ThisModuleFolderIsUsable();

// Loads a shared library by its full path. Returns null and a reason in error when that fails. The library stays loaded.
void* OpenLibrary(const std::string& path, std::string* error);

// Finds an exported symbol of a library OpenLibrary returned. Returns null when library is null or the symbol is missing.
void* FindSymbol(void* library, const char* name);

} // namespace platform
} // namespace genonline_eos
