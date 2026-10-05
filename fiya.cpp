#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <atomic>
#include <chrono>
#include <fiya-rdtsc-measure.h>
#include <fstream>
#include <memory>
#include <pthread.h>
#include <sched.h>

namespace std {

template <class T>
unique_ptr(T*) -> unique_ptr<T, default_delete<T>>;

} // namespace std

using measure_t = fiya::cyg_measure_rdtsc_t<void*>;

struct ThreadFlamegraphContext {
    measure_t::recorder_type recorder;
    measure_t measure;

    ThreadFlamegraphContext() :
        recorder { {}, nullptr, measure_t::measure_type::now() },
        measure { &recorder } {}
};

thread_local ThreadFlamegraphContext* tl_Ctx __attribute__((tls_model("initial-exec"))) = nullptr;
static std::atomic_uint32_t g_fileCounter { 0 };

fiya::scoping_interface_t<void*>* get_recorder() {
    auto current = tl_Ctx;
    return current ? &current->measure : nullptr;
}

[[gnu::visibility("default")]] void start_flamegraph();
void start_flamegraph() {
    auto current = tl_Ctx;
    if (current == nullptr) {
        current = new ThreadFlamegraphContext {};
    }
    tl_Ctx = current;

    auto current_cpu = sched_getcpu();
    if (current_cpu == -1) {
        current_cpu = 1;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(current_cpu, &cpuset);

    pthread_t thread = pthread_self();
    pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
}

[[gnu::visibility("default")]] void stop_flamegraph();
void stop_flamegraph() {
    auto current = tl_Ctx;
    if (current == nullptr) {
        return;
    }
    tl_Ctx = nullptr;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < CPU_SETSIZE; i++) {
        CPU_SET(i, &cpuset);
    }

    pthread_t thread = pthread_self();
    pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);

    std::unique_ptr ctx { current };
    std::ofstream os { "stacks-" + std::to_string(g_fileCounter.fetch_add(1)) + ".collapsed" };

    auto tsc_freq = fiya::measure_tsc_freq(std::chrono::milliseconds(10));
    auto print_label = [](std::ostream& os, void* l) -> void { os << l; };
    auto print_measure = [tsc_freq](std::ostream& os, measure_t::measure_type mes) -> void {
        os << mes.get_duration<double, std::micro>(tsc_freq).count();
    };
    ctx->recorder.to_collapsed_stacks(os, print_label, print_measure);
}

#include <fiya-cyg-overloads.cpp>
