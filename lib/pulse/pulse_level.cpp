#include "pulse_level.h"

#include <algorithm>

namespace pulse {

namespace {

// The value `q` (0..1) of the way through sorted values, between neighbours.
float quantile(const std::vector<float>& sorted, float q) {
    const float pos = q * static_cast<float>(sorted.size() - 1);
    const size_t i = static_cast<size_t>(pos);
    if (i + 1 >= sorted.size()) return sorted.back();
    return sorted[i] + (pos - static_cast<float>(i)) * (sorted[i + 1] - sorted[i]);
}

}  // namespace

Level levelOf(const std::vector<float>& samples, int first, int end, size_t minCount) {
    first = std::max(first, 0);
    end = std::min(end, static_cast<int>(samples.size()));

    std::vector<float> pulse;
    for (int i = first; i < end; ++i) {
        if (samples[i] > 0.0f) pulse.push_back(samples[i]);
    }
    Level l;
    if (pulse.empty() || pulse.size() < minCount) return l;

    float sum = 0.0f;
    for (float b : pulse) sum += b;
    std::sort(pulse.begin(), pulse.end());
    l.valid = true;
    l.mean = sum / static_cast<float>(pulse.size());
    l.low = quantile(pulse, 0.1f);
    l.high = quantile(pulse, 0.9f);
    return l;
}

}  // namespace pulse
