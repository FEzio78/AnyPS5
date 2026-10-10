#include "prx/libSceAgcDriver/Graphics/include/ImageProfile.hpp"
#include <stdexcept>
#include <string_view>

using namespace AgcDriver::Graphics::ImageProfile;
void Require(bool ok) { if (!ok) throw std::runtime_error("image profile regression"); }
int main(int argc, char** argv) {
    const bool enabled = argc == 2 && std::string_view(argv[1]) == "on";
    Require(Enabled() == enabled);
    Totals totals;
    totals.Add(8, 3, {0x100, 1024, 56, 16, 16, 1, 3}, "first");
    Totals other;
    other.Add(12, 4, {0x200, 2048, 20, 32, 16, 2, 4}, "slowest");
    totals.Merge(other);
    Require(totals.calls == 2 && totals.bytes == 3072 && totals.inclusive == 20 && totals.exclusive == 13);
    Require(totals.maximum == 12 && totals.slowest.address == 0x200 && std::string_view(totals.reason) == "slowest");
    {
        Scope outside(Read);
        Require(active == nullptr);
    }
    {
        Scope root(Images, "test", true);
        root.Describe({0x300, 4096, 56, 32, 32});
        if (enabled) Require(active != nullptr);
        Measure(Read, "inherited", [] {});
    }
    Require(active == nullptr);
    auto& window = Shared();
    Require(window.totals[Images].calls == (enabled ? 1u : 0u));
    Require(window.totals[Read].calls == (enabled ? 1u : 0u));
    if (enabled) {
        Require(window.totals[Read].slowest.address == 0x300);
        Require(window.totals[Images].inclusive >= window.totals[Read].inclusive);
        window.since = Clock::now() - std::chrono::seconds(11);
        { Scope root(Images, "report", true); }
        Require(window.totals[Images].calls == 0 && window.totals[Read].calls == 0);
        AgcDriver::ProfileOutput_nid_no_patch().Flush();
    }
}
