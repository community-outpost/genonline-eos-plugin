// SPDX-License-Identifier: MIT
#include "core/transport_core.h"

#include <GenOnlinePluginApi.h>

#include <utility>

namespace genonline_eos
{

TransportCore::TransportCore(PeerRegistry& peers, P2PBackend& backend, StateSink states, LogSink log)
	: m_peers(peers), m_backend(backend), m_states(std::move(states)), m_log(std::move(log))
{
}

void TransportCore::SetCapabilityEnabled(bool enabled)
{
	m_capability = enabled;
}

void TransportCore::SetEnabled(bool enabled)
{
	m_enabled = enabled;
	if (!enabled)
	{
		CloseEverything();
	}
}

void TransportCore::SetSignedIn(bool signedIn)
{
	if (m_signedIn && !signedIn)
	{
		CloseEverything();
	}
	m_signedIn = signedIn;
}

bool TransportCore::IsAvailable() const
{
	return m_capability && m_enabled && m_signedIn;
}

void TransportCore::SetRelayPolicy(bool forceRelay)
{
	m_forceRelay = forceRelay;
	m_backend.SetRelayControl(forceRelay);
}

bool TransportCore::IsRelayForced() const
{
	return m_forceRelay;
}

void TransportCore::SetLocalUser(const std::string& productUserId)
{
	m_localUser = productUserId;
}

bool TransportCore::OpensConnectionTo(const std::string& productUserId) const
{
	// Without its own id this side cannot pick, so it opens like the peer may.
	return m_localUser.empty() || m_localUser < productUserId;
}

bool TransportCore::Open(const std::string& productUserId, Connection& connection)
{
	if (!m_backend.OpenConnection(productUserId))
	{
		return false;
	}
	connection.opened = true;
	return true;
}

void TransportCore::StartSignalling(const std::string& productUserId, uint64_t genOnlineUserId)
{
	// A connection that cannot start fails at once, so the game moves on without waiting for its timeout.
	if (productUserId.empty())
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "StartSignalling without a product user id fails at once");
		if (m_states)
		{
			m_states(productUserId, genOnlineUserId, GENONLINE_PLUGIN_CONNECTION_FAILED);
		}
		return;
	}
	if (!IsAvailable())
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "StartSignalling while the transport is not available fails at once");
		if (m_states)
		{
			m_states(productUserId, genOnlineUserId, GENONLINE_PLUGIN_CONNECTION_FAILED);
		}
		return;
	}
	m_peers.MarkSignalled(productUserId, genOnlineUserId);
	auto existing = m_connections.find(productUserId);
	// The peer opened first and this side accepted it already, so that connection stays.
	if (existing != m_connections.end() && existing->second.fromRequest && existing->second.state == State::Connecting)
	{
		Report(productUserId, GENONLINE_PLUGIN_CONNECTION_CONNECTING_DIRECT);
		return;
	}
	if (existing != m_connections.end())
	{
		m_connections.erase(existing);
		m_backend.CloseConnection(productUserId);
		m_queues.DropFrom(productUserId);
	}
	Connection& connection = m_connections[productUserId];
	Report(productUserId, GENONLINE_PLUGIN_CONNECTION_CONNECTING_DIRECT);
	const bool requested = m_earlyRequests.erase(productUserId) != 0;
	if (!OpensConnectionTo(productUserId) && !requested)
	{
		// The peer opens, its request reaches OnConnectionRequest.
		return;
	}
	if (!Open(productUserId, connection))
	{
		Log(GENONLINE_PLUGIN_LOG_ERROR, "Opening a P2P connection to " + productUserId + " failed");
		m_connections.erase(productUserId);
		Report(productUserId, GENONLINE_PLUGIN_CONNECTION_FAILED);
	}
}

void TransportCore::Send(const std::string& productUserId, const uint8_t* data, uint32_t length, uint8_t channel, int32_t reliability)
{
	if (!IsAvailable())
	{
		return;
	}
	if (!IsHostChannel(channel))
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "A packet on a probe channel was dropped");
		return;
	}
	int32_t eosReliability = 0;
	if (!MapReliability(reliability, &eosReliability))
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "A packet with unknown reliability " + std::to_string(reliability) + " was dropped");
		return;
	}
	if (data == nullptr || length == 0)
	{
		return;
	}
	if (length > kMaxPacketSize)
	{
		if (m_oversizedDrops.Count())
		{
			Log(GENONLINE_PLUGIN_LOG_WARNING, "A packet of " + std::to_string(length) + " bytes exceeds the EOS P2P maximum of " + std::to_string(kMaxPacketSize) + " and was dropped, " + std::to_string(m_oversizedDrops.Total()) + " so far");
		}
		return;
	}
	if (m_connections.find(productUserId) == m_connections.end())
	{
		if (m_unconnectedDrops.Count())
		{
			Log(GENONLINE_PLUGIN_LOG_DEBUG, "A packet for a peer without a connection was dropped, " + std::to_string(m_unconnectedDrops.Total()) + " so far");
		}
		return;
	}
	m_backend.Send(productUserId, channel, eosReliability, data, length);
}

void TransportCore::DisconnectPeer(const std::string& productUserId)
{
	m_peers.ClearSignalled(productUserId);
	m_earlyRequests.erase(productUserId);
	m_queues.DropFrom(productUserId);
	if (m_connections.erase(productUserId) != 0)
	{
		m_backend.CloseConnection(productUserId);
	}
}

void TransportCore::DisconnectAll()
{
	CloseEverything();
}

void TransportCore::Reset()
{
	CloseEverything();
}

void TransportCore::CloseEverything()
{
	if (!m_connections.empty())
	{
		m_backend.CloseAllConnections();
	}
	m_connections.clear();
	m_earlyRequests.clear();
	m_queues.Clear();
}

void TransportCore::OnConnectionRequest(const std::string& productUserId)
{
	if (!IsAvailable())
	{
		return;
	}
	if (!m_peers.MayConnect(productUserId))
	{
		// The game may name the peer only after its request arrives, StartSignalling accepts it then.
		if (m_earlyRequests.size() < kMaxEarlyRequests)
		{
			m_earlyRequests.insert(productUserId);
		}
		if (m_refusedRequests.Count())
		{
			Log(GENONLINE_PLUGIN_LOG_WARNING, "A P2P connection request of the unknown peer " + productUserId + " waits for its signalling, " + std::to_string(m_refusedRequests.Total()) + " so far");
		}
		return;
	}
	auto existing = m_connections.find(productUserId);
	// A connection this side opened already accepts the peer.
	if (existing != m_connections.end() && existing->second.opened)
	{
		return;
	}
	const bool isNew = existing == m_connections.end();
	Connection& connection = isNew ? m_connections[productUserId] : existing->second;
	if (!Open(productUserId, connection))
	{
		Log(GENONLINE_PLUGIN_LOG_ERROR, "Accepting the P2P connection of " + productUserId + " failed");
		if (isNew)
		{
			m_connections.erase(productUserId);
		}
		return;
	}
	if (isNew)
	{
		connection.fromRequest = true;
	}
}

void TransportCore::OnEstablished(const std::string& productUserId, bool relayed)
{
	auto it = m_connections.find(productUserId);
	if (it == m_connections.end())
	{
		return;
	}
	it->second.relayed = relayed;
	if (it->second.state == State::Established)
	{
		return;
	}
	it->second.state = State::Established;
	Log(GENONLINE_PLUGIN_LOG_INFO, "P2P connection to " + productUserId + (relayed ? " established through a relay" : " established directly"));
	Report(productUserId, GENONLINE_PLUGIN_CONNECTION_CONNECTED_DIRECT);
}

void TransportCore::OnClosed(const std::string& productUserId, bool closedByLocalUser)
{
	// The plugin closes connections only when the host asks, and those closes give no report.
	if (closedByLocalUser)
	{
		return;
	}
	auto it = m_connections.find(productUserId);
	if (it == m_connections.end())
	{
		return;
	}
	const bool established = it->second.state == State::Established;
	m_connections.erase(it);
	m_queues.DropFrom(productUserId);
	Log(GENONLINE_PLUGIN_LOG_INFO, "P2P connection to " + productUserId + (established ? " closed" : " failed"));
	Report(productUserId, established ? GENONLINE_PLUGIN_CONNECTION_DISCONNECTED : GENONLINE_PLUGIN_CONNECTION_FAILED);
}

void TransportCore::OnPacket(const std::string& productUserId, uint8_t channel, const uint8_t* data, uint32_t length, uint64_t nowMs)
{
	if (!IsAvailable())
	{
		return;
	}
	auto connection = m_connections.find(productUserId);
	if (channel == GENONLINE_PLUGIN_CHANNEL_PING)
	{
		uint64_t sentMs = 0;
		if (connection != m_connections.end() && DecodeProbe(data, length, &sentMs))
		{
			m_backend.Send(productUserId, GENONLINE_PLUGIN_CHANNEL_PONG, kEosUnreliableUnordered, data, length);
		}
		return;
	}
	if (channel == GENONLINE_PLUGIN_CHANNEL_PONG)
	{
		uint64_t sentMs = 0;
		if (connection != m_connections.end() && DecodeProbe(data, length, &sentMs) && sentMs <= nowMs)
		{
			connection->second.latency.AddSample(static_cast<double>(nowMs - sentMs));
		}
		return;
	}
	if (!m_peers.IsRegistered(productUserId))
	{
		if (m_unregisteredDrops.Count())
		{
			Log(GENONLINE_PLUGIN_LOG_DEBUG, "A packet of the unregistered peer " + productUserId + " was dropped, " + std::to_string(m_unregisteredDrops.Total()) + " so far");
		}
		return;
	}
	if (!m_queues.Push(channel, productUserId, m_peers.GenOnlineUserId(productUserId), data, length))
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "The receive queue of channel " + std::to_string(channel) + " is full, a packet was dropped");
	}
}

void TransportCore::Tick(uint64_t nowMs)
{
	if (!IsAvailable())
	{
		return;
	}
	for (auto& entry : m_connections)
	{
		Connection& connection = entry.second;
		if (connection.state != State::Established || nowMs < connection.nextProbeMs)
		{
			continue;
		}
		uint8_t probe[kProbeSize];
		EncodeProbe(nowMs, probe);
		m_backend.Send(entry.first, GENONLINE_PLUGIN_CHANNEL_PING, kEosUnreliableUnordered, probe, kProbeSize);
		connection.nextProbeMs = nowMs + kProbeIntervalMs;
	}
}

int32_t TransportCore::Latency(const std::string& productUserId) const
{
	auto it = m_connections.find(productUserId);
	return it != m_connections.end() ? it->second.latency.Milliseconds() : 0;
}

bool TransportCore::IsRelayed(const std::string& productUserId) const
{
	auto it = m_connections.find(productUserId);
	return it != m_connections.end() && it->second.state == State::Established && it->second.relayed;
}

bool TransportCore::HasConnection(const std::string& productUserId) const
{
	return m_connections.find(productUserId) != m_connections.end();
}

void TransportCore::Report(const std::string& productUserId, int32_t state)
{
	if (m_states)
	{
		m_states(productUserId, m_peers.GenOnlineUserId(productUserId), state);
	}
}

void TransportCore::Log(int32_t level, const std::string& text) const
{
	if (m_log)
	{
		m_log(level, text);
	}
}

} // namespace genonline_eos
