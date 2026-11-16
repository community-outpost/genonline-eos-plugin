// SPDX-License-Identifier: MIT
// The Easy Anti-Cheat peer to peer session rules behind the anticheat table. Pure logic without EOS.
#pragma once

#include "core/linked_account_machine.h"
#include "core/peer_registry.h"
#include "core/transport_rules.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace genonline_eos
{

// The peer handle EAC uses for the local user, the pointer value -1 like EOS_ANTICHEATCLIENT_PEER_SELF.
constexpr uintptr_t kSelfPeerHandle = static_cast<uintptr_t>(-1);

// The time a registered peer has to authenticate, in seconds.
constexpr uint32_t kPeerAuthenticationTimeoutSeconds = 120;

// Maps an EOS_EAntiCheatCommonClientAction value to a GENONLINE_PLUGIN_ACTION_ value.
int32_t MapAnticheatAction(int32_t eosAction);

// Maps an EOS_EAntiCheatCommonClientActionReason value to a GENONLINE_PLUGIN_ACTION_REASON_ value.
int32_t MapAnticheatActionReason(int32_t eosReason);

// The EAC client calls the anticheat makes. Each returns false when EAC refuses the call.
class EacBackend
{
public:
	virtual ~EacBackend()
	{
	}

	// Starts a peer to peer session for the signed in product user.
	virtual bool BeginSession() = 0;

	virtual void EndSession() = 0;

	virtual bool RegisterPeer(uintptr_t handle, const std::string& productUserId, bool protectedClient, uint32_t authenticationTimeoutSeconds) = 0;

	virtual bool UnregisterPeer(uintptr_t handle) = 0;

	virtual bool ReceiveFromPeer(uintptr_t handle, const uint8_t* data, uint32_t length) = 0;
};

// The host callbacks the anticheat reaches.
struct AnticheatHooks
{
	std::function<void(uint64_t genOnlineUserId, const uint8_t* data, uint32_t length)> sendToPeer;
	std::function<void(uint64_t genOnlineUserId, const std::string& reason, int32_t action, int32_t actionReason)> actionRequired;
	std::function<void(const std::string& reason, int32_t violationType)> integrityViolation;
	LogSink log;
};

class AnticheatCore
{
public:
	AnticheatCore(PeerRegistry& peers, EacBackend& backend, AnticheatHooks hooks);

	// True when the host enabled ANTICHEAT and the EAC client is usable. Without it only the peer registry runs.
	void SetEnabled(bool enabled);

	bool IsEnabled() const
	{
		return m_enabled;
	}

	// The product user signed in to EOS Connect, used to find the local GenOnline user id. A completed sign in starts a waiting EAC session.
	void SetLocalProductUserId(const std::string& productUserId);

	// Starts the session. A second call while one runs restarts nothing. Before the sign in the EAC part waits for it.
	void BeginSession();

	// Ends the session and forgets its peers. Without a session it does nothing.
	void EndSession();

	// clientType is one of GENONLINE_PLUGIN_CLIENT_.
	bool RegisterPeer(const std::string& productUserId, uint64_t genOnlineUserId, int32_t clientType);
	bool DeregisterPeer(const std::string& productUserId);

	// A message a peer sent through the game transport.
	void MessageArrived(uint64_t genOnlineUserId, const uint8_t* data, uint32_t length);

	// EAC notifications.
	void OnMessageToPeer(uintptr_t handle, const uint8_t* data, uint32_t length);
	void OnActionRequired(uintptr_t handle, int32_t eosAction, int32_t eosReason, const char* details);
	void OnIntegrityViolated(int32_t violationType, const char* message);

	// Ends the session and forgets every peer.
	void Reset();

	bool IsSessionActive() const
	{
		return m_session;
	}

	bool IsEacSessionRunning() const
	{
		return m_eacSession;
	}

private:
	struct Peer
	{
		uint64_t genOnlineUserId = 0;
		bool protectedClient = true;
		uintptr_t handle = 0;
		bool inEac = false;
	};

	void StartEacSession();
	bool AddToEac(const std::string& productUserId, Peer& peer);
	const std::string* FindByHandle(uintptr_t handle) const;
	const std::string* FindByGenOnlineUserId(uint64_t genOnlineUserId) const;
	uintptr_t NextHandle();
	void Log(int32_t level, const std::string& text) const;

	PeerRegistry& m_registry;
	EacBackend& m_backend;
	AnticheatHooks m_hooks;
	std::map<std::string, Peer> m_peers;
	std::string m_localProductUserId;
	uintptr_t m_lastHandle = 0;
	bool m_enabled = false;
	bool m_session = false;
	bool m_eacSession = false;
	bool m_waitingForSignIn = false;
	DropCounter m_unknownMessages;
};

} // namespace genonline_eos
