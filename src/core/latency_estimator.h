// SPDX-License-Identifier: MIT
// Smooths round trip samples of the plugin probes into one latency value. Pure logic without EOS.
#pragma once

#include <cstdint>

namespace genonline_eos
{

class LatencyEstimator
{
public:
	// Samples above this value are ignored.
	static constexpr double kOutlierMs = 5000.0;

	// Weight of a new sample in the smoothed value.
	static constexpr double kSmoothing = 0.125;

	// Adds one round trip sample in milliseconds. Returns false when the sample is ignored.
	bool AddSample(double roundTripMs);

	// The smoothed round trip in whole milliseconds, 0 until the first sample and at least 1 after it.
	int32_t Milliseconds() const;

	bool HasSample() const
	{
		return m_hasSample;
	}

private:
	double m_smoothed = 0.0;
	bool m_hasSample = false;
};

} // namespace genonline_eos
