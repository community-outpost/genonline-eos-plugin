// SPDX-License-Identifier: MIT
// The compiled EOS credentials and their checks. Pure logic without EOS.
#pragma once

#include <cstdint>
#include <string>

namespace genonline_eos
{

struct Credentials
{
	const char* productId;
	const char* sandboxId;
	const char* deploymentId;
	const char* clientId;
	const char* clientSecret;
};

// Bits of MissingCredentials.
enum CredentialField : uint32_t
{
	kCredentialProductId = 1u << 0,
	kCredentialSandboxId = 1u << 1,
	kCredentialDeploymentId = 1u << 2,
	kCredentialClientId = 1u << 3,
	kCredentialClientSecret = 1u << 4,
};

// Returns the CredentialField bits of every null or empty credential.
uint32_t MissingCredentials(const Credentials& credentials);

// Returns the field names of the missing bits, for example "product id, client secret". Never holds a credential value.
std::string DescribeCredentialFields(uint32_t fields);

// Longest value EOS accepts for each credential, without the NUL.
constexpr size_t kCredentialMaxLength = 64;

// Returns the CredentialField bits of every credential longer than kCredentialMaxLength.
uint32_t OverlongCredentials(const Credentials& credentials);

} // namespace genonline_eos
