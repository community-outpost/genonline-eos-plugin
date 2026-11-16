// SPDX-License-Identifier: MIT
// Unit tests of the transport logic, driven by a fake EOS P2P backend.
#include "test_harness.h"

#include "core/latency_estimator.h"
#include "core/packet_queues.h"
#include "core/peer_registry.h"
#include "core/transport_core.h"
#include "core/transport_rules.h"

#include <GenOnlinePluginApi.h>

#include <cstring>
#include <string>
#include <vector>

using namespace genonline_eos;

namespace
{

const std::string kPeer = "0002aaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const std::string kOther = "0002bbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr uint64_t kPeerId = 1001;
constexpr uint64_t kOtherId = 2002;

struct SentPacket
{
	std::string peer;
	uint8_t channel;
	int32_t reliability;
	std::vector<uint8_t> data;
};

struct FakeP2P : P2PBackend
{
	bool acceptOpen = true;
	std::vector<std::string> opened;
	std::vector<std::string> closed;
	int closeAllCalls = 0;
	std::vector<SentPacket> sent;

	bool OpenConnection(const std::string& peer) override
	{
		opened.push_back(peer);
		return acceptOpen;
	}

	void CloseConnection(const std::string& peer) override
	{
		closed.push_back(peer);
	}

	void CloseAllConnections() override
	{
		++closeAllCalls;
	}

	bool Send(const std::string& peer, uint8_t channel, int32_t reliability, const uint8_t* data, uint32_t length) override
	{
		sent.push_back(SentPacket{peer, channel, reliability, std::vector<uint8_t>(data, data + length)});
		return true;
	}

	size_t SentOn(uint8_t channel) const
	{
		size_t count = 0;
		for (const SentPacket& packet : sent)
		{
			if (packet.channel == channel)
			{
				++count;
			}
		}
		return count;
	}
};

struct Report
{
	std::string peer;
	uint64_t genOnlineUserId;
	int32_t state;
};

// A transport that is available, with a fake backend and recorded state reports.
struct Rig
{
	PeerRegistry peers;
	FakeP2P backend;
	std::vector<Report> reports;
	std::vector<std::string> logs;
	TransportCore core;

	Rig()
		: core(peers, backend,
			  [this](const std::string& peer, uint64_t id, int32_t state)
			  {
				  reports.push_back(Report{peer, id, state});
			  },
			  [this](int32_t, const std::string& text)
			  {
				  logs.push_back(text);
			  })
	{
		core.SetCapabilityEnabled(true);
		core.SetEnabled(true);
		core.SetSignedIn(true);
	}

	void Connect(const std::string& peer, uint64_t id)
	{
		core.StartSignalling(peer, id);
		core.OnEstablished(peer, false);
		reports.clear();
	}
};

const uint8_t kPayload[3] = {7, 8, 9};

} // namespace

TEST_CASE(reliability_maps_to_eos_values)
{
	int32_t eos = -1;
	CHECK(MapReliability(GENONLINE_PLUGIN_RELIABILITY_UNRELIABLE_UNORDERED, &eos) && eos == kEosUnreliableUnordered);
	CHECK(MapReliability(GENONLINE_PLUGIN_RELIABILITY_RELIABLE_UNORDERED, &eos) && eos == kEosReliableUnordered);
	CHECK(MapReliability(GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED, &eos) && eos == kEosReliableOrdered);
	CHECK(!MapReliability(3, &eos));
	CHECK(!MapReliability(-1, &eos));
}

TEST_CASE(probe_channels_are_reserved)
{
	CHECK(IsHostChannel(GENONLINE_PLUGIN_CHANNEL_GAME));
	CHECK(IsHostChannel(GENONLINE_PLUGIN_CHANNEL_ANTICHEAT));
	CHECK(IsHostChannel(GENONLINE_PLUGIN_CHANNEL_SIGNALLING));
	CHECK(IsHostChannel(9));
	CHECK(!IsHostChannel(GENONLINE_PLUGIN_CHANNEL_PING));
	CHECK(!IsHostChannel(GENONLINE_PLUGIN_CHANNEL_PONG));
}

TEST_CASE(probe_encoding_round_trips)
{
	uint8_t probe[kProbeSize];
	EncodeProbe(0x0102030405060708ull, probe);
	uint64_t sent = 0;
	CHECK(DecodeProbe(probe, kProbeSize, &sent));
	CHECK(sent == 0x0102030405060708ull);
	CHECK(!DecodeProbe(probe, kProbeSize - 1, &sent));
	probe[0] ^= 0xFF;
	CHECK(!DecodeProbe(probe, kProbeSize, &sent));
	CHECK(!DecodeProbe(nullptr, kProbeSize, &sent));
}

TEST_CASE(drop_counter_logs_at_powers_of_two)
{
	DropCounter counter;
	std::vector<uint64_t> logged;
	for (int i = 0; i < 20; ++i)
	{
		if (counter.Count())
		{
			logged.push_back(counter.Total());
		}
	}
	CHECK((logged == std::vector<uint64_t>{1, 2, 4, 8, 16}));
}

TEST_CASE(registry_registers_and_deregisters)
{
	PeerRegistry peers;
	CHECK(!peers.Register("", kPeerId));
	CHECK(peers.Register(kPeer, kPeerId));
	CHECK(peers.IsRegistered(kPeer));
	CHECK(peers.MayConnect(kPeer));
	CHECK(peers.GenOnlineUserId(kPeer) == kPeerId);
	std::string found;
	CHECK(peers.FindProductUserId(kPeerId, &found) && found == kPeer);
	CHECK(peers.Deregister(kPeer));
	CHECK(!peers.Deregister(kPeer));
	CHECK(!peers.IsRegistered(kPeer));
	CHECK(!peers.MayConnect(kPeer));
	CHECK(peers.GenOnlineUserId(kPeer) == 0);
	CHECK(!peers.FindProductUserId(kPeerId, &found));
}

TEST_CASE(registry_signalled_peers_may_connect_but_are_not_registered)
{
	PeerRegistry peers;
	peers.MarkSignalled(kPeer, kPeerId);
	CHECK(peers.MayConnect(kPeer));
	CHECK(!peers.IsRegistered(kPeer));
	CHECK(peers.GenOnlineUserId(kPeer) == kPeerId);
	peers.Register(kPeer, kPeerId);
	peers.ForgetRegistrations();
	CHECK(!peers.IsRegistered(kPeer));
	CHECK(peers.MayConnect(kPeer));
	peers.ClearSignalled(kPeer);
	CHECK(!peers.MayConnect(kPeer));
	CHECK(!peers.MayConnect(kOther));
}

TEST_CASE(latency_is_zero_until_the_first_sample)
{
	LatencyEstimator latency;
	CHECK(latency.Milliseconds() == 0);
	CHECK(!latency.HasSample());
	CHECK(latency.AddSample(80.0));
	CHECK(latency.Milliseconds() == 80);
}

TEST_CASE(latency_smooths_samples)
{
	LatencyEstimator latency;
	latency.AddSample(100.0);
	latency.AddSample(180.0);
	CHECK(latency.Milliseconds() == 110);
	for (int i = 0; i < 100; ++i)
	{
		latency.AddSample(40.0);
	}
	CHECK(latency.Milliseconds() == 40);
}

TEST_CASE(latency_ignores_outliers_above_5000_ms)
{
	LatencyEstimator latency;
	CHECK(!latency.AddSample(5000.5));
	CHECK(latency.Milliseconds() == 0);
	latency.AddSample(50.0);
	CHECK(!latency.AddSample(9000.0));
	CHECK(!latency.AddSample(-1.0));
	CHECK(latency.Milliseconds() == 50);
	CHECK(latency.AddSample(5000.0));
}

TEST_CASE(latency_below_one_ms_reports_one)
{
	LatencyEstimator latency;
	latency.AddSample(0.2);
	CHECK(latency.Milliseconds() == 1);
}

TEST_CASE(queues_are_first_in_first_out_per_channel_with_the_sender)
{
	PacketQueues queues;
	const uint8_t a[2] = {1, 2};
	const uint8_t b[3] = {3, 4, 5};
	const uint8_t c[1] = {6};
	CHECK(queues.Push(0, kPeer, kPeerId, a, 2));
	CHECK(queues.Push(2, kOther, kOtherId, c, 1));
	CHECK(queues.Push(0, kOther, kOtherId, b, 3));
	CHECK(queues.NextSize(0) == 2);
	CHECK(queues.NextSize(2) == 1);
	CHECK(queues.NextSize(1) == 0);
	const uint8_t* data = nullptr;
	uint32_t length = 0;
	uint64_t sender = 0;
	REQUIRE(queues.Take(0, &data, &length, &sender));
	CHECK(length == 2 && data[0] == 1 && data[1] == 2 && sender == kPeerId);
	queues.Free(data);
	REQUIRE(queues.Take(0, &data, &length, &sender));
	CHECK(length == 3 && data[2] == 5 && sender == kOtherId);
	queues.Free(data);
	CHECK(!queues.Take(0, &data, &length, &sender));
	CHECK(queues.NextSize(0) == 0);
}

TEST_CASE(queues_lend_buffers_until_free_once)
{
	PacketQueues queues;
	queues.Push(0, kPeer, kPeerId, kPayload, 3);
	queues.Push(0, kPeer, kPeerId, kPayload, 3);
	const uint8_t* first = nullptr;
	const uint8_t* second = nullptr;
	uint32_t length = 0;
	uint64_t sender = 0;
	REQUIRE(queues.Take(0, &first, &length, &sender));
	REQUIRE(queues.Take(0, &second, &length, &sender));
	CHECK(first != second);
	CHECK(queues.LentCount() == 2);
	queues.Clear();
	CHECK(first[0] == 7);
	queues.Free(first);
	CHECK(queues.LentCount() == 1);
	queues.Free(first);
	const uint8_t unknown = 0;
	queues.Free(&unknown);
	queues.Free(nullptr);
	CHECK(queues.LentCount() == 1);
	queues.Free(second);
	CHECK(queues.LentCount() == 0);
}

TEST_CASE(queues_take_an_empty_packet_with_a_buffer)
{
	PacketQueues queues;
	CHECK(queues.Push(0, kPeer, kPeerId, nullptr, 0));
	const uint8_t* data = nullptr;
	uint32_t length = 7;
	uint64_t sender = 0;
	REQUIRE(queues.Take(0, &data, &length, &sender));
	CHECK(data != nullptr);
	CHECK(length == 0);
	queues.Free(data);
	CHECK(queues.LentCount() == 0);
}

TEST_CASE(queues_drop_the_packets_of_one_peer)
{
	PacketQueues queues;
	queues.Push(0, kPeer, kPeerId, kPayload, 3);
	queues.Push(0, kOther, kOtherId, kPayload, 2);
	queues.Push(1, kPeer, kPeerId, kPayload, 1);
	queues.DropFrom(kPeer);
	CHECK(queues.QueuedCount() == 1);
	CHECK(queues.NextSize(0) == 2);
	CHECK(queues.NextSize(1) == 0);
}

TEST_CASE(queues_refuse_packets_beyond_the_channel_limit)
{
	PacketQueues queues(2);
	CHECK(queues.Push(0, kPeer, kPeerId, kPayload, 3));
	CHECK(queues.Push(0, kPeer, kPeerId, kPayload, 3));
	CHECK(!queues.Push(0, kPeer, kPeerId, kPayload, 3));
	CHECK(queues.Push(1, kPeer, kPeerId, kPayload, 3));
}

TEST_CASE(transport_is_available_only_with_capability_flag_and_sign_in)
{
	Rig rig;
	CHECK(rig.core.IsAvailable());
	rig.core.SetEnabled(false);
	CHECK(!rig.core.IsAvailable());
	rig.core.SetEnabled(true);
	rig.core.SetSignedIn(false);
	CHECK(!rig.core.IsAvailable());
	rig.core.SetSignedIn(true);
	rig.core.SetCapabilityEnabled(false);
	CHECK(!rig.core.IsAvailable());

	PeerRegistry peers;
	FakeP2P backend;
	TransportCore fresh(peers, backend, nullptr, nullptr);
	fresh.SetCapabilityEnabled(true);
	fresh.SetSignedIn(true);
	CHECK(!fresh.IsAvailable());
}

TEST_CASE(start_signalling_opens_and_reports_connecting_direct)
{
	Rig rig;
	rig.core.StartSignalling(kPeer, kPeerId);
	CHECK((rig.backend.opened == std::vector<std::string>{kPeer}));
	CHECK(rig.backend.closed.empty());
	REQUIRE(rig.reports.size() == 1);
	CHECK(rig.reports[0].peer == kPeer);
	CHECK(rig.reports[0].genOnlineUserId == kPeerId);
	CHECK(rig.reports[0].state == GENONLINE_PLUGIN_CONNECTION_CONNECTING_DIRECT);
	CHECK(rig.core.HasConnection(kPeer));
}

TEST_CASE(start_signalling_closes_an_existing_connection_first)
{
	Rig rig;
	rig.Connect(kPeer, kPeerId);
	rig.core.StartSignalling(kPeer, kPeerId);
	CHECK((rig.backend.closed == std::vector<std::string>{kPeer}));
	CHECK(rig.backend.opened.size() == 2);
	REQUIRE(rig.reports.size() == 1);
	CHECK(rig.reports[0].state == GENONLINE_PLUGIN_CONNECTION_CONNECTING_DIRECT);
	rig.core.OnClosed(kPeer, true);
	CHECK(rig.reports.size() == 1);
	CHECK(rig.core.HasConnection(kPeer));
}

TEST_CASE(start_signalling_that_cannot_open_reports_failed)
{
	Rig rig;
	rig.backend.acceptOpen = false;
	rig.core.StartSignalling(kPeer, kPeerId);
	REQUIRE(rig.reports.size() == 2);
	CHECK(rig.reports[0].state == GENONLINE_PLUGIN_CONNECTION_CONNECTING_DIRECT);
	CHECK(rig.reports[1].state == GENONLINE_PLUGIN_CONNECTION_FAILED);
	CHECK(!rig.core.HasConnection(kPeer));
}

TEST_CASE(start_signalling_while_unavailable_reports_failed_at_once)
{
	Rig rig;
	rig.core.SetSignedIn(false);
	rig.core.StartSignalling(kPeer, kPeerId);
	CHECK(rig.backend.opened.empty());
	CHECK(!rig.core.HasConnection(kPeer));
	REQUIRE(rig.reports.size() == 1);
	CHECK(rig.reports[0].peer == kPeer);
	CHECK(rig.reports[0].genOnlineUserId == kPeerId);
	CHECK(rig.reports[0].state == GENONLINE_PLUGIN_CONNECTION_FAILED);
	rig.core.SetSignedIn(true);
	rig.core.SetEnabled(false);
	rig.core.StartSignalling(kOther, kOtherId);
	REQUIRE(rig.reports.size() == 2);
	CHECK(rig.reports[1].genOnlineUserId == kOtherId);
	CHECK(rig.reports[1].state == GENONLINE_PLUGIN_CONNECTION_FAILED);
	CHECK(rig.backend.opened.empty());
}

TEST_CASE(established_reports_connected_direct_once)
{
	Rig rig;
	rig.core.StartSignalling(kPeer, kPeerId);
	rig.core.OnEstablished(kPeer, false);
	rig.core.OnEstablished(kPeer, true);
	REQUIRE(rig.reports.size() == 2);
	CHECK(rig.reports[1].state == GENONLINE_PLUGIN_CONNECTION_CONNECTED_DIRECT);
	CHECK(rig.reports[1].genOnlineUserId == kPeerId);
	CHECK(rig.core.IsRelayed(kPeer));
}

TEST_CASE(established_reports_the_relay_type)
{
	Rig rig;
	rig.core.StartSignalling(kPeer, kPeerId);
	rig.core.OnEstablished(kPeer, true);
	CHECK(rig.core.IsRelayed(kPeer));
	rig.core.StartSignalling(kOther, kOtherId);
	rig.core.OnEstablished(kOther, false);
	CHECK(!rig.core.IsRelayed(kOther));
	CHECK(!rig.core.IsRelayed("0002cccccccccccccccccccccccccccc"));
}

TEST_CASE(close_before_established_reports_failed)
{
	Rig rig;
	rig.core.StartSignalling(kPeer, kPeerId);
	rig.core.OnClosed(kPeer, false);
	REQUIRE(rig.reports.size() == 2);
	CHECK(rig.reports[1].state == GENONLINE_PLUGIN_CONNECTION_FAILED);
	CHECK(!rig.core.HasConnection(kPeer));
	rig.core.OnClosed(kPeer, false);
	CHECK(rig.reports.size() == 2);
}

TEST_CASE(close_after_established_reports_disconnected)
{
	Rig rig;
	rig.Connect(kPeer, kPeerId);
	rig.core.OnClosed(kPeer, false);
	REQUIRE(rig.reports.size() == 1);
	CHECK(rig.reports[0].state == GENONLINE_PLUGIN_CONNECTION_DISCONNECTED);
	CHECK(rig.reports[0].genOnlineUserId == kPeerId);
}

TEST_CASE(host_disconnect_reports_nothing_and_drops_queued_packets)
{
	Rig rig;
	rig.peers.Register(kPeer, kPeerId);
	rig.Connect(kPeer, kPeerId);
	rig.core.OnPacket(kPeer, 0, kPayload, 3, 10);
	CHECK(rig.core.Queues().NextSize(0) == 3);
	rig.core.DisconnectPeer(kPeer);
	CHECK((rig.backend.closed == std::vector<std::string>{kPeer}));
	CHECK(rig.core.Queues().NextSize(0) == 0);
	CHECK(!rig.core.HasConnection(kPeer));
	rig.core.OnClosed(kPeer, true);
	rig.core.OnClosed(kPeer, false);
	CHECK(rig.reports.empty());
}

TEST_CASE(disconnect_all_and_disabling_close_every_connection)
{
	Rig rig;
	rig.peers.Register(kPeer, kPeerId);
	rig.Connect(kPeer, kPeerId);
	rig.Connect(kOther, kOtherId);
	rig.core.OnPacket(kPeer, 0, kPayload, 3, 10);
	rig.core.DisconnectAll();
	CHECK(rig.backend.closeAllCalls == 1);
	CHECK(!rig.core.HasConnection(kPeer));
	CHECK(!rig.core.HasConnection(kOther));
	CHECK(rig.core.Queues().QueuedCount() == 0);
	rig.Connect(kPeer, kPeerId);
	rig.core.SetEnabled(false);
	CHECK(rig.backend.closeAllCalls == 2);
	CHECK(!rig.core.HasConnection(kPeer));
	CHECK(rig.reports.empty());
}

TEST_CASE(incoming_requests_are_accepted_only_from_known_peers)
{
	Rig rig;
	rig.core.OnConnectionRequest(kPeer);
	CHECK(rig.backend.opened.empty());
	CHECK(!rig.core.HasConnection(kPeer));
	rig.peers.Register(kPeer, kPeerId);
	rig.core.OnConnectionRequest(kPeer);
	CHECK((rig.backend.opened == std::vector<std::string>{kPeer}));
	CHECK(rig.core.HasConnection(kPeer));
	rig.core.OnEstablished(kPeer, false);
	REQUIRE(rig.reports.size() == 1);
	CHECK(rig.reports[0].state == GENONLINE_PLUGIN_CONNECTION_CONNECTED_DIRECT);
	rig.peers.MarkSignalled(kOther, kOtherId);
	rig.core.OnConnectionRequest(kOther);
	CHECK(rig.backend.opened.size() == 2);
}

TEST_CASE(incoming_requests_are_refused_while_unavailable)
{
	Rig rig;
	rig.peers.Register(kPeer, kPeerId);
	rig.core.SetEnabled(false);
	rig.core.OnConnectionRequest(kPeer);
	CHECK(rig.backend.opened.empty());
}

TEST_CASE(send_maps_channel_and_reliability)
{
	Rig rig;
	rig.Connect(kPeer, kPeerId);
	rig.core.Send(kPeer, kPayload, 3, GENONLINE_PLUGIN_CHANNEL_ANTICHEAT, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	rig.core.Send(kPeer, kPayload, 2, GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_UNRELIABLE_UNORDERED);
	REQUIRE(rig.backend.sent.size() == 2);
	CHECK(rig.backend.sent[0].peer == kPeer);
	CHECK(rig.backend.sent[0].channel == GENONLINE_PLUGIN_CHANNEL_ANTICHEAT);
	CHECK(rig.backend.sent[0].reliability == kEosReliableOrdered);
	CHECK(rig.backend.sent[0].data.size() == 3);
	CHECK(rig.backend.sent[1].reliability == kEosUnreliableUnordered);
}

TEST_CASE(send_drops_packets_it_cannot_deliver)
{
	Rig rig;
	std::vector<uint8_t> large(kMaxPacketSize + 1, 0);
	std::vector<uint8_t> largest(kMaxPacketSize, 0);
	rig.core.Send(kPeer, kPayload, 3, GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	CHECK(rig.backend.sent.empty());
	rig.Connect(kPeer, kPeerId);
	rig.core.Send(kPeer, large.data(), static_cast<uint32_t>(large.size()), GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	rig.core.Send(kPeer, kPayload, 3, GENONLINE_PLUGIN_CHANNEL_PING, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	rig.core.Send(kPeer, kPayload, 3, GENONLINE_PLUGIN_CHANNEL_GAME, 7);
	rig.core.Send(kPeer, nullptr, 3, GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	CHECK(rig.backend.sent.empty());
	rig.core.Send(kPeer, largest.data(), static_cast<uint32_t>(largest.size()), GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	CHECK(rig.backend.sent.size() == 1);
	rig.core.SetEnabled(true);
	rig.core.SetSignedIn(false);
	rig.core.Send(kPeer, kPayload, 3, GENONLINE_PLUGIN_CHANNEL_GAME, GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED);
	CHECK(rig.backend.sent.size() == 1);
}

TEST_CASE(packets_of_unregistered_peers_are_dropped)
{
	Rig rig;
	rig.Connect(kPeer, kPeerId);
	rig.core.OnPacket(kPeer, 0, kPayload, 3, 10);
	CHECK(rig.core.Queues().QueuedCount() == 0);
	rig.peers.Register(kPeer, kPeerId);
	rig.core.OnPacket(kPeer, 0, kPayload, 3, 10);
	const uint8_t* data = nullptr;
	uint32_t length = 0;
	uint64_t sender = 0;
	REQUIRE(rig.core.Queues().Take(0, &data, &length, &sender));
	CHECK(sender == kPeerId);
	rig.core.Queues().Free(data);
}

TEST_CASE(pings_are_answered_and_never_queued)
{
	Rig rig;
	rig.peers.Register(kPeer, kPeerId);
	rig.Connect(kPeer, kPeerId);
	uint8_t probe[kProbeSize];
	EncodeProbe(5000, probe);
	rig.core.OnPacket(kPeer, GENONLINE_PLUGIN_CHANNEL_PING, probe, kProbeSize, 6000);
	CHECK(rig.core.Queues().QueuedCount() == 0);
	REQUIRE(rig.backend.SentOn(GENONLINE_PLUGIN_CHANNEL_PONG) == 1);
	CHECK(rig.backend.sent[0].data == std::vector<uint8_t>(probe, probe + kProbeSize));
	CHECK(rig.backend.sent[0].reliability == kEosUnreliableUnordered);
	rig.core.OnPacket(kOther, GENONLINE_PLUGIN_CHANNEL_PING, probe, kProbeSize, 6000);
	CHECK(rig.backend.SentOn(GENONLINE_PLUGIN_CHANNEL_PONG) == 1);
}

TEST_CASE(pongs_measure_latency_and_are_never_queued)
{
	Rig rig;
	rig.peers.Register(kPeer, kPeerId);
	rig.Connect(kPeer, kPeerId);
	CHECK(rig.core.Latency(kPeer) == 0);
	uint8_t probe[kProbeSize];
	EncodeProbe(1000, probe);
	rig.core.OnPacket(kPeer, GENONLINE_PLUGIN_CHANNEL_PONG, probe, kProbeSize, 1060);
	CHECK(rig.core.Latency(kPeer) == 60);
	CHECK(rig.core.Queues().QueuedCount() == 0);
	EncodeProbe(2000, probe);
	rig.core.OnPacket(kPeer, GENONLINE_PLUGIN_CHANNEL_PONG, probe, kProbeSize, 9000);
	CHECK(rig.core.Latency(kPeer) == 60);
	rig.core.OnPacket(kPeer, GENONLINE_PLUGIN_CHANNEL_PONG, kPayload, 3, 9000);
	CHECK(rig.core.Queues().QueuedCount() == 0);
	CHECK(rig.core.Latency(kOther) == 0);
}

TEST_CASE(tick_probes_each_connected_peer_about_once_per_second)
{
	Rig rig;
	rig.Connect(kPeer, kPeerId);
	rig.core.StartSignalling(kOther, kOtherId);
	rig.core.Tick(10000);
	CHECK(rig.backend.SentOn(GENONLINE_PLUGIN_CHANNEL_PING) == 1);
	CHECK(rig.backend.sent[0].peer == kPeer);
	rig.core.Tick(10500);
	CHECK(rig.backend.SentOn(GENONLINE_PLUGIN_CHANNEL_PING) == 1);
	rig.core.Tick(11000);
	CHECK(rig.backend.SentOn(GENONLINE_PLUGIN_CHANNEL_PING) == 2);
	uint64_t sentMs = 0;
	CHECK(DecodeProbe(rig.backend.sent[1].data.data(), static_cast<uint32_t>(rig.backend.sent[1].data.size()), &sentMs));
	CHECK(sentMs == 11000);
}

TEST_CASE(sign_out_and_reset_close_everything_without_reports)
{
	Rig rig;
	rig.peers.Register(kPeer, kPeerId);
	rig.Connect(kPeer, kPeerId);
	rig.core.OnPacket(kPeer, 0, kPayload, 3, 10);
	rig.core.SetSignedIn(false);
	CHECK(rig.backend.closeAllCalls == 1);
	CHECK(!rig.core.HasConnection(kPeer));
	CHECK(rig.core.Queues().QueuedCount() == 0);
	rig.core.SetSignedIn(true);
	rig.Connect(kPeer, kPeerId);
	rig.core.Reset();
	CHECK(!rig.core.HasConnection(kPeer));
	CHECK(rig.reports.empty());
}

int main(int argc, char** argv)
{
	return testing::RunMain(argc, argv);
}
