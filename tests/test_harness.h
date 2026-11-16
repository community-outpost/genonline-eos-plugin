// SPDX-License-Identifier: MIT
// A minimal test registry. Each test executable runs the one case named on its command line, so CTest runs every case in its own process.
#pragma once

#include <cstdio>
#include <cstring>
#include <vector>

namespace testing
{

using CaseFn = void (*)();

struct Case
{
	const char* name;
	CaseFn fn;
};

inline std::vector<Case>& Registry()
{
	static std::vector<Case> cases;
	return cases;
}

inline int& FailureCount()
{
	static int failures = 0;
	return failures;
}

struct Registrar
{
	Registrar(const char* name, CaseFn fn)
	{
		Registry().push_back(Case{name, fn});
	}
};

inline void ReportFailure(const char* file, int line, const char* expression)
{
	std::fprintf(stderr, "%s(%d): check failed: %s\n", file, line, expression);
	++FailureCount();
}

// Runs the case named by the first argument, or prints every case name for the list flag.
inline int RunMain(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: %s <case> [arguments] | --list\n", argv[0]);
		return 2;
	}
	if (std::strcmp(argv[1], "--list") == 0)
	{
		for (const Case& c : Registry())
		{
			std::printf("%s\n", c.name);
		}
		return 0;
	}
	for (const Case& c : Registry())
	{
		if (std::strcmp(c.name, argv[1]) == 0)
		{
			c.fn();
			if (FailureCount() != 0)
			{
				std::fprintf(stderr, "%s: %d check(s) failed\n", c.name, FailureCount());
				return 1;
			}
			std::printf("%s: passed\n", c.name);
			return 0;
		}
	}
	std::fprintf(stderr, "unknown case %s\n", argv[1]);
	return 2;
}

} // namespace testing

#define TEST_CASE(name) \
	static void name(); \
	static testing::Registrar name##_registrar(#name, &name); \
	static void name()

// Like CHECK, and also ends the case when the expression is false.
#define REQUIRE(expression) \
	do \
	{ \
		if (!(expression)) \
		{ \
			testing::ReportFailure(__FILE__, __LINE__, #expression); \
			return; \
		} \
	} while (0)

#define CHECK(expression) \
	do \
	{ \
		if (!(expression)) \
		{ \
			testing::ReportFailure(__FILE__, __LINE__, #expression); \
		} \
	} while (0)
