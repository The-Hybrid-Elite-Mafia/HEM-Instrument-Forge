#include "SincResampler.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace pg
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kStopbandDb = 100.0;

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }
}

SincResampler::SincResampler (std::uint32_t inRate, std::uint32_t outRate)
{
    const std::uint64_t g = std::gcd<std::uint64_t> (inRate, outRate);
    in_ = inRate / g;
    out_ = outRate / g;
    if (in_ == out_) return;

    const double lower = (double) std::min (inRate, outRate);
    passHz_ = 0.4535 * lower;
    const double stopHz = 0.5 * lower;
    const double fc = 0.5 * (passHz_ + stopHz) / inRate;        // cutoff, cycles per INPUT sample
    const double df = (stopHz - passHz_) / inRate;              // transition width
    const double beta = 0.1102 * (kStopbandDb - 8.7);
    const int n = (int) std::ceil ((kStopbandDb - 7.95) / (2.285 * 2.0 * kPi * df));
    taps_ = ((n + 7) / 8) * 8;
    const double half = taps_ * 0.5;
    const double i0b = besselI0 (beta);

    // Row p = taps for fractional position p / kPhases; tap k multiplies input[i - (taps/2 - 1) + k].
    std::vector<double> rows ((size_t) (kPhases + 1) * (size_t) taps_);
    for (int p = 0; p <= kPhases; ++p)
    {
        const double phi = (double) p / kPhases;
        double sum = 0.0;
        for (int k = 0; k < taps_; ++k)
        {
            const double t = (k - (taps_ / 2 - 1)) - phi;
            const double x = 2.0 * fc * t;
            const double sinc = std::fabs (x) < 1e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = t / half;
            const double w = std::fabs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
            const double h = 2.0 * fc * sinc * w;
            rows[(size_t) p * (size_t) taps_ + (size_t) k] = h;
            sum += h;
        }
        for (int k = 0; k < taps_; ++k) rows[(size_t) p * (size_t) taps_ + (size_t) k] /= sum;   // unity gain at DC
    }
    table_.resize ((size_t) kPhases * (size_t) taps_);
    delta_.resize (table_.size());
    for (int p = 0; p < kPhases; ++p)
        for (int k = 0; k < taps_; ++k)
        {
            const double a = rows[(size_t) p * (size_t) taps_ + (size_t) k], b = rows[(size_t) (p + 1) * (size_t) taps_ + (size_t) k];
            table_[(size_t) p * (size_t) taps_ + (size_t) k] = (float) a;
            delta_[(size_t) p * (size_t) taps_ + (size_t) k] = (float) (b - a);
        }
}

std::uint64_t SincResampler::outputLength (std::uint64_t n) const noexcept
{
    return (n * out_ + in_ - 1) / in_;
}

bool SincResampler::process (const float* in, std::uint64_t n, float* out, const std::function<bool()>& keepGoing) const
{
    const std::uint64_t m = outputLength (n);
    if (in_ == out_) { std::memcpy (out, in, sizeof (float) * n); return true; }
    const int T = taps_, lead = T / 2 - 1;
    std::vector<float> coef ((size_t) T);
    for (std::uint64_t o = 0; o < m; ++o)
    {
        const std::uint64_t num = o * in_;
        const std::uint64_t i = num / out_;
        const double fp = (double) (num % out_) / (double) out_ * kPhases;
        const int p = std::min ((int) fp, kPhases - 1);
        const float t = (float) (fp - p);
        const float* a = &table_[(size_t) p * (size_t) T];
        const float* d = &delta_[(size_t) p * (size_t) T];
        const std::int64_t first = (std::int64_t) i - lead;
        float acc = 0.0f;
        if (first >= 0 && first + T <= (std::int64_t) n)
        {
            const float* x = in + first;
            for (int k = 0; k < T; ++k) acc += x[k] * (a[k] + t * d[k]);
        }
        else
        {
            for (int k = 0; k < T; ++k)
            {
                const std::int64_t j = first + k;
                if (j >= 0 && j < (std::int64_t) n) acc += in[j] * (a[k] + t * d[k]);
            }
        }
        out[o] = acc;
        if (keepGoing && (o & 0xFFFF) == 0xFFFF && ! keepGoing()) return false;
    }
    return true;
}
} // namespace pg
