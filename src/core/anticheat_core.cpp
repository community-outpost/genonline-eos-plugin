// SPDX-License-Identifier: MIT
#include "core/anticheat_core.h"

#include <GenOnlinePluginApi.h>

#include <utility>

namespace genonline_eos
{

int32_t MapAnticheatAction(int32_t eosAction)
{
	// EOS_ACCCA_RemovePlayer is the only action EAC asks for.
	return eosAction == 1 ? GENONLINE_PLUGIN_ACTION_KICK : GENONLINE_PLUGIN_ACTION_NONE;
}

int32_t MapAnticheatActionReason(int32_t eosReason)
{
	// The EOS_EAntiCheatCommonClientActionReason values in declaration order.
	switch (eosReason)
	{
	case 1:
		return GENONLINE_PLUGIN_ACTION_REASON_INTERNAL_ERROR;
	case 2:
		return GENONLINE_PLUGIN_ACTION_REASON_INVALID_MESSAGE;
	case 3:
		return GENONLINE_PLUGIN_ACTION_REASON_AUTH_FAILURE;
	case 4:
		return GENONLINE_PLUGIN_ACTION_REASON_NOT_RUNNING;
	case 5:
		return GENONLINE_PLUGIN_ACTION_REASON_HEARTBEAT_TIMED_OUT;
	case 6:
		return GENONLINE_PLUGIN_ACTION_REASON_CLIENT_VIOLATION;
	case 7:
		return GENONLINE_PLUGIN_ACTION_REASON_BACKEND_VIOLATION;
	case 8:
		return GENONLINE_PLUGIN_ACTION_REASON_TEMP_COOLDOWN;
	case 9:
		return GENONLINE_PLUGIN_ACTION_REASON_TEMP_BANNED;
	case 10:
		return GENONLINE_PLUGIN_ACTION_REASON_PERMA_BANNED;
	default:
		return GENONLINE_PLUGIN_ACTION_REASON_UNKNOWN;
	}
}

AnticheatCore::AnticheatCore(PeerRegistry& peers, EacBackend& backend, AnticheatHooks hooks)
	: m_registry(peers), m_backend(backend), m_hooks(std::move(hooks))
{
}

void AnticheatCore::SetEnabled(bool enabled)
{
	m_enabled = enabled;
}

void AnticheatCore::SetLocalProductUserId(const std::string& productUserId)
{
	const bool signedInNow = m_localProductUserId.empty() && !productUserId.empty();
	m_localProductUserId = productUserId;
	if (signedInNow && m_waitingForSignIn)
	{
		m_waitingForSignIn = false;
		Log(GENONLINE_PLUGIN_LOG_INFO, "The sign in completed, starting the waiting Easy Anti-Cheat session");
		StartEacSession();
	}
}

void AnticheatCore::BeginSession()
{
	if (m_session)
	{
		Log(GENONLINE_PLUGIN_LOG_DEBUG, "BeginSession while a session runs restarts nothing");
		return;
	}
	m_session = true;
	if (!m_enabled)
	{
		return;
	}
	if (m_localProductUserId.empty())
	{
		// EAC needs the signed in product user, so the session starts when the sign in completes.
		m_waitingForSignIn = true;
		Log(GENONLINE_PLUGIN_LOG_INFO, "The Easy Anti-Cheat session waits for the EOS Connect sign in");
		return;
	}
	StartEacSession();
}

void AnticheatCore::StartEacSession()
{
	m_eacSession = m_backend.BeginSession();
	if (!m_eacSession)
	{
		Log(GENONLINE_PLUGIN_LOG_ERROR, "The Easy Anti-Cheat session did not start");
		return;
	}
	Log(GENONLINE_PLUGIN_LOG_INFO, "Easy Anti-Cheat peer to peer session started");
	for (auto& entry : m_peers)
	{
		AddToEac(entry.first, entry.second);
	}
}

void AnticheatCore::EndSession()
{
	if (!m_session)
	{
		return;
	}
	if (m_eacSession)
	{
		// Ending the EAC session also removes every peer from it.
		m_backend.EndSession();
		Log(GENONLINE_PLUGIN_LOG_INFO, "Easy Anti-Cheat session ended");
	}
	m_eacSession = false;
	m_waitingForSignIn = false;
	m_session = false;
	m_peers.clear();
	m_registry.ForgetRegistrations();
}

bool AnticheatCore::RegisterPeer(const std::string& productUserId, uint64_t genOnlineUserId, int32_t clientType)
{
	if (productUserId.empty())
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "RegisterPeer without a product user id");
		return false;
	}
	if (clientType != GENONLINE_PLUGIN_CLIENT_PROTECTED && clientType != GENONLINE_PLUGIN_CLIENT_UNPROTECTED)
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "RegisterPeer with the unknown client type " + std::to_string(clientType));
		return false;
	}
	auto existing = m_peers.find(productUserId);
	if (existing != m_peers.end() && existing->second.inEac)
	{
		m_backend.UnregisterPeer(existing->second.handle);
		existing->second.inEac = false;
	}
	Peer& peer = m_peers[productUserId];
	peer.genOnlineUserId = genOnlineUserId;
	peer.protectedClient = clientType == GENONLINE_PLUGIN_CLIENT_PROTECTED;
	if (m_eacSession && !AddToEac(productUserId, peer))
	{
		m_peers.erase(productUserId);
		m_registry.Deregister(productUserId);
		return false;
	}
	return m_registry.Register(productUserId, genOnlineUserId);
}

bool AnticheatCore::DeregisterPeer(const std::string& productUserId)
{
	auto it = m_peers.find(productUserId);
	if (it == m_peers.end())
	{
		return false;
	}
	if (it->second.inEac)
	{
		m_backend.UnregisterPeer(it->second.handle);
	}
	m_peers.erase(it);
	m_registry.Deregister(productUserId);
	return true;
}

void AnticheatCore::MessageArrived(uint64_t genOnlineUserId, const uint8_t* data, uint32_t length)
{
	if (!m_enabled)
	{
		return;
	}
	const std::string* productUserId = FindByGenOnlineUserId(genOnlineUserId);
	if (productUserId == nullptr || !m_eacSession || !m_peers[*productUserId].inEac)
	{
		if (m_unknownMessages.Count())
		{
			Log(GENONLINE_PLUGIN_LOG_WARNING, "An anticheat message of the unknown peer " + std::to_string(genOnlineUserId) + " was dropped, " + std::to_string(m_unknownMessages.Total()) + " so far");
		}
		return;
	}
	if (data == nullptr || length == 0)
	{
		return;
	}
	if (!m_backend.ReceiveFromPeer(m_peers[*productUserId].handle, data, length))
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "Easy Anti-Cheat refused a message of peer " + std::to_string(genOnlineUserId));
	}
}

void AnticheatCore::OnMessageToPeer(uintptr_t handle, const uint8_t* data, uint32_t length)
{
	if (!m_enabled)
	{
		return;
	}
	const std::string* productUserId = FindByHandle(handle);
	if (productUserId == nullptr)
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, "Easy Anti-Cheat sent a message for an unknown peer");
		return;
	}
	if (m_hooks.sendToPeer)
	{
		m_hooks.sendToPeer(m_peers[*productUserId].genOnlineUserId, data, length);
	}
}

void AnticheatCore::OnActionRequired(uintptr_t handle, int32_t eosAction, int32_t eosReason, const char* details)
{
	if (!m_enabled)
	{
		return;
	}
	uint64_t genOnlineUserId = 0;
	if (handle == kSelfPeerHandle)
	{
		genOnlineUserId = m_localProductUserId.empty() ? 0 : m_registry.GenOnlineUserId(m_localProductUserId);
	}
	else
	{
		const std::string* productUserId = FindByHandle(handle);
		if (productUserId == nullptr)
		{
			Log(GENONLINE_PLUGIN_LOG_WARNING, "Easy Anti-Cheat asked for an action against an unknown peer");
			return;
		}
		genOnlineUserId = m_peers[*productUserId].genOnlineUserId;
	}
	const std::string reason = details != nullptr ? details : "";
	const int32_t action = MapAnticheatAction(eosAction);
	const int32_t actionReason = MapAnticheatActionReason(eosReason);
	Log(GENONLINE_PLUGIN_LOG_WARNING, "Easy Anti-Cheat asks for action " + std::to_string(action) + " with reason " + std::to_string(actionReason) + " against user " + std::to_string(genOnlineUserId) + ": " + reason);
	if (m_hooks.actionRequired)
	{
		m_hooks.actionRequired(genOnlineUserId, reason, action, actionReason);
	}
}

void AnticheatCore::OnIntegrityViolated(int32_t violationType, const char* message)
{
	if (!m_enabled)
	{
		return;
	}
	const std::string reason = message != nullptr ? message : "";
	Log(GENONLINE_PLUGIN_LOG_ERROR, "Easy Anti-Cheat reports an integrity violation of type " + std::to_string(violationType) + ": " + reason);
	if (m_hooks.integrityViolation)
	{
		m_hooks.integrityViolation(reason, violationType);
	}
}

void AnticheatCore::Reset()
{
	EndSession();
	for (const auto& entry : m_peers)
	{
		m_registry.Deregister(entry.first);
	}
	m_peers.clear();
}

bool AnticheatCore::AddToEac(const std::string& productUserId, Peer& peer)
{
	if (peer.handle == 0)
	{
		peer.handle = NextHandle();
	}
	peer.inEac = m_backend.RegisterPeer(peer.handle, productUserId, peer.protectedClient, kPeerAuthenticationTimeoutSeconds);
	if (!peer.inEac)
	{
		Log(GENONLINE_PLUGIN_LOG_ERROR, "Easy Anti-Cheat refused the peer " + productUserId);
	}
	return peer.inEac;
}

const std::string* AnticheatCore::FindByHandle(uintptr_t handle) const
{
	for (const auto& entry : m_peers)
	{
		if (entry.second.inEac && entry.second.handle == handle)
		{
			return &entry.first;
		}
	}
	return nullptr;
}

const std::string* AnticheatCore::FindByGenOnlineUserId(uint64_t genOnlineUserId) const
{
	for (const auto& entry : m_peers)
	{
		if (entry.second.genOnlineUserId == genOnlineUserId)
		{
			return &entry.first;
		}
	}
	return nullptr;
}

uintptr_t AnticheatCore::NextHandle()
{
	// Handles start at 1 and skip the self handle, so a peer handle never reads as null or as the local user.
	do
	{
		++m_lastHandle;
	} while (m_lastHandle == 0 || m_lastHandle == kSelfPeerHandle);
	return m_lastHandle;
}

void AnticheatCore::Log(int32_t level, const std::string& text) const
{
	if (m_hooks.log)
	{
		m_hooks.log(level, text);
	}
}

} // namespace genonline_eos
