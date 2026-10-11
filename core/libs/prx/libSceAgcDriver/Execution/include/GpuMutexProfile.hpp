#pragma once
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace AgcDriver::GpuMutexProfile {

inline bool Enabled() {
    static const bool enabled = std::getenv("APS5_PROFILE_GPU_MUTEX") != nullptr;
    return enabled;
}

enum Phase : std::size_t { Labels, Device, Acquire, Verify, Flush, Collect, Changed, Compare, Snapshots, Writeback, StoreWalk, StoreMask, StoreStamp, ResidentStore, Count };
inline constexpr std::array<const char*, Count> Names{"labels", "device", "acquire", "verify", "flush", "collect", "changed", "compare", "snapshots", "writeback", "store-walk", "store-mask", "store-stamp", "resident-store"};
struct Row {
    double ms = 0;
    double maxMs = 0;
    std::uint64_t calls = 0;
    std::uint64_t bytes = 0;
    std::uint64_t address = 0;
    std::uint64_t maxBytes = 0;
};
struct Sample {
    std::array<Row, Count> rows{};
    std::uint64_t program = 0;
    std::array<std::uint32_t, 3> groups{};
    std::size_t words = 0;
};
inline thread_local Sample* active = nullptr;

class Scope {
public:
    explicit Scope(Phase phase, std::uint64_t address = 0, std::uint64_t bytes = 0) : sample(active), phase(phase), address(address), bytes(bytes) {
        if (sample != nullptr) start = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (sample == nullptr) return;
        auto& row = sample->rows[phase];
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        row.ms += ms;
        ++row.calls;
        row.bytes += bytes;
        if (ms >= row.maxMs) {
            row.maxMs = ms;
            row.address = address;
            row.maxBytes = bytes;
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Sample* sample;
    Phase phase;
    std::uint64_t address;
    std::uint64_t bytes;
    std::chrono::steady_clock::time_point start{};
};

class Dispatch {
public:
    Dispatch(std::uint64_t program, std::array<std::uint32_t, 3> groups, std::size_t words) : previous(active) {
        if (!Enabled()) return;
        sample.program = program;
        sample.groups = groups;
        sample.words = words;
        active = &sample;
        start = std::chrono::steady_clock::now();
    }
    ~Dispatch() {
        if (!Enabled()) return;
        active = previous;
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration<double, std::milli>(now - start).count();
        struct Window {
            std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
            double total = 0, max = 0;
            std::uint64_t calls = 0;
            Sample slowest;
            struct Program {
                double ms = 0;
                std::uint64_t calls = 0;
                std::array<double, Count> phaseMs{};
            };
            std::unordered_map<std::uint64_t, Program> programs;
        };
        static thread_local Window window;
        window.total += ms;
        ++window.calls;
        if (ms > window.max) {
            window.max = ms;
            window.slowest = sample;
        }
        auto& program = window.programs[sample.program];
        program.ms += ms;
        ++program.calls;
        for (std::size_t i = 0; i < Count; ++i) program.phaseMs[i] += sample.rows[i].ms;
        if (now - window.at < std::chrono::seconds(10)) return;
        const auto& slow = window.slowest;
        std::string phases;
        char text[256];
        for (std::size_t i = 0; i < Count; ++i) {
            const auto& row = slow.rows[i];
            if (row.calls == 0) continue;
            std::snprintf(text, sizeof(text), " %s=%llux%.3fms/%.2fMiB(max %.3fms 0x%llx+0x%llx)", Names[i], static_cast<unsigned long long>(row.calls), row.ms, row.bytes / 1048576.0, row.maxMs, static_cast<unsigned long long>(row.address), static_cast<unsigned long long>(row.maxBytes));
            phases += text;
        }
        ProfilePrint_nid_no_patch("[gpu-mutex] q0x%x %llu holds %.1fms (10 s), slowest %.3fms program=0x%llx groups=%ux%ux%u spirv=%zu words; inclusive phases:%s\n", GuestMemory::CurrentPacket().queue, static_cast<unsigned long long>(window.calls), window.total, window.max, static_cast<unsigned long long>(slow.program), slow.groups[0], slow.groups[1], slow.groups[2], slow.words, phases.c_str());
        std::vector<std::pair<std::uint64_t, const typename decltype(window.programs)::mapped_type*>> ranked;
        for (const auto& [address, entry] : window.programs) ranked.emplace_back(address, &entry);
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second->ms > b.second->ms; });
        std::string top;
        for (std::size_t rank = 0; rank < std::min<std::size_t>(5, ranked.size()); ++rank) {
            const auto& [address, entry] = ranked[rank];
            std::snprintf(text, sizeof(text), " 0x%llx %llux %.1fms (%.3f each:", static_cast<unsigned long long>(address), static_cast<unsigned long long>(entry->calls), entry->ms, entry->ms / static_cast<double>(entry->calls));
            top += text;
            for (std::size_t i = 0; i < Count; ++i) {
                if (entry->phaseMs[i] < 0.05 * entry->ms) continue;
                std::snprintf(text, sizeof(text), " %s %.1f", Names[i], entry->phaseMs[i]);
                top += text;
            }
            top += ")";
        }
        ProfilePrint_nid_no_patch("[gpu-mutex-programs] q0x%x top holds by program (10 s, inclusive phases >= 5%%):%s\n", GuestMemory::CurrentPacket().queue, top.c_str());
        window = Window{};
    }
    Dispatch(const Dispatch&) = delete;
    Dispatch& operator=(const Dispatch&) = delete;
private:
    Sample sample;
    Sample* previous;
    std::chrono::steady_clock::time_point start{};
};

}
