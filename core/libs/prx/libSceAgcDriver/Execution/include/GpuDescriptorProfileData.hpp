#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <tuple>
#include <vector>

namespace AgcDriver::GpuDescriptorProfile {

struct ClassKey {
    std::uint64_t program = 0, code = 0, plan = 0;
    auto operator<=>(const ClassKey&) const = default;
};

inline std::uint64_t HashCode(std::span<const std::uint32_t> code) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (const auto word : code) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= (word >> shift) & 255u;
            hash *= 0x100000001b3ull;
        }
    }
    return hash;
}

enum Metric : std::size_t { Capture, Validation, BuildA, BuildB, RevalidateFast, RevalidateFull, RevalidateFailed, T1Refresh, DescriptorUpdate, BdaLease, BdaMirror, MutexHold, LabelWait, LandingWait, MetricCount };
inline constexpr std::array<const char*, MetricCount> MetricNames{"capture", "validation", "build-a", "build-b", "revalidate-fast", "revalidate-full", "revalidate-failed", "t1-refresh", "descriptor-update", "bda-lease", "bda-mirror", "mutex-hold", "label-wait", "landing-wait"};
enum Producer : std::size_t { MemoryAtEntry, DriverStore, RecorderLabel, Completion, MemoryObserved, Timeout, ProducerCount };
inline constexpr std::array<const char*, ProducerCount> ProducerNames{"memory-at-entry", "driver-store", "recorder-label", "completion", "memory-observed-unknown", "unsatisfied"};

struct Counter {
    std::uint64_t count = 0;
    double us = 0;
    void Add(double elapsed, std::uint64_t calls = 1) { count += calls; us += elapsed; }
    void Merge(const Counter& other) { count += other.count; us += other.us; }
    Counter Difference(const Counter& before) const { return {count - before.count, us - before.us}; }
    double PerDispatch(std::uint64_t dispatches) const { return dispatches == 0 ? 0 : us / static_cast<double>(dispatches); }
    double PerFrame(std::uint64_t frames) const { return frames == 0 ? 0 : us / static_cast<double>(frames); }
};

struct WordVariation {
    std::uint32_t first = 0, differentBits = 0;
    std::uint64_t observations = 0;
    void Add(std::uint32_t value) {
        if (observations == 0) first = value;
        else differentBits |= first ^ value;
        ++observations;
    }
    void Merge(const WordVariation& other) {
        if (other.observations == 0) return;
        if (observations == 0) { *this = other; return; }
        differentBits |= other.differentBits | (first ^ other.first);
        observations += other.observations;
    }
};

struct Row {
    std::uint64_t dispatches = 0, attempts = 0, specializationSamples = 0;
    std::array<Counter, MetricCount> metrics{};
    std::array<Counter, 18> images{};
    std::array<Counter, ProducerCount> producers{};
    std::map<std::uint32_t, WordVariation> words;
    Counter gpu;
    double cpuUs = 0;
    void Merge(const Row& other) {
        dispatches += other.dispatches;
        attempts += other.attempts;
        specializationSamples += other.specializationSamples;
        cpuUs += other.cpuUs;
        gpu.Merge(other.gpu);
        for (std::size_t i = 0; i < MetricCount; ++i) metrics[i].Merge(other.metrics[i]);
        for (std::size_t i = 0; i < images.size(); ++i) images[i].Merge(other.images[i]);
        for (std::size_t i = 0; i < ProducerCount; ++i) producers[i].Merge(other.producers[i]);
        for (const auto& [id, word] : other.words) words[id].Merge(word);
    }
    double TotalCost() const { return cpuUs + gpu.us; }
    double T1Share() const {
        const auto total = metrics[RevalidateFast].count + metrics[RevalidateFull].count + metrics[RevalidateFailed].count;
        return total == 0 ? 0 : static_cast<double>(metrics[T1Refresh].count) / static_cast<double>(total);
    }
};

struct GpuInterval {
    std::uint64_t begin = 0, end = 0;
    std::size_t category = 0, owner = 0;
};

inline std::vector<double> ExclusiveTicks(std::span<const GpuInterval> ranges) {
    std::vector<double> ticks(ranges.size());
    std::vector<std::uint64_t> edges;
    for (const auto& range : ranges) if (range.end >= range.begin) { edges.push_back(range.begin); edges.push_back(range.end); }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    for (std::size_t edge = 1; edge < edges.size(); ++edge) {
        std::size_t chosen = ranges.size();
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto& range = ranges[i];
            if (range.begin > edges[edge - 1] || range.end < edges[edge]) continue;
            if (chosen == ranges.size() || std::tuple(range.end - range.begin, range.category, i) < std::tuple(ranges[chosen].end - ranges[chosen].begin, ranges[chosen].category, chosen)) chosen = i;
        }
        if (chosen != ranges.size()) ticks[chosen] += static_cast<double>(edges[edge] - edges[edge - 1]);
    }
    return ticks;
}

}
