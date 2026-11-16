// SPDX-License-Identifier: MIT
// Process wide plugin state shared by the export and the sub interface parts.
#pragma once

#include "core/host_link.h"
#include "core/peer_registry.h"

#include <GenOnlinePluginApi.h>

#include <string>

namespace genonline_eos
{

class EosRuntime;

// Capabilities this plugin offers.
constexpr uint32_t kOfferedCapabilities = GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT | GENONLINE_PLUGIN_CAP_TRANSPORT;

// The value the GenOnline service uses for Easy Anti-Cheat.
constexpr int32_t kEasyAntiCheatIdentifier = 9481;

// The bound host table. Only the main thread uses it.
HostLink& Host();

// The EOS lifecycle. Only the main thread uses it.
EosRuntime& Runtime();

// The peers the game registered or signalled, shared by the anticheat and transport tables. Only the main thread uses it.
PeerRegistry& Peers();

// The product user id the host passed, or the one known for genOnlineUserId when the host passed none.
std::string ResolvePeer(const char* platformUserId, uint64_t genOnlineUserId);

} // namespace genonline_eos
