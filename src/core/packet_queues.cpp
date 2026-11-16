// SPDX-License-Identifier: MIT
#include "core/packet_queues.h"

#include <cstring>
#include <utility>

namespace genonline_eos
{

PacketQueues::PacketQueues(size_t perChannelLimit) : m_limit(perChannelLimit)
{
}

bool PacketQueues::Push(uint8_t channel, const std::string& productUserId, uint64_t genOnlineUserId, const uint8_t* data, uint32_t length)
{
	std::deque<Packet>& queue = m_channels[channel];
	if (queue.size() >= m_limit || (data == nullptr && length != 0))
	{
		return false;
	}
	Packet packet;
	// An empty packet still gets one byte, so every lent buffer has its own address.
	packet.data.reset(new uint8_t[length != 0 ? length : 1]);
	if (length != 0)
	{
		std::memcpy(packet.data.get(), data, length);
	}
	packet.length = length;
	packet.productUserId = productUserId;
	packet.genOnlineUserId = genOnlineUserId;
	queue.push_back(std::move(packet));
	return true;
}

uint32_t PacketQueues::NextSize(uint8_t channel) const
{
	const std::deque<Packet>& queue = m_channels[channel];
	return queue.empty() ? 0 : queue.front().length;
}

bool PacketQueues::Take(uint8_t channel, const uint8_t** data, uint32_t* length, uint64_t* genOnlineUserId)
{
	std::deque<Packet>& queue = m_channels[channel];
	if (queue.empty() || data == nullptr || length == nullptr)
	{
		return false;
	}
	Packet& packet = queue.front();
	const uint8_t* buffer = packet.data.get();
	*data = buffer;
	*length = packet.length;
	if (genOnlineUserId != nullptr)
	{
		*genOnlineUserId = packet.genOnlineUserId;
	}
	m_lent.emplace(buffer, std::move(packet.data));
	queue.pop_front();
	return true;
}

void PacketQueues::Free(const uint8_t* data)
{
	if (data != nullptr)
	{
		m_lent.erase(data);
	}
}

void PacketQueues::DropFrom(const std::string& productUserId)
{
	for (std::deque<Packet>& queue : m_channels)
	{
		for (auto it = queue.begin(); it != queue.end();)
		{
			if (it->productUserId == productUserId)
			{
				it = queue.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}

void PacketQueues::Clear()
{
	for (std::deque<Packet>& queue : m_channels)
	{
		queue.clear();
	}
}

size_t PacketQueues::QueuedCount() const
{
	size_t count = 0;
	for (const std::deque<Packet>& queue : m_channels)
	{
		count += queue.size();
	}
	return count;
}

} // namespace genonline_eos
