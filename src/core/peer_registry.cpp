// SPDX-License-Identifier: MIT
#include "core/peer_registry.h"

namespace genonline_eos
{

bool PeerRegistry::Register(const std::string& productUserId, uint64_t genOnlineUserId)
{
	if (productUserId.empty())
	{
		return false;
	}
	Peer& peer = m_peers[productUserId];
	peer.genOnlineUserId = genOnlineUserId;
	peer.registered = true;
	return true;
}

bool PeerRegistry::Deregister(const std::string& productUserId)
{
	auto it = m_peers.find(productUserId);
	if (it == m_peers.end() || !it->second.registered)
	{
		return false;
	}
	it->second.registered = false;
	DropUnused(productUserId);
	return true;
}

void PeerRegistry::MarkSignalled(const std::string& productUserId, uint64_t genOnlineUserId)
{
	if (productUserId.empty())
	{
		return;
	}
	Peer& peer = m_peers[productUserId];
	peer.signalled = true;
	if (!peer.registered || peer.genOnlineUserId == 0)
	{
		peer.genOnlineUserId = genOnlineUserId;
	}
}

void PeerRegistry::ClearSignalled(const std::string& productUserId)
{
	auto it = m_peers.find(productUserId);
	if (it == m_peers.end())
	{
		return;
	}
	it->second.signalled = false;
	DropUnused(productUserId);
}

void PeerRegistry::ForgetRegistrations()
{
	for (auto it = m_peers.begin(); it != m_peers.end();)
	{
		it->second.registered = false;
		if (!it->second.signalled)
		{
			it = m_peers.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void PeerRegistry::Clear()
{
	m_peers.clear();
}

bool PeerRegistry::IsRegistered(const std::string& productUserId) const
{
	auto it = m_peers.find(productUserId);
	return it != m_peers.end() && it->second.registered;
}

bool PeerRegistry::MayConnect(const std::string& productUserId) const
{
	auto it = m_peers.find(productUserId);
	return it != m_peers.end() && (it->second.registered || it->second.signalled);
}

uint64_t PeerRegistry::GenOnlineUserId(const std::string& productUserId) const
{
	auto it = m_peers.find(productUserId);
	return it != m_peers.end() ? it->second.genOnlineUserId : 0;
}

bool PeerRegistry::FindProductUserId(uint64_t genOnlineUserId, std::string* productUserId) const
{
	for (const auto& entry : m_peers)
	{
		if (entry.second.genOnlineUserId == genOnlineUserId)
		{
			if (productUserId != nullptr)
			{
				*productUserId = entry.first;
			}
			return true;
		}
	}
	return false;
}

void PeerRegistry::DropUnused(const std::string& productUserId)
{
	auto it = m_peers.find(productUserId);
	if (it != m_peers.end() && !it->second.registered && !it->second.signalled)
	{
		m_peers.erase(it);
	}
}

} // namespace genonline_eos
