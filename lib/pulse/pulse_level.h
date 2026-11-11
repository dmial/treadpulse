// Where the pulse has been lately: its average and the band its middle values
// stay in. The chart draws them as a corridor over the last few minutes, so a
// pulse wobbling under a line shows the level it wobbles around.
// Pure C++, no Arduino dependencies, so it can be unit-tested on the host.
#pragma once

#include <cstddef>
#include <vector>

namespace pulse {

struct Level {
    bool valid = false;
    float mean = 0.0f;
    float low = 0.0f;   // 10th percentile
    float high = 0.0f;  // 90th percentile
};

// The level of samples[first, end): bpm, equally spaced in time, 0 = no pulse
// (skipped). The band leaves out the lowest and highest tenth, so a short spike
// does not widen it. Not valid with fewer than `minCount` pulse samples (and
// never without any); the range is clamped to the samples.
Level levelOf(const std::vector<float>& samples, int first, int end, size_t minCount);

}  // namespace pulse
