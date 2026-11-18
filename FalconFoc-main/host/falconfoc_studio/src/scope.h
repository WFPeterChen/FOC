// Rolling time-series buffers for real-time plotting + a small radix-2 FFT.
#pragma once
#include <vector>
#include <cstddef>
#include <cmath>
#include <complex>

// Fixed-capacity ring of (t, value) for ImPlot. Stores contiguous arrays so
// ImPlot can read them directly via offset/stride.
struct Channel {
    std::vector<float> t;
    std::vector<float> y;
    size_t cap;
    size_t head = 0;   // next write index
    bool   full = false;
    explicit Channel(size_t capacity = 4096) : cap(capacity) { t.resize(cap); y.resize(cap); }
    void push(float ti, float yi) {
        t[head] = ti; y[head] = yi;
        head = (head + 1) % cap;
        if (head == 0) full = true;
    }
    size_t size() const { return full ? cap : head; }
    void clear() { head = 0; full = false; }
    // newest value
    float last() const { size_t n = size(); return n ? y[(head + cap - 1) % cap] : 0.0f; }
};

// Collect the most recent N samples of a channel into a flat, time-ordered array.
inline void latest(const Channel& c, size_t n, std::vector<float>& ts, std::vector<float>& ys) {
    size_t sz = c.size();
    if (n > sz) n = sz;
    ts.resize(n); ys.resize(n);
    // oldest of the n samples
    size_t start = (c.head + c.cap - n) % c.cap;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % c.cap;
        ts[i] = c.t[idx]; ys[i] = c.y[idx];
    }
}

// In-place iterative radix-2 FFT. n must be a power of 2.
inline void fft_pow2(std::vector<std::complex<float>>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; k++) {
                std::complex<float> u = a[i + k];
                std::complex<float> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Compute single-sided amplitude spectrum of ys (Hann windowed). Returns freq[Hz]
// and amplitude. fs = sample rate (Hz). Picks the largest pow2 <= ys.size().
inline void amplitude_spectrum(const std::vector<float>& ys, float fs,
                               std::vector<float>& freq, std::vector<float>& amp) {
    size_t n = 1; while (n * 2 <= ys.size()) n *= 2;
    freq.clear(); amp.clear();
    if (n < 8) return;
    std::vector<std::complex<float>> a(n);
    // mean-remove + Hann window
    float mean = 0; for (size_t i = 0; i < n; i++) mean += ys[ys.size() - n + i];
    mean /= (float)n;
    for (size_t i = 0; i < n; i++) {
        float w = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (n - 1)));
        a[i] = std::complex<float>((ys[ys.size() - n + i] - mean) * w, 0.0f);
    }
    fft_pow2(a);
    size_t half = n / 2;
    freq.resize(half); amp.resize(half);
    for (size_t i = 0; i < half; i++) {
        freq[i] = (float)i * fs / (float)n;
        amp[i] = 2.0f * std::abs(a[i]) / (float)n;
    }
}
