#pragma once

#include "prx/libSceAgcDriver/Execution/include/GpuDescriptorProfileData.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace AgcDriver::GpuDescriptorProfile {

using Clock = std::chrono::steady_clock;
inline bool Enabled() {
    static const bool enabled = [] { const auto* value = std::getenv("APS5_GPU_DESC_PROFILE"); return value != nullptr && value[0] == '1' && value[1] == 0; }();
    return enabled;
}

inline double Microseconds(Clock::time_point from) { return std::chrono::duration<double, std::micro>(Clock::now() - from).count(); }
struct Window {
    std::uint64_t id = 0, firstFrame = 0, lastFrame = 0, pendingCpu = 0, pendingGpu = 0, droppedGpu = 0;
    Clock::time_point begin{}, end{};
    std::map<std::pair<std::uint32_t, ClassKey>, Row> rows;
    std::map<std::pair<std::uint32_t, std::string>, Counter> gpu;
    double bookkeepingUs = 0;
};
struct State {
    std::mutex mutex;
    std::uint64_t frames = 0, serial = 0, planSerial = 0;
    std::shared_ptr<Window> current;
    std::vector<std::shared_ptr<Window>> sealed;
    std::map<std::weak_ptr<const void>, std::uint64_t, std::owner_less<std::weak_ptr<const void>>> plans;
};
inline State& Shared() { static State state; return state; }
inline std::size_t TopN() {
    static const auto count = [] { const auto* value = std::getenv("APS5_GPU_DESC_PROFILE_TOP"); return value == nullptr ? 8ull : std::clamp(std::strtoull(value, nullptr, 10), 1ull, 64ull); }();
    return static_cast<std::size_t>(count);
}

inline void Print(const Window& window) {
    const auto frames = window.lastFrame - window.firstFrame;
    const auto seconds = std::chrono::duration<double>(window.end - window.begin).count();
    ProfilePrint_nid_no_patch("[gpu-desc-window] w=%llu seconds=%.6f flips=%llu..%llu frames=%llu pending-cpu=%llu pending-gpu=%llu dropped-gpu=%llu bookkeeping-us=%.3f normalization=count,total-us,us/dispatch,us/frame; recording-window GPU results resolved asynchronously; CPU phases inclusive\n", static_cast<unsigned long long>(window.id), seconds, static_cast<unsigned long long>(window.firstFrame), static_cast<unsigned long long>(window.lastFrame), static_cast<unsigned long long>(frames), static_cast<unsigned long long>(window.pendingCpu), static_cast<unsigned long long>(window.pendingGpu), static_cast<unsigned long long>(window.droppedGpu), window.bookkeepingUs);
    std::map<std::uint32_t, std::vector<std::pair<ClassKey, const Row*>>> queues;
    for (const auto& [key, row] : window.rows) queues[key.first].emplace_back(key.second, &row);
    char text[320];
    for (auto& [queue, ranked] : queues) {
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second->TotalCost() > b.second->TotalCost() || (a.second->TotalCost() == b.second->TotalCost() && a.first < b.first); });
        std::uint64_t dispatches = 0, attempts = 0;
        for (const auto& [key, row] : ranked) { dispatches += row->dispatches; attempts += row->attempts; }
        ProfilePrint_nid_no_patch("[gpu-desc-queue] w=%llu q=0x%x classes=%zu dispatches=%llu attempts=%llu dispatches/frame=%.3f top=%zu rank=cpu-inclusive+gpu-us\n", static_cast<unsigned long long>(window.id), queue, ranked.size(), static_cast<unsigned long long>(dispatches), static_cast<unsigned long long>(attempts), frames == 0 ? 0 : static_cast<double>(dispatches) / frames, std::min(TopN(), ranked.size()));
        for (std::size_t rank = 0; rank < std::min(TopN(), ranked.size()); ++rank) {
            const auto& [key, pointer] = ranked[rank];
            const auto& row = *pointer;
            std::string metrics;
            const auto append = [&](const char* name, const Counter& counter) {
                std::snprintf(text, sizeof(text), " %s=%llu,%.3f,%.3f,%.3f", name, static_cast<unsigned long long>(counter.count), counter.us, counter.PerDispatch(row.dispatches), counter.PerFrame(frames));
                metrics += text;
            };
            for (std::size_t i = 0; i < MetricCount; ++i) append(MetricNames[i], row.metrics[i]);
            append("gpu", row.gpu);
            ProfilePrint_nid_no_patch("[gpu-desc-class] w=%llu q=0x%x rank=%zu program=0x%llx code=%016llx plan=%llu dispatches=%llu attempts=%llu dispatches/frame=%.3f cpu-us=%.3f cpu-us/dispatch=%.3f cpu-us/frame=%.3f t1-share=%.4f%s\n", static_cast<unsigned long long>(window.id), queue, rank + 1, static_cast<unsigned long long>(key.program), static_cast<unsigned long long>(key.code), static_cast<unsigned long long>(key.plan), static_cast<unsigned long long>(row.dispatches), static_cast<unsigned long long>(row.attempts), frames == 0 ? 0 : static_cast<double>(row.dispatches) / frames, row.cpuUs, row.dispatches == 0 ? 0 : row.cpuUs / row.dispatches, frames == 0 ? 0 : row.cpuUs / frames, row.T1Share(), metrics.c_str());
            metrics.clear();
            static constexpr const char* imageNames[]{"sampled-fast", "sampled-fast-miss", "sampled-view-hit", "sampled-clear-hit", "sampled-snapshot-hit", "sampled-view-new", "sampled-snapshot-new", "storage-hit", "storage-new", "refresh-unchanged", "refresh-compared", "refresh-proved", "upload-direct", "upload-cpu", "upload-clear", "dcc-scan", "pending-flush", "refresh-memo"};
            for (std::size_t i = 0; i < row.images.size(); ++i) if (row.images[i].count != 0) append(imageNames[i], row.images[i]);
            for (std::size_t i = 0; i < ProducerCount; ++i) if (row.producers[i].count != 0) append(ProducerNames[i], row.producers[i]);
            ProfilePrint_nid_no_patch("[gpu-desc-outcomes] w=%llu q=0x%x program=0x%llx code=%016llx plan=%llu%s\n", static_cast<unsigned long long>(window.id), queue, static_cast<unsigned long long>(key.program), static_cast<unsigned long long>(key.code), static_cast<unsigned long long>(key.plan), metrics.c_str());
            for (const auto& [id, word] : row.words) {
                const bool present = word.observations == row.specializationSamples;
                ProfilePrint_nid_no_patch("[gpu-desc-spec] w=%llu q=0x%x program=0x%llx code=%016llx plan=%llu id=%u first=%08x varying-bits=%08x observed=%llu/%llu state=%s\n", static_cast<unsigned long long>(window.id), queue, static_cast<unsigned long long>(key.program), static_cast<unsigned long long>(key.code), static_cast<unsigned long long>(key.plan), id, word.first, word.differentBits, static_cast<unsigned long long>(word.observations), static_cast<unsigned long long>(row.specializationSamples), !present ? "varying-presence" : word.differentBits != 0 ? "varying" : "invariant-in-sample");
            }
        }
    }
    double titleUs = 0, emulatorUs = 0;
    for (const auto& [key, counter] : window.gpu) {
        const bool title = key.second == "title-compute" || key.second == "title-graphics";
        (title ? titleUs : emulatorUs) += counter.us;
        ProfilePrint_nid_no_patch("[gpu-desc-work] w=%llu q=0x%x kind=%s count=%llu exclusive-us=%.3f us/frame=%.3f\n", static_cast<unsigned long long>(window.id), key.first, key.second.c_str(), static_cast<unsigned long long>(counter.count), counter.us, counter.PerFrame(frames));
    }
    ProfilePrint_nid_no_patch("[gpu-desc-frame] w=%llu frames=%llu title-ms/frame=%.4f emulator-ms/frame=%.4f total-covered-ms/frame=%.4f budget-ms=16.6667; disjoint timestamp spans, recording cohort, single VkQueue; untimed standalone work is not included\n", static_cast<unsigned long long>(window.id), static_cast<unsigned long long>(frames), frames == 0 ? 0 : titleUs / frames / 1000, frames == 0 ? 0 : emulatorUs / frames / 1000, frames == 0 ? 0 : (titleUs + emulatorUs) / frames / 1000);
}

inline std::shared_ptr<Window> CurrentLocked(State& state, Clock::time_point now) {
    if (state.current != nullptr && now - state.current->begin >= std::chrono::seconds(10)) {
        state.current->end = now;
        state.current->lastFrame = state.frames;
        state.sealed.push_back(std::move(state.current));
    }
    if (state.current == nullptr) {
        state.current = std::make_shared<Window>();
        state.current->id = ++state.serial;
        state.current->firstFrame = state.frames;
        state.current->begin = now;
    }
    return state.current;
}

inline void PublishLocked(State& state) {
    for (auto it = state.sealed.begin(); it != state.sealed.end();) {
        if ((*it)->pendingCpu != 0 || (*it)->pendingGpu != 0) { ++it; continue; }
        Print(**it);
        it = state.sealed.erase(it);
    }
}

inline void Flip() {
    if (!Enabled()) return;
    auto& state = Shared();
    std::lock_guard lock(state.mutex);
    CurrentLocked(state, Clock::now());
    ++state.frames;
    PublishLocked(state);
}

inline std::uint64_t PlanIdentity(const std::shared_ptr<const void>& plan) {
    if (plan == nullptr) return 0;
    auto& state = Shared();
    std::lock_guard lock(state.mutex);
    const auto found = state.plans.find(plan);
    if (found != state.plans.end()) return found->second;
    for (auto it = state.plans.begin(); it != state.plans.end();) {
        if (it->first.expired()) it = state.plans.erase(it); else ++it;
    }
    const auto id = ++state.planSerial;
    state.plans.emplace(plan, id);
    return id;
}

struct Sample {
    std::shared_ptr<Window> window;
    std::uint32_t queue = 0;
    ClassKey key;
    Row row;
};
inline thread_local Sample* active = nullptr;
inline thread_local std::map<std::uint32_t, Row> waits;

class Invocation {
public:
    explicit Invocation(std::uint32_t queue) : previous(active) {
        if (!Enabled()) return;
        const auto overhead = Clock::now();
        auto& state = Shared();
        std::lock_guard lock(state.mutex);
        sample.window = CurrentLocked(state, overhead);
        ++sample.window->pendingCpu;
        sample.queue = queue;
        active = &sample;
        sample.window->bookkeepingUs += Microseconds(overhead);
        start = Clock::now();
    }
    ~Invocation() {
        if (sample.window == nullptr) return;
        sample.row.cpuUs = Microseconds(start);
        const auto overhead = Clock::now();
        active = previous;
        auto& state = Shared();
        std::lock_guard lock(state.mutex);
        if (sample.key.program != 0) sample.window->rows[{sample.queue, sample.key}].Merge(sample.row);
        --sample.window->pendingCpu;
        sample.window->bookkeepingUs += Microseconds(overhead);
        PublishLocked(state);
    }
    void Identify(ClassKey key) { if (sample.window != nullptr) sample.key = key; }
    void Dispatched() {
        if (sample.window == nullptr) return;
        ++sample.row.dispatches;
        if (const auto found = waits.find(sample.queue); found != waits.end()) {
            sample.row.Merge(found->second);
            waits.erase(found);
        }
    }
    void Attempt() { if (sample.window != nullptr) ++sample.row.attempts; }
    template<typename T> void Specialization(const T& constants) {
        if (sample.window == nullptr) return;
        ++sample.row.specializationSamples;
        for (const auto& constant : constants) sample.row.words[constant.id].Add(constant.value);
    }
    Invocation(const Invocation&) = delete;
    Invocation& operator=(const Invocation&) = delete;
private:
    Sample sample;
    Sample* previous;
    Clock::time_point start{};
};

class Scope {
public:
    explicit Scope(Metric metric) : sample(active), metric(metric) { if (sample != nullptr) start = Clock::now(); }
    ~Scope() { if (sample != nullptr) sample->row.metrics[metric].Add(Microseconds(start)); }
    void Outcome(Metric value) { metric = value; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Sample* sample;
    Metric metric;
    Clock::time_point start{};
};

inline void Add(Metric metric, double us) { if (active != nullptr) active->row.metrics[metric].Add(us); }
inline void Image(std::size_t kind, double us) { if (active != nullptr && kind < active->row.images.size()) active->row.images[kind].Add(us); }

class Wait {
public:
    Wait(std::uint32_t queue, bool landing) : queue(queue), landing(landing), enabled(Enabled()) { if (enabled) start = Clock::now(); }
    ~Wait() {
        if (!enabled) return;
        const auto us = Microseconds(start);
        auto& row = waits[queue];
        row.metrics[landing ? LandingWait : LabelWait].Add(us);
        row.producers[producer].Add(us);
    }
    void Satisfied(Producer value) { producer = value; }
private:
    std::uint32_t queue;
    bool landing, enabled;
    Producer producer = Timeout;
    Clock::time_point start{};
};

struct GpuTag {
    std::shared_ptr<Window> window;
    std::uint32_t queue = 0;
    ClassKey key;
    std::string kind;
};

inline GpuTag BeginGpu(std::uint32_t queue, const char* kind, bool shader = false) {
    if (!Enabled()) return {};
    const auto overhead = Clock::now();
    auto& state = Shared();
    std::lock_guard lock(state.mutex);
    auto window = active != nullptr ? active->window : CurrentLocked(state, overhead);
    ++window->pendingGpu;
    window->bookkeepingUs += Microseconds(overhead);
    return {window, active != nullptr ? active->queue : queue, shader && active != nullptr ? active->key : ClassKey{}, kind};
}

inline void EndGpu(const GpuTag& tag, double inclusiveUs, double exclusiveUs, bool valid = true) {
    if (tag.window == nullptr) return;
    const auto overhead = Clock::now();
    auto& state = Shared();
    std::lock_guard lock(state.mutex);
    if (valid) {
        tag.window->gpu[{tag.queue, tag.kind}].Add(exclusiveUs);
        if (tag.key.program != 0) tag.window->rows[{tag.queue, tag.key}].gpu.Add(inclusiveUs);
    } else ++tag.window->droppedGpu;
    --tag.window->pendingGpu;
    tag.window->bookkeepingUs += Microseconds(overhead);
}

inline void DroppedGpu() {
    if (!Enabled()) return;
    auto& state = Shared();
    std::lock_guard lock(state.mutex);
    ++CurrentLocked(state, Clock::now())->droppedGpu;
}

}
