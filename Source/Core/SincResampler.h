#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace pg
{
/**
    High-quality offline sample-rate conversion, used once when a file is loaded so the engine plays
    every stem at the device rate with no on-the-fly interpolation.

    Copied from HybridCore's media::Resampler (Steven's own framework, built by the other AI) with his
    OK: Kaiser-windowed sinc designed for 100 dB stopband, 1024-phase table with linear interpolation
    between phases, exact rational positions (no drift on long songs), zero delay. Passband edge is
    0.4535 x the lower rate (20 kHz for 44.1 kHz material). Plain C++ (the compiler vectorises the inner
    loop) instead of HybridCore's AVX2 intrinsics, so it runs on any PC.
    Loader threads only: it allocates.
*/
class SincResampler
{
public:
    SincResampler (std::uint32_t inRate, std::uint32_t outRate);

    std::uint64_t outputLength (std::uint64_t inFrames) const noexcept;
    bool passthrough() const noexcept { return in_ == out_; }
    int taps() const noexcept { return taps_; }
    double passbandHz() const noexcept { return passHz_; }

    /** Converts one channel. `out` must hold outputLength(n). Returns false if `keepGoing` said stop. */
    bool process (const float* in, std::uint64_t n, float* out, const std::function<bool()>& keepGoing = {}) const;

private:
    std::uint64_t in_ = 1, out_ = 1;   // reduced ratio
    int taps_ = 0;
    double passHz_ = 0.0;
    static constexpr int kPhases = 1024;
    std::vector<float> table_, delta_;
};
} // namespace pg
