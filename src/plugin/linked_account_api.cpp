// SPDX-License-Identifier: MIT
// The linked account table backed by EOS Connect. The sign in rules live in core/linked_account_machine.
#include "plugin/sub_tables.h"

#include "core/host_buffer.h"
#include "core/host_link.h"
#include "core/linked_account_machine.h"
#include "plugin/eos_ids.h"
#include "plugin/plugin_state.h"
#include "plugin/runtime_module.h"

#include <eos_connect.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace genonline_eos
{

namespace
{

void* OperationToClientData(uint32_t operation)
{
	return reinterpret_cast<void*>(static_cast<uintptr_t>(operation));
}

uint32_t OperationFromClientData(void* clientData)
{
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clientData));
}

// Connects the sign in state machine to EOS Connect. Every EOS callback arrives during EOS_Platform_Tick on the main thread.
class ConnectModule final : public RuntimeModule, public ConnectBackend
{
public:
	ConnectModule()
		: m_machine(*this, [](int32_t level, const std::string& text)
			  {
				  Host().Log(level, text.c_str());
			  })
	{
	}

	const char* Name() const override
	{
		return "linked account";
	}

	void Start(EOS_HPlatform platform) override
	{
		m_connect = EOS_Platform_GetConnectInterface(platform);
		if (m_connect == nullptr)
		{
			Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "EOS Connect is not available");
			return;
		}
		EOS_Connect_AddNotifyAuthExpirationOptions expirationOptions = {};
		expirationOptions.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
		m_authExpiration = EOS_Connect_AddNotifyAuthExpiration(m_connect, &expirationOptions, nullptr, &OnAuthExpiration);
		EOS_Connect_AddNotifyLoginStatusChangedOptions statusOptions = {};
		statusOptions.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
		m_loginStatus = EOS_Connect_AddNotifyLoginStatusChanged(m_connect, &statusOptions, nullptr, &OnLoginStatusChanged);
	}

	void Tick() override
	{
	}

	void Stop() override
	{
		EOS_HConnect connect = m_connect;
		// Without the interface every new sign in fails at once, also one started from a status callback below.
		m_connect = nullptr;
		if (connect != nullptr)
		{
			if (m_authExpiration != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_Connect_RemoveNotifyAuthExpiration(connect, m_authExpiration);
			}
			if (m_loginStatus != EOS_INVALID_NOTIFICATIONID)
			{
				EOS_Connect_RemoveNotifyLoginStatusChanged(connect, m_loginStatus);
			}
		}
		m_authExpiration = EOS_INVALID_NOTIFICATIONID;
		m_loginStatus = EOS_INVALID_NOTIFICATIONID;
		m_localUser = nullptr;
		m_continuance = nullptr;
		m_machine.Abort();
	}

	bool StartLogin(uint32_t operation, const char* token) override
	{
		if (m_connect == nullptr)
		{
			return false;
		}
		EOS_Connect_Credentials credentials = {};
		credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		credentials.Token = token;
		credentials.Type = EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN;
		EOS_Connect_LoginOptions options = {};
		options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		options.Credentials = &credentials;
		options.UserLoginInfo = nullptr;
		EOS_Connect_Login(m_connect, &options, OperationToClientData(operation), &OnLogin);
		return true;
	}

	bool StartCreateUser(uint32_t operation) override
	{
		if (m_connect == nullptr || m_continuance == nullptr)
		{
			return false;
		}
		EOS_Connect_CreateUserOptions options = {};
		options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
		options.ContinuanceToken = m_continuance;
		EOS_Connect_CreateUser(m_connect, &options, OperationToClientData(operation), &OnCreateUser);
		return true;
	}

	LinkedAccountMachine& Machine()
	{
		return m_machine;
	}

	EOS_ProductUserId SignedInUser() const
	{
		return m_machine.IsSignedIn() ? m_localUser : nullptr;
	}

	bool CopyIdentityProof(char* buffer, uint32_t bufferSize)
	{
		if (m_connect == nullptr || !m_machine.IsSignedIn() || m_localUser == nullptr)
		{
			return false;
		}
		EOS_Connect_CopyIdTokenOptions options = {};
		options.ApiVersion = EOS_CONNECT_COPYIDTOKEN_API_LATEST;
		options.LocalUserId = m_localUser;
		EOS_Connect_IdToken* idToken = nullptr;
		const EOS_EResult result = EOS_Connect_CopyIdToken(m_connect, &options, &idToken);
		if (result != EOS_EResult::EOS_Success || idToken == nullptr)
		{
			Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "EOS_Connect_CopyIdToken failed: %s", EOS_EResult_ToString(result));
			return false;
		}
		const bool copied = CopyToHostBuffer(idToken->JsonWebToken, buffer, bufferSize);
		if (!copied)
		{
			const size_t length = idToken->JsonWebToken != nullptr ? std::strlen(idToken->JsonWebToken) : 0;
			Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "The ID token of %u bytes does not fit the host buffer of %u bytes", static_cast<unsigned>(length), bufferSize);
		}
		EOS_Connect_IdToken_Release(idToken);
		return copied;
	}

private:
	// Keeps the EOS handle of the signed in product user in step with the machine.
	void TakeUser(EOS_ProductUserId id, const std::string& text)
	{
		if (m_machine.IsSignedIn() && m_machine.ProductUserId() == text)
		{
			m_localUser = id;
		}
		else if (!m_machine.IsSignedIn())
		{
			m_localUser = nullptr;
		}
	}

	static void EOS_CALL OnLogin(const EOS_Connect_LoginCallbackInfo* info);
	static void EOS_CALL OnCreateUser(const EOS_Connect_CreateUserCallbackInfo* info);
	static void EOS_CALL OnAuthExpiration(const EOS_Connect_AuthExpirationCallbackInfo* info);
	static void EOS_CALL OnLoginStatusChanged(const EOS_Connect_LoginStatusChangedCallbackInfo* info);

	LinkedAccountMachine m_machine;
	EOS_HConnect m_connect = nullptr;
	EOS_ContinuanceToken m_continuance = nullptr;
	EOS_ProductUserId m_localUser = nullptr;
	EOS_NotificationId m_authExpiration = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId m_loginStatus = EOS_INVALID_NOTIFICATIONID;
};

ConnectModule& Module()
{
	static ConnectModule module;
	return module;
}

void EOS_CALL ConnectModule::OnLogin(const EOS_Connect_LoginCallbackInfo* info)
{
	if (info == nullptr || !EOS_EResult_IsOperationComplete(info->ResultCode))
	{
		return;
	}
	ConnectModule& module = Module();
	const uint32_t operation = OperationFromClientData(info->ClientData);
	if (info->ResultCode == EOS_EResult::EOS_Success)
	{
		const std::string text = ProductUserIdText(info->LocalUserId);
		module.m_machine.OnLoginResult(operation, LoginOutcome::Success, text);
		module.TakeUser(info->LocalUserId, text);
		return;
	}
	if (info->ResultCode == EOS_EResult::EOS_InvalidUser && info->ContinuanceToken != nullptr)
	{
		// The continuance token is used right away while the callback runs.
		module.m_continuance = info->ContinuanceToken;
		module.m_machine.OnLoginResult(operation, LoginOutcome::UserNotFound, std::string());
		module.m_continuance = nullptr;
		return;
	}
	Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "EOS_Connect_Login failed: %s", EOS_EResult_ToString(info->ResultCode));
	module.m_machine.OnLoginResult(operation, LoginOutcome::Failure, std::string());
}

void EOS_CALL ConnectModule::OnCreateUser(const EOS_Connect_CreateUserCallbackInfo* info)
{
	if (info == nullptr || !EOS_EResult_IsOperationComplete(info->ResultCode))
	{
		return;
	}
	ConnectModule& module = Module();
	const uint32_t operation = OperationFromClientData(info->ClientData);
	const bool success = info->ResultCode == EOS_EResult::EOS_Success;
	if (!success)
	{
		Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "EOS_Connect_CreateUser failed: %s", EOS_EResult_ToString(info->ResultCode));
	}
	const std::string text = success ? ProductUserIdText(info->LocalUserId) : std::string();
	module.m_machine.OnCreateUserResult(operation, success, text);
	module.TakeUser(info->LocalUserId, text);
}

void EOS_CALL ConnectModule::OnAuthExpiration(const EOS_Connect_AuthExpirationCallbackInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	Host().Log(GENONLINE_PLUGIN_LOG_WARNING, "The EOS Connect sign in expires soon and needs a refresh");
}

void EOS_CALL ConnectModule::OnLoginStatusChanged(const EOS_Connect_LoginStatusChangedCallbackInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	ConnectModule& module = Module();
	const bool loggedIn = info->CurrentStatus == EOS_ELoginStatus::EOS_LS_LoggedIn;
	module.m_machine.OnLoginStatusChanged(ProductUserIdText(info->LocalUserId), loggedIn);
	module.TakeUser(info->LocalUserId, ProductUserIdText(info->LocalUserId));
}

void GENONLINE_PLUGIN_CALL SignIn(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData)
{
	try
	{
		Module().Machine().SignIn(token, onStatus, userData);
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "Sign in failed with an unknown error");
	}
}

void GENONLINE_PLUGIN_CALL Refresh(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData)
{
	try
	{
		Module().Machine().Refresh(token, onStatus, userData);
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "Refresh failed with an unknown error");
	}
}

int32_t GENONLINE_PLUGIN_CALL IsSignedIn()
{
	return Module().Machine().IsSignedIn() ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
}

int32_t GENONLINE_PLUGIN_CALL GetPlatformUserId(char* buffer, uint32_t bufferSize)
{
	return CopyToHostBuffer(Module().Machine().ProductUserId().c_str(), buffer, bufferSize) ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
}

int32_t GENONLINE_PLUGIN_CALL GetIdentityProof(char* buffer, uint32_t bufferSize)
{
	return Module().CopyIdentityProof(buffer, bufferSize) ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
}

const GenOnlinePluginLinkedAccountApi kTable = {
	sizeof(GenOnlinePluginLinkedAccountApi),
	GENONLINE_PLUGIN_LINKED_ACCOUNT_API_VERSION,
	&SignIn,
	&Refresh,
	&IsSignedIn,
	&GetPlatformUserId,
	&GetIdentityProof,
};

} // namespace

const GenOnlinePluginLinkedAccountApi* LinkedAccountTable()
{
	return &kTable;
}

RuntimeModule* LinkedAccountModule()
{
	return &Module();
}

EOS_ProductUserId SignedInProductUser()
{
	return Module().SignedInUser();
}

} // namespace genonline_eos
