// SPDX-License-Identifier: MIT
// The EOS P2P transport rules behind the transport table. Pure logic without EOS.
#pragma once

#include "core/latency_estimator.h"
#include "core/linked_account_machine.h"
#include "core/packet_queues.h"
#include "core/peer_registry.h"
#include "core/transport_rules.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace genonline_eos
{

// The EOS P2P calls the transport makes, all on the plugin socket.
class P2PBackend
{
public:
	virtual ~P2PBackend()
	{
	}

	// Requests or accepts a connection to the peer. Returns false when that fails.
	virtual bool OpenConnection(const std::string& productUserId) = 0;

	virtual void CloseConnection(const std::string& productUserId) = 0;

	virtual void CloseAllConnections() = 0;

	// Sends one packet over an existing connection. Returns false when EOS refuses it.
	virtual bool Send(const std::string& productUserId, uint8_t channel, int32_t eosReliability, const uint8_t* data, uint32_t length) = 0;
};

// Receives a GENONLINE_PLUGIN_CONNECTION_ state of a peer.
using StateSink = std::function<void(const std::string& productUserId, uint64_t genOnlineUserId, int32_t state)>;

class TransportCore
{
public:
	// The interval of the latency probes per connected peer.
	static constexpr uint64_t kProbeIntervalMs = 1000;

	TransportCore(PeerRegistry& peers, P2PBackend& backend, StateSink states, LogSink log);

	// The host enabled the TRANSPORT capability.
	void SetCapabilityEnabled(bool enabled);

	// The host SetEnabled flag. FALSE closes every connection.
	void SetEnabled(bool enabled);

	// The EOS Connect sign in. A sign out closes every connection.
	void SetSignedIn(bool signedIn);

	bool IsAvailable() const;

	void StartSignalling(const std::string& productUserId, uint64_t genOnlineUserId);
	void Send(const std::string& productUserId, const uint8_t* data, uint32_t length, uint8_t channel, int32_t reliability);
	void DisconnectPeer(const std::string& productUserId);
	void DisconnectAll();

	// Closes every connection and drops every queued packet without a state report.
	void Reset();

	// EOS notifications and received packets.
	void OnConnectionRequest(const std::string& productUserId);
	void OnEstablished(const std::string& productUserId, bool relayed);
	void OnClosed(const std::string& productUserId, bool closedByLocalUser);
	void OnPacket(const std::string& productUserId, uint8_t channel, const uint8_t* data, uint32_t length, uint64_t nowMs);

	// Sends the due latency probes.
	void Tick(uint64_t nowMs);

	int32_t Latency(const std::string& productUserId) const;
	bool IsRelayed(const std::string& productUserId) const;
	bool HasConnection(const std::string& productUserId) const;

	PacketQueues& Queues()
	{
		return m_queues;
	}

private:
	enum class State
	{
		Connecting,
		Established,
	};

	struct Connection
	{
		State state = State::Connecting;
		bool relayed = false;
		uint64_t nextProbeMs = 0;
		LatencyEstimator latency;
	};

	void Report(const std::string& productUserId, int32_t state);
	void Log(int32_t level, const std::string& text) const;
	void CloseEverything();

	PeerRegistry& m_peers;
	P2PBackend& m_backend;
	StateSink m_states;
	LogSink m_log;
	PacketQueues m_queues;
	std::map<std::string, Connection> m_connections;
	bool m_capability = false;
	bool m_enabled = false;
	bool m_signedIn = false;
	DropCounter m_unregisteredDrops;
	DropCounter m_unconnectedDrops;
	DropCounter m_oversizedDrops;
	DropCounter m_refusedRequests;
};

} // namespace genonline_eos
