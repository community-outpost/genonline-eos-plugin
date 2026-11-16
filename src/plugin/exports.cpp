// SPDX-License-Identifier: MIT
// The one export of the plugin DLL and the root plugin table.
#include "build_config.h"
#include "core/api_table.h"
#include "core/credentials.h"
#include "plugin/eos_runtime.h"
#include "plugin/plugin_state.h"
#include "plugin/sub_tables.h"

#include <GenOnlinePluginApi.h>

#include <exception>

namespace genonline_eos
{

namespace
{

bool g_shutDown = false;

Credentials CompiledCredentials()
{
	return Credentials{
		GENONLINE_EOS_PRODUCT_ID,
		GENONLINE_EOS_SANDBOX_ID,
		GENONLINE_EOS_DEPLOYMENT_ID,
		GENONLINE_EOS_CLIENT_ID,
		GENONLINE_EOS_CLIENT_SECRET,
	};
}

int32_t GENONLINE_PLUGIN_CALL Initialize()
{
	try
	{
		Host().Logf(GENONLINE_PLUGIN_LOG_INFO, "Initializing version %s", GENONLINE_EOS_PLUGIN_VERSION);
		return Runtime().Start(CompiledCredentials(), GENONLINE_EOS_PLUGIN_VERSION);
	}
	catch (const std::exception& error)
	{
		Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "Initialize failed: %s", error.what());
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "Initialize failed with an unknown error");
	}
	return GENONLINE_PLUGIN_RESULT_INTERNAL_ERROR;
}

void GENONLINE_PLUGIN_CALL Tick()
{
	try
	{
		Runtime().Tick();
	}
	catch (const std::exception& error)
	{
		Host().Logf(GENONLINE_PLUGIN_LOG_ERROR, "Tick failed: %s", error.what());
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "Tick failed with an unknown error");
	}
}

void GENONLINE_PLUGIN_CALL Shutdown()
{
	if (g_shutDown)
	{
		return;
	}
	g_shutDown = true;
	try
	{
		Runtime().Stop();
		Host().Log(GENONLINE_PLUGIN_LOG_INFO, "Shut down");
	}
	catch (...)
	{
		Host().Log(GENONLINE_PLUGIN_LOG_ERROR, "Shutdown failed");
	}
	Host().Release();
}

int32_t GENONLINE_PLUGIN_CALL IsExternalProcessRunning()
{
	if (!Host().IsEnabled(GENONLINE_PLUGIN_CAP_ANTICHEAT))
	{
		return GENONLINE_PLUGIN_TRUE;
	}
	return AnticheatClientAvailable() ? GENONLINE_PLUGIN_TRUE : GENONLINE_PLUGIN_FALSE;
}

int32_t GENONLINE_PLUGIN_CALL GetAnticheatIdentifier()
{
	return kEasyAntiCheatIdentifier;
}

GenOnlinePluginApi CompleteTable()
{
	GenOnlinePluginApi table = {};
	table.structSize = sizeof(GenOnlinePluginApi);
	table.apiVersion = GENONLINE_PLUGIN_API_VERSION;
	table.name = GENONLINE_EOS_PLUGIN_NAME;
	table.version = GENONLINE_EOS_PLUGIN_VERSION;
	table.capabilities = kOfferedCapabilities;
	table.Initialize = &Initialize;
	table.Tick = &Tick;
	table.Shutdown = &Shutdown;
	table.IsExternalProcessRunning = &IsExternalProcessRunning;
	table.GetAnticheatIdentifier = &GetAnticheatIdentifier;
	table.linkedAccount = LinkedAccountTable();
	table.anticheat = AnticheatTable();
	table.transport = TransportTable();
	return table;
}

void RegisterModules()
{
	EosRuntime& runtime = Runtime();
	runtime.AddModule(LinkedAccountModule());
	runtime.AddModule(AnticheatModule());
	runtime.AddModule(TransportModule());
}

} // namespace

} // namespace genonline_eos

extern "C" GENONLINE_PLUGIN_EXPORT int32_t GENONLINE_PLUGIN_CALL GenOnlinePlugin_GetApi(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, GenOnlinePluginApi* out)
{
	using namespace genonline_eos;
	const int32_t check = CheckGetApiArguments(hostApiVersion, host, out);
	if (check != GENONLINE_PLUGIN_RESULT_OK)
	{
		return check;
	}
	try
	{
		const int32_t filled = FillPluginApi(CompleteTable(), out);
		if (filled != GENONLINE_PLUGIN_RESULT_OK)
		{
			return filled;
		}
		Host().Bind(*host, kOfferedCapabilities);
		RegisterModules();
		g_shutDown = false;
		return GENONLINE_PLUGIN_RESULT_OK;
	}
	catch (...)
	{
		return GENONLINE_PLUGIN_RESULT_INTERNAL_ERROR;
	}
}
