// SPDX-License-Identifier: MIT
// The plugin side copy of the host table and the log entry point. Pure logic without EOS.
#pragma once

#include <GenOnlinePluginApi.h>

#include <string>

namespace genonline_eos
{

class HostLink
{
public:
	// Copies the whole fields inside host->structSize. A host table too short for enabledCapabilities enables offeredCapabilities.
	void Bind(const GenOnlinePluginHostApi& host, uint32_t offeredCapabilities);

	// Forgets the host table. No host callback runs after this.
	void Release();

	bool IsBound() const
	{
		return m_bound;
	}

	uint32_t EnabledCapabilities() const
	{
		return m_table.enabledCapabilities;
	}

	bool IsEnabled(uint32_t capability) const
	{
		return (m_table.enabledCapabilities & capability) == capability;
	}

	// The bound host table. Callbacks outside the host structSize are null.
	const GenOnlinePluginHostApi& Table() const
	{
		return m_table;
	}

	// Sends one line with the plugin prefix to the host Log.
	void Log(int32_t level, const char* message) const;

	// Formats one line like printf and sends it to the host Log.
	void Logf(int32_t level, const char* format, ...) const;

private:
	GenOnlinePluginHostApi m_table = {};
	bool m_bound = false;
};

// Prefix of every plugin log line.
extern const char* const kLogPrefix;

// Returns message with the plugin prefix.
std::string PrefixLogLine(const char* message);

} // namespace genonline_eos
