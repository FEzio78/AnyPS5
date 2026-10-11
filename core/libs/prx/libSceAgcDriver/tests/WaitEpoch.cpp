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

std::uint64_t Allocate(std::size_t blocks = 3) {
    const auto bytes = blocks * Block;
#ifdef _WIN32
    auto* memory = GuestArena::GuestArenaAllocate_nid_postfix(bytes, Block);
    GuestArena::GuestArenaCommit_nid_postfix(memory, bytes, PAGE_READWRITE, bytes);
#else
    auto* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Check(memory != MAP_FAILED, "watched allocation failed");
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(memory, bytes);
#endif
    const auto base = reinterpret_cast<std::uint64_t>(memory);
    std::memset(memory, 0, bytes);
    Check(CollectWritesUncached(base, bytes) != 0, "allocation is not watched");
    return base;
}

std::array<std::uint32_t, 7> MakeWaitPacket(std::uint64_t label, std::uint32_t value = 7) {
    return {0xc0053c00u, 0x13u, static_cast<std::uint32_t>(label), static_cast<std::uint32_t>(label >> 32u), value, 0xffffffffu, 0};
}

const std::array<std::byte, 4> Value{std::byte{7}, std::byte{0}, std::byte{0}, std::byte{0}};

void CheckCpu(std::string_view mode) {
    const auto base = Allocate(), label = base + Block;
    const bool equal = mode == "equal" || mode == "collected" || mode == "before-driver";
    if (equal) StoreOwnBytes(label, Value);
    BumpCollectEpoch();
    const auto epoch = CollectEpoch();
    const auto old = CollectWrites(base, Block);
    const auto write = [&] {
        *reinterpret_cast<volatile std::uint32_t*>(base) = 42;
        *reinterpret_cast<volatile std::uint32_t*>(label) = 7;
    };
    if (mode == "polled") {
        std::jthread cpu([&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); write(); });
        WaitMemoryTests::Wait(MakeWaitPacket(label));
    } else {
        write();
        if (mode == "collected") CollectWritesUncached(label, 4);
        if (mode == "before-driver") StoreOwnBytes(label + 64, Value);
        WaitMemoryTests::Wait(MakeWaitPacket(label));
    }
    Check(CollectEpoch() != epoch, "CPU signal did not end the collect epoch");
    CollectWrites(base, Block);
    Check(!UnchangedSince(base, Block, old), "collect after the CPU signal hid the surface store");
    Check(WaitMemoryTests::Counts().waits == 1, "CPU wait bump was not counted");
}

void CheckDriver(std::string_view mode) {
    const auto base = Allocate(), label = base + Block;
    StoreOwnBytes(label, Value);
    BumpCollectEpoch();
    const auto epoch = CollectEpoch();
    CollectWrites(base, Block);
    if (mode == "history") {
        StoreOwnBytes(label, 4, [&] { *reinterpret_cast<std::uint32_t*>(label) = 9; });
        WaitMemoryTests::Store(label, Value, 2, 0x20);
    } else if (mode == "adjacent") {
        *reinterpret_cast<volatile std::uint32_t*>(label + 4096) = 123;
        CollectWritesUncached(label + 4096, 4);
    } else if (mode == "wide") {
        const std::array<std::byte, 8> wide{std::byte{7}};
        StoreOwnBytes(label, wide);
    }
    const auto bytes = ThreadCollectedBytes();
    if (mode == "wide") {
        const std::array<std::uint32_t, 9> packet{0xc0079300u, 0x13u, static_cast<std::uint32_t>(label), static_cast<std::uint32_t>(label >> 32u), 7, 0, 0xffffffffu, 0xffffffffu, 0};
        WaitMemoryTests::Wait(packet);
    } else {
        WaitMemoryTests::Wait(MakeWaitPacket(label));
    }
    Check(CollectEpoch() == epoch, "driver-written wait ended the collect epoch");
    CollectWrites(base, Block);
    Check(ThreadCollectedBytes() == bytes, "second surface collect missed the memo");
    Check(WaitMemoryTests::Counts().waitsSkipped == 1, "driver wait skip was not counted");
}

void CheckMemo() {
    constexpr std::size_t surfaces = 192;
    const auto base = Allocate(surfaces);
    BumpCollectEpoch();
    const auto before = CollectMemoStatistics();
    const auto start = std::chrono::steady_clock::now();
    for (int pass = 0; pass < 20; ++pass) {
        for (std::size_t surface = 0; surface < surfaces; ++surface) CollectWrites(base + surface * Block, Block);
    }
    const auto after = CollectMemoStatistics();
    const auto hits = after.hits - before.hits;
    const auto misses = after.misses - before.misses;
    const auto evictions = after.evictions - before.evictions;
    if (after.capacity >= surfaces) {
        Check(hits == 19 * surfaces && misses == surfaces && evictions == 0, "submission surfaces did not stay in the memo");
    } else {
        Check(hits == 0 && misses == 20 * surfaces && evictions == 20 * surfaces - after.capacity, "small memo eviction counters differ");
    }
    Check(after.epochMissPeak >= misses, "epoch miss peak lost misses");
    BumpCollectEpoch();
    const auto bumped = CollectMemoStatistics();
    CollectWrites(base, Block);
    Check(CollectMemoStatistics().misses == bumped.misses + 1, "new epoch reused a stale memo entry");
    Check(CollectMemoStatistics().evictions == bumped.evictions, "new epoch reuse counted as eviction");
    std::cout << "memo capacity " << after.capacity << ": hits " << hits << ", misses " << misses << ", evictions " << evictions << ", 192 surfaces x20 in "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms\n";
}

void CheckUnknown(bool forced) {
    const auto base = Allocate(), label = base + Block;
    *reinterpret_cast<volatile std::uint32_t*>(label) = 7;
    CollectWritesUncached(label, 4);
    BumpCollectEpoch();
    const auto epoch = CollectEpoch();
    if (forced) StoreOwnBytes(label, Value);
    WaitMemoryTests::Wait(MakeWaitPacket(label));
    Check(CollectEpoch() != epoch, "unexplained or forced wait kept its epoch");
    Check(forced ? WaitMemoryTests::Counts().waitsForced == 1 : WaitMemoryTests::Counts().waits == 1, "conservative bump was not counted");
}

}

int main(int argc, char** argv) {
    try {
        if (!WriteWatched()) return 77;
        Check(argc == 2, "expected one test mode");
        const std::string_view mode = argv[1];
        if (mode == "cpu" || mode == "equal" || mode == "collected" || mode == "polled" || mode == "before-driver") CheckCpu(mode);
        else if (mode == "driver" || mode == "history" || mode == "adjacent" || mode == "wide") CheckDriver(mode);
        else if (mode == "memo") CheckMemo();
        else if (mode == "unknown" || mode == "forced") CheckUnknown(mode == "forced");
        else throw std::runtime_error("unknown test mode");
        DriverDetail::Driver::Get().Shutdown();
        std::cout << "wait epoch " << mode << " passed\n";
    } catch (const std::exception& error) {
        DriverDetail::Driver::Get().Shutdown();
        std::cerr << "wait epoch failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
