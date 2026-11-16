// SPDX-License-Identifier: MIT
// The anticheat table backed by the Easy Anti-Cheat client in peer to peer mode. The session rules live in core/anticheat_core.
#include "plugin/sub_tables.h"

#include "core/anticheat_core.h"
#include "core/host_link.h"
#include "plugin/eos_ids.h"
#include "plugin/plugin_state.h"
#include "plugin/runtime_module.h"

#include <eos_anticheatclient.h>

#include <cstdint>
#include <string>

namespace genonline_eos
{

namespace
{

static_assert(kPeerAuthenticationTimeoutSeconds >= EOS_ANTICHEATCLIENT_REGISTERPEER_MIN_AUTHENTICATIONTIMEOUT, "the timeout is within the EAC range");
static_assert(kPeerAuthenticationTimeoutSeconds <= EOS_ANTICHEATCLIENT_REGISTERPEER_MAX_AUTHENTICATIONTIMEOUT, "the timeout is within the EAC range");

// EAC peer handles are pointer sized values the plugin chooses, and the local user is the pointer value -1.
EOS_AntiCheatCommon_ClientHandle ToClientHandle(uintptr_t handle)
{
	return reinterpret_cast<EOS_AntiCheatCommon_ClientHandle>(handle);
}

uintptr_t FromClientHandle(EOS_AntiCheatCommon_ClientHandle handle)
{
	if (handle == EOS_ANTICHEATCLIENT_PEER_SELF)
	{
		return kSelfPeerHandle;
	}
	return reinterpret_cast<uintptr_t>(handle);
}

void SendToPeerViaHost(uint64_t genOnlineUserId, const uint8_t* data, uint32_t length)
{
	const GenOnlinePluginHostApi& host = Host().Table();
	if (Host().IsBound() && host.SendMessageViaTransport != nullptr)
	{
		host.SendMessageViaTransport(host.userData, genOnlineUserId, data, length);
	}
}

void ActionRequiredToHost(uint64_t genOnlineUserId, const std::string& reason, int32_t action, int32_t actionReason)
{
	const GenOnlinePluginHostApi& host = Host().Table();
	if (Host().IsBound() && host.ActionRequired != nullptr)
	{
		host.ActionRequired(host.userData, genOnlineUserId, reason.c_str(), action, actionReason);
	}
}

void IntegrityViolationToHost(const std::string& reason, int32_t violationType)
{
	const GenOnlinePluginHostApi& host = Host().Table();
	if (Host().IsBound() && host.IntegrityViolation != nullptr)
	{
		host.IntegrityViolation(host.userData, reason.c_str(), violationType);
	}
}

// Connects the anticheat rules to the EAC client. Every EOS callback arrives during EOS_Platform_Tick on the main thread.
class EacModule final : public RuntimeModule, public EacBackend
{
public:
	EacModule()
		: m_core(Peers(), *this,
			  AnticheatHooks{&SendToPeerViaHost, &ActionRequiredToHost, &IntegrityViolationToHost,
				  [](int32_t level, const std::string& text)
				  {
					  Host().Log(level, text.c_str());
				  }})
	{
	}

	const char* Name() const override
	{
		return "anticheat";
	}

	void Start(EOS_HPlatform platform) override
	{
		m_core.SetEnabled(false);
		if (!Host().IsEnabled(GENONLINE_PLUGIN_CAP_ANTICHEAT))
		{
			Host().Log(GENONLINE_PLUGIN_LOG_INFO, "Anticheat is not enabled, only the peer registry runs");
			return;
		}
		m_eac = EOS_Platform_GetAntiCheatClientInterface(platform);
		if (m_eac == nullptr)
		{
			Host().Log(GENONLINE_PLUGIN_LOG_WARNING, "The Easy Anti-Cheat client is not available. The game must start through the Easy Anti-Cheat bootstrapper");
			return;
		}
		EOS_AntiCheatClient_AddNotifyMessageToPeerOptions messageOptions = {};
		messageOptions.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYMESSAGETOPEER_API_LATEST;
		m_messageNotification = EOS_AntiCheatClient_AddNotifyMessageToPeer(m_eac, &messageOptions, nullptr, &OnMessageToPeer);
		EOS_AntiCheatClient_AddNotifyPeerActionRequiredOptions actionOptions = {};
		actionOptions.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYPEERACTIONREQUIRED_API_LATEST;
		m_actionNotification = EOS_AntiCheatClient_AddNotifyPeerActionRequired(m_eac, &actionOptions, nullptr, &OnPeerActionRequired);
		EOS_AntiCheatClient_AddNotifyPeerAuthStatusChangedOptions authOptions = {};
		authOptions.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYPEERAUTHSTATUSCHANGED_API_LATEST;
		m_authNotification = EOS_AntiCheatClient_AddNotifyPeerAuthStatusChanged(m_eac, &authOptions, nullptr, &OnPeerAuthStatusChanged);
		EOS_AntiCheatClient_AddNotifyClientIntegrityViolatedOptions integrityOptions = {};
		integrityOptions.ApiVersion = EOS_ANTICHEATCLIENT_ADDNOTIFYCLIENTINTEGRITYVIOLATED_API_LATEST;
		m_integrityNotification = EOS_AntiCheatClient_AddNotifyClientIntegrityViolated(m_eac, &integrityOptions, nullptr, &OnClientIntegrityViolated);
		if (m_messageNotification == EOS_INVALID_NOTIFICATIONID || m_actionNotification == EOS_INVALID_NOTIFICATIONID || m_integrityNotification == EOS_INVALID_NOTIFICATIONID)
		{
			Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "The Easy Anti-Cheat client refused its notifications and is not usable");
			RemoveNotifications();
			m_eac = nullptr;
			return;
		}
		m_usable = true;
		m_core.SetEnabled(true);
		Host().Log(GENONLINE_PLUGIN_LOG_INFO, "Easy Anti-Cheat client ready");
	}

	void Tick() override
	{
		m_core.SetLocalProductUserId(ProductUserIdText(SignedInProductUser()));
	}

	void Stop() override
	{
		m_core.Reset();
		RemoveNotifications();
		m_core.SetEnabled(false);
		m_eac = nullptr;
		m_usable = false;
	}

	AnticheatCore& Core()
	{
		return m_core;
	}

	bool IsUsable() const
	{
		return m_usable && !m_protectionMissing;
	}

	bool BeginSession() override
	{
		EOS_ProductUserId user = SignedInProductUser();
		if (m_eac == nullptr || user == nullptr)
		{
			Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "An Easy Anti-Cheat session needs a signed in product user");
			return false;
		}
		EOS_AntiCheatClient_BeginSessionOptions options = {};
		options.ApiVersion = EOS_ANTICHEATCLIENT_BEGINSESSION_API_LATEST;
		options.LocalUserId = user;
		options.Mode = EOS_EAntiCheatClientMode::EOS_ACCM_PeerToPeer;
		return Check(EOS_AntiCheatClient_BeginSession(m_eac, &options), "EOS_AntiCheatClient_BeginSession");
	}

	void EndSession() override
	{
		if (m_eac == nullptr)
		{
			return;
		}
		EOS_AntiCheatClient_EndSessionOptions options = {};
		options.ApiVersion = EOS_ANTICHEATCLIENT_ENDSESSION_API_LATEST;
		Check(EOS_AntiCheatClient_EndSession(m_eac, &options), "EOS_AntiCheatClient_EndSession");
	}

	bool RegisterPeer(uintptr_t handle, const std::string& productUserId, bool protectedClient, uint32_t authenticationTimeoutSeconds) override
	{
		EOS_ProductUserId peer = EOS_ProductUserId_FromString(productUserId.c_str());
		if (m_eac == nullptr || peer == nullptr || !EOS_ProductUserId_IsValid(peer))
		{
			return false;
		}
		EOS_AntiCheatClient_RegisterPeerOptions options = {};
		options.ApiVersion = EOS_ANTICHEATCLIENT_REGISTERPEER_API_LATEST;
		options.PeerHandle = ToClientHandle(handle);
		options.ClientType = protectedClient ? EOS_EAntiCheatCommonClientType::EOS_ACCCT_ProtectedClient : EOS_EAntiCheatCommonClientType::EOS_ACCCT_UnprotectedClient;
		options.ClientPlatform = EOS_EAntiCheatCommonClientPlatform::EOS_ACCCP_Windows;
		options.AuthenticationTimeout = authenticationTimeoutSeconds;
		options.AccountId_DEPRECATED = nullptr;
		options.IpAddress = nullptr;
		options.PeerProductUserId = peer;
		return Check(EOS_AntiCheatClient_RegisterPeer(m_eac, &options), "EOS_AntiCheatClient_RegisterPeer");
	}

	bool UnregisterPeer(uintptr_t handle) override
	{
		if (m_eac == nullptr)
		{
			return false;
		}
		EOS_AntiCheatClient_UnregisterPeerOptions options = {};
		options.ApiVersion = EOS_ANTICHEATCLIENT_UNREGISTERPEER_API_LATEST;
		options.PeerHandle = ToClientHandle(handle);
		return Check(EOS_AntiCheatClient_UnregisterPeer(m_eac, &options), "EOS_AntiCheatClient_UnregisterPeer");
	}

	bool ReceiveFromPeer(uintptr_t handle, const uint8_t* data, uint32_t length) override
	{
		if (m_eac == nullptr)
		{
			return false;
		}
		EOS_AntiCheatClient_ReceiveMessageFromPeerOptions options = {};
		options.ApiVersion = EOS_ANTICHEATCLIENT_RECEIVEMESSAGEFROMPEER_API_LATEST;
		options.PeerHandle = ToClientHandle(handle);
		options.DataLengthBytes = length;
		options.Data = data;
		return Check(EOS_AntiCheatClient_ReceiveMessageFromPeer(m_eac, &options), "EOS_AntiCheatClient_ReceiveMessageFromPeer");
	}

private:
	// Logs a failed EAC call and notes when client protection is missing, which makes the client unusable.
	bool Check(EOS_EResult result, const char* call)
	{
		if (result == EOS_EResult::EOS_Success)
		{
			return true;
		}
		if (result == EOS_EResult::EOS_AntiCheat_ClientProtectionNotAvailable)
		{
			m_protectionMissing = true;
		}
		Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "%s failed: %s", call, EOS_EResult_ToString(result));
		return false;
	}

	void RemoveNotifications()
	{
		if (m_eac != nullptr)
		{
			if (m_messageNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_AntiCheatClient_RemoveNotifyMessageToPeer(m_eac, m_messageNotification);
			}
			if (m_actionNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_AntiCheatClient_RemoveNotifyPeerActionRequired(m_eac, m_actionNotification);
			}
			if (m_authNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_AntiCheatClient_RemoveNotifyPeerAuthStatusChanged(m_eac, m_authNotification);
			}
			if (m_integrityNotification != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_AntiCheatClient_RemoveNotifyClientIntegrityViolated(m_eac, m_integrityNotification);
			}
		}
		m_messageNotification = EOS_INVALID_NOTIFICATIONID;
		m_actionNotification = EOS_INVALID_NOTIFICATIONID;
		m_authNotification = EOS_INVALID_NOTIFICATIONID;
		m_integrityNotification = EOS_INVALID_NOTIFICATIONID;
	}

	static void EOS_CALL OnMessageToPeer(const EOS_AntiCheatCommon_OnMessageToClientCallbackInfo* info);
	static void EOS_CALL OnPeerActionRequired(const EOS_AntiCheatCommon_OnClientActionRequiredCallbackInfo* info);
	static void EOS_CALL OnPeerAuthStatusChanged(const EOS_AntiCheatCommon_OnClientAuthStatusChangedCallbackInfo* info);
	static void EOS_CALL OnClientIntegrityViolated(const EOS_AntiCheatClient_OnClientIntegrityViolatedCallbackInfo* info);

	AnticheatCore m_core;
	EOS_HAntiCheatClient m_eac = nullptr;
	bool m_usable = false;
	bool m_protectionMissing = false;
	EOS_NotificationId m_messageNotification = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_actionNotification = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_authNotification = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_integrityNotification = EOS_INVALID_NOTIFICATIONID;
};

EacModule& Module()
{
	static EacModule module;
	return module;
}

void EOS_CALL EacModule::OnMessageToPeer(const EOS_AntiCheatCommon_OnMessageToClientCallbackInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	Module().m_core.OnMessageToPeer(FromClientHandle(info->ClientHandle), static_cast<const uint8_t*>(info->MessageData), info->MessageDataSizeBytes);
}

void EOS_CALL EacModule::OnPeerActionRequired(const EOS_AntiCheatCommon_OnClientActionRequiredCallbackInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	Module().m_core.OnActionRequired(FromClientHandle(info->ClientHandle), static_cast<int32_t>(info->ClientAction), static_cast<int32_t>(info->ActionReasonCode), info->ActionReasonDetailsString);
}

void EOS_CALL EacModule::OnPeerAuthStatusChanged(const EOS_AntiCheatCommon_OnClientAuthStatusChangedCallbackInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	Host().Logf(GENONLINE_PLUGIN_LOG_INFO, "Easy Anti-Cheat peer %llu reached authentication status %d", static_cast<unsigned long long>(FromClientHandle(info->ClientHandle)), static_cast<int>(info->ClientAuthStatus));
}

void EOS_CALL EacModule::OnClientIntegrityViolated(const EOS_AntiCheatClient_OnClientIntegrityViolatedCallbackInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	Module().m_core.OnIntegrityViolated(static_cast<int32_t>(info->ViolationType), info->ViolationMessage);
}

void GENONLINE_PLUGIN_CALL BeginSession()
{
	try
	{
		Module().Core().BeginSession();
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "BeginSession failed with an unknown error");
	}
}

void GENONLINE_PLUGIN_CALL EndSession()
{
	try
	{
		Module().Core().EndSession();
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "EndSession failed with an unknown error");
	}
}

int32_t GENONLINE_PLUGIN_CALL RegisterPeer(const char* platformUserId, uint64_t genOnlineUserId, int32_t clientType)
{
	try
	{
		const std::string peer = platformUserId != nullptr ? platformUserId : "";
		return Module().Core().RegisterPeer(peer, genOnlineUserId, clientType) ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
	}
	catch (...)
	{
		return GENONLINE_PLUGIN_FALSE;
	}
}

int32_t GENONLINE_PLUGIN_CALL DeregisterPeer(const char* platformUserId, uint64_t genOnlineUserId)
{
	try
	{
		return Module().Core().DeregisterPeer(ResolvePeer(platformUserId, genOnlineUserId)) ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
	}
	catch (...)
	{
		return GENONLINE_PLUGIN_FALSE;
	}
}

void GENONLINE_PLUGIN_CALL MessageArrived(uint64_t genOnlineUserId, const void* data, uint32_t dataLength)
{
	try
	{
		Module().Core().MessageArrived(genOnlineUserId, static_cast<const uint8_t*>(data), dataLength);
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "MessageArrived failed with an unknown error");
	}
}

const GenOnlinePluginAnticheatApi kTable = {
	sizeof(GenOnlinePluginAnticheatApi),
	GENONLINE_PLUGIN_ANTICHEAT_API_VERSION,
	&BeginSession,
	&EndSession,
	&RegisterPeer,
	&DeregisterPeer,
	&MessageArrived,
};

} // namespace

const GenOnlinePluginAnticheatApi* AnticheatTable()
{
	return &kTable;
}

RuntimeModule* AnticheatModule()
{
	return &Module();
}

bool AnticheatClientAvailable()
{
	return Module().IsUsable();
}

} // namespace genonline_eos
