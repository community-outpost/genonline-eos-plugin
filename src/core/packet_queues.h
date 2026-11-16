// SPDX-License-Identifier: MIT
// One first in, first out queue of received packets per channel, and the buffers lent to the host. Pure logic without EOS.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

namespace genonline_eos
{

class PacketQueues
{
public:
	explicit PacketQueues(size_t perChannelLimit = 8192);

	// Copies a packet to the end of its channel queue. Returns false when that queue is full.
	bool Push(uint8_t channel, const std::string& productUserId, uint64_t genOnlineUserId, const uint8_t* data, uint32_t length);

	// Size of the head packet of channel, or 0 when the queue is empty.
	uint32_t NextSize(uint8_t channel) const;

	// Takes the head packet of channel. The buffer stays owned by the queues until Free.
	bool Take(uint8_t channel, const uint8_t** data, uint32_t* length, uint64_t* genOnlineUserId);

	// Releases a buffer that Take returned. An unknown pointer does nothing.
	void Free(const uint8_t* data);

	// Drops the queued packets of one peer.
	void DropFrom(const std::string& productUserId);

	// Drops every queued packet. Lent buffers stay valid until Free.
	void Clear();

	size_t QueuedCount() const;

	size_t LentCount() const
	{
		return m_lent.size();
	}

private:
	struct Packet
	{
		std::unique_ptr<uint8_t[]> data;
		uint32_t length = 0;
		std::string productUserId;
		uint64_t genOnlineUserId = 0;
	};

	std::array<std::deque<Packet>, 256> m_channels;
	std::unordered_map<const uint8_t*, std::unique_ptr<uint8_t[]>> m_lent;
	size_t m_limit;
};

} // namespace genonline_eos
