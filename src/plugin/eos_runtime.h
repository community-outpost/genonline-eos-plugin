// SPDX-License-Identifier: MIT
// The EOS SDK and platform lifecycle behind Initialize, Tick and Shutdown.
#pragma once

#include "core/credentials.h"

#include <eos_sdk.h>

#include <vector>

namespace genonline_eos
{

class HostLink;
class RuntimeModule;

class EosRuntime
{
public:
	// Adds a module that starts with the platform. Modules stop in reverse order.
	void AddModule(RuntimeModule* module);

	// Initializes the SDK, routes its log to the host and creates the platform. Returns a GENONLINE_PLUGIN_RESULT_ code.
	int32_t Start(const Credentials& credentials, const char* productVersion);

	// Ticks the platform and the modules and hands queued SDK log lines to the host.
	void Tick();

	// Stops the modules, releases the platform and shuts the SDK down. A second call does nothing.
	void Stop();

	EOS_HPlatform Platform() const
	{
		return m_platform;
	}

private:
	enum class SdkOwnership
	{
		None,
		Owned,
		Borrowed,
	};

	int32_t InitializeSdk(const char* productVersion);
	void FlushSdkLog();

	std::vector<RuntimeModule*> m_modules;
	std::vector<RuntimeModule*> m_startedModules;
	EOS_HPlatform m_platform = nullptr;
	SdkOwnership m_sdk = SdkOwnership::None;
};

} // namespace genonline_eos
