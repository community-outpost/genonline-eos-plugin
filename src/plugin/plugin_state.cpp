// SPDX-License-Identifier: MIT
#include "plugin/plugin_state.h"

#include "plugin/eos_runtime.h"

namespace genonline_eos
{

HostLink& Host()
{
	static HostLink host;
	return host;
}

EosRuntime& Runtime()
{
	static EosRuntime runtime;
	return runtime;
}

PeerRegistry& Peers()
{
	static PeerRegistry peers;
	return peers;
}

std::string ResolvePeer(const char* platformUserId, uint64_t genOnlineUserId)
{
	if (platformUserId != nullptr && platformUserId[0] != '\0')
	{
		return std::string(platformUserId);
	}
	std::string found;
	Peers().FindProductUserId(genOnlineUserId, &found);
	return found;
}

} // namespace genonline_eos
