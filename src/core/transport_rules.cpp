// SPDX-License-Identifier: MIT
#include "core/transport_rules.h"

#include <GenOnlinePluginApi.h>

namespace genonline_eos
{

namespace
{

constexpr uint8_t kProbeTag[4] = {'G', 'O', 'P', 'R'};

} // namespace

bool MapReliability(int32_t hostReliability, int32_t* eosReliability)
{
	int32_t mapped = 0;
	switch (hostReliability)
	{
	case GENONLINE_PLUGIN_RELIABILITY_UNRELIABLE_UNORDERED:
		mapped = kEosUnreliableUnordered;
		break;
	case GENONLINE_PLUGIN_RELIABILITY_RELIABLE_UNORDERED:
		mapped = kEosReliableUnordered;
		break;
	case GENONLINE_PLUGIN_RELIABILITY_RELIABLE_ORDERED:
		mapped = kEosReliableOrdered;
		break;
	default:
		return false;
	}
	if (eosReliability != nullptr)
	{
		*eosReliability = mapped;
	}
	return true;
}

bool IsHostChannel(uint8_t channel)
{
	return channel != GENONLINE_PLUGIN_CHANNEL_PING && channel != GENONLINE_PLUGIN_CHANNEL_PONG;
}

void EncodeProbe(uint64_t sentMs, uint8_t (&out)[kProbeSize])
{
	for (size_t i = 0; i < sizeof(kProbeTag); ++i)
	{
		out[i] = kProbeTag[i];
	}
	for (size_t i = 0; i < 8; ++i)
	{
		out[4 + i] = static_cast<uint8_t>(sentMs >> (8 * i));
	}
}

bool DecodeProbe(const uint8_t* data, uint32_t length, uint64_t* sentMs)
{
	if (data == nullptr || length != kProbeSize)
	{
		return false;
	}
	for (size_t i = 0; i < sizeof(kProbeTag); ++i)
	{
		if (data[i] != kProbeTag[i])
		{
			return false;
		}
	}
	uint64_t value = 0;
	for (size_t i = 0; i < 8; ++i)
	{
		value |= static_cast<uint64_t>(data[4 + i]) << (8 * i);
	}
	if (sentMs != nullptr)
	{
		*sentMs = value;
	}
	return true;
}

bool DropCounter::Count()
{
	++m_total;
	return (m_total & (m_total - 1)) == 0;
}

} // namespace genonline_eos
