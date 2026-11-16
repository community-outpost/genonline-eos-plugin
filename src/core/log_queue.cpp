// SPDX-License-Identifier: MIT
#include "core/log_queue.h"

#include <GenOnlinePluginApi.h>

#include <utility>

namespace genonline_eos
{

LogQueue::LogQueue(size_t capacity) : m_capacity(capacity)
{
}

void LogQueue::Push(int32_t level, std::string text)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_lines.size() >= m_capacity)
	{
		++m_dropped;
		return;
	}
	m_lines.push_back(QueuedLogLine{level, std::move(text)});
}

std::vector<QueuedLogLine> LogQueue::Drain(size_t* droppedOut)
{
	std::vector<QueuedLogLine> lines;
	std::lock_guard<std::mutex> lock(m_mutex);
	lines.swap(m_lines);
	if (droppedOut != nullptr)
	{
		*droppedOut = m_dropped;
	}
	m_dropped = 0;
	return lines;
}

int32_t HostLevelFromEosLevel(int32_t eosLevel)
{
	// The numbers are the EOS_ELogLevel values, kept here so this file needs no EOS header.
	constexpr int32_t kEosError = 200;
	constexpr int32_t kEosWarning = 300;
	constexpr int32_t kEosInfo = 400;
	if (eosLevel <= kEosError)
	{
		return GENONLINE_PLUGIN_LOG_ERROR;
	}
	if (eosLevel <= kEosWarning)
	{
		return GENONLINE_PLUGIN_LOG_WARNING;
	}
	if (eosLevel <= kEosInfo)
	{
		return GENONLINE_PLUGIN_LOG_INFO;
	}
	return GENONLINE_PLUGIN_LOG_DEBUG;
}

} // namespace genonline_eos
