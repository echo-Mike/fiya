#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <iterator>
#include <vector>

#include <iostream>

#include <cxxabi.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>

#include <fiya/connectors/fiya-connector-interface.h>

#define FIYA_RECORDER_TYPE measure_t
#include <fiya-rdtsc-measure.h>

namespace std {

template <class T>
unique_ptr(T*) -> unique_ptr<T, default_delete<T>>;

} // namespace std

using measure_t = fiya::cyg_measure_rdtsc_t<void*>;

class FunctionResolveCache {
public:
    FunctionResolveCache(std::size_t storage_size) :
        m_storage(),
        m_resolve_buffer(static_cast<char*>(std::malloc(512)), std::free),
        m_resolve_buffer_size { 512 } {
        m_storage.reserve(storage_size);
    }

    const char* resolve(void* func) noexcept {
        if (auto it = m_cache.find(func); it != end(m_cache)) {
            return m_storage.data() + it->second;
        }
        Dl_info info {};
        if (dladdr(func, &info) && info.dli_sname) {
            int status = 0;
            auto prev_size = m_resolve_buffer_size;
            const char* name = nullptr;
            char* dem = abi::__cxa_demangle(info.dli_sname, m_resolve_buffer.get(), &m_resolve_buffer_size, &status);
            if (status == 0 && dem) {
                if (m_resolve_buffer_size != prev_size) {
                    m_resolve_buffer.release();
                    m_resolve_buffer.reset(dem);
                }
                name = dem;
            } else {
                name = info.dli_sname;
            }
            auto name_length = std::strlen(name);
            auto offset = m_storage.size();
            std::copy(name, name + name_length + 1, std::back_inserter(m_storage));
            m_cache.emplace(std::make_pair(func, offset));
            return m_storage.data() + offset;
        } else {
            std::snprintf(m_resolve_buffer.get(), m_resolve_buffer_size, "%p", func);
            return m_resolve_buffer.get();
        }
    }
private:
    std::vector<char> m_storage;
    std::unique_ptr<char, decltype(&std::free)> m_resolve_buffer;
    std::size_t m_resolve_buffer_size;
    std::unordered_map<void*, std::size_t> m_cache;
};

struct ThreadFlamegraphContext {
    measure_t::recorder_type recorder;
    measure_t measure;

    ThreadFlamegraphContext() :
        recorder { {}, nullptr, measure_t::measure_type::now() },
        measure { &recorder } {}
};

thread_local ThreadFlamegraphContext* tl_Ctx __attribute__((tls_model("initial-exec"))) = nullptr;
static std::atomic_uint32_t g_fileCounter { 0 };
static FunctionResolveCache g_resolveCache { 2 * 1024 * 1024 };

FIYA_RECORDER_TYPE* get_recorder() {
    auto current = tl_Ctx;
    return current ? &current->measure : nullptr;
}

namespace fiya {
namespace connectors {

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
    auto print_label = [](std::ostream& os, void* l) -> void { os << (l ? g_resolveCache.resolve(l) : "root"); };
    auto print_measure = [tsc_freq](std::ostream& os, measure_t::measure_type mes) -> void {
        os << mes.get_duration<double, std::micro>(tsc_freq).count();
    };
    ctx->recorder.to_collapsed_stacks(os, print_label, print_measure);
}

} // namespace connectors
} // namespace fiya

#include <fiya-cyg-overloads.cpp>
