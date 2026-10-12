#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace AgcDriver {

class HleProfile {
public:
    explicit HleProfile(const char* row) : row(row), enabled(Enabled()) {
        if (enabled) started = phaseStart = Clock::now();
    }
    void Phase(const char* name) {
        if (!enabled) return;
        const auto now = Clock::now();
        if (phaseCount < phases.size()) phases[phaseCount++] = {name, std::chrono::duration<double, std::micro>(now - phaseStart).count()};
        phaseStart = now;
    }
    void Count(const char* name) { if (enabled && eventCount < events.size()) events[eventCount++] = name; }
    void Path(const char* name) { path = name; }
    ~HleProfile() {
        if (!enabled) return;
        Phase("other");
        const auto now = Clock::now();
        static std::mutex mutex;
        static std::map<std::string, Totals> rows;
        std::lock_guard lock(mutex);
        auto& totals = rows[row];
        ++totals.paths[path];
        for (std::size_t i = 0; i < phaseCount; ++i) totals.phases[phases[i].first] += phases[i].second;
        for (std::size_t i = 0; i < eventCount; ++i) ++totals.events[events[i]];
        totals.us += std::chrono::duration<double, std::micro>(now - started).count();
        ++totals.count;
        if (now - totals.reported < std::chrono::seconds(10)) return;
        totals.reported = now;
        std::string line = "[" + std::string(row) + "] cumulative: " + std::to_string(totals.count) + " calls; paths:";
        for (const auto& [name, count] : totals.paths) line += " " + std::string(name) + "=" + std::to_string(count);
        char text[128];
        std::snprintf(text, sizeof(text), "; total %.3f ms (%.2f us/call); phases ms:", totals.us / 1000, totals.us / totals.count);
        line += text;
        for (const auto& [name, us] : totals.phases) {
            std::snprintf(text, sizeof(text), " %s=%.3f", name, us / 1000);
            line += text;
        }
        line += "; events:";
        for (const auto& [name, count] : totals.events) line += " " + std::string(name) + "=" + std::to_string(count);
        std::fprintf(stderr, "%s\n", line.c_str());
    }
private:
    static bool Enabled() { static const bool value = std::getenv("APS5_PROFILE_DRAW") != nullptr; return value; }
    using Clock = std::chrono::steady_clock;
    struct NameLess { bool operator()(const char* a, const char* b) const { return std::strcmp(a, b) < 0; } };
    struct Totals {
        std::map<const char*, std::uint64_t, NameLess> paths, events;
        std::map<const char*, double, NameLess> phases;
        std::uint64_t count = 0;
        double us = 0;
        Clock::time_point reported = Clock::now() - std::chrono::seconds(10);
    };
    const char* row;
    bool enabled;
    const char* path = "refused";
    Clock::time_point started{}, phaseStart{};
    std::array<std::pair<const char*, double>, 32> phases{};
    std::array<const char*, 8> events{};
    std::size_t phaseCount = 0, eventCount = 0;
};

}
