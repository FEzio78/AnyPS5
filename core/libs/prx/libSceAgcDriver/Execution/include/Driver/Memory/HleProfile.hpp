#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace AgcDriver {

class HleProfile {
public:
    explicit HleProfile(const char* row) : row(row), enabled(std::getenv("APS5_PROFILE_DRAW") != nullptr) {
        if (enabled) started = phaseStart = Clock::now();
    }
    void Phase(const char* name) {
        if (!enabled) return;
        const auto now = Clock::now();
        phases[name] += std::chrono::duration<double, std::micro>(now - phaseStart).count();
        phaseStart = now;
    }
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
        for (const auto& [name, us] : phases) totals.phases[name] += us;
        totals.us += std::chrono::duration<double, std::micro>(now - started).count();
        ++totals.count;
        if (now - totals.reported < std::chrono::seconds(10)) return;
        totals.reported = now;
        std::string line = "[" + std::string(row) + "] cumulative: " + std::to_string(totals.count) + " calls; paths:";
        for (const auto& [name, count] : totals.paths) line += " " + name + "=" + std::to_string(count);
        char text[128];
        std::snprintf(text, sizeof(text), "; total %.3f ms (%.2f us/call); phases ms:", totals.us / 1000, totals.us / totals.count);
        line += text;
        for (const auto& [name, us] : totals.phases) {
            std::snprintf(text, sizeof(text), " %s=%.3f", name.c_str(), us / 1000);
            line += text;
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
private:
    using Clock = std::chrono::steady_clock;
    struct Totals {
        std::map<std::string, std::uint64_t> paths;
        std::map<std::string, double> phases;
        std::uint64_t count = 0;
        double us = 0;
        Clock::time_point reported = Clock::now() - std::chrono::seconds(10);
    };
    const char* row;
    bool enabled;
    const char* path = "refused";
    Clock::time_point started{}, phaseStart{};
    std::map<std::string, double> phases;
};

}
