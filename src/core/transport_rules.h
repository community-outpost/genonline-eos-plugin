// SPDX-License-Identifier: MIT
// Small transport rules: reliability mapping, reserved channels, probe packets and log throttling. Pure logic without EOS.
#pragma once

#include <cstddef>
#include <cstdint>

namespace genonline_eos
{

// The largest packet EOS P2P sends, equal to EOS_P2P_MAX_PACKET_SIZE.
constexpr uint32_t kMaxPacketSize = 1170;

// The P2P socket name of the plugin.
constexpr const char* kSocketName = "genonline";

// The EOS_EPacketReliability values, kept here so this file needs no EOS header.
constexpr int32_t kEosUnreliableUnordered = 0;
constexpr int32_t kEosReliableUnordered = 1;
constexpr int32_t kEosReliableOrdered = 2;

// Maps a GENONLINE_PLUGIN_RELIABILITY_ value to its EOS_EPacketReliability value. Returns false for an unknown value.
bool MapReliability(int32_t hostReliability, int32_t* eosReliability);

// Returns false for the ping and pong channels, which only the plugin uses.
bool IsHostChannel(uint8_t channel);

// A probe holds a 4 byte tag and the 8 byte send time of the prober in milliseconds.
constexpr uint32_t kProbeSize = 12;

void EncodeProbe(uint64_t sentMs, uint8_t (&out)[kProbeSize]);

// Reads a probe. Returns false when data is not a probe.
bool DecodeProbe(const uint8_t* data, uint32_t length, uint64_t* sentMs);

// Counts repeated events and says when to log one, which is at counts 1, 2, 4, 8 and so on.
class DropCounter
{
public:
	// Counts one event and returns true when this count should be logged.
	bool Count();

	uint64_t Total() const
	{
		return m_total;
	}

private:
	uint64_t m_total = 0;
};

} // namespace genonline_eos
