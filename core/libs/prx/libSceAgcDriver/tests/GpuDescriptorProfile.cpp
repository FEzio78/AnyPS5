#include "prx/libSceAgcDriver/Execution/include/GpuDescriptorProfileData.hpp"
#include <stdexcept>

using namespace AgcDriver::GpuDescriptorProfile;

void Require(bool condition) { if (!condition) throw std::runtime_error("GPU descriptor profile regression"); }

int main() {
    const std::array<std::uint32_t, 2> code{0x12345678, 0x87654321};
    auto changed = code;
    changed[1] ^= 1;
    const ClassKey key{0x2489ed100, HashCode(code), 1};
    Require(key == ClassKey{key.program, HashCode(code), 1});
    Require(key != ClassKey{key.program + 4, key.code, key.plan});
    Require(key != ClassKey{key.program, HashCode(changed), key.plan});
    Require(key != ClassKey{key.program, key.code, key.plan + 1});
    std::map<ClassKey, int> classes;
    classes[key] = 1;
    classes[{key.program, key.code, key.plan + 1}] = 2;
    Require(classes.size() == 2);
    Counter before{7, 90}, after{12, 250};
    const auto delta = after.Difference(before);
    Require(delta.count == 5 && delta.us == 160);
    Require(delta.PerDispatch(8) == 20 && delta.PerFrame(4) == 40);
    Require(delta.PerDispatch(0) == 0 && delta.PerFrame(0) == 0);
    Row row, other;
    row.dispatches = 4;
    row.metrics[RevalidateFast] = {3, 30};
    row.metrics[T1Refresh] = {1, 10};
    other.dispatches = 4;
    other.metrics[RevalidateFull] = {1, 20};
    other.metrics[RevalidateFailed] = {1, 5};
    other.cpuUs = 120;
    other.gpu = {4, 80};
    row.Merge(other);
    Require(row.dispatches == 8 && row.T1Share() == 0.2 && row.TotalCost() == 200);
    WordVariation invariant, varying;
    invariant.Add(0x123);
    invariant.Add(0x123);
    varying.Add(0x123);
    varying.Add(0x163);
    Require(invariant.observations == 2 && invariant.differentBits == 0);
    invariant.Merge(varying);
    Require(invariant.observations == 4 && invariant.differentBits == 0x40);
    const std::array<GpuInterval, 4> intervals{{{0, 100, 9, 0}, {10, 90, 4, 0}, {20, 50, 2, 0}, {60, 70, 1, 0}}};
    const auto exclusive = ExclusiveTicks(intervals);
    Require(exclusive == std::vector<double>{20, 40, 30, 10});
    const std::array<GpuInterval, 2> equal{{{0, 10, 5, 0}, {0, 10, 2, 0}}};
    Require(ExclusiveTicks(equal) == std::vector<double>{0, 10});
    const std::array<GpuInterval, 2> invalid{{{10, 0, 0, 0}, {20, 30, 0, 0}}};
    Require(ExclusiveTicks(invalid) == std::vector<double>{0, 10});
}
