// SPDX-License-Identifier: MIT
// The seam through which the linked account, anticheat and transport parts join the EOS lifecycle.
#pragma once

#include <eos_sdk.h>

namespace genonline_eos
{

// A part of the plugin that needs the EOS platform. Every call runs on the main thread.
class RuntimeModule
{
public:
	virtual ~RuntimeModule()
	{
	}

	// Short name for the log.
	virtual const char* Name() const = 0;

	// Runs once after the platform exists. The platform stays valid until Stop returns.
	virtual void Start(EOS_HPlatform platform) = 0;

	// Runs once per host Tick after the platform ticked.
	virtual void Tick() = 0;

	// Ends every session and connection and removes every notification. No host callback runs after it returns.
	virtual void Stop() = 0;
};

} // namespace genonline_eos
