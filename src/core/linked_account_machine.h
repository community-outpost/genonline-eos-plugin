// SPDX-License-Identifier: MIT
// The EOS Connect sign in state machine behind the linked account table. Pure logic without EOS.
#pragma once

#include <GenOnlinePluginApi.h>

#include <cstdint>
#include <functional>
#include <string>

namespace genonline_eos
{

// Receives one log line without the plugin prefix.
using LogSink = std::function<void(int32_t level, const std::string& text)>;

// Holds a host status callback and runs it at most once.
class StatusOnce
{
public:
	StatusOnce() = default;
	StatusOnce(GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData);

	// True until Report runs.
	bool IsArmed() const
	{
		return m_armed;
	}

	// Runs the callback with the result the first time and does nothing afterwards.
	void Report(bool success);

private:
	GenOnlinePluginLinkedAccountStatusFn m_onStatus = nullptr;
	void* m_userData = nullptr;
	bool m_armed = false;
};

// The EOS calls the machine starts. Each returns false when the call cannot start, and otherwise reports back through the machine with the same operation number.
class ConnectBackend
{
public:
	virtual ~ConnectBackend()
	{
	}

	// Starts an EOS Connect login with token as an OpenID access token.
	virtual bool StartLogin(uint32_t operation, const char* token) = 0;

	// Starts the creation of a product user with the continuance token of the last login.
	virtual bool StartCreateUser(uint32_t operation) = 0;
};

enum class LoginOutcome
{
	Success,
	UserNotFound,
	Failure,
};

class LinkedAccountMachine
{
public:
	LinkedAccountMachine(ConnectBackend& backend, LogSink log);

	// Signs in with token. onStatus runs exactly once.
	void SignIn(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData);

	// Logs in again with a new token for the signed in user. onStatus runs exactly once. Without a signed in user it works like SignIn.
	void Refresh(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData);

	// Results of the backend calls. Results of another operation than the running one are ignored.
	void OnLoginResult(uint32_t operation, LoginOutcome outcome, const std::string& productUserId);
	void OnCreateUserResult(uint32_t operation, bool success, const std::string& productUserId);

	// EOS reports a new login status of a product user.
	void OnLoginStatusChanged(const std::string& productUserId, bool loggedIn);

	// Fails a pending sign in, signs out and ignores every later result.
	void Abort();

	bool IsSignedIn() const
	{
		return !m_productUserId.empty();
	}

	bool IsBusy() const
	{
		return m_phase != Phase::Idle;
	}

	// The signed in product user id, empty when none.
	const std::string& ProductUserId() const
	{
		return m_productUserId;
	}

private:
	enum class Phase
	{
		Idle,
		LoggingIn,
		CreatingUser,
	};

	void Start(const char* token, GenOnlinePluginLinkedAccountStatusFn onStatus, void* userData, bool refresh);
	void Finish(bool success, const std::string& productUserId);
	void Log(int32_t level, const std::string& text) const;

	ConnectBackend& m_backend;
	LogSink m_log;
	StatusOnce m_status;
	Phase m_phase = Phase::Idle;
	bool m_refresh = false;
	uint32_t m_operation = 0;
	std::string m_productUserId;
};

} // namespace genonline_eos
