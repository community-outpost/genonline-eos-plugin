// SPDX-License-Identifier: MIT
#include "core/credentials.h"

#include <cstring>

namespace genonline_eos
{

namespace
{

struct FieldView
{
	uint32_t bit;
	const char* name;
	const char* value;
};

constexpr size_t kFieldCount = 5;

void ListFields(const Credentials& credentials, FieldView (&fields)[kFieldCount])
{
	fields[0] = FieldView{kCredentialProductId, "product id", credentials.productId};
	fields[1] = FieldView{kCredentialSandboxId, "sandbox id", credentials.sandboxId};
	fields[2] = FieldView{kCredentialDeploymentId, "deployment id", credentials.deploymentId};
	fields[3] = FieldView{kCredentialClientId, "client id", credentials.clientId};
	fields[4] = FieldView{kCredentialClientSecret, "client secret", credentials.clientSecret};
}

} // namespace

uint32_t MissingCredentials(const Credentials& credentials)
{
	FieldView fields[kFieldCount];
	ListFields(credentials, fields);
	uint32_t missing = 0;
	for (const FieldView& field : fields)
	{
		if (field.value == nullptr || field.value[0] == '\0')
		{
			missing |= field.bit;
		}
	}
	return missing;
}

std::string DescribeCredentialFields(uint32_t bits)
{
	FieldView fields[kFieldCount];
	ListFields(Credentials{}, fields);
	std::string text;
	for (const FieldView& field : fields)
	{
		if ((bits & field.bit) == 0)
		{
			continue;
		}
		if (!text.empty())
		{
			text += ", ";
		}
		text += field.name;
	}
	return text;
}

uint32_t OverlongCredentials(const Credentials& credentials)
{
	FieldView fields[kFieldCount];
	ListFields(credentials, fields);
	uint32_t overlong = 0;
	for (const FieldView& field : fields)
	{
		if (field.value != nullptr && std::strlen(field.value) > kCredentialMaxLength)
		{
			overlong |= field.bit;
		}
	}
	return overlong;
}

} // namespace genonline_eos
