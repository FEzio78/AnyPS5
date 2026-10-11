#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "WaitEpoch.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {

using namespace AgcDriver;
using namespace AgcDriver::GuestMemory;
using DriverDetail::WaitMemoryTests;
constexpr std::size_t Block = 65536;

void Check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

std::uint64_t Allocate() {
#ifdef _WIN32
    auto* memory = GuestArena::GuestArenaAllocate_nid_postfix(3 * Block, Block);
    GuestArena::GuestArenaCommit_nid_postfix(memory, 3 * Block, PAGE_READWRITE, 3 * Block);
#else
    auto* memory = mmap(nullptr, 3 * Block, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Check(memory != MAP_FAILED, "watched allocation failed");
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(memory, 3 * Block);
#endif
    const auto base = reinterpret_cast<std::uint64_t>(memory);
    std::memset(memory, 0, 3 * Block);
    Check(CollectWritesUncached(base, 3 * Block) != 0, "allocation is not watched");
    return base;
}

std::array<std::uint32_t, 7> MakeWaitPacket(std::uint64_t label, std::uint32_t value = 7) {
    return {0xc0053c00u, 0x13u, static_cast<std::uint32_t>(label), static_cast<std::uint32_t>(label >> 32u), value, 0xffffffffu, 0};
}

const std::array<std::byte, 4> Value{std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0}};

}

using namespace AgcDriver;
using namespace AgcDriver::GuestMemory;
using DriverDetail::WaitMemoryTests;

void CheckWaitEpochRecorder(Graphics::Recorder& recorder, std::string_view mode) {
    const auto base = Allocate(), label = base + Block;
    BumpCollectEpoch();
    const auto epoch = CollectEpoch();
    const auto old = CollectWrites(base, Block);
    recorder.NoteLabel(label, Value, 2, mode == "same-queue" ? 0 : 0x20);
    MarkWritten(label, 4);
    Graphics::Recorder::CloseLabelGroup(TrackerGeneration());
    if (mode == "recorder-cpu") {
        *reinterpret_cast<volatile std::uint32_t*>(base) = 42;
        *reinterpret_cast<volatile std::uint32_t*>(label + 64) = 99;
    }
    WaitMemoryTests::Wait(MakeWaitPacket(label), 0, mode == "late" ? 3 : 1);
    const auto bytes = ThreadCollectedBytes();
    const auto hits = CollectMemoStatistics().hits;
    const auto outcomes = WaitMemoryTests::Outcomes();
    Check(outcomes.fromRecorder == 1, "wait was not satisfied from the recorder");
    if (mode == "recorder-cpu") {
        Check(CollectEpoch() != epoch, "recorder shortcut hid CPU page evidence");
        CollectWrites(base, Block);
        Check(!UnchangedSince(base, Block, old), "recorder wait hid the CPU surface store");
    } else {
        Check(CollectEpoch() == epoch, "recorder-written wait ended the collect epoch");
        CollectWrites(base, Block);
        Check(ThreadCollectedBytes() == bytes && CollectMemoStatistics().hits == hits + 1, "collect after recorder wait missed the memo");
    }
    recorder.Sync();
    DriverDetail::Driver::Get().Shutdown();
}
