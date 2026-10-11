#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_WAITEPOCH_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_WAITEPOCH_HPP

#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include <string_view>

namespace AgcDriver::DriverDetail {

struct WaitMemoryTests {
    static void Wait(std::span<const std::uint32_t> packet, std::uint32_t queue = 0, std::uint64_t received = 1) {
        Driver::Get().waitMemory(packet, queue, {}, received, false);
    }
    static void Store(std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue) {
        Driver::Get().noteLabelStore(address, bytes, stamp, queue);
    }
    static EpochBumps Counts() { return Driver::epochBumps(); }
    static WaitOutcomes Outcomes() { return Driver::waitOutcomes(); }
};

}

void CheckWaitEpochRecorder(AgcDriver::Graphics::Recorder& recorder, std::string_view mode);

#endif
