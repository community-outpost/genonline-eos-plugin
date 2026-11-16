// SPDX-License-Identifier: MIT
#include "core/linked_account_machine.h"

#include <cstring>
#include <utility>

namespace genonline_eos
{

StatusOnce::StatusOnce(GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData) : m_onStatus(onStatus), m_userData(userData), m_armed(true)
{
}

void StatusOnce::Report(bool success)
{
	if (!m_armed)
	{
		return;
	}
	// The guard disarms before the call, so the callback may start the next sign in.
	m_armed = false;
	GenOnlinePluginLinkedAccountStatusFn onStatus = m_onStatus;
	void* userData = m_userData;
	m_onStatus = nullptr;
	m_userData = nullptr;
	if (onStatus != nullptr)
	{
		onStatus(userData, success ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE);
	}
}

LinkedAccountMachine::LinkedAccountMachine(ConnectBackend& backend, LogSink log) : m_backend(backend), m_log(std::move(log))
{
}

void LinkedAccountMachine::SignIn(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData)
{
	Start(token, onStatus, userData, false);
}

void LinkedAccountMachine::Refresh(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData)
{
	Start(token, onStatus, userData, true);
}

void LinkedAccountMachine::Start(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData, bool refresh)
{
	const char* what = refresh ? "Refresh" : "Sign in";
	StatusOnce status(onStatus, userData);
	if (token == nullptr || token[0] == '\0')
	{
		Log(GENONLINE_PLUGIN_LOG_ERROR, std::string(what) + " has no token");
		status.Report(false);
		return;
	}
	if (IsBusy())
	{
		Log(GENONLINE_PLUGIN_LOG_WARNING, std::string(what) + " refused because another sign in is running");
		status.Report(false);
		return;
	}

	m_status = status;
	m_refresh = refresh && IsSignedIn();
	m_phase = Phase::LoggingIn;
	const uint32_t operation = ++m_operation;
	Log(GENONLINE_PLUGIN_LOG_INFO, std::string(what) + " with a token of " + std::to_string(std::strlen(token)) + " bytes");
	if (!m_backend.StartLogin(operation, token))
	{
		Log(GENONLINE_PLUGIN_LOG_ERROR, std::string(what) + " cannot start the EOS Connect login");
		Finish(false, std::string());
	}
}

void LinkedAccountMachine::OnLoginResult(uint32_t operation, LoginOutcome outcome, const std::string& productUserId)
{
	if (m_phase != Phase::LoggingIn || operation != m_operation)
	{
		return;
	}
	switch (outcome)
	{
	case LoginOutcome::Success:
		if (m_refresh && productUserId != m_productUserId)
		{
			Log(GENONLINE_PLUGIN_LOG_ERROR, "Refresh logged in another product user than the signed in one");
			Finish(false, std::string());
			return;
		}
		Finish(true, productUserId);
		return;
	case LoginOutcome::UserNotFound:
		if (m_refresh)
		{
			Log(GENONLINE_PLUGIN_LOG_ERROR, "Refresh found no product user for the token");
			Finish(false, std::string());
			return;
		}
		Log(GENONLINE_PLUGIN_LOG_INFO, "No product user exists yet, creating one");
		m_phase = Phase::CreatingUser;
		if (!m_backend.StartCreateUser(operation))
		{
			Log(GENONLINE_PLUGIN_LOG_ERROR, "Creating the product user cannot start");
			Finish(false, std::string());
		}
		return;
	case LoginOutcome::Failure:
		Finish(false, std::string());
		return;
	}
}

void LinkedAccountMachine::OnCreateUserResult(uint32_t operation, bool success, const std::string& productUserId)
{
	if (m_phase != Phase::CreatingUser || operation != m_operation)
	{
		return;
	}
	if (success)
	{
		Log(GENONLINE_PLUGIN_LOG_INFO, "Product user created");
	}
	Finish(success, productUserId);
}

void LinkedAccountMachine::OnLoginStatusChanged(const std::string& productUserId, bool loggedIn)
{
	if (loggedIn || productUserId.empty() || productUserId != m_productUserId)
	{
		return;
	}
	Log(GENONLINE_PLUGIN_LOG_WARNING, "EOS Connect signed the product user out");
	m_productUserId.clear();
}

void LinkedAccountMachine::Abort()
{
	// A new operation number makes every later result of the running operation stale.
	++m_operation;
	m_phase = Phase::Idle;
	m_refresh = false;
	m_productUserId.clear();
	StatusOnce status = m_status;
	m_status = StatusOnce();
	status.Report(false);
}

void LinkedAccountMachine::Finish(bool success, const std::string& productUserId)
{
	const bool refresh = m_refresh;
	m_phase = Phase::Idle;
	m_refresh = false;
	if (success && !productUserId.empty())
	{
		m_productUserId = productUserId;
		Log(GENONLINE_PLUGIN_LOG_INFO, refresh ? "Refresh succeeded" : "Signed in to EOS Connect");
	}
	else
	{
		success = false;
		Log(GENONLINE_PLUGIN_LOG_ERROR, refresh ? "Refresh failed, the current sign in stays" : "Sign in failed");
	}
	m_backend.OnSignInFinished(success, m_productUserId);
	// The status runs last, so the callback sees the final state and may start the next sign in.
	StatusOnce status = m_status;
	m_status = StatusOnce();
	status.Report(success);
}

void LinkedAccountMachine::Log(int32_t level, const std::string& text) const
{
	if (m_log)
	{
		m_log(level, text);
	}
}

} // namespace genonline_eos
