// SPDX-License-Identifier: MIT
// Unit tests of the pure plugin logic. They run without EOS and without credentials.
#include "test_harness.h"

#include "core/api_table.h"
#include "core/credentials.h"
#include "core/host_link.h"
#include "core/log_queue.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace genonline_eos;

namespace
{

int32_t GENONLINE_PLUGIN_CALL FakeInitialize()
{
	return GENONLINE_PLUGIN_RESULT_OK;
}

void GENONLINE_PLUGIN_CALL FakeTick()
{
}

GenOnlinePluginApi MakeCompleteTable()
{
	static const GenOnlinePluginLinkedAccountApi linked = {};
	static const GenOnlinePluginAnticheatApi anticheat = {};
	static const GenOnlinePluginTransportApi transport = {};
	GenOnlinePluginApi table = {};
	table.structSize = sizeof(GenOnlinePluginApi);
	table.apiVersion = GENONLINE_PLUGIN_API_VERSION;
	table.name = "genonline_eos";
	table.version = "2026.11.0";
	table.capabilities = GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT | GENONLINE_PLUGIN_CAP_TRANSPORT;
	table.Initialize = &FakeInitialize;
	table.Tick = &FakeTick;
	table.Shutdown = &FakeTick;
	table.linkedAccount = &linked;
	table.anticheat = &anticheat;
	table.transport = &transport;
	return table;
}

// A destination buffer larger than the table, filled with a marker byte so untouched bytes are visible.
struct Destination
{
	unsigned char bytes[sizeof(GenOnlinePluginApi) + 32];

	explicit Destination(uint32_t structSize)
	{
		std::memset(bytes, 0xCD, sizeof(bytes));
		std::memcpy(bytes, &structSize, sizeof(structSize));
	}

	GenOnlinePluginApi* Table()
	{
		return reinterpret_cast<GenOnlinePluginApi*>(bytes);
	}

	bool UntouchedFrom(size_t offset) const
	{
		for (size_t i = offset; i < sizeof(bytes); ++i)
		{
			if (bytes[i] != 0xCD)
			{
				return false;
			}
		}
		return true;
	}
};

struct LogCapture
{
	std::vector<int32_t> levels;
	std::vector<std::string> lines;
};

void GENONLINE_PLUGIN_CALL CaptureLog(void* userData, int32_t level, const char* message)
{
	LogCapture* capture = static_cast<LogCapture*>(userData);
	capture->levels.push_back(level);
	capture->lines.push_back(message);
}

void GENONLINE_PLUGIN_CALL IgnoreChat(void*, const char*)
{
}

void GENONLINE_PLUGIN_CALL IgnoreIntegrity(void*, const char*, int32_t)
{
}

void GENONLINE_PLUGIN_CALL IgnoreSend(void*, uint64_t, const void*, uint32_t)
{
}

GenOnlinePluginHostApi MakeHost(LogCapture* capture)
{
	GenOnlinePluginHostApi host = {};
	host.structSize = sizeof(GenOnlinePluginHostApi);
	host.userData = capture;
	host.Log = &CaptureLog;
	host.LobbyChatOutput = &IgnoreChat;
	host.IntegrityViolation = &IgnoreIntegrity;
	host.SendMessageViaTransport = &IgnoreSend;
	host.enabledCapabilities = GENONLINE_PLUGIN_CAP_TRANSPORT;
	return host;
}

constexpr uint32_t kAllCapabilities = GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT | GENONLINE_PLUGIN_CAP_ANTICHEAT | GENONLINE_PLUGIN_CAP_TRANSPORT;

} // namespace

TEST_CASE(getapi_rejects_other_api_versions)
{
	GenOnlinePluginHostApi host = {};
	host.structSize = sizeof(host);
	GenOnlinePluginApi out = {};
	out.structSize = sizeof(out);
	CHECK(CheckGetApiArguments(0, &host, &out) == GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH);
	CHECK(CheckGetApiArguments(2, &host, &out) == GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH);
	CHECK(CheckGetApiArguments(2, nullptr, nullptr) == GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH);
	CHECK(CheckGetApiArguments(1, &host, &out) == GENONLINE_PLUGIN_RESULT_OK);
}

TEST_CASE(getapi_rejects_null_arguments)
{
	GenOnlinePluginHostApi host = {};
	host.structSize = sizeof(host);
	GenOnlinePluginApi out = {};
	out.structSize = sizeof(out);
	CHECK(CheckGetApiArguments(1, nullptr, &out) == GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT);
	CHECK(CheckGetApiArguments(1, &host, nullptr) == GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT);
}

TEST_CASE(fill_copies_every_field_for_a_full_size)
{
	const GenOnlinePluginApi complete = MakeCompleteTable();
	Destination destination(sizeof(GenOnlinePluginApi));
	CHECK(FillPluginApi(complete, destination.Table()) == GENONLINE_PLUGIN_RESULT_OK);
	const GenOnlinePluginApi& out = *destination.Table();
	CHECK(out.structSize == sizeof(GenOnlinePluginApi));
	CHECK(out.apiVersion == GENONLINE_PLUGIN_API_VERSION);
	CHECK(out.name == complete.name);
	CHECK(out.version == complete.version);
	CHECK(out.capabilities == complete.capabilities);
	CHECK(out.Initialize == complete.Initialize);
	CHECK(out.transport == complete.transport);
	CHECK(destination.UntouchedFrom(sizeof(GenOnlinePluginApi)));
}

TEST_CASE(fill_reports_its_own_size_to_a_larger_host)
{
	const GenOnlinePluginApi complete = MakeCompleteTable();
	Destination destination(sizeof(GenOnlinePluginApi) + 16);
	CHECK(FillPluginApi(complete, destination.Table()) == GENONLINE_PLUGIN_RESULT_OK);
	CHECK(destination.Table()->structSize == sizeof(GenOnlinePluginApi));
	CHECK(destination.Table()->transport == complete.transport);
	CHECK(destination.UntouchedFrom(sizeof(GenOnlinePluginApi)));
}

TEST_CASE(fill_stops_at_a_shorter_host_size)
{
	const GenOnlinePluginApi complete = MakeCompleteTable();
	const uint32_t size = static_cast<uint32_t>(offsetof(GenOnlinePluginApi, capabilities) + sizeof(uint32_t));
	Destination destination(size);
	CHECK(FillPluginApi(complete, destination.Table()) == GENONLINE_PLUGIN_RESULT_OK);
	CHECK(destination.Table()->structSize == size);
	CHECK(destination.Table()->name == complete.name);
	CHECK(destination.Table()->capabilities == complete.capabilities);
	CHECK(destination.UntouchedFrom(offsetof(GenOnlinePluginApi, Initialize)));
}

TEST_CASE(fill_skips_a_field_cut_by_the_host_size)
{
	const GenOnlinePluginApi complete = MakeCompleteTable();
	const uint32_t size = static_cast<uint32_t>(offsetof(GenOnlinePluginApi, name) + 2);
	Destination destination(size);
	CHECK(FillPluginApi(complete, destination.Table()) == GENONLINE_PLUGIN_RESULT_OK);
	CHECK(destination.Table()->structSize == offsetof(GenOnlinePluginApi, name));
	CHECK(destination.Table()->apiVersion == GENONLINE_PLUGIN_API_VERSION);
	CHECK(destination.UntouchedFrom(offsetof(GenOnlinePluginApi, name)));
}

TEST_CASE(fill_rejects_a_size_without_room_for_structsize)
{
	const GenOnlinePluginApi complete = MakeCompleteTable();
	Destination destination(2);
	CHECK(FillPluginApi(complete, destination.Table()) == GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT);
	CHECK(destination.Table()->structSize == 2);
}

TEST_CASE(host_bind_copies_a_full_table)
{
	LogCapture capture;
	const GenOnlinePluginHostApi host = MakeHost(&capture);
	HostLink link;
	CHECK(!link.IsBound());
	link.Bind(host, kAllCapabilities);
	CHECK(link.IsBound());
	CHECK(link.EnabledCapabilities() == GENONLINE_PLUGIN_CAP_TRANSPORT);
	CHECK(link.IsEnabled(GENONLINE_PLUGIN_CAP_TRANSPORT));
	CHECK(!link.IsEnabled(GENONLINE_PLUGIN_CAP_ANTICHEAT));
	CHECK(link.Table().SendMessageViaTransport == host.SendMessageViaTransport);
	CHECK(link.Table().userData == &capture);
}

TEST_CASE(host_bind_without_enabled_capabilities_enables_every_capability)
{
	LogCapture capture;
	GenOnlinePluginHostApi host = MakeHost(&capture);
	host.structSize = static_cast<uint32_t>(offsetof(GenOnlinePluginHostApi, enabledCapabilities));
	HostLink link;
	link.Bind(host, kAllCapabilities);
	CHECK(link.EnabledCapabilities() == kAllCapabilities);
	CHECK(link.Table().SendMessageViaTransport == host.SendMessageViaTransport);
}

TEST_CASE(host_bind_treats_fields_past_structsize_as_absent)
{
	LogCapture capture;
	GenOnlinePluginHostApi host = MakeHost(&capture);
	host.structSize = static_cast<uint32_t>(offsetof(GenOnlinePluginHostApi, IntegrityViolation));
	HostLink link;
	link.Bind(host, kAllCapabilities);
	CHECK(link.Table().Log == host.Log);
	CHECK(link.Table().IntegrityViolation == nullptr);
	CHECK(link.Table().SendMessageViaTransport == nullptr);
	CHECK(link.EnabledCapabilities() == kAllCapabilities);
}

TEST_CASE(host_log_prefixes_every_line)
{
	LogCapture capture;
	HostLink link;
	link.Bind(MakeHost(&capture), kAllCapabilities);
	link.Log(GENONLINE_PLUGIN_LOG_WARNING, "hello");
	link.Logf(GENONLINE_PLUGIN_LOG_INFO, "value %d of %s", 7, "seven");
	CHECK(capture.lines.size() == 2);
	if (capture.lines.size() == 2)
	{
		CHECK(capture.lines[0] == "[genonline_eos] hello");
		CHECK(capture.levels[0] == GENONLINE_PLUGIN_LOG_WARNING);
		CHECK(capture.lines[1] == "[genonline_eos] value 7 of seven");
		CHECK(capture.levels[1] == GENONLINE_PLUGIN_LOG_INFO);
	}
}

TEST_CASE(host_log_formats_long_lines_completely)
{
	LogCapture capture;
	HostLink link;
	link.Bind(MakeHost(&capture), kAllCapabilities);
	const std::string longText(3000, 'x');
	link.Logf(GENONLINE_PLUGIN_LOG_INFO, "%s", longText.c_str());
	CHECK(capture.lines.size() == 1);
	if (capture.lines.size() == 1)
	{
		CHECK(capture.lines[0] == std::string("[genonline_eos] ") + longText);
	}
}

TEST_CASE(host_log_without_a_log_callback_does_nothing)
{
	GenOnlinePluginHostApi host = {};
	host.structSize = sizeof(host);
	HostLink link;
	link.Bind(host, kAllCapabilities);
	link.Log(GENONLINE_PLUGIN_LOG_INFO, "dropped");
	HostLink unbound;
	unbound.Log(GENONLINE_PLUGIN_LOG_INFO, "dropped");
}

TEST_CASE(host_release_stops_every_callback)
{
	LogCapture capture;
	HostLink link;
	link.Bind(MakeHost(&capture), kAllCapabilities);
	link.Release();
	CHECK(!link.IsBound());
	link.Log(GENONLINE_PLUGIN_LOG_INFO, "after release");
	CHECK(capture.lines.empty());
	CHECK(link.Table().Log == nullptr);
	CHECK(link.EnabledCapabilities() == 0);
}

TEST_CASE(credentials_complete_set_has_nothing_missing)
{
	const Credentials credentials = {"p", "s", "d", "c", "secret"};
	CHECK(MissingCredentials(credentials) == 0);
	CHECK(OverlongCredentials(credentials) == 0);
}

TEST_CASE(credentials_empty_or_null_fields_are_missing)
{
	const Credentials empty = {"", "", "", "", ""};
	const uint32_t all = kCredentialProductId | kCredentialSandboxId | kCredentialDeploymentId | kCredentialClientId | kCredentialClientSecret;
	CHECK(MissingCredentials(empty) == all);
	const Credentials nulls = {nullptr, nullptr, nullptr, nullptr, nullptr};
	CHECK(MissingCredentials(nulls) == all);
	const Credentials noSecret = {"p", "s", "d", "c", ""};
	CHECK(MissingCredentials(noSecret) == kCredentialClientSecret);
	const Credentials noSandbox = {"p", nullptr, "d", "c", "secret"};
	CHECK(MissingCredentials(noSandbox) == kCredentialSandboxId);
}

TEST_CASE(credentials_description_names_fields_only)
{
	CHECK(DescribeCredentialFields(0) == "");
	CHECK(DescribeCredentialFields(kCredentialProductId) == "product id");
	CHECK(DescribeCredentialFields(kCredentialProductId | kCredentialClientSecret) == "product id, client secret");
	CHECK(DescribeCredentialFields(kCredentialSandboxId | kCredentialDeploymentId | kCredentialClientId) == "sandbox id, deployment id, client id");
}

TEST_CASE(credentials_longer_than_eos_allows_are_flagged)
{
	const std::string longest(kCredentialMaxLength, 'a');
	const std::string tooLong(kCredentialMaxLength + 1, 'a');
	const Credentials fits = {longest.c_str(), longest.c_str(), longest.c_str(), longest.c_str(), longest.c_str()};
	CHECK(OverlongCredentials(fits) == 0);
	const Credentials overlong = {"p", "s", tooLong.c_str(), "c", tooLong.c_str()};
	CHECK(OverlongCredentials(overlong) == (kCredentialDeploymentId | kCredentialClientSecret));
}

TEST_CASE(eos_log_levels_map_to_host_levels)
{
	// EOS_ELogLevel values: Fatal 100, Error 200, Warning 300, Info 400, Verbose 500, VeryVerbose 600.
	CHECK(HostLevelFromEosLevel(100) == GENONLINE_PLUGIN_LOG_ERROR);
	CHECK(HostLevelFromEosLevel(200) == GENONLINE_PLUGIN_LOG_ERROR);
	CHECK(HostLevelFromEosLevel(300) == GENONLINE_PLUGIN_LOG_WARNING);
	CHECK(HostLevelFromEosLevel(400) == GENONLINE_PLUGIN_LOG_INFO);
	CHECK(HostLevelFromEosLevel(500) == GENONLINE_PLUGIN_LOG_DEBUG);
	CHECK(HostLevelFromEosLevel(600) == GENONLINE_PLUGIN_LOG_DEBUG);
}

TEST_CASE(log_queue_keeps_arrival_order_across_threads)
{
	LogQueue queue;
	queue.Push(GENONLINE_PLUGIN_LOG_INFO, "first");
	std::thread worker([&queue]() { queue.Push(GENONLINE_PLUGIN_LOG_ERROR, "second"); });
	worker.join();
	size_t dropped = 99;
	const std::vector<QueuedLogLine> lines = queue.Drain(&dropped);
	CHECK(dropped == 0);
	CHECK(lines.size() == 2);
	if (lines.size() == 2)
	{
		CHECK(lines[0].text == "first");
		CHECK(lines[1].text == "second");
		CHECK(lines[1].level == GENONLINE_PLUGIN_LOG_ERROR);
	}
	CHECK(queue.Drain(&dropped).empty());
}

TEST_CASE(log_queue_counts_lines_dropped_when_full)
{
	LogQueue queue(2);
	queue.Push(GENONLINE_PLUGIN_LOG_INFO, "a");
	queue.Push(GENONLINE_PLUGIN_LOG_INFO, "b");
	queue.Push(GENONLINE_PLUGIN_LOG_INFO, "c");
	size_t dropped = 0;
	const std::vector<QueuedLogLine> lines = queue.Drain(&dropped);
	CHECK(lines.size() == 2);
	CHECK(dropped == 1);
	queue.Drain(&dropped);
	CHECK(dropped == 0);
}

int main(int argc, char** argv)
{
	return testing::RunMain(argc, argv);
}
