#pragma once

#include <array>
#include <chrono>
#include <cstdint>

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

} // namespace details

enum class RdtscImpl {
    INTR_RDTSCP,
    INTR_RDTSC,
    ASM_RDTSCP,
    ASM_RDTSC,
};

constexpr RdtscImpl CounterReadStrategy = RdtscImpl::INTR_RDTSCP;

template <RdtscImpl Impl>
std::uint64_t rdtsc_start() noexcept;

template <>
std::uint64_t rdtsc_start<RdtscImpl::INTR_RDTSCP>() noexcept {
    details::compiler_barrier();
    unsigned aux;
    return __rdtscp(&aux);
}

template <>
std::uint64_t rdtsc_start<RdtscImpl::INTR_RDTSC>() noexcept {
    details::compiler_barrier();
    _mm_lfence();
    return __rdtsc();
}

template <>
std::uint64_t rdtsc_start<RdtscImpl::ASM_RDTSCP>() noexcept {
    uint32_t lo, hi, aux;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

template <>
std::uint64_t rdtsc_start<RdtscImpl::ASM_RDTSC>() noexcept {
    uint32_t lo, hi;
    asm volatile("lfence\n\t"
                 "rdtsc"
                 : "=a"(lo), "=d"(hi)
                 :
                 : "memory");
    return ((uint64_t)hi << 32) | lo;
}

template <RdtscImpl Impl>
std::uint64_t rdtsc_end() noexcept;

template <>
std::uint64_t rdtsc_end<RdtscImpl::INTR_RDTSCP>() noexcept {
    unsigned aux;
    auto t = __rdtscp(&aux);
    details::compiler_barrier();
    return t;
}

template <>
std::uint64_t rdtsc_end<RdtscImpl::INTR_RDTSC>() noexcept {
    _mm_lfence();
    auto t = __rdtsc();
    details::compiler_barrier();
    return t;
}

template <>
std::uint64_t rdtsc_end<RdtscImpl::ASM_RDTSCP>() noexcept {
    uint32_t lo, hi, aux;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) : : "memory");
    return ((uint64_t)hi << 32) | lo;
}

template <>
std::uint64_t rdtsc_end<RdtscImpl::ASM_RDTSC>() noexcept {
    uint32_t lo, hi;
    asm volatile("lfence\n\t"
                 "rdtsc"
                 : "=a"(lo), "=d"(hi)
                 :
                 : "memory");
    return ((uint64_t)hi << 32) | lo;
}

template <class Repetition, class Period>
double measure_tsc_freq(std::chrono::duration<Repetition, Period> one_interval) {
    using clock = std::chrono::steady_clock;
    constexpr int N = 5;
    std::array<double, N> samples {};

    for (int i = 0; i < N; ++i) {
        auto t0 = clock::now();

        auto c0 = rdtsc_start<CounterReadStrategy>();
        while (clock::now() - t0 < one_interval) {
        }
        auto c1 = rdtsc_end<CounterReadStrategy>();

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

    static rdtsc_value_t now() {
        rdtsc_value_t result {};
        result.m_start = rdtsc_start<CounterReadStrategy>();
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

template <typename LabelType>
class cyg_measure_rdtsc_t final : public scoping_interface_t<LabelType> {
public:
    using measure_type = rdtsc_value_t;
    using recorder_type = recorder_t<LabelType, measure_type>;

    static const measure_type zero;

    cyg_measure_rdtsc_t(recorder_type* recorder) :
        m_recorder { recorder } {}

    void begin_scope(const LabelType& label) override {
        measure_type& prev_measure = m_recorder->cnt();
        prev_measure.m_duration += rdtsc_end<CounterReadStrategy>() - prev_measure.m_start;
        m_recorder->begin_scope(label);
        m_recorder->cnt().m_start = rdtsc_start<CounterReadStrategy>();
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
        prev_measure.m_duration += rdtsc_end<CounterReadStrategy>() - prev_measure.m_start;
        m_recorder->end_scope(args...);
        m_recorder->cnt().m_start = rdtsc_start<CounterReadStrategy>();
    }
private:
    recorder_type* m_recorder;
};

template <typename LabelType>
const cyg_measure_rdtsc_t<LabelType>::measure_type cyg_measure_rdtsc_t<LabelType>::zero = {};

} // namespace fiya
