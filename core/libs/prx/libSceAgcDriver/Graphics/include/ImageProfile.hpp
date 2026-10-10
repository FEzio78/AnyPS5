#pragma once

#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace AgcDriver::Graphics::ImageProfile {

using Clock = std::chrono::steady_clock;
enum Phase { Images, Revalidate, Sampled, Storage, Fast, Lookup, CacheLock, Source, Collect, Keys, Flush, Compare, Read, TextureCreate, StorageLookup, StorageRefresh, StorageCreate, StorageUpload, StorageCpuUpload, Baseline, MemoryRead, MemoryCompare, StorageWriteBack, StandaloneWait, HookSync, BoundKeeps, Count };
inline constexpr std::array<const char*, Count> Names{"B images", "revalidate", "sampled element", "storage element", "fast proof", "sampled lookup", "cache lock", "storage source", "collect writes", "dcc keys", "pending flush", "snapshot compare", "snapshot read", "sampled create/upload", "storage lookup", "storage refresh", "storage create", "storage upload", "storage CPU upload", "capture baseline", "guest read", "guest compare", "storage writeback", "standalone submit/wait", "hook sync", "bound kept bytes"};

struct Surface {
    std::uint64_t address = 0, bytes = 0;
    std::uint32_t format = 0, width = 0, height = 0, word0 = 0, word3 = 0;
};
struct Totals {
    std::uint64_t calls = 0, bytes = 0;
    double inclusive = 0, exclusive = 0, maximum = 0;
    Surface slowest{};
    const char* reason = "none";
    void Add(double elapsed, double children, Surface surface, const char* why) {
        ++calls;
        bytes += surface.bytes;
        inclusive += elapsed;
        exclusive += std::max(0.0, elapsed - children);
        if (elapsed >= maximum) {
            maximum = elapsed;
            slowest = surface;
            reason = why;
        }
    }
    void Merge(const Totals& other) {
        calls += other.calls;
        bytes += other.bytes;
        inclusive += other.inclusive;
        exclusive += other.exclusive;
        if (other.calls != 0 && other.maximum >= maximum) {
            maximum = other.maximum;
            slowest = other.slowest;
            reason = other.reason;
        }
    }
};
inline bool Enabled() {
    static const bool enabled = [] { const auto* value = std::getenv("APS5_PROFILE_IMAGES"); return value != nullptr && value[0] == '1' && value[1] == 0; }();
    return enabled;
}
struct Window {
    std::mutex mutex;
    std::array<Totals, Count> totals{};
    Clock::time_point since = Clock::now();
};
inline Window& Shared() { static Window window; return window; }
inline thread_local std::array<Totals, Count> pending{};
class Scope;
inline thread_local Scope* active = nullptr;

inline void Publish() {
    auto& window = Shared();
    std::lock_guard lock(window.mutex);
    for (std::size_t i = 0; i < Count; ++i) window.totals[i].Merge(std::exchange(pending[i], {}));
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - window.since).count();
    if (seconds < 10) return;
    window.since = now;
    for (std::size_t i = 0; i < Count; ++i) {
        const auto totals = std::exchange(window.totals[i], {});
        if (totals.calls == 0) continue;
        const auto& s = totals.slowest;
        ProfilePrint_nid_no_patch("[images] (%.2f s) %s: count=%llu inclusive=%.3fms exclusive=%.3fms touched=%.2fMiB max=%.3fms T#base=0x%llx format=%u size=%ux%u bytes=%llu word0=%08x word3=%08x reason=%s\n", seconds, Names[i], static_cast<unsigned long long>(totals.calls), totals.inclusive, totals.exclusive, totals.bytes / 1048576.0, totals.maximum, static_cast<unsigned long long>(s.address), s.format, s.width, s.height, static_cast<unsigned long long>(s.bytes), s.word0, s.word3, totals.reason);
    }
}

class Scope {
public:
    explicit Scope(Phase phase, const char* reason = "none", bool root = false) : phase(phase), reason(reason) {
        if (!Enabled() || (!root && active == nullptr)) return;
        parent = active;
        if (parent != nullptr) surface = parent->surface;
        active = this;
        start = Clock::now();
        engaged = true;
    }
    ~Scope() {
        if (!engaged) return;
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        active = parent;
        pending[phase].Add(elapsed, children, surface, reason);
        if (parent != nullptr) parent->children += elapsed;
        else Publish();
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    void Describe(Surface value) { surface = value; }
    void Reason(const char* value) { reason = value; }
private:
    Phase phase;
    const char* reason;
    Surface surface{};
    Scope* parent = nullptr;
    Clock::time_point start{};
    double children = 0;
    bool engaged = false;
};

template<typename F> decltype(auto) Measure(Phase phase, const char* reason, F&& call) {
    Scope scope(phase, reason);
    return std::forward<F>(call)();
}

}
