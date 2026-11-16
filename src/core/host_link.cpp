// SPDX-License-Identifier: MIT
#include "core/host_link.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace genonline_eos
{

const char* const kLogPrefix = "[genonline_eos] ";

namespace
{

// Copies one field from source when the host table is long enough to hold all of it.
#define GENONLINE_EOS_COPY_FIELD(field) \
	do \
	{ \
		if (offsetof(GenOnlinePluginHostApi, field) + sizeof(source.field) <= size) \
		{ \
			target.field = source.field; \
		} \
	} while (0)

GenOnlinePluginHostApi CopyWholeFields(const GenOnlinePluginHostApi& source, uint32_t offeredCapabilities)
{
	const size_t size = source.structSize;
	GenOnlinePluginHostApi target = {};
	target.structSize = sizeof(GenOnlinePluginHostApi);
	target.enabledCapabilities = offeredCapabilities;
	GENONLINE_EOS_COPY_FIELD(userData);
	GENONLINE_EOS_COPY_FIELD(Log);
	GENONLINE_EOS_COPY_FIELD(LobbyChatOutput);
	GENONLINE_EOS_COPY_FIELD(ConnectionStateChanged);
	GENONLINE_EOS_COPY_FIELD(ActionRequired);
	GENONLINE_EOS_COPY_FIELD(IntegrityViolation);
	GENONLINE_EOS_COPY_FIELD(SendMessageViaTransport);
	GENONLINE_EOS_COPY_FIELD(enabledCapabilities);
	target.enabledCapabilities &= offeredCapabilities;
	return target;
}

#undef GENONLINE_EOS_COPY_FIELD

} // namespace

void HostLink::Bind(const GenOnlinePluginHostApi& host, uint32_t offeredCapabilities)
{
	m_table = CopyWholeFields(host, offeredCapabilities);
	m_bound = true;
}

void HostLink::Release()
{
	m_table = GenOnlinePluginHostApi{};
	m_bound = false;
}

void HostLink::Log(int32_t level, const char* message) const
{
	if (!m_bound || m_table.Log == nullptr)
	{
		return;
	}
	const std::string line = PrefixLogLine(message);
	m_table.Log(m_table.userData, level, line.c_str());
}

void HostLink::Logf(int32_t level, const char* format, ...) const
{
	if (!m_bound || m_table.Log == nullptr || format == nullptr)
	{
		return;
	}
	va_list arguments;
	va_start(arguments, format);
	va_list measure;
	va_copy(measure, arguments);
	const int length = std::vsnprintf(nullptr, 0, format, measure);
	va_end(measure);
	std::string text;
	if (length > 0)
	{
		text.resize(static_cast<size_t>(length) + 1);
		std::vsnprintf(&text[0], text.size(), format, arguments);
		text.resize(static_cast<size_t>(length));
	}
	va_end(arguments);
	Log(level, text.c_str());
}

std::string PrefixLogLine(const char* message)
{
	std::string line(kLogPrefix);
	if (message != nullptr)
	{
		line += message;
	}
	return line;
}

} // namespace genonline_eos
