#pragma once

#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <random>
#include <ratio>
#include <type_traits>

#include "fiya-recorder.h"

#if defined(__x86_64__) || defined(__amd64__) || defined(__i386__) || defined(__i386)
#include <x86intrin.h>
#else
#error "Unsupported CPU architecture"
#endif

namespace fiya {

namespace details {

inline void compiler_barrier() noexcept {
    asm volatile("" ::: "memory");
}

template <typename T>
struct is_chance_ratio : std::false_type {};

template <std::intmax_t Den>
struct is_chance_ratio<std::ratio<1, Den>> : std::true_type {};

template <typename T>
inline constexpr bool is_chance_ratio_v = is_chance_ratio<T>::value;

template <typename T>
concept ChanceRatio = is_chance_ratio_v<T>;

} // namespace details

inline std::uint64_t rdtsc_start() noexcept {
    details::compiler_barrier();
    unsigned aux;
    return __rdtscp(&aux);
}

inline std::uint64_t rdtsc_end() noexcept {
    unsigned aux;
    auto t = __rdtscp(&aux);
    details::compiler_barrier();
    return t;
}

template <class Repetition, class Period>
double measure_tsc_freq(std::chrono::duration<Repetition, Period> one_interval) {
    using clock = std::chrono::steady_clock;
    constexpr int N = 5;
    std::array<double, N> samples {};

    for (int i = 0; i < N; ++i) {
        auto t0 = clock::now();

        auto c0 = rdtsc_start();
        while (clock::now() - t0 < one_interval) {
        }
        auto c1 = rdtsc_end();

        auto t1 = clock::now();

        double seconds = std::chrono::duration<double>(t1 - t0).count();
        samples[i] = (c1 - c0) / seconds;
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

class rdtsc_value_t {
public:
    rdtsc_value_t() = default;

    template <class Repetition, class Period>
    auto get_duration(double tsc_freq) noexcept -> std::chrono::duration<Repetition, Period> {
        return std::chrono::duration_cast<std::chrono::duration<Repetition, Period>>(
            std::chrono::duration<double>(m_duration * 1.0 / tsc_freq));
    }

    static rdtsc_value_t now() noexcept {
        rdtsc_value_t result {};
        result.m_start = rdtsc_start();
        result.m_duration = 0;
        return result;
    }
private:
    template <typename T>
    friend class cyg_measure_rdtsc_t;
private:
    std::uint64_t m_start;
    std::uint64_t m_duration;
};

class Prng {
public:
    using result_type = std::uint32_t;

    constexpr Prng(result_type seed) noexcept :
        m_state { seed ? seed : 1 } {}

    constexpr result_type operator()() noexcept {
        return next();
    }

    static constexpr result_type min() noexcept {
        return 0;
    }
    static constexpr result_type max() noexcept {
        return UINT32_MAX;
    }

    constexpr result_type next() noexcept {
        result_type x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        m_state = x;
        return x;
    }

    constexpr float nextf() noexcept {
        return ((next() >> 8) + 0.5f) * (1.0f / 16777216.0f);
    }
private:
    result_type m_state;
};

// See: https://en.wikipedia.org/wiki/Reservoir_sampling#Optimizing_running_time_by_skipping_elements
template <std::size_t N>
class AlgorithmLSampler {
public:
    AlgorithmLSampler(std::uint32_t seed) :
        m_nextSample { 0 },
        m_rng { seed },
        m_W { 1.0f } {}

    bool need_sample(std::uint32_t sample) noexcept {
        if (sample <= N) {
            if (sample == N) {
                compute_next_W();
                compute_next_sample(sample);
            }
            return true;
        }
        if (sample < m_nextSample) {
            return false;
        }
        compute_next_W();
        compute_next_sample(sample);
        return true;
    }

    std::size_t accept_sample(std::uint32_t sample) noexcept {
        if (sample <= N) {
            return sample - 1;
        }
        std::uniform_int_distribution<std::size_t> pick(0, N - 1);
        return pick(m_rng);
    }
private:
    void compute_next_sample(std::uint32_t sample) noexcept {
        m_nextSample = sample + static_cast<std::uint32_t>(std::floor(std::log(get_open_random()) / std::log1pf(-m_W))) + 1;
    }

    void compute_next_W() noexcept {
        m_W = m_W * std::exp(std::log(get_open_random()) / (N * 1.0f));
    }

    float get_open_random() noexcept {
        // (0, 1) interval
        return 1.0f - m_rng.nextf();
    }
private:
    std::uint32_t m_nextSample;
    Prng m_rng;
    float m_W;
};

template <class T, std::size_t N, class Sampler = AlgorithmLSampler<N>>
class ReservoirSampler : private Sampler {
public:
    static constexpr std::size_t SampleSize = N;

    ReservoirSampler(Sampler sampler) :
        Sampler(sampler),
        m_sample { 0 },
        m_data {} {}

    std::uint32_t sample_id() const noexcept {
        return m_sample;
    }

    bool need_sample() noexcept {
        ++m_sample;
        return static_cast<Sampler&>(*this).need_sample(m_sample);
    }

    void accept_sample(T value) noexcept {
        m_data[static_cast<Sampler&>(*this).accept_sample(m_sample)] = value;
    }

    const std::array<T, N>& get_samples() const noexcept {
        return m_data;
    }
private:
    std::uint32_t m_sample;
    std::array<T, N> m_data;
};

template <class T, details::ChanceRatio R>
class StratifiedSampler {
public:
    StratifiedSampler() noexcept :
        m_sample { 0 },
        m_offset { 0 },
        m_sum { 0 } {}

    void reinit(Prng& rng) {
        for (auto i { 0 }; i < 7; ++i) {
            rng();
        }
        m_sample = 0;
        m_offset = rng() % R::den;
        m_sum[0] = 0;
    }

    std::uint32_t sample_id() const noexcept {
        return m_sample;
    }

    bool need_sample() noexcept {
        return !m_sample || ((m_sample++) % R::den) == m_offset;
    }

    void accept_sample(T value) noexcept {
        m_sum[0] += value;
    }

    const std::array<T, 1>& get_samples() const noexcept {
        return m_sum;
    }
private:
    std::uint32_t m_sample;
    std::uint32_t m_offset;
    std::array<T, 1> m_sum;
};

template <double PBase, double PMin, double PMax>
struct SampleProbabilities {
    static constexpr double Base = PBase;
    static constexpr double Min = PMin;
    static constexpr double Max = PMax;
};

namespace details {

template <typename T>
struct is_probs : std::false_type {};

template <double PBase, double PMin, double PMax>
struct is_probs<SampleProbabilities<PBase, PMin, PMax>> : std::true_type {};

template <typename T>
inline constexpr bool is_probs_v = is_probs<T>::value;

template <typename T>
concept Probs = is_probs_v<T>;

} // namespace details

template <std::size_t EpochCount, std::size_t EpochSize, details::Probs P>
class VarianceGuidedMean {
    // Minimum amount of samples to take before starting to refine current epoch's probability
    static constexpr std::size_t MinSamples = 5;

    static_assert(EpochCount >= 2, "need >= 2 epochs for forgetting");
    static_assert(EpochSize >= MinSamples + 1);
    static_assert(0.0 < P::Min, "P::Min must be > 0 (used as a divisor)");
    static_assert(P::Min <= P::Base, "P::Min must be <= P::Base");
    static_assert(P::Base <= P::Max, "P::Base must be <= P::Max");
public:
    VarianceGuidedMean(std::uint32_t seed) :
        m_sample { 0 },
        m_rng(seed) {
        for (auto& s : m_strata) {
            s.prob = P::Base;
        }
    }

    std::uint32_t sample_id() const noexcept {
        return m_sample;
    }

    bool need_sample() noexcept {
        ++m_sample;

        const std::size_t previous_epoch = (m_sample >= 2) ? (m_sample - 2) / EpochSize : EpochCount - 1;
        const std::size_t epoch = (m_sample - 1) / EpochSize;
        const std::size_t slot = epoch % EpochCount;

        if (previous_epoch != epoch) {
            m_strata[slot].clear(m_strata[previous_epoch % EpochCount].prob);
        }

        return m_sample == 1 || m_rng.nextf() < m_strata[slot].prob;
    }

    void accept_sample(double value) noexcept {
        const auto epoch = (m_sample - 1) / EpochSize;
        const std::size_t slot = epoch % EpochCount;
        Stratum& st = m_strata[slot];
        // Horvitz–Thompson sample weight
        const double w = 1.0 / st.prob;
        st.add(value, w);

        if (MinSamples <= st.n) {
            const double avg_sigma = average_sigma_excluding(slot);
            if (avg_sigma > 0) {
                // Update stratum's sampling probability using computed variance
                const double sigma = std::sqrt(st.variance());
                st.prob = std::clamp(P::Base * (sigma / std::max(avg_sigma, 1e-9)), P::Min, P::Max);
            }
        }
    }

    double mean() const {
        double swx = 0, sw = 0;
        for (const auto& s : m_strata) {
            swx += s.sum_wx;
            sw += s.sum_w;
        }
        return sw > 0 ? swx / sw : 0.0;
    }
private:
    struct Stratum {
        /// Σ wᵢ xᵢ
        double sum_wx = 0;
        /// Σ wᵢ
        double sum_w = 0;
        /// Σ wᵢ xᵢ²
        double sum_wx2 = 0;
        /// current sampling probability
        double prob = 0.1;
        /// samples in this strata
        std::uint64_t n = 0;

        void clear(double p) {
            sum_wx = sum_w = sum_wx2 = 0;
            n = 0;
            prob = p;
        }

        void add(double value, double weight) {
            sum_wx += weight * value;
            sum_w += weight;
            sum_wx2 += weight * value * value;
            ++n;
        }

        double variance() const {
            if (sum_w <= 0)
                return 0;
            double mu = sum_wx / sum_w;
            return std::max(0.0, sum_wx2 / sum_w - mu * mu);
        }
    };

    /// Average standard deviation excluding specified slot
    double average_sigma_excluding(std::size_t slot) const {
        double s = 0;
        int n = 0;
        for (std::size_t i { 1 }; i < EpochCount; ++i) {
            const Stratum& st = m_strata[(slot + i) % EpochCount];
            if (st.sum_w > 0) {
                s += std::sqrt(st.variance());
                ++n;
            }
        }
        if (n) {
            return s / n;
        }
        const Stratum& prev = m_strata[(slot + EpochCount - 1) % EpochCount];
        return prev.sum_w > 0 ? std::sqrt(prev.variance()) : -1.0;
    }
private:
    std::uint32_t m_sample;
    Prng m_rng;
    std::array<Stratum, EpochCount> m_strata;
};

template <typename LabelType>
class cyg_measure_rdtsc_t final : public scoping_interface_t<LabelType> {
public:
    using measure_type = rdtsc_value_t;
    using recorder_type = recorder_t<LabelType, measure_type>;

    static const measure_type zero;

    cyg_measure_rdtsc_t(recorder_type* recorder) :
        m_recorder { recorder },
        m_begin_sampler(rdtsc_start()),
        m_end_sampler(rdtsc_start()) {}

    void begin_scope(const LabelType& label) override {
        measure_type& prev_measure = m_recorder->cnt();
        auto end_value = rdtsc_end();
        prev_measure.m_duration += end_value - prev_measure.m_start;
        m_recorder->begin_scope(label);
        rdtsc_t value = m_begin_estimate;
        if (m_begin_sampler.need_sample()) {
            value = rdtsc_start() - end_value;
            m_begin_sampler.accept_sample(value);
            value = m_begin_estimate = m_begin_sampler.mean();
        }
        m_recorder->cnt().m_start = end_value + value;
    }

    void end_scope() override {
        end_scope_local();
    }

    void end_scope(const LabelType& label) override {
        end_scope_local(label);
    }

    bool recorder_internal_running() const override {
        return m_recorder->recorder_internal_running();
    }
private:
    template <typename... Args>
    void end_scope_local(Args... args) {
        measure_type& prev_measure = m_recorder->cnt();
        auto end_value = rdtsc_end();
        prev_measure.m_duration += end_value - prev_measure.m_start;
        m_recorder->end_scope(args...);
        rdtsc_t value = m_end_estimate;
        if (m_end_sampler.need_sample()) {
            value = rdtsc_start() - end_value;
            m_end_sampler.accept_sample(value);
            value = m_end_estimate = m_end_sampler.mean();
        }
        m_recorder->cnt().m_start = end_value + value;
    }

    using rdtsc_t = decltype(rdtsc_start());

    using Probabilities = SampleProbabilities<0.001, 0.0001, 0.01>;
private:
    recorder_type* const m_recorder;
    VarianceGuidedMean<4, 1 << 8, Probabilities> m_begin_sampler;
    rdtsc_t m_begin_estimate { 0 };
    VarianceGuidedMean<4, 1 << 8, Probabilities> m_end_sampler;
    rdtsc_t m_end_estimate { 0 };
};

template <typename LabelType>
const cyg_measure_rdtsc_t<LabelType>::measure_type cyg_measure_rdtsc_t<LabelType>::zero = {};

} // namespace fiya
