#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/Shutdown.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

extern "C" std::uint32_t APS5_VABI sceAgcDcbBeginOcclusionQueryGetSize(std::uint32_t queryType);
extern "C" std::uint32_t APS5_VABI sceAgcDcbEndOcclusionQueryGetSize();

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename TAction>
void expectFailure(TAction action) {
    try {
        action();
    } catch (const std::runtime_error& error) {
        check(error.what()[0] != '\0', "empty exception message");
        return;
    }
    throw std::runtime_error("expected an exception");
}

void testSizes() {
    check(sceAgcDcbBeginOcclusionQueryGetSize(0) == 16, "begin occlusion query size for type 0");
    check(sceAgcDcbBeginOcclusionQueryGetSize(1) == 288, "begin occlusion query size for type 1");
    check(sceAgcDcbBeginOcclusionQueryGetSize(0x101) == 288, "begin occlusion query type 1 is not selected by its low byte");
    check(sceAgcDcbBeginOcclusionQueryGetSize(0xffffff01u) == 288, "begin occlusion query type 1 with high bits set");
    check(sceAgcDcbEndOcclusionQueryGetSize() == 16, "end occlusion query size");
}

void testRejections() {
    expectFailure([] { sceAgcDcbBeginOcclusionQueryGetSize(2); });
    expectFailure([] { sceAgcDcbBeginOcclusionQueryGetSize(0xff); });
    expectFailure([] { sceAgcDcbBeginOcclusionQueryGetSize(0x100); });
}

}

int main() {
    try {
        testSizes();
        testRejections();
        LibcRunShutdown_nid_postfix();
        std::puts("AGC occlusion query size tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        try { LibcRunShutdown_nid_postfix(); }
        catch (const std::exception& shutdown) { std::fprintf(stderr, "shutdown: %s\n", shutdown.what()); }
        return 1;
    }
}
