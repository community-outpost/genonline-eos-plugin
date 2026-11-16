// SPDX-License-Identifier: MIT
// The sub interface tables and runtime modules of the linked account, anticheat and transport parts.
#pragma once

#include <GenOnlinePluginApi.h>

#include <eos_common.h>

namespace genonline_eos
{

class RuntimeModule;

// Each table is a static object that stays valid until the DLL unloads.
const GenOnlinePluginLinkedAccountApi* LinkedAccountTable();
const GenOnlinePluginAnticheatApi* AnticheatTable();
const GenOnlinePluginTransportApi* TransportTable();

// The module of each part, or null while the part needs no EOS lifecycle.
RuntimeModule* LinkedAccountModule();
RuntimeModule* AnticheatModule();
RuntimeModule* TransportModule();

// The EOS handle of the product user signed in to EOS Connect, or null when none.
EOS_ProductUserId SignedInProductUser();

// Returns true when the Easy Anti-Cheat client is available to this process.
bool AnticheatClientAvailable();

} // namespace genonline_eos
