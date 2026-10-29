#pragma once

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace pg::dsp
{
/** In-place radix-2 complex FFT. Tables are built in prepare(); perform() never allocates. */
class Fft
{
public:
    void prepare (int size)
    {
        n = size;
        bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.assign ((size_t) n, 0);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * std::numbers::pi * i / n));
    }

    int size() const noexcept { return n; }

    /** Forward (inverse = false) or inverse (unscaled) transform of n points. */
    void perform (std::complex<float>* x, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const auto a = x[i + j], b = x[i + j + half] * w;
                    x[i + j] = a + b;
                    x[i + j + half] = a - b;
                }
        }
    }

private:
    int n = 0, bits = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};
} // namespace pg::dsp
