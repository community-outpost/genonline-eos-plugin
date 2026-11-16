// SPDX-License-Identifier: MIT
// Conversions between EOS product user ids and their strings.
#pragma once

#include <eos_common.h>

#include <string>

namespace genonline_eos
{

// The string form of a product user id, or an empty string for a null or invalid id.
std::string ProductUserIdText(EOS_ProductUserId id);

} // namespace genonline_eos
