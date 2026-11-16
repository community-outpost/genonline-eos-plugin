// SPDX-License-Identifier: MIT
#include "platform/platform.h"

#include <windows.h>

namespace genonline_eos
{
namespace platform
{

namespace
{

// An address inside this module, so the lookup finds the library that holds this code.
void ModuleAnchor()
{
}

std::string ToUtf8(const wchar_t* text, int length)
{
	const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
	if (size <= 0)
	{
		return std::string();
	}
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, length, &result[0], size, nullptr, nullptr);
	return result;
}

std::wstring FromUtf8(const std::string& text)
{
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	if (size <= 0)
	{
		return std::wstring();
	}
	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size);
	return result;
}

} // namespace

std::string ThisModulePath()
{
	HMODULE self = nullptr;
	const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&ModuleAnchor), &self))
	{
		return std::string();
	}
	wchar_t path[MAX_PATH];
	const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return std::string();
	}
	return ToUtf8(path, static_cast<int>(length));
}

bool ThisModuleFolderIsUsable()
{
	// ThisModulePath is empty when the path does not fit MAX_PATH, which the EOS SDK cannot handle.
	return !ThisModulePath().empty();
}

void* OpenLibrary(const std::string& path, std::string* error)
{
	// The altered search path finds the libraries next to the loaded one only for a path with backslashes.
	std::wstring windowsPath = FromUtf8(path);
	for (wchar_t& c : windowsPath)
	{
		if (c == L'/')
		{
			c = L'\\';
		}
	}
	HMODULE library = LoadLibraryExW(windowsPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (library == nullptr && error != nullptr)
	{
		*error = "LoadLibraryEx failed with error " + std::to_string(GetLastError());
	}
	return reinterpret_cast<void*>(library);
}

void* FindSymbol(void* library, const char* name)
{
	if (library == nullptr || name == nullptr)
	{
		return nullptr;
	}
	return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
}

} // namespace platform
} // namespace genonline_eos
