// SPDX-License-Identifier: MIT
// The transport table backed by EOS P2P. The transport rules live in core/transport_core.
#include "plugin/sub_tables.h"

#include "core/host_link.h"
#include "core/transport_core.h"
#include "plugin/eos_ids.h"
#include "plugin/plugin_state.h"
#include "plugin/runtime_module.h"

#include <windows.h>

#include <eos_p2p.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace genonline_eos
{

namespace
{

static_assert(kMaxPacketSize == EOS_P2P_MAX_PACKET_SIZE, "kMaxPacketSize matches the EOS P2P maximum");
static_assert(sizeof("genonline") <= EOS_P2P_SOCKETID_SOCKETNAME_SIZE, "the socket name fits the EOS socket id");
static_assert(kEosUnreliableUnordered == static_cast<int32_t>(EOS_EPacketReliability::EOS_PR_UnreliableUnordered), "reliability values match EOS");
static_assert(kEosReliableUnordered == static_cast<int32_t>(EOS_EPacketReliability::EOS_PR_ReliableUnordered), "reliability values match EOS");
static_assert(kEosReliableOrdered == static_cast<int32_t>(EOS_EPacketReliability::EOS_PR_ReliableOrdered), "reliability values match EOS");

// Received packets handled per Tick, so a flood cannot stall the main thread.
constexpr int kReceiveBudget = 1024;

uint64_t NowMs()
{
	return GetTickCount64();
}

void ReportState(const std::string& productUserId, uint64_t genOnlineUserId, int32_t state)
{
	const GenOnlinePluginHostApi& host = Host().Table();
	if (Host().IsBound() && host.ConnectionStateChanged != nullptr)
	{
		host.ConnectionStateChanged(host.userData, productUserId.c_str(), genOnlineUserId, state);
	}
}

// Connects the transport rules to EOS P2P. Every EOS callback arrives during EOS_Platform_Tick on the main thread.
class P2PModule final : public RuntimeModule, public P2PBackend
{
public:
	P2PModule()
		: m_core(Peers(), *this, &ReportState,
			  [](int32_t level, const std::string& text)
			  {
				  Host().Log(level, text.c_str());
			  })
	{
		m_socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
		std::memset(m_socket.SocketName, 0, sizeof(m_socket.SocketName));
		std::memcpy(m_socket.SocketName, kSocketName, std::strlen(kSocketName));
	}

	const char* Name() const override
	{
		return "transport";
	}

	void Start(EOS_HPlatform platform) override
	{
		m_core.SetCapabilityEnabled(Host().IsEnabled(GENONLINE_PLUGIN_CAP_TRANSPORT));
		if (!Host().IsEnabled(GENONLINE_PLUGIN_CAP_TRANSPORT))
		{
			return;
		}
		m_p2p = EOS_Platform_GetP2PInterface(platform);
		if (m_p2p == nullptr)
		{
			Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "EOS P2P is not available");
		}
	}

	void Tick() override
	{
		SyncSignedInUser();
		ReceivePackets();
		m_core.Tick(NowMs());
	}

	void Stop() override
	{
		Unbind();
		m_core.Reset();
		m_core.SetCapabilityEnabled(false);
		m_p2p = nullptr;
		Peers().Clear();
	}

	TransportCore& Core()
	{
		return m_core;
	}

	bool OpenConnection(const std::string& productUserId) override
	{
		EOS_ProductUserId remote = RemoteId(productUserId);
		if (m_p2p == nullptr || m_localUser == nullptr || remote == nullptr)
		{
			return false;
		}
		EOS_P2P_AcceptConnectionOptions options = {};
		options.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
		options.LocalUserId = m_localUser;
		options.RemoteUserId = remote;
		options.SocketId = &m_socket;
		const EOS_EResult result = EOS_P2P_AcceptConnection(m_p2p, &options);
		if (result != EOS_EResult::EOS_Success)
		{
			Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "EOS_P2P_AcceptConnection failed: %s", EOS_EResult_ToString(result));
			return false;
		}
		return true;
	}

	void CloseConnection(const std::string& productUserId) override
	{
		EOS_ProductUserId remote = RemoteId(productUserId);
		if (m_p2p == nullptr || m_localUser == nullptr || remote == nullptr)
		{
			return;
		}
		EOS_P2P_CloseConnectionOptions options = {};
		options.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
		options.LocalUserId = m_localUser;
		options.RemoteUserId = remote;
		options.SocketId = &m_socket;
		EOS_P2P_CloseConnection(m_p2p, &options);
	}

	void CloseAllConnections() override
	{
		if (m_p2p == nullptr || m_localUser == nullptr)
		{
			return;
		}
		EOS_P2P_CloseConnectionsOptions options = {};
		options.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
		options.LocalUserId = m_localUser;
		options.SocketId = &m_socket;
		EOS_P2P_CloseConnections(m_p2p, &options);
	}

	bool Send(const std::string& productUserId, uint8_t channel, int32_t eosReliability, const uint8_t* data, uint32_t length) override
	{
		EOS_ProductUserId remote = RemoteId(productUserId);
		if (m_p2p == nullptr || m_localUser == nullptr || remote == nullptr)
		{
			return false;
		}
		EOS_P2P_SendPacketOptions options = {};
		options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
		options.LocalUserId = m_localUser;
		options.RemoteUserId = remote;
		options.SocketId = &m_socket;
		options.Channel = channel;
		options.DataLengthBytes = length;
		options.Data = data;
		// Packets wait while the connection opens, and a send never opens a connection by itself.
		options.bAllowDelayedDelivery = EOS_TRUE;
		options.Reliability = static_cast<EOS_EPacketReliability>(eosReliability);
		options.bDisableAutoAcceptConnection = EOS_TRUE;
		const EOS_EResult result = EOS_P2P_SendPacket(m_p2p, &options);
		if (result != EOS_EResult::EOS_Success)
		{
			if (m_sendFailures.Count())
			{
				Host().Logf(GENONLINE_PLUGIN_LOG_WARNING, "EOS_P2P_SendPacket failed: %s, %u so far", EOS_EResult_ToString(result), static_cast<unsigned>(m_sendFailures.Total()));
			}
			return false;
		}
		return true;
	}

private:
	EOS_ProductUserId RemoteId(const std::string& productUserId)
	{
		if (productUserId.empty())
		{
			return nullptr;
		}
		auto it = m_remoteIds.find(productUserId);
		if (it != m_remoteIds.end())
		{
			return it->second;
		}
		EOS_ProductUserId id = EOS_ProductUserId_FromString(productUserId.c_str());
		if (id == nullptr || !EOS_ProductUserId_IsValid(id))
		{
			return nullptr;
		}
		m_remoteIds.emplace(productUserId, id);
		return id;
	}

	bool IsOurSocket(const EOS_P2P_SocketId* socket) const
	{
		return socket != nullptr && std::strncmp(socket->SocketName, kSocketName, sizeof(socket->SocketName)) == 0;
	}

	// Binds the transport to the product user signed in to EOS Connect, once per sign in.
	void SyncSignedInUser()
	{
		if (m_p2p == nullptr)
		{
			return;
		}
		EOS_ProductUserId user = SignedInProductUser();
		if (user == m_localUser)
		{
			return;
		}
		Unbind();
		if (user != nullptr)
		{
			Bind(user);
		}
	}

	void Bind(EOS_ProductUserId user)
	{
		m_localUser = user;

		EOS_P2P_SetRelayControlOptions relay = {};
		relay.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
		relay.RelayControl = EOS_ERelayControl::EOS_RC_AllowRelays;
		const EOS_EResult relayResult = EOS_P2P_SetRelayControl(m_p2p, &relay);
		if (relayResult != EOS_EResult::EOS_Success)
		{
			Host().Logf(GENONLINE_PLUGIN_LOG_WARNING, "EOS_P2P_SetRelayControl failed: %s", EOS_EResult_ToString(relayResult));
		}

		EOS_P2P_AddNotifyPeerConnectionRequestOptions request = {};
		request.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
		request.LocalUserId = user;
		request.SocketId = &m_socket;
		m_requestNotification = EOS_P2P_AddNotifyPeerConnectionRequest(m_p2p, &request, nullptr, &OnRequest);

		EOS_P2P_AddNotifyPeerConnectionEstablishedOptions established = {};
		established.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
		established.LocalUserId = user;
		established.SocketId = &m_socket;
		m_establishedNotification = EOS_P2P_AddNotifyPeerConnectionEstablished(m_p2p, &established, nullptr, &OnEstablished);

		EOS_P2P_AddNotifyPeerConnectionInterruptedOptions interrupted = {};
		interrupted.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST;
		interrupted.LocalUserId = user;
		interrupted.SocketId = &m_socket;
		m_interruptedNotification = EOS_P2P_AddNotifyPeerConnectionInterrupted(m_p2p, &interrupted, nullptr, &OnInterrupted);

		EOS_P2P_AddNotifyPeerConnectionClosedOptions closed = {};
		closed.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
		closed.LocalUserId = user;
		closed.SocketId = &m_socket;
		m_closedNotification = EOS_P2P_AddNotifyPeerConnectionClosed(m_p2p, &closed, nullptr, &OnClosed);

		if (m_requestNotification == EOS_INVALID_NOTIFICATIONID || m_establishedNotification == EOS_INVALID_NOTIFICATIONID || m_closedNotification == EOS_INVALID_NOTIFICATIONID)
		{
			Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "EOS P2P notifications are missing, the transport stays unavailable");
			Unbind();
			return;
		}
		m_core.SetSignedIn(true);
		Host().Log(GENONLINE_PLUGIN_LOG_INFO, "EOS P2P bound to the signed in product user with relays allowed");
	}

	void Unbind()
	{
		// Closing needs the local user, so it runs before the user is forgotten.
		m_core.SetSignedIn(false);
		if (m_p2p != nullptr)
		{
			if (m_requestNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_P2P_RemoveNotifyPeerConnectionRequest(m_p2p, m_requestNotification);
			}
			if (m_establishedNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_P2P_RemoveNotifyPeerConnectionEstablished(m_p2p, m_establishedNotification);
			}
			if (m_interruptedNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_P2P_RemoveNotifyPeerConnectionInterrupted(m_p2p, m_interruptedNotification);
			}
			if (m_closedNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_P2P_RemoveNotifyPeerConnectionClosed(m_p2p, m_closedNotification);
			}
		}
		m_requestNotification = EOS_INVALID_NOTIFICATIONID;
		m_establishedNotification = EOS_INVALID_NOTIFICATIONID;
		m_interruptedNotification = EOS_INVALID_NOTIFICATIONID;
		m_closedNotification = EOS_INVALID_NOTIFICATIONID;
		m_localUser = nullptr;
		m_remoteIds.clear();
	}

	void ReceivePackets()
	{
		if (m_p2p == nullptr || m_localUser == nullptr)
		{
			return;
		}
		uint8_t buffer[EOS_P2P_MAX_PACKET_SIZE];
		for (int i = 0; i < kReceiveBudget; ++i)
		{
			EOS_P2P_ReceivePacketOptions options = {};
			options.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
			options.LocalUserId = m_localUser;
			options.MaxDataSizeBytes = sizeof(buffer);
			options.RequestedChannel = nullptr;
			EOS_ProductUserId peer = nullptr;
			EOS_P2P_SocketId socket = {};
			socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
			uint8_t channel = 0;
			uint32_t written = 0;
			const EOS_EResult result = EOS_P2P_ReceivePacket(m_p2p, &options, &peer, &socket, &channel, buffer, &written);
			if (result != EOS_EResult::EOS_Success)
			{
				return;
			}
			if (!IsOurSocket(&socket))
			{
				continue;
			}
			m_core.OnPacket(ProductUserIdText(peer), channel, buffer, written, NowMs());
		}
	}

	static void EOS_CALL OnRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* info);
	static void EOS_CALL OnEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* info);
	static void EOS_CALL OnInterrupted(const EOS_P2P_OnPeerConnectionInterruptedInfo* info);
	static void EOS_CALL OnClosed(const EOS_P2P_OnRemoteConnectionClosedInfo* info);

	TransportCore m_core;
	DropCounter m_sendFailures;
	EOS_HP2P m_p2p = nullptr;
	EOS_ProductUserId m_localUser = nullptr;
	EOS_P2P_SocketId m_socket = {};
	std::unordered_map<std::string, EOS_ProductUserId> m_remoteIds;
	EOS_NotificationId m_requestNotification = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_establishedNotification = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_interruptedNotification = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_closedNotification = EOS_INVALID_NOTIFICATIONID;
};

P2PModule& Module()
{
	static P2PModule module;
	return module;
}

void EOS_CALL P2PModule::OnRequest(const EOS_P2P_OnIncomingConnectionRequestInfo* info)
{
	if (info == nullptr || !Module().IsOurSocket(info->SocketId))
	{
		return;
	}
	Module().m_core.OnConnectionRequest(ProductUserIdText(info->RemoteUserId));
}

void EOS_CALL P2PModule::OnEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* info)
{
	if (info == nullptr || !Module().IsOurSocket(info->SocketId))
	{
		return;
	}
	const bool relayed = info->NetworkType == EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection;
	Module().m_core.OnEstablished(ProductUserIdText(info->RemoteUserId), relayed);
}

void EOS_CALL P2PModule::OnInterrupted(const EOS_P2P_OnPeerConnectionInterruptedInfo* info)
{
	if (info == nullptr || !Module().IsOurSocket(info->SocketId))
	{
		return;
	}
	Host().Logf(GENONLINE_PLUGIN_LOG_INFO, "P2P connection to %s interrupted, EOS tries to restore it", ProductUserIdText(info->RemoteUserId).c_str());
}

void EOS_CALL P2PModule::OnClosed(const EOS_P2P_OnRemoteConnectionClosedInfo* info)
{
	if (info == nullptr || !Module().IsOurSocket(info->SocketId))
	{
		return;
	}
	const bool byLocalUser = info->Reason == EOS_EConnectionClosedReason::EOS_CCR_ClosedByLocalUser;
	Module().m_core.OnClosed(ProductUserIdText(info->RemoteUserId), byLocalUser);
}

int32_t GENONLINE_PLUGIN_CALL IsAvailable()
{
	return Module().Core().IsAvailable() ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
}

void GENONLINE_PLUGIN_CALL SetEnabled(int32_t enabled)
{
	Module().Core().SetEnabled(enabled != GENONLINE_PLUGIN_FALSE);
}

void GENONLINE_PLUGIN_CALL StartSignalling(const char* platformUserId, uint64_t genOnlineUserId)
{
	try
	{
		Module().Core().StartSignalling(ResolvePeer(platformUserId, genOnlineUserId), genOnlineUserId);
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "StartSignalling failed with an unknown error");
	}
}

void GENONLINE_PLUGIN_CALL SendPacket(const char* platformUserId, uint64_t genOnlineUserId, const void* data, uint32_t dataLength, uint8_t channel, int32_t reliability)
{
	try
	{
		Module().Core().Send(ResolvePeer(platformUserId, genOnlineUserId), static_cast<const uint8_t*>(data), dataLength, channel, reliability);
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "SendPacket failed with an unknown error");
	}
}

uint32_t GENONLINE_PLUGIN_CALL GetNextPacketSize(uint8_t channel)
{
	return Module().Core().Queues().NextSize(channel);
}

int32_t GENONLINE_PLUGIN_CALL ReceivePacketFrom(uint8_t channel, const uint8_t** outData, uint32_t* outDataLength, uint64_t* outSenderGenOnlineUserId)
{
	if (outData != nullptr)
	{
		*outData = nullptr;
	}
	if (outDataLength != nullptr)
	{
		*outDataLength = 0;
	}
	if (outSenderGenOnlineUserId != nullptr)
	{
		*outSenderGenOnlineUserId = 0;
	}
	try
	{
		return Module().Core().Queues().Take(channel, outData, outDataLength, outSenderGenOnlineUserId) ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
	}
	catch (...)
	{
		return GENONLINE_PLUGIN_FALSE;
	}
}

int32_t GENONLINE_PLUGIN_CALL ReceivePacket(uint8_t channel, const uint8_t** outData, uint32_t* outDataLength)
{
	return ReceivePacketFrom(channel, outData, outDataLength, nullptr);
}

void GENONLINE_PLUGIN_CALL FreePacket(const uint8_t* data)
{
	Module().Core().Queues().Free(data);
}

int32_t GENONLINE_PLUGIN_CALL GetLatency(const char* platformUserId, uint64_t genOnlineUserId)
{
	try
	{
		return Module().Core().Latency(ResolvePeer(platformUserId, genOnlineUserId));
	}
	catch (...)
	{
		return 0;
	}
}

int32_t GENONLINE_PLUGIN_CALL IsRelayed(const char* platformUserId, uint64_t genOnlineUserId)
{
	try
	{
		return Module().Core().IsRelayed(ResolvePeer(platformUserId, genOnlineUserId)) ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
	}
	catch (...)
	{
		return GENONLINE_PLUGIN_FALSE;
	}
}

void GENONLINE_PLUGIN_CALL DisconnectPeer(const char* platformUserId, uint64_t genOnlineUserId)
{
	try
	{
		Module().Core().DisconnectPeer(ResolvePeer(platformUserId, genOnlineUserId));
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "DisconnectPeer failed with an unknown error");
	}
}

void GENONLINE_PLUGIN_CALL DisconnectAll()
{
	Module().Core().DisconnectAll();
}

const GenOnlinePluginTransportApi kTable = {
	sizeof(GenOnlinePluginTransportApi),
	GENONLINE_PLUGIN_TRANSPORT_API_VERSION,
	&IsAvailable,
	&SetEnabled,
	&StartSignalling,
	&SendPacket,
	&GetNextPacketSize,
	&ReceivePacket,
	&FreePacket,
	&GetLatency,
	&IsRelayed,
	&DisconnectPeer,
	&DisconnectAll,
	&ReceivePacketFrom,
};

} // namespace

const GenOnlinePluginTransportApi* TransportTable()
{
	return &kTable;
}

RuntimeModule* TransportModule()
{
	return &Module();
}

} // namespace genonline_eos
