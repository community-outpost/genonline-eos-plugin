// SPDX-License-Identifier: MIT
// Unit tests of the anticheat logic, driven by a fake Easy Anti-Cheat backend.
#include "test_harness.h"

#include "core/anticheat_core.h"
#include "core/peer_registry.h"

#include <GenOnlinePluginApi.h>

#include <string>
#include <vector>

using namespace genonline_eos;

namespace
{

const std::string kPeer = "0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kOther = "0002bbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const std::string kLocal = "0002cccccccccccccccccccccccccccc";
constexpr uint64_t kPeerId = 1001;
constexpr uint64_t kOtherId = 2002;
constexpr uint64_t kLocalId = 3003;

struct RegisteredPeer
{
	uint32_t handle;
	std::string productUserId;
	bool protectedClient;
	uint32_t timeout;
};

struct FakeEac : EacBackend
{
	bool acceptSession = true;
	bool acceptRegister = true;
	int beginCalls = 0;
	int endCalls = 0;
	std::vector<RegisteredPeer> registered;
	std::vector<uint32_t> unregistered;
	std::vector<uint32_t> receivedFrom;
	std::vector<std::vector<uint8_t>> received;

	bool BeginSession() override
	{
		++beginCalls;
		return acceptSession;
	}

	void EndSession() override
	{
		++endCalls;
	}

	bool RegisterPeer(uint32_t handle, const std::string& productUserId, bool protectedClient, uint32_t timeout) override
	{
		registered.push_back(RegisteredPeer{handle, productUserId, protectedClient, timeout});
		return acceptRegister;
	}

	bool UnregisterPeer(uint32_t handle) override
	{
		unregistered.push_back(handle);
		return true;
	}

	bool ReceiveFromPeer(uint32_t handle, const uint8_t* data, uint32_t length) override
	{
		receivedFrom.push_back(handle);
		received.push_back(std::vector<uint8_t>(data, data + length));
		return true;
	}

	uint32_t HandleOf(const std::string& productUserId) const
	{
		for (auto it = registered.rbegin(); it != registered.rend(); ++it)
		{
			if (it->productUserId == productUserId)
			{
				return it->handle;
			}
		}
		return 0;
	}
};

struct Sent
{
	uint64_t genOnlineUserId;
	std::vector<uint8_t> data;
};

struct Action
{
	uint64_t genOnlineUserId;
	std::string reason;
	int32_t action;
	int32_t actionReason;
};

struct Violation
{
	std::string reason;
	int32_t type;
};

// An anticheat with a fake EAC and recorded host callbacks.
struct Rig
{
	PeerRegistry registry;
	FakeEac eac;
	std::vector<Sent> sent;
	std::vector<Action> actions;
	std::vector<Violation> violations;
	std::vector<std::string> logs;
	AnticheatCore core;

	explicit Rig(bool enabled = true, bool signedIn = true)
		: core(registry, eac,
			  AnticheatHooks{
				  [this](uint64_t id, const uint8_t* data, uint32_t length)
				  {
					  sent.push_back(Sent{id, std::vector<uint8_t>(data, data + length)});
				  },
				  [this](uint64_t id, const std::string& reason, int32_t action, int32_t actionReason)
				  {
					  actions.push_back(Action{id, reason, action, actionReason});
				  },
				  [this](const std::string& reason, int32_t type)
				  {
					  violations.push_back(Violation{reason, type});
				  },
				  [this](int32_t, const std::string& text)
				  {
					  logs.push_back(text);
				  }})
	{
		core.SetEnabled(enabled);
		if (signedIn)
		{
			core.SetLocalProductUserId(kLocal);
		}
	}
};

// A rig whose local user is not signed in to EOS Connect yet.
struct SignedOutRig : Rig
{
	SignedOutRig()
		: Rig(true, false)
	{
	}
};

const uint8_t kMessage[4] = {1, 2, 3, 4};

} // namespace

TEST_CASE(actions_map_to_host_values)
{
	// EOS_EAntiCheatCommonClientAction: Invalid 0, RemovePlayer 1.
	CHECK(MapAnticheatAction(0) == GENONLINE_PLUGIN_ACTION_NONE);
	CHECK(MapAnticheatAction(1) == GENONLINE_PLUGIN_ACTION_KICK);
	CHECK(MapAnticheatAction(7) == GENONLINE_PLUGIN_ACTION_NONE);
}

TEST_CASE(action_reasons_map_to_host_values)
{
	// EOS_EAntiCheatCommonClientActionReason values 0 to 10 in declaration order.
	CHECK(MapAnticheatActionReason(0) == GENONLINE_PLUGIN_ACTION_REASON_UNKNOWN);
	CHECK(MapAnticheatActionReason(1) == GENONLINE_PLUGIN_ACTION_REASON_INTERNAL_ERROR);
	CHECK(MapAnticheatActionReason(2) == GENONLINE_PLUGIN_ACTION_REASON_INVALID_MESSAGE);
	CHECK(MapAnticheatActionReason(3) == GENONLINE_PLUGIN_ACTION_REASON_AUTH_FAILURE);
	CHECK(MapAnticheatActionReason(4) == GENONLINE_PLUGIN_ACTION_REASON_NOT_RUNNING);
	CHECK(MapAnticheatActionReason(5) == GENONLINE_PLUGIN_ACTION_REASON_HEARTBEAT_TIMED_OUT);
	CHECK(MapAnticheatActionReason(6) == GENONLINE_PLUGIN_ACTION_REASON_CLIENT_VIOLATION);
	CHECK(MapAnticheatActionReason(7) == GENONLINE_PLUGIN_ACTION_REASON_BACKEND_VIOLATION);
	CHECK(MapAnticheatActionReason(8) == GENONLINE_PLUGIN_ACTION_REASON_TEMP_COOLDOWN);
	CHECK(MapAnticheatActionReason(9) == GENONLINE_PLUGIN_ACTION_REASON_TEMP_BANNED);
	CHECK(MapAnticheatActionReason(10) == GENONLINE_PLUGIN_ACTION_REASON_PERMA_BANNED);
	CHECK(MapAnticheatActionReason(11) == GENONLINE_PLUGIN_ACTION_REASON_UNKNOWN);
	CHECK(MapAnticheatActionReason(-1) == GENONLINE_PLUGIN_ACTION_REASON_UNKNOWN);
}

TEST_CASE(begin_session_starts_eac_once)
{
	Rig rig;
	rig.core.BeginSession();
	CHECK(rig.eac.beginCalls == 1);
	CHECK(rig.core.IsSessionActive());
	CHECK(rig.core.IsEacSessionRunning());
	rig.core.BeginSession();
	CHECK(rig.eac.beginCalls == 1);
	CHECK(rig.eac.endCalls == 0);
}

TEST_CASE(end_session_ends_eac_and_forgets_the_peers)
{
	Rig rig;
	rig.core.BeginSession();
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	rig.core.EndSession();
	CHECK(rig.eac.endCalls == 1);
	CHECK(!rig.core.IsSessionActive());
	CHECK(!rig.core.IsEacSessionRunning());
	CHECK(!rig.registry.IsRegistered(kPeer));
	CHECK(!rig.core.DeregisterPeer(kPeer));
	rig.core.EndSession();
	CHECK(rig.eac.endCalls == 1);
}

TEST_CASE(end_session_without_a_session_does_nothing)
{
	Rig rig;
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	rig.core.EndSession();
	CHECK(rig.eac.endCalls == 0);
	CHECK(rig.registry.IsRegistered(kPeer));
}

TEST_CASE(register_peer_passes_client_type_and_timeout)
{
	Rig rig;
	rig.core.BeginSession();
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(rig.core.RegisterPeer(kOther, kOtherId, GENONLINE_PLUGIN_CLIENT_UNPROTECTED));
	REQUIRE(rig.eac.registered.size() == 2);
	CHECK(rig.eac.registered[0].productUserId == kPeer);
	CHECK(rig.eac.registered[0].protectedClient);
	CHECK(rig.eac.registered[0].timeout == 120);
	CHECK(!rig.eac.registered[1].protectedClient);
	CHECK(rig.eac.registered[0].handle != rig.eac.registered[1].handle);
	CHECK(rig.eac.registered[0].handle != 0 && rig.eac.registered[0].handle != kSelfPeerHandle);
	CHECK(rig.registry.IsRegistered(kPeer));
	CHECK(rig.registry.GenOnlineUserId(kOther) == kOtherId);
}

TEST_CASE(register_peer_refuses_bad_input_and_eac_refusals)
{
	Rig rig;
	rig.core.BeginSession();
	CHECK(!rig.core.RegisterPeer("", kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(!rig.core.RegisterPeer(kPeer, kPeerId, 5));
	rig.eac.acceptRegister = false;
	CHECK(!rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(!rig.registry.IsRegistered(kPeer));
}

TEST_CASE(peers_registered_before_the_session_join_it_when_it_begins)
{
	Rig rig;
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_UNPROTECTED));
	CHECK(rig.eac.registered.empty());
	rig.core.BeginSession();
	REQUIRE(rig.eac.registered.size() == 1);
	CHECK(rig.eac.registered[0].productUserId == kPeer);
	CHECK(!rig.eac.registered[0].protectedClient);
}

TEST_CASE(deregister_peer_removes_peer_and_mapping)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	const uint32_t handle = rig.eac.HandleOf(kPeer);
	CHECK(rig.core.DeregisterPeer(kPeer));
	CHECK((rig.eac.unregistered == std::vector<uint32_t>{handle}));
	CHECK(!rig.registry.IsRegistered(kPeer));
	CHECK(!rig.core.DeregisterPeer(kPeer));
	rig.core.MessageArrived(kPeerId, kMessage, 4);
	CHECK(rig.eac.receivedFrom.empty());
}

TEST_CASE(eac_messages_for_a_peer_go_to_the_host_transport)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.OnMessageToPeer(rig.eac.HandleOf(kPeer), kMessage, 4);
	REQUIRE(rig.sent.size() == 1);
	CHECK(rig.sent[0].genOnlineUserId == kPeerId);
	CHECK((rig.sent[0].data == std::vector<uint8_t>{1, 2, 3, 4}));
	rig.core.OnMessageToPeer(999, kMessage, 4);
	CHECK(rig.sent.size() == 1);
}

TEST_CASE(arrived_messages_go_to_eac_for_their_peer)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.MessageArrived(kPeerId, kMessage, 4);
	REQUIRE(rig.eac.receivedFrom.size() == 1);
	CHECK(rig.eac.receivedFrom[0] == rig.eac.HandleOf(kPeer));
	CHECK((rig.eac.received[0] == std::vector<uint8_t>{1, 2, 3, 4}));
}

TEST_CASE(arrived_messages_of_unknown_peers_are_dropped_with_a_log_line)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.MessageArrived(kOtherId, kMessage, 4);
	CHECK(rig.eac.receivedFrom.empty());
	CHECK(!rig.logs.empty());
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.MessageArrived(kPeerId, nullptr, 4);
	rig.core.MessageArrived(kPeerId, kMessage, 0);
	CHECK(rig.eac.receivedFrom.empty());
}

TEST_CASE(peer_actions_go_to_the_host_with_mapped_values)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.OnActionRequired(rig.eac.HandleOf(kPeer), 1, 5, "heartbeat lost");
	REQUIRE(rig.actions.size() == 1);
	CHECK(rig.actions[0].genOnlineUserId == kPeerId);
	CHECK(rig.actions[0].reason == "heartbeat lost");
	CHECK(rig.actions[0].action == GENONLINE_PLUGIN_ACTION_KICK);
	CHECK(rig.actions[0].actionReason == GENONLINE_PLUGIN_ACTION_REASON_HEARTBEAT_TIMED_OUT);
	rig.core.OnActionRequired(rig.eac.HandleOf(kPeer), 1, 9, nullptr);
	REQUIRE(rig.actions.size() == 2);
	CHECK(rig.actions[1].reason.empty());
	rig.core.OnActionRequired(4242, 1, 5, "unknown peer");
	CHECK(rig.actions.size() == 2);
}

TEST_CASE(local_actions_use_the_local_id_when_known)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.OnActionRequired(kSelfPeerHandle, 1, 6, "local");
	REQUIRE(rig.actions.size() == 1);
	CHECK(rig.actions[0].genOnlineUserId == 0);
	CHECK(rig.actions[0].actionReason == GENONLINE_PLUGIN_ACTION_REASON_CLIENT_VIOLATION);
	rig.core.SetLocalProductUserId(kLocal);
	rig.core.RegisterPeer(kLocal, kLocalId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.OnActionRequired(kSelfPeerHandle, 1, 6, "local");
	REQUIRE(rig.actions.size() == 2);
	CHECK(rig.actions[1].genOnlineUserId == kLocalId);
}

TEST_CASE(integrity_violations_go_to_the_host)
{
	Rig rig;
	rig.core.OnIntegrityViolated(9, "forbidden module");
	REQUIRE(rig.violations.size() == 1);
	CHECK(rig.violations[0].reason == "forbidden module");
	CHECK(rig.violations[0].type == 9);
	rig.core.OnIntegrityViolated(5, nullptr);
	REQUIRE(rig.violations.size() == 2);
	CHECK(rig.violations[1].reason.empty());
}

TEST_CASE(without_anticheat_only_the_registry_runs)
{
	Rig rig(false);
	rig.core.BeginSession();
	CHECK(rig.core.IsSessionActive());
	CHECK(!rig.core.IsEacSessionRunning());
	CHECK(rig.eac.beginCalls == 0);
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(rig.registry.IsRegistered(kPeer));
	CHECK(rig.eac.registered.empty());
	rig.core.MessageArrived(kPeerId, kMessage, 4);
	CHECK(rig.eac.receivedFrom.empty());
	rig.core.OnMessageToPeer(1, kMessage, 4);
	rig.core.OnActionRequired(kSelfPeerHandle, 1, 6, "x");
	rig.core.OnIntegrityViolated(9, "x");
	CHECK(rig.sent.empty());
	CHECK(rig.actions.empty());
	CHECK(rig.violations.empty());
	CHECK(rig.core.DeregisterPeer(kPeer));
	CHECK(rig.eac.unregistered.empty());
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.EndSession();
	CHECK(rig.eac.endCalls == 0);
	CHECK(!rig.registry.IsRegistered(kPeer));
}

TEST_CASE(a_failed_eac_session_keeps_the_registry_working)
{
	Rig rig;
	rig.eac.acceptSession = false;
	rig.core.BeginSession();
	CHECK(rig.core.IsSessionActive());
	CHECK(!rig.core.IsEacSessionRunning());
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(rig.eac.registered.empty());
	rig.core.EndSession();
	CHECK(rig.eac.endCalls == 0);
}

TEST_CASE(session_begun_before_sign_in_starts_when_the_sign_in_completes)
{
	SignedOutRig rig;
	rig.core.BeginSession();
	CHECK(rig.eac.beginCalls == 0);
	CHECK(rig.core.IsSessionActive());
	CHECK(!rig.core.IsEacSessionRunning());
	CHECK(rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_UNPROTECTED));
	CHECK(rig.core.RegisterPeer(kOther, kOtherId, GENONLINE_PLUGIN_CLIENT_PROTECTED));
	CHECK(rig.eac.registered.empty());
	rig.core.SetLocalProductUserId("");
	CHECK(rig.eac.beginCalls == 0);
	rig.core.SetLocalProductUserId(kLocal);
	CHECK(rig.eac.beginCalls == 1);
	CHECK(rig.core.IsEacSessionRunning());
	REQUIRE(rig.eac.registered.size() == 2);
	CHECK(rig.eac.HandleOf(kPeer) != 0);
	CHECK(rig.eac.HandleOf(kOther) != 0);
	rig.core.SetLocalProductUserId(kLocal);
	CHECK(rig.eac.beginCalls == 1);
	CHECK(rig.eac.registered.size() == 2);
	rig.core.MessageArrived(kPeerId, kMessage, 4);
	CHECK(rig.eac.receivedFrom.size() == 1);
}

TEST_CASE(session_ended_before_sign_in_never_starts)
{
	SignedOutRig rig;
	rig.core.BeginSession();
	rig.core.EndSession();
	rig.core.SetLocalProductUserId(kLocal);
	CHECK(rig.eac.beginCalls == 0);
	CHECK(!rig.core.IsSessionActive());
}

TEST_CASE(sign_in_without_a_session_starts_nothing)
{
	SignedOutRig rig;
	rig.core.SetLocalProductUserId(kLocal);
	CHECK(rig.eac.beginCalls == 0);
	SignedOutRig disabled;
	disabled.core.SetEnabled(false);
	disabled.core.BeginSession();
	disabled.core.SetLocalProductUserId(kLocal);
	CHECK(disabled.eac.beginCalls == 0);
}

TEST_CASE(reset_ends_the_session_and_forgets_every_peer)
{
	Rig rig;
	rig.core.BeginSession();
	rig.core.RegisterPeer(kPeer, kPeerId, GENONLINE_PLUGIN_CLIENT_PROTECTED);
	rig.core.Reset();
	CHECK(rig.eac.endCalls == 1);
	CHECK(!rig.core.IsSessionActive());
	CHECK(!rig.registry.IsRegistered(kPeer));
	CHECK(rig.sent.empty() && rig.actions.empty() && rig.violations.empty());
}

int main(int argc, char** argv)
{
	return testing::RunMain(argc, argv);
}
