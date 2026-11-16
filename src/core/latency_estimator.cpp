// SPDX-License-Identifier: MIT
#include "core/latency_estimator.h"

#include <cmath>

namespace genonline_eos
{

bool LatencyEstimator::AddSample(double roundTripMs)
{
	if (!(roundTripMs >= 0.0) || roundTripMs > kOutlierMs)
	{
		return false;
	}
	if (!m_hasSample)
	{
		m_smoothed = roundTripMs;
		m_hasSample = true;
	}
	else
	{
		m_smoothed += kSmoothing * (roundTripMs - m_smoothed);
	}
	return true;
}

int32_t LatencyEstimator::Milliseconds() const
{
	if (!m_hasSample)
	{
		return 0;
	}
	const int32_t rounded = static_cast<int32_t>(std::lround(m_smoothed));
	return rounded < 1 ? 1 : rounded;
}

} // namespace genonline_eos
