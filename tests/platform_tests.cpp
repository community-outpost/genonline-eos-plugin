// SPDX-License-Identifier: MIT
// Unit tests of the platform helper and of the pointer sized anticheat peer handles.
#include "test_harness.h"

#include "core/anticheat_core.h"
#include "platform/platform.h"

#include <cstdint>
#include <string>

using namespace genonline_eos;

static_assert(sizeof(kSelfPeerHandle) == sizeof(void*), "anticheat peer handles are pointer sized");
static_assert(kSelfPeerHandle == static_cast<uintptr_t>(-1), "the local peer handle is the pointer value -1");

TEST_CASE(this_module_path_names_the_running_program)
{
	const std::string path = platform::ThisModulePath();
	CHECK(!path.empty());
	CHECK(path.find("genonline_eos_platform_tests") != std::string::npos);
}

TEST_CASE(this_module_folder_is_usable)
{
	CHECK(platform::ThisModuleFolderIsUsable());
}

TEST_CASE(opening_a_missing_library_fails_with_a_reason)
{
	std::string error;
	void* library = platform::OpenLibrary(platform::ThisModulePath() + ".missing", &error);
	CHECK(library == nullptr);
	CHECK(!error.empty());
}

TEST_CASE(a_missing_symbol_is_null)
{
	CHECK(platform::FindSymbol(nullptr, "GenOnlinePlugin_GetApi") == nullptr);
}

TEST_CASE(peer_handles_cover_the_pointer_range)
{
	struct CountingEac : EacBackend
	{
		uintptr_t lastHandle = 0;

		bool BeginSession() override
		{
			return true;
		}

		void EndSession() override
		{
		}

		bool RegisterPeer(uintptr_t handle, const std::string&, bool, uint32_t) override
		{
			lastHandle = handle;
			return true;
		}

		bool UnregisterPeer(uintptr_t) override
		{
			return true;
		}

		bool ReceiveFromPeer(uintptr_t, const uint8_t*, uint32_t) override
		{
			return true;
		}
	};
	PeerRegistry registry;
	CountingEac eac;
	AnticheatCore core(registry, eac, AnticheatHooks{});
	core.SetEnabled(true);
	core.SetLocalProductUserId("0002cccccccccccccccccccccccccccc");
	core.BeginSession();
	CHECK(core.RegisterPeer("0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(eac.lastHandle != 0);
	CHECK(eac.lastHandle != kSelfPeerHandle);
}

int main(int argc, char** argv)
{
	return testing::RunMain(argc, argv);
}
