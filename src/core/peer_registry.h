// SPDX-License-Identifier: MIT
// The peers the game registered or signalled, keyed by product user id. Pure logic without EOS.
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace genonline_eos
{

class PeerRegistry
{
public:
	// Registers a peer of the anticheat session. Returns false for an empty product user id.
	bool Register(const std::string& productUserId, uint64_t genOnlineUserId);

	// Removes the registration. Returns false when the peer was not registered.
	bool Deregister(const std::string& productUserId);

	// Records that the game asked for a connection to the peer.
	void MarkSignalled(const std::string& productUserId, uint64_t genOnlineUserId);
	void ClearSignalled(const std::string& productUserId);

	// Removes every registration and keeps the signalled peers.
	void ForgetRegistrations();

	void Clear();

	bool IsRegistered(const std::string& productUserId) const;

	// True for a registered or signalled peer.
	bool MayConnect(const std::string& productUserId) const;

	// The GenOnline user id of the peer, or 0 when unknown.
	uint64_t GenOnlineUserId(const std::string& productUserId) const;

	// Finds the product user id of a GenOnline user id. Returns false when unknown.
	bool FindProductUserId(uint64_t genOnlineUserId, std::string* productUserId) const;

private:
	struct Peer
	{
		uint64_t genOnlineUserId = 0;
		bool registered = false;
		bool signalled = false;
	};

	void DropUnused(const std::string& productUserId);

	std::map<std::string, Peer> m_peers;
};

} // namespace genonline_eos
