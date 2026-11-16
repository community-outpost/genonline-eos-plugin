// SPDX-License-Identifier: MIT
#include "core/api_table.h"

#include <cstddef>
#include <cstring>

namespace genonline_eos
{

namespace
{

#define GENONLINE_EOS_FIELD_END(field) (offsetof(GenOnlinePluginApi, field) + sizeof(static_cast<GenOnlinePluginApi*>(nullptr)->field))

// End offsets of every field of GenOnlinePluginApi in declaration order.
constexpr size_t kFieldEnds[] = {
	GENONLINE_EOS_FIELD_END(structSize),
	GENONLINE_EOS_FIELD_END(apiVersion),
	GENONLINE_EOS_FIELD_END(name),
	GENONLINE_EOS_FIELD_END(version),
	GENONLINE_EOS_FIELD_END(capabilities),
	GENONLINE_EOS_FIELD_END(Initialize),
	GENONLINE_EOS_FIELD_END(Tick),
	GENONLINE_EOS_FIELD_END(Shutdown),
	GENONLINE_EOS_FIELD_END(IsExternalProcessRunning),
	GENONLINE_EOS_FIELD_END(GetAnticheatIdentifier),
	GENONLINE_EOS_FIELD_END(linkedAccount),
	GENONLINE_EOS_FIELD_END(anticheat),
	GENONLINE_EOS_FIELD_END(transport),
};

#undef GENONLINE_EOS_FIELD_END

static_assert(kFieldEnds[sizeof(kFieldEnds) / sizeof(kFieldEnds[0]) - 1] == sizeof(GenOnlinePluginApi), "every field of GenOnlinePluginApi is listed");

// Returns the end of the last whole field that fits in limit.
size_t WholeFieldBytes(size_t limit)
{
	size_t filled = 0;
	for (size_t end : kFieldEnds)
	{
		if (end > limit)
		{
			break;
		}
		filled = end;
	}
	return filled;
}

} // namespace

int32_t CheckGetApiArguments(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, const GenOnlinePluginApi* out)
{
	if (hostApiVersion != GENONLINE_PLUGIN_API_VERSION)
	{
		return GENONLINE_PLUGIN_RESULT_VERSION_MISMATCH;
	}
	if (host == nullptr || out == nullptr)
	{
		return GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT;
	}
	return GENONLINE_PLUGIN_RESULT_OK;
}

int32_t FillPluginApi(const GenOnlinePluginApi& complete, GenOnlinePluginApi* out)
{
	if (out == nullptr || out->structSize < sizeof(out->structSize))
	{
		return GENONLINE_PLUGIN_RESULT_INVALID_ARGUMENT;
	}
	const size_t filled = WholeFieldBytes(out->structSize);
	std::memcpy(out, &complete, filled);
	out->structSize = static_cast<uint32_t>(filled);
	return GENONLINE_PLUGIN_RESULT_OK;
}

} // namespace genonline_eos
