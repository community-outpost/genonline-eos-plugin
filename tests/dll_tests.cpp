// SPDX-License-Identifier: MIT
// Tests of the staged plugin DLL through its export, loaded the way the game loads it.
#include "test_harness.h"

#include <GenOnlinePluginApi.h>

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace
{

const char* g_dllPath = nullptr;

struct HostRecorder
{
	std::vector<int32_t> levels;
	std::vector<std::string> lines;
	int otherCallbacks = 0;
	std::vector<uint64_t> stateUsers;
	std::vector<int32_t> states;
};

void GENONLINE_PLUGIN_CALL RecordLog(void* userData, int32_t level, const char* message)
{
	HostRecorder* recorder = static_cast<HostRecorder*>(userData);
	recorder->levels.push_back(level);
	recorder->lines.push_back(message);
}

void GENONLINE_PLUGIN_CALL RecordChat(void* userData, const char*)
{
	++static_cast<HostRecorder*>(userData)->otherCallbacks;
}

void GENONLINE_PLUGIN_CALL RecordState(void* userData, const char*, uint64_t genOnlineUserId, int32_t state)
{
	HostRecorder* recorder = static_cast<HostRecorder*>(userData);
	recorder->stateUsers.push_back(genOnlineUserId);
	recorder->states.push_back(state);
}

void GENONLINE_PLUGIN_CALL RecordAction(void* userData, uint64_t, const char*, int32_t, int32_t)
{
	++static_cast<HostRecorder*>(userData)->otherCallbacks;
}

void GENONLINE_PLUGIN_CALL RecordIntegrity(void* userData, const char*, int32_t)
{
	++static_cast<HostRecorder*>(userData)->otherCallbacks;
}

void GENONLINE_PLUGIN_CALL RecordSend(void* userData, uint64_t, const void*, uint32_t)
{
	++static_cast<HostRecorder*>(userData)->otherCallbacks;
}

GenOnlinePluginHostApi MakeHost(HostRecorder* recorder, uint32_t enabledCapabilities)
{
	GenOnlinePluginHostApi host = {};
	host.structSize = sizeof(host);
	host.userData = recorder;
	host.Log = &RecordLog;
	host.LobbyChatOutput = &RecordChat;
	host.ConnectionStateChanged = &RecordState;
	host.ActionRequired = &RecordAction;
	host.IntegrityViolation = &RecordIntegrity;
	host.SendMessageViaTransport = &RecordSend;
	host.enabledCapabilities = enabledCapabilities;
	return host;
}

constexpr uint32_t kAllCapabilities = GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT | GENONLINE_PLUGIN_CAP_TRANSPORT;

GenOnlinePluginGetApiFn LoadGetApi()
{
	// The altered search path finds EOSSDK-Win32-Shipping.dll next to the plugin only for a path with backslashes.
	std::string path = g_dllPath;
	for (char& c : path)
	{
		if (c == '/')
		{
			c = '\\';
		}
	}
	HMODULE module = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (module == nullptr)
	{
		std::fprintf(stderr, "LoadLibraryEx(%s) failed with %lu\n", path.c_str(), GetLastError());
		return nullptr;
	}
	return reinterpret_cast<GenOnlinePluginGetApiFn>(GetProcAddress(module, GENONLINE_PLUGIN_GET_API_EXPORT_NAME));
}

// Loads the DLL and fills a full size plugin table. Returns false when that fails.
bool GetTable(const GenOnlinePluginHostApi& host, GenOnlinePluginApi* out)
{
	GenOnlinePluginGetApiFn getApi = LoadGetApi();
	if (getApi == nullptr)
	{
		return false;
	}
	std::memset(out, 0, sizeof(*out));
	out->structSize = sizeof(*out);
	return getApi(GENONLINE_PLUGIN_API_VERSION, &host, out) == GENONLINE_PLUGIN_RESULT_OK;
}

bool AllPrefixed(const HostRecorder& recorder)
{
	for (const std::string& line : recorder.lines)
	{
		if (line.rfind("[genonline_eos] ", 0) != 0)
		{
			return false;
		}
	}
	return true;
}

int g_statusCalls = 0;
int32_t g_lastStatus = -1;

void GENONLINE_PLUGIN_CALL RecordStatus(void* userData, int32_t success)
{
	CHECK(userData == &g_statusCalls);
	++g_statusCalls;
	g_lastStatus = success;
}

} // namespace

TEST_CASE(dll_exports_getapi)
{
	CHECK(LoadGetApi() != nullptr);
}

TEST_CASE(dll_getapi_rejects_other_versions_and_null_arguments)
{
	GenOnlinePluginGetApiFn getApi = LoadGetApi();
	CHECK(getApi != nullptr);
	if (getApi == nullptr)
	{
		return;
	}
	HostRecorder recorder;
	const GenOnlinePluginHostApi host = MakeHost(&recorder, kAllCapabilities);
	GenOnlinePluginApi out = {};
	out.structSize = sizeof(out);
	CHECK(getApi(2, &host, &out) == GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH);
	CHECK(getApi(0, &host, &out) == GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH);
	CHECK(getApi(1, nullptr, &out) == GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT);
	CHECK(getApi(1, &host, nullptr) == GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT);
	CHECK(out.name == nullptr);
}

TEST_CASE(dll_table_fields_are_complete)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	CHECK(api.structSize == sizeof(GenOnlinePluginApi));
	CHECK(api.apiVersion == GENONLINE_PLUGIN_API_VERSION);
	CHECK(api.name != nullptr && std::strcmp(api.name, "genonline_eos") == 0);
	CHECK(api.version != nullptr && std::strcmp(api.version, GENONLINE_EOS_EXPECTED_VERSION) == 0);
	CHECK(api.capabilities == kAllCapabilities);
	CHECK(api.Initialize != nullptr);
	CHECK(api.Tick != nullptr);
	CHECK(api.Shutdown != nullptr);
	CHECK(api.IsExternalProcessRunning != nullptr);
	CHECK(api.GetAnticheatIdentifier != nullptr);

	const GenOnlinePluginLinkedAccountApi* linked = api.linkedAccount;
	CHECK(linked != nullptr);
	if (linked != nullptr)
	{
		CHECK(linked->structSize == sizeof(GenOnlinePluginLinkedAccountApi));
		CHECK(linked->version == GENONLINE_PLUGIN_LINKED_ACCOUNT_API_VERSION);
		CHECK(linked->SignIn != nullptr);
		CHECK(linked->Refresh != nullptr);
		CHECK(linked->IsSignedIn != nullptr);
		CHECK(linked->GetPlatformUserId != nullptr);
		CHECK(linked->GetIdentityProof != nullptr);
	}

	const GenOnlinePluginAnticheatApi* anticheat = api.anticheat;
	CHECK(anticheat != nullptr);
	if (anticheat != nullptr)
	{
		CHECK(anticheat->structSize == sizeof(GenOnlinePluginAnticheatApi));
		CHECK(anticheat->version == GENONLINE_PLUGIN_ANTICHEAT_API_VERSION);
		CHECK(anticheat->BeginSession != nullptr);
		CHECK(anticheat->EndSession != nullptr);
		CHECK(anticheat->RegisterPeer != nullptr);
		CHECK(anticheat->DeregisterPeer != nullptr);
		CHECK(anticheat->MessageArrived != nullptr);
	}

	const GenOnlinePluginTransportApi* transport = api.transport;
	CHECK(transport != nullptr);
	if (transport != nullptr)
	{
		CHECK(transport->structSize == sizeof(GenOnlinePluginTransportApi));
		CHECK(transport->version == GENONLINE_PLUGIN_TRANSPORT_API_VERSION);
		CHECK(transport->IsAvailable != nullptr);
		CHECK(transport->SetEnabled != nullptr);
		CHECK(transport->StartSignalling != nullptr);
		CHECK(transport->SendPacket != nullptr);
		CHECK(transport->GetNextPacketSize != nullptr);
		CHECK(transport->ReceivePacket != nullptr);
		CHECK(transport->FreePacket != nullptr);
		CHECK(transport->GetLatency != nullptr);
		CHECK(transport->IsRelayed != nullptr);
		CHECK(transport->DisconnectPeer != nullptr);
		CHECK(transport->DisconnectAll != nullptr);
		CHECK(transport->ReceivePacketFrom != nullptr);
	}
}

TEST_CASE(dll_getapi_respects_a_shorter_host_plugin_table)
{
	GenOnlinePluginGetApiFn getApi = LoadGetApi();
	CHECK(getApi != nullptr);
	if (getApi == nullptr)
	{
		return;
	}
	HostRecorder recorder;
	const GenOnlinePluginHostApi host = MakeHost(&recorder, kAllCapabilities);
	GenOnlinePluginApi out;
	std::memset(&out, 0, sizeof(out));
	out.structSize = static_cast<uint32_t>(offsetof(GenOnlinePluginApi, linkedAccount));
	CHECK(getApi(GENONLINE_PLUGIN_API_VERSION, &host, &out) == GENONLINE_PLUGIN_RESULT_OK);
	CHECK(out.structSize == offsetof(GenOnlinePluginApi, linkedAccount));
	CHECK(out.GetAnticheatIdentifier != nullptr);
	CHECK(out.linkedAccount == nullptr);
	CHECK(out.transport == nullptr);
}

TEST_CASE(dll_anticheat_identifier_is_easy_anti_cheat)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	CHECK(api.GetAnticheatIdentifier != nullptr && api.GetAnticheatIdentifier() == 9481);
}

TEST_CASE(dll_external_process_check_passes_without_anticheat)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, GENONLINE_PLUGIN_CAP_TRANSPORT | GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT), &api));
	CHECK(api.IsExternalProcessRunning != nullptr && api.IsExternalProcessRunning() == GENONLINE_PLUGIN_TRUE);
}

TEST_CASE(dll_external_process_check_fails_with_anticheat_outside_the_bootstrapper)
{
	HostRecorder recorder;
	GenOnlinePluginHostApi host = MakeHost(&recorder, 0);
	// A host table without enabledCapabilities enables every capability, anticheat included.
	host.structSize = static_cast<uint32_t>(offsetof(GenOnlinePluginHostApi, enabledCapabilities));
	GenOnlinePluginApi api;
	REQUIRE(GetTable(host, &api));
	CHECK(api.IsExternalProcessRunning != nullptr && api.IsExternalProcessRunning() == GENONLINE_PLUGIN_FALSE);
}

TEST_CASE(dll_initialize_without_credentials_reports_missing_credentials)
{
#if GENONLINE_EOS_EXPECT_MISSING_CREDENTIALS
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	CHECK(api.Initialize() == GENONLINE_PLUGIN_RESULT_MISSING_CREDENTIALS);
	CHECK(!recorder.lines.empty());
	CHECK(AllPrefixed(recorder));
	bool namesTheSecret = false;
	for (const std::string& line : recorder.lines)
	{
		if (line.find("client secret") != std::string::npos)
		{
			namesTheSecret = true;
		}
	}
	CHECK(namesTheSecret);
	api.Shutdown();
#else
	std::printf("skipped: this build has credentials\n");
#endif
}

TEST_CASE(dll_shutdown_twice_does_nothing_the_second_time)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	api.Tick();
	api.Shutdown();
	const size_t linesAfterFirst = recorder.lines.size();
	api.Shutdown();
	api.Tick();
	CHECK(recorder.lines.size() == linesAfterFirst);
	CHECK(recorder.otherCallbacks == 0);
	CHECK(AllPrefixed(recorder));
}

TEST_CASE(dll_sign_in_without_a_platform_reports_failure_once)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	api.linkedAccount->SignIn("token", &RecordStatus, &g_statusCalls);
	CHECK(g_statusCalls == 1);
	CHECK(g_lastStatus == GENONLINE_PLUGIN_FALSE);
	api.linkedAccount->Refresh("token", &RecordStatus, &g_statusCalls);
	CHECK(g_statusCalls == 2);
	CHECK(g_lastStatus == GENONLINE_PLUGIN_FALSE);
	CHECK(api.linkedAccount->IsSignedIn() == GENONLINE_PLUGIN_FALSE);
	char buffer[64] = {};
	CHECK(api.linkedAccount->GetPlatformUserId(buffer, sizeof(buffer)) == GENONLINE_PLUGIN_FALSE);
	CHECK(api.linkedAccount->GetIdentityProof(buffer, sizeof(buffer)) == GENONLINE_PLUGIN_FALSE);
	CHECK(api.transport->IsAvailable() == GENONLINE_PLUGIN_FALSE);
	CHECK(api.transport->GetNextPacketSize(GENONLINE_PLUGIN_CHANNEL_GAME) == 0);
	api.Shutdown();
}

TEST_CASE(dll_linked_account_fails_without_credentials)
{
#if GENONLINE_EOS_EXPECT_MISSING_CREDENTIALS
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	CHECK(api.Initialize() == GENONLINE_PLUGIN_RESULT_MISSING_CREDENTIALS);
	api.linkedAccount->SignIn("SECRET_TOKEN_VALUE", &RecordStatus, &g_statusCalls);
	CHECK(g_statusCalls == 1);
	CHECK(g_lastStatus == GENONLINE_PLUGIN_FALSE);
	api.Tick();
	api.linkedAccount->Refresh("SECRET_TOKEN_VALUE", &RecordStatus, &g_statusCalls);
	CHECK(g_statusCalls == 2);
	CHECK(g_lastStatus == GENONLINE_PLUGIN_FALSE);
	CHECK(api.linkedAccount->IsSignedIn() == GENONLINE_PLUGIN_FALSE);
	char buffer[4096];
	std::memset(buffer, 'x', sizeof(buffer));
	CHECK(api.linkedAccount->GetPlatformUserId(buffer, sizeof(buffer)) == GENONLINE_PLUGIN_FALSE);
	CHECK(api.linkedAccount->GetIdentityProof(buffer, sizeof(buffer)) == GENONLINE_PLUGIN_FALSE);
	CHECK(buffer[0] == 'x');
	api.Shutdown();
	CHECK(g_statusCalls == 2);
	CHECK(!recorder.lines.empty());
	for (const std::string& line : recorder.lines)
	{
		CHECK(line.find("SECRET_TOKEN_VALUE") == std::string::npos);
	}
	CHECK(AllPrefixed(recorder));
#else
	std::printf("skipped: this build has credentials\n");
#endif
}

TEST_CASE(dll_sign_in_after_shutdown_reports_failure_once)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	api.Shutdown();
	api.linkedAccount->SignIn("token", &RecordStatus, &g_statusCalls);
	CHECK(g_statusCalls == 1);
	CHECK(g_lastStatus == GENONLINE_PLUGIN_FALSE);
	CHECK(api.linkedAccount->IsSignedIn() == GENONLINE_PLUGIN_FALSE);
}

TEST_CASE(dll_sign_in_without_a_status_callback_is_safe)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	api.linkedAccount->SignIn("token", nullptr, nullptr);
	api.linkedAccount->Refresh(nullptr, nullptr, nullptr);
	CHECK(api.linkedAccount->GetPlatformUserId(nullptr, 0) == GENONLINE_PLUGIN_FALSE);
	CHECK(api.linkedAccount->GetIdentityProof(nullptr, 4096) == GENONLINE_PLUGIN_FALSE);
	api.Shutdown();
}

TEST_CASE(dll_transport_is_unavailable_without_sign_in)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	const GenOnlinePluginTransportApi* transport = api.transport;
	CHECK(transport->IsAvailable() == GENONLINE_PLUGIN_FALSE);
	transport->SetEnabled(GENONLINE_PLUGIN_TRUE);
	CHECK(transport->IsAvailable() == GENONLINE_PLUGIN_FALSE);
	api.Initialize();
	api.Tick();
	CHECK(transport->IsAvailable() == GENONLINE_PLUGIN_FALSE);
	transport->StartSignalling("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001);
	CHECK(recorder.states.size() == 1);
	CHECK(recorder.states.size() == 1 && recorder.states[0] == GENONLINE_PLUGIN_CONNECTION_FAILED);
	CHECK(recorder.stateUsers.size() == 1 && recorder.stateUsers[0] == 1001);
	const uint8_t payload[4] = {1, 2, 3, 4};
	transport->SendPacket("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001, payload, sizeof(payload), GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	transport->SendPacket(nullptr, 0, nullptr, 0, GENONLINE_PLUGIN_CHANNEL_GAME, 99);
	api.Tick();
	CHECK(transport->GetNextPacketSize(GENONLINE_PLUGIN_CHANNEL_GAME) == 0);
	const uint8_t* data = payload;
	uint32_t length = 9;
	uint64_t sender = 9;
	CHECK(transport->ReceivePacketFrom(GENONLINE_PLUGIN_CHANNEL_GAME, &data, &length, &sender) == GENONLINE_PLUGIN_FALSE);
	CHECK(data == nullptr && length == 0 && sender == 0);
	CHECK(transport->ReceivePacket(GENONLINE_PLUGIN_CHANNEL_GAME, &data, &length) == GENONLINE_PLUGIN_FALSE);
	transport->FreePacket(nullptr);
	transport->FreePacket(payload);
	CHECK(transport->GetLatency("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001) == 0);
	CHECK(transport->IsRelayed(nullptr, 1001) == GENONLINE_PLUGIN_FALSE);
	transport->DisconnectPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001);
	transport->DisconnectAll();
	transport->SetEnabled(GENONLINE_PLUGIN_FALSE);
	api.Shutdown();
	CHECK(recorder.otherCallbacks == 0);
	CHECK(recorder.states.size() == 1);
	CHECK(AllPrefixed(recorder));
}

TEST_CASE(dll_register_peer_takes_effect)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	const GenOnlinePluginAnticheatApi* anticheat = api.anticheat;
	CHECK(anticheat->RegisterPeer(nullptr, 1001, GENONLINE_PLUGIN_CLIENT_PROTECTED) == GENONLINE_PLUGIN_FALSE);
	CHECK(anticheat->RegisterPeer("", 1001, GENONLINE_PLUGIN_CLIENT_PROTECTED) == GENONLINE_PLUGIN_FALSE);
	CHECK(anticheat->RegisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001, GENONLINE_PLUGIN_CLIENT_PROTECTED) == GENONLINE_PLUGIN_TRUE);
	CHECK(anticheat->DeregisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001) == GENONLINE_PLUGIN_TRUE);
	CHECK(anticheat->DeregisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001) == GENONLINE_PLUGIN_FALSE);
	CHECK(anticheat->RegisterPeer("0002bbbbbbbbbbbbbbbbbbbbbbbbbbbb", 2002, GENONLINE_PLUGIN_CLIENT_UNPROTECTED) == GENONLINE_PLUGIN_TRUE);
	CHECK(anticheat->DeregisterPeer(nullptr, 2002) == GENONLINE_PLUGIN_TRUE);
	anticheat->BeginSession();
	CHECK(anticheat->RegisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001, GENONLINE_PLUGIN_CLIENT_PROTECTED) == GENONLINE_PLUGIN_TRUE);
	anticheat->EndSession();
	CHECK(anticheat->DeregisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001) == GENONLINE_PLUGIN_FALSE);
	anticheat->EndSession();
	api.Shutdown();
}

TEST_CASE(dll_anticheat_without_the_eac_client_runs_only_the_registry)
{
	HostRecorder recorder;
	GenOnlinePluginApi api;
	REQUIRE(GetTable(MakeHost(&recorder, kAllCapabilities), &api));
	api.Initialize();
	api.Tick();
	CHECK(api.IsExternalProcessRunning() == GENONLINE_PLUGIN_FALSE);
	const GenOnlinePluginAnticheatApi* anticheat = api.anticheat;
	anticheat->BeginSession();
	anticheat->BeginSession();
	CHECK(anticheat->RegisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001, GENONLINE_PLUGIN_CLIENT_PROTECTED) == GENONLINE_PLUGIN_TRUE);
	CHECK(anticheat->RegisterPeer("0002bbbbbbbbbbbbbbbbbbbbbbbbbbbb", 2002, 7) == GENONLINE_PLUGIN_FALSE);
	const uint8_t message[3] = {1, 2, 3};
	anticheat->MessageArrived(1001, message, sizeof(message));
	anticheat->MessageArrived(9999, message, sizeof(message));
	anticheat->MessageArrived(1001, nullptr, 0);
	api.Tick();
	anticheat->EndSession();
	CHECK(anticheat->DeregisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1001) == GENONLINE_PLUGIN_FALSE);
	api.Shutdown();
	CHECK(recorder.otherCallbacks == 0);
	CHECK(AllPrefixed(recorder));
}

int main(int argc, char** argv)
{
	if (argc < 3 && !(argc == 2 && std::strcmp(argv[1], "--list") == 0))
	{
		std::fprintf(stderr, "usage: %s <case> <path to genonline_eos.dll>\n", argv[0]);
		return 2;
	}
	if (argc >= 3)
	{
		g_dllPath = argv[2];
	}
	return testing::RunMain(argc, argv);
}
