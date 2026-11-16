// SPDX-License-Identifier: MIT
// Version and size handling of GenOnlinePlugin_GetApi. Pure logic without EOS.
#pragma once

#include <GenOnlinePluginApi.h>

namespace genonline_eos
{

// Checks the arguments of GenOnlinePlugin_GetApi and returns GENONLINE_PLUGIN_RESULT_OK when they are usable.
int32_t CheckGetApiArguments(uint32_t hostApiVersion, const GenOnlinePluginHostApi* host, const GenOnlinePluginApi* out);

// Copies the complete table into out, limited to the whole fields inside out->structSize, and sets out->structSize to the bytes filled.
int32_t FillPluginApi(const GenOnlinePluginApi& complete, GenOnlinePluginApi* out);

} // namespace genonline_eos
