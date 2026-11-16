// SPDX-License-Identifier: MIT
#include "plugin/eos_ids.h"

namespace genonline_eos
{

std::string ProductUserIdText(EOS_ProductUserId id)
{
	if (id == nullptr || !EOS_ProductUserId_IsValid(id))
	{
		return std::string();
	}
	char buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
	int32_t length = static_cast<int32_t>(sizeof(buffer));
	if (EOS_ProductUserId_ToString(id, buffer, &length) != EOS_EResult::EOS_Success)
	{
		return std::string();
	}
	return std::string(buffer);
}

} // namespace genonline_eos
