// SPDX-License-Identifier: MIT
#include "plugin/eos_runtime.h"

#include "core/host_link.h"
#include "core/log_queue.h"
#include "plugin/plugin_state.h"
#include "platform/platform.h"
#include "plugin/runtime_module.h"

#include <eos_init.h>
#include <eos_logging.h>
#include <eos_version.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace genonline_eos
{

namespace
{

// GENONLINE_EOS_LOG_P2P=verbose turns on the verbose EOS log of the P2P category only, for diagnosing connections.
bool WantsVerboseP2pLog()
{
#if defined(_WIN32)
	char* value = nullptr;
	size_t length = 0;
	if (_dupenv_s(&value, &length, "GENONLINE_EOS_LOG_P2P") != 0 || value == nullptr)
	{
		return false;
	}
	const bool verbose = std::string(value) == "verbose";
	std::free(value);
	return verbose;
#else
	const char* value = std::getenv("GENONLINE_EOS_LOG_P2P");
	return value != nullptr && std::string(value) == "verbose";
#endif
}


// EOS may log from its own threads, so its lines wait here until the main thread hands them to the host.
LogQueue& SdkLogQueue()
{
	static LogQueue queue;
	return queue;
}

void EOS_CALL OnSdkLog(const EOS_LogMessage* message)
{
	if (message == nullptr)
	{
		return;
	}
	std::string text = "EOS ";
	text += message->Category != nullptr ? message->Category : "?";
	text += ": ";
	text += message->Message != nullptr ? message->Message : "";
	SdkLogQueue().Push(HostLevelFromEosLevel(static_cast<int32_t>(message->Level)), std::move(text));
}

} // namespace

void EosRuntime::AddModule(RuntimeModule* module)
{
	if (module != nullptr && std::find(m_modules.begin(), m_modules.end(), module) == m_modules.end())
	{
		m_modules.push_back(module);
	}
}

int32_t EosRuntime::Start(const Credentials& credentials, const char* productVersion)
{
	const HostLink& host = Host();
	if (m_platform != nullptr)
	{
		host.Log(GENONLINE_PLUGIN_LOG_WARNING, "Initialize called while the platform runs");
		return GENONLINE_PLUGIN_RESULT_OK;
	}

	const uint32_t missing = MissingCredentials(credentials);
	if (missing != 0)
	{
		host.Logf(GENONLINE_PLUGIN_LOG_ERROR, "This build has no EOS credentials for: %s", DescribeCredentialFields(missing).c_str());
		return GENONLINE_PLUGIN_RESULT_MISSING_CREDENTIALS;
	}

	const uint32_t overlong = OverlongCredentials(credentials);
	if (overlong != 0)
	{
		host.Logf(GENONLINE_PLUGIN_LOG_ERROR, "These EOS credentials are longer than EOS allows: %s", DescribeCredentialFields(overlong).c_str());
		return GENONLINE_PLUGIN_RESULT_PLATFORM_OPTIONS_FAILED;
	}

	if (!platform::ThisModuleFolderIsUsable())
	{
		host.Log(GENONLINE_PLUGIN_LOG_ERROR, "The plugin folder path is unknown or longer than the EOS SDK supports");
		return GENONLINE_PLUGIN_RESULT_BAD_ENVIRONMENT;
	}

	const int32_t sdkResult = InitializeSdk(productVersion);
	if (sdkResult != GENONLINE_PLUGIN_RESULT_OK)
	{
		return sdkResult;
	}

	EOS_Platform_Options options = {};
	options.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	options.ProductId = credentials.productId;
	options.SandboxId = credentials.sandboxId;
	options.DeploymentId = credentials.deploymentId;
	options.ClientCredentials.ClientId = credentials.clientId;
	options.ClientCredentials.ClientSecret = credentials.clientSecret;
	options.bIsServer = EOS_FALSE;
	// The game renders with Direct3D 8, which the overlay does not support.
	options.Flags = EOS_PF_DISABLE_OVERLAY;
	options.TickBudgetInMilliseconds = 0;

	m_platform = EOS_Platform_Create(&options);
	FlushSdkLog();
	if (m_platform == nullptr)
	{
		host.Log(GENONLINE_PLUGIN_LOG_ERROR, "EOS_Platform_Create failed");
		return GENONLINE_PLUGIN_RESULT_PLATFORM_CREATE_FAILED;
	}
	host.Logf(GENONLINE_PLUGIN_LOG_INFO, "EOS platform created with capabilities 0x%x", host.EnabledCapabilities());

	for (RuntimeModule* module : m_modules)
	{
		module->Start(m_platform);
		m_startedModules.push_back(module);
	}
	FlushSdkLog();
	return GENONLINE_PLUGIN_RESULT_OK;
}

int32_t EosRuntime::InitializeSdk(const char* productVersion)
{
	const HostLink& host = Host();
	EOS_InitializeOptions options = {};
	options.ApiVersion = EOS_INITIALIZE_API_LATEST;
	options.ProductName = "GenOnline";
	options.ProductVersion = productVersion;

	const EOS_EResult result = EOS_Initialize(&options);
	if (result == EOS_EResult::EOS_Success)
	{
		m_sdk = SdkOwnership::Owned;
	}
	else if (result == EOS_EResult::EOS_AlreadyConfigured)
	{
		// Another part of the process initialized the SDK, so it also shuts it down.
		m_sdk = SdkOwnership::Borrowed;
		host.Log(GENONLINE_PLUGIN_LOG_WARNING, "The EOS SDK was already initialized in this process");
	}
	else
	{
		host.Logf(GENONLINE_PLUGIN_LOG_ERROR, "EOS_Initialize failed: %s", EOS_EResult_ToString(result));
		return GENONLINE_PLUGIN_RESULT_SDK_INIT_FAILED;
	}

	host.Logf(GENONLINE_PLUGIN_LOG_INFO, "EOS SDK %s initialized", EOS_GetVersion());
	if (EOS_Logging_SetCallback(&OnSdkLog) != EOS_EResult::EOS_Success)
	{
		host.Log(GENONLINE_PLUGIN_LOG_WARNING, "EOS log output is not available");
	}
	// Info keeps the SDK log free of tokens, which verbose levels may print.
	EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Info);
	// The P2P category carries no tokens, so its verbose level is safe to turn on.
	if (WantsVerboseP2pLog())
	{
		EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_P2P, EOS_ELogLevel::EOS_LOG_Verbose);
		host.Log(GENONLINE_PLUGIN_LOG_INFO, "Verbose EOS P2P log is on");
	}
	return GENONLINE_PLUGIN_RESULT_OK;
}

void EosRuntime::Tick()
{
	if (m_platform != nullptr)
	{
		EOS_Platform_Tick(m_platform);
		for (RuntimeModule* module : m_startedModules)
		{
			module->Tick();
		}
	}
	FlushSdkLog();
}

void EosRuntime::Stop()
{
	for (auto it = m_startedModules.rbegin(); it != m_startedModules.rend(); ++it)
	{
		(*it)->Stop();
	}
	m_startedModules.clear();

	if (m_platform != nullptr)
	{
		EOS_Platform_Release(m_platform);
		m_platform = nullptr;
		Host().Log(GENONLINE_PLUGIN_LOG_INFO, "EOS platform released");
	}

	if (m_sdk == SdkOwnership::Owned)
	{
		EOS_Logging_SetCallback(nullptr);
		const EOS_EResult result = EOS_Shutdown();
		Host().Logf(GENONLINE_PLUGIN_LOG_INFO, "EOS SDK shut down: %s", EOS_EResult_ToString(result));
	}
	m_sdk = SdkOwnership::None;
	FlushSdkLog();
}

void EosRuntime::FlushSdkLog()
{
	size_t dropped = 0;
	const std::vector<QueuedLogLine> lines = SdkLogQueue().Drain(&dropped);
	const HostLink& host = Host();
	for (const QueuedLogLine& line : lines)
	{
		host.Log(line.level, line.text.c_str());
	}
	if (dropped != 0)
	{
		host.Logf(GENONLINE_PLUGIN_LOG_WARNING, "%u EOS log lines were dropped", static_cast<unsigned>(dropped));
	}
}

} // namespace genonline_eos
