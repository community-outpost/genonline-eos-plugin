// SPDX-License-Identifier: MIT
// Holds log lines from any thread until the main thread hands them to the host. Pure logic without EOS.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace genonline_eos
{

struct QueuedLogLine
{
	int32_t level;
	std::string text;
};

class LogQueue
{
public:
	explicit LogQueue(size_t capacity = 1024);

	// Adds a line. When the queue is full the line is dropped and counted.
	void Push(int32_t level, std::string text);

	// Takes every queued line in arrival order and the number of lines dropped since the last call.
	std::vector<QueuedLogLine> Drain(size_t* droppedOut);

private:
	std::mutex m_mutex;
	std::vector<QueuedLogLine> m_lines;
	size_t m_capacity;
	size_t m_dropped = 0;
};

// Maps an EOS_ELogLevel value to a GENONLINE_PLUGIN_LOG_ level.
int32_t HostLevelFromEosLevel(int32_t eosLevel);

} // namespace genonline_eos
