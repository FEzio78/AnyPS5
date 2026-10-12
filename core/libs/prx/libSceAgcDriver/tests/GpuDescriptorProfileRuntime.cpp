#include "prx/libSceAgcDriver/Execution/include/GpuDescriptorProfile.hpp"
#include <cstdio>
#include <stdexcept>
#include <string_view>

using namespace AgcDriver::GpuDescriptorProfile;

void Require(bool condition) { if (!condition) throw std::runtime_error("GPU descriptor window regression"); }

int main(int argc, char** argv) {
    const bool enabled = argc > 1 && std::string_view(argv[1]) == "on";
    Require(Enabled() == enabled);
    auto first = std::make_shared<int>(1), second = std::make_shared<int>(1);
    if (enabled) {
        Require(PlanIdentity(first) == PlanIdentity(first));
        Require(PlanIdentity(first) != PlanIdentity(second));
    }
    {
        Wait wait(3, true);
        wait.Satisfied(RecorderLabel);
    }
    GpuTag delayed;
    std::shared_ptr<Window> old;
    {
        Invocation invocation(3);
        invocation.Identify({0x1234, 55, enabled ? PlanIdentity(first) : 0});
        invocation.Attempt();
        { Scope capture(Capture); }
        invocation.Dispatched();
        if (enabled) {
            old = active->window;
            delayed = BeginGpu(3, "title-compute", true);
        }
    }
    Require(active == nullptr);
    if (enabled) {
        const auto& row = old->rows.begin()->second;
        Require(row.dispatches == 1 && row.attempts == 1 && row.metrics[Capture].count == 1);
        Require(row.metrics[LandingWait].count == 1 && row.producers[RecorderLabel].count == 1);
        old->begin = Clock::now() - std::chrono::seconds(11);
        Flip();
        Require(Shared().sealed.size() == 1 && old->pendingGpu == 1);
        const std::array<GpuTag, 1> tags{delayed};
        const std::array<std::uint64_t, 2> stamps{100, 400};
        ResolveGpu(tags, stamps, 10);
        Require(old->rows.begin()->second.gpu.us == 3 && old->pendingGpu == 0);
        Require(Shared().sealed.empty());
        Require(Shared().current->rows.empty());
        Require(Shared().frames == 1 && old->lastFrame == 0);
    } else {
        Require(Shared().current == nullptr && waits.empty());
    }
    if (argc > 2 && std::string_view(argv[2]) == "bench") {
        constexpr unsigned iterations = 200000;
        volatile unsigned sink = 0;
        for (unsigned repetition = 0; repetition < 5; ++repetition) {
            auto start = Clock::now();
            for (unsigned i = 0; i < iterations; ++i) sink = sink + 1;
            const auto baseline = Microseconds(start);
            start = Clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                Invocation invocation(3);
                invocation.Identify({0x1234, 55, 1});
                invocation.Attempt();
                { Scope capture(Capture); sink = sink + 1; }
                { Scope validate(Validation); }
                { Scope build(BuildA); }
                { Scope update(DescriptorUpdate); }
                invocation.Dispatched();
            }
            const auto instrumented = Microseconds(start);
            std::printf("gpu-desc-bench enabled=%u iteration=%u baseline-us=%.3f instrumented-us=%.3f incremental-ns/invocation=%.3f sink=%u\n", enabled, repetition, baseline, instrumented, (instrumented - baseline) * 1000 / iterations, sink);
        }
    }
    AgcDriver::StopProfileOutput_nid_no_patch();
}
