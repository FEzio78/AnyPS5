#include "prx/libSceAgc/Command/include/Packet.hpp"
#include "prx/libSceAgc/Command/include/Memory.hpp"
#include "prx/libSceAgc/Command/include/RegisterDefaults.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libc/include/Shutdown.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>

extern "C" std::uint32_t* APS5_VABI sceAgcDcbEventWrite(CommandBuffer* buf, std::uint8_t eventType, const volatile void* address);
extern "C" std::uint64_t APS5_VABI sceAgcDcbEventWriteGetSize(std::uint8_t eventType);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbResetQueue(CommandBuffer* buf, std::uint32_t op, std::uint32_t state);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbClearState(CommandBuffer* buf, std::uint32_t command);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetFlip(CommandBuffer* buf, std::uint32_t handle, std::int32_t index, std::uint32_t mode, std::int64_t argument);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbWaitUntilSafeForRendering(CommandBuffer* buf, std::uint32_t handle, std::uint32_t index);
extern "C" int APS5_VABI sceAgcSuspendPoint();
extern "C" int APS5_VABI sceAgcInit(std::uint32_t version);
extern "C" void* APS5_VABI sceAgcGetRegisterDefaults();
extern "C" void* APS5_VABI sceAgcGetRegisterDefaultsInternal();
extern "C" void* APS5_VABI sceAgcGetRegisterDefaults2Internal(std::uint32_t version);
extern "C" int APS5_VABI sceAgcGetDefaultCxStateFlat(void* out_state, std::uint32_t size);
extern "C" int APS5_VABI sceAgcInit_0090(std::uint32_t* state, std::uint32_t version);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbDrawIndexAuto(CommandBuffer* buf, std::uint32_t indexCount, std::uint64_t modifier);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbDrawIndexIndirect(CommandBuffer* buf, std::uint32_t dataOffsetInBytes, std::uint64_t modifier);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbDrawIndexIndirectMulti(CommandBuffer* buf, std::uint32_t dataOffsetInBytes, std::uint32_t countIndirect, std::uint32_t maxCountOrCount, const volatile void* countAddress, std::uint32_t strideInBytes, std::uint64_t modifier);
extern "C" int APS5_VABI sceAgcWaitRegMemPatchReference(std::uint32_t* cmd, std::uint64_t reference);
extern "C" int APS5_VABI sceAgcWaitRegMemPatchMask(std::uint32_t* cmd, std::uint64_t mask);
extern "C" int APS5_VABI sceAgcGetDataPacketPayloadAddress_0090(std::uint32_t** addr, std::uint32_t* cmd, int type);
extern "C" std::uint32_t* APS5_VABI sceAgcCbSetShRegisterRangeDirect(CommandBuffer* buf, std::uint32_t offset, const std::uint32_t* values, std::uint32_t numValues);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbContextStateOp_0100(CommandBuffer* buf, std::uint32_t operation);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbPushMarker(CommandBuffer* buf, const char* str, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbPopMarker(CommandBuffer* buf);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetWorkloadComplete(CommandBuffer* buf, std::uint32_t stream_id, std::uint32_t workload_id);
extern "C" int APS5_VABI sceAgcDriverRegisterWorkloadStream(std::uint32_t stream_id, const void* stream);
extern "C" int APS5_VABI sceAgcDriverUnregisterWorkloadStream(std::uint32_t stream_id);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetWorkloadsActive(CommandBuffer* buf, std::uint32_t stream_id, const std::uint32_t* workload_ids, std::uint32_t workload_count);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetWorkloadStreamInactive(CommandBuffer* buf, std::uint32_t streamId);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbSetWorkloadsActive(CommandBuffer* buf, std::uint32_t streamId, const std::uint32_t* workloadIds, std::uint32_t workloadCount);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbSetWorkloadComplete(CommandBuffer* buf, std::uint32_t streamId, std::uint32_t workloadId);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbSetWorkloadStreamInactive(CommandBuffer* buf, std::uint32_t streamId);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetMarker(CommandBuffer* buf, const char* str, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbPushMarker(CommandBuffer* buf, const char* str, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbPopMarker(CommandBuffer* buf);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbSetMarker(CommandBuffer* buf, const char* str, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbPushMarkerSpan(CommandBuffer* buf, const char* str, std::uint32_t length, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetMarkerSpan(CommandBuffer* buf, const char* str, std::uint32_t length, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbPushMarkerSpan(CommandBuffer* buf, const char* str, std::uint32_t length, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbSetMarkerSpan(CommandBuffer* buf, const char* str, std::uint32_t length, std::uint32_t color);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetIndexBuffer(CommandBuffer* buf, std::uint64_t indexAddress);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetBaseIndirectArgs(CommandBuffer* buf, std::uint32_t shaderType, const volatile void* indirectBaseAddr);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetIndexCount(CommandBuffer* buf, std::uint32_t indexCount);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetIndexSize(CommandBuffer* buf, std::uint8_t indexSize, std::uint8_t cachePolicy);
extern "C" std::uint32_t* APS5_VABI sceAgcDcbSetIndexIndirectArgs(CommandBuffer* buf, const volatile void* attributes, std::uint16_t attributeIndex);
extern "C" std::uint32_t APS5_VABI sceAgcDcbSetIndexIndirectArgsGetSize();
extern "C" std::uint32_t* APS5_VABI sceAgcSetNop(CommandBuffer* buf, std::uint32_t sizeDw);
extern "C" int APS5_VABI sceAgcDebugRaiseException(std::uint32_t exceptionId);
extern "C" std::uint32_t* APS5_VABI sceAgcAcbAtomicGds_0900(CommandBuffer* buf, std::uint32_t control, std::uint32_t mode, std::uint32_t gdsOp, std::uint32_t src, std::uint16_t offset, std::uint16_t index, std::uint32_t mask, std::uint32_t data, std::uint64_t compareData, std::uint64_t extraData);

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

struct Storage {
    std::array<std::uint32_t, 64> words{};
    CommandBuffer buffer{words.data(), words.data() + words.size(), words.data(), words.data() + words.size(), nullptr, nullptr, 0};
};

bool APS5_VABI grow(CommandBuffer* buffer, std::uint32_t count, void* userData) {
    auto& storage = *static_cast<Storage*>(userData);
    check(count == 5, "incorrect callback allocation including reserved words");
    buffer->bottom = storage.words.data();
    buffer->top = storage.words.data() + storage.words.size();
    buffer->cursor_up = buffer->bottom;
    buffer->cursor_down = buffer->top;
    return true;
}

bool APS5_VABI growReserved(CommandBuffer* buffer, std::uint32_t count, void* userData) {
    auto& storage = *static_cast<Storage*>(userData);
    check(count == 5 + 4, "incorrect callback allocation including reserved words");
    buffer->bottom = storage.words.data();
    buffer->top = storage.words.data() + storage.words.size();
    buffer->cursor_up = buffer->bottom;
    buffer->cursor_down = buffer->top;
    return true;
}

void testPackets() {
    Storage storage;
    sceAgcDcbResetQueue(&storage.buffer, 0, 3);
    sceAgcDcbDrawIndexAuto(&storage.buffer, 17, 0);
    const std::array<std::uint32_t, 5> expected{0xc0001200u, 3, 0xc0012d00u, 17, 2};
    check(std::equal(expected.begin(), expected.end(), storage.words.begin()), "reset or draw packet mismatch");
    check(storage.buffer.cursor_up == storage.words.data() + expected.size(), "incorrect packet cursor advance");
    const auto before = storage.words;
    expectFailure([&] { sceAgcDcbResetQueue(&storage.buffer, 0, 16); });
    check(storage.words == before, "invalid reset modified packet memory");
    Storage destination;
    CommandBuffer empty{nullptr, nullptr, nullptr, nullptr, grow, &destination, 0};
    Agc::Command::Emit(&empty, 0x15u, {1, 1, 1, 0x41u}, __func__);
    check(empty.cursor_up == destination.words.data() + 5, "guest ABI allocation callback failed");
    Storage reservedDestination;
    CommandBuffer reserved{nullptr, nullptr, nullptr, nullptr, growReserved, &reservedDestination, 4};
    Agc::Command::Emit(&reserved, 0x15u, {1, 1, 1, 0x41u}, __func__);
    check(reserved.cursor_up == reservedDestination.words.data() + 5, "buffer with less room than its reserved words did not grow");
    Storage exhausted;
    exhausted.buffer.cursor_down = exhausted.words.data() + 2;
    expectFailure([&] { Agc::Command::WriteNop(&exhausted.buffer, 3, __func__); });
    check(exhausted.buffer.cursor_up == exhausted.words.data(), "failed allocation advanced cursor");
    std::array<std::uint32_t, 8> scratch{};
    CommandBuffer noDown{scratch.data(), scratch.data() + scratch.size(), scratch.data(), nullptr, nullptr, nullptr, 0};
    Agc::Command::WriteNop(&noDown, 6, __func__);
    check(noDown.cursor_up == scratch.data() + 6, "buffer without a down cursor did not use its top as the limit");
    expectFailure([&] { Agc::Command::WriteNop(&noDown, 3, __func__); });
    check(noDown.cursor_up == scratch.data() + 6, "allocation past the top of a buffer without a down cursor advanced its cursor");
}

void testNop() {
    Storage storage;
    storage.words.fill(0xdeadbeefu);
    auto* single = sceAgcSetNop(&storage.buffer, 1);
    auto* pair = sceAgcSetNop(&storage.buffer, 2);
    auto* triple = sceAgcSetNop(&storage.buffer, 3);
    const std::array<std::uint32_t, 7> expected{0xffff1000u, 0xc0001000u, 0, 0xc0011000u, 0, 0, 0xdeadbeefu};
    check(single == storage.words.data() && pair == single + 1 && triple == pair + 2, "incorrect NOP placement");
    check(std::equal(expected.begin(), expected.end(), storage.words.begin()), "incorrect NOP packet");
    check(storage.buffer.cursor_up == storage.words.data() + 6, "incorrect NOP cursor advance");
    expectFailure([&] { sceAgcSetNop(&storage.buffer, 0); });
    expectFailure([&] { sceAgcSetNop(&storage.buffer, 0x4002u); });
    check(storage.buffer.cursor_up == storage.words.data() + 6, "invalid NOP advanced the cursor");
}

void testClearState() {
    Storage storage;
    storage.words.fill(0xdeadbeefu);
    for (std::uint32_t command = 0; command <= 0xfu; ++command) {
        auto* packet = sceAgcDcbClearState(&storage.buffer, command);
        check(packet == storage.words.data() + command * 2 && packet[0] == 0xc0001200u && packet[1] == command, "incorrect CLEAR_STATE packet");
    }
    auto* maskedZero = sceAgcDcbClearState(&storage.buffer, 0x10u);
    check(maskedZero == storage.words.data() + 32 && maskedZero[0] == 0xc0001200u && maskedZero[1] == 0, "CLEAR_STATE did not mask the command to four bits");
    auto* maskedF = sceAgcDcbClearState(&storage.buffer, 0xffffffffu);
    check(maskedF == storage.words.data() + 34 && maskedF[0] == 0xc0001200u && maskedF[1] == 0xfu, "CLEAR_STATE did not mask the command to four bits");
    check(storage.buffer.cursor_up == storage.words.data() + 36 && storage.words[36] == 0xdeadbeefu, "incorrect CLEAR_STATE cursor advance");
    const auto before = storage.words;
    expectFailure([] { sceAgcDcbClearState(nullptr, 0); });
    check(storage.words == before && storage.buffer.cursor_up == storage.words.data() + 36, "invalid CLEAR_STATE modified the buffer");
}

struct ContextGrowth {
    Storage destination;
    std::uint32_t* expectedCursor;
    std::uint32_t expectedCount;
    std::uint32_t calls = 0;
    bool success = true;
};

bool APS5_VABI growContext(CommandBuffer* buffer, std::uint32_t count, void* userData) {
    auto& growth = *static_cast<ContextGrowth*>(userData);
    check(++growth.calls == 1, "unexpected repeated context allocation callback");
    check(buffer->cursor_up == growth.expectedCursor, "context callback at wrong packet boundary");
    check(count == growth.expectedCount, "incorrect context reservation size");
    if (!growth.success) {
        return false;
    }
    buffer->bottom = growth.destination.buffer.bottom;
    buffer->top = growth.destination.buffer.top;
    buffer->cursor_up = growth.destination.buffer.cursor_up;
    buffer->cursor_down = growth.destination.buffer.cursor_down;
    return true;
}

void testIndexedIndirectDraws() {
    Storage storage;
    const auto* single = sceAgcDcbDrawIndexIndirect(&storage.buffer, 0x40, 0);
    const std::array<std::uint32_t, 5> expectedSingle{0xc0032500u, 0x40, 0x280, 0x280, 0};
    check(single == storage.words.data() && std::equal(expectedSingle.begin(), expectedSingle.end(), single), "indexed indirect draw packet mismatch");
    alignas(4) std::uint32_t count = 0;
    const auto* multi = sceAgcDcbDrawIndexIndirectMulti(&storage.buffer, 0x80, 1, 8, &count, 20, 0);
    const auto address = reinterpret_cast<std::uintptr_t>(&count);
    const std::array<std::uint32_t, 10> expectedMulti{0xc0083800u, 0x80, 0x280, 0x280, 0x40000280u, 8, static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u), 20, 0};
    check(multi == storage.words.data() + expectedSingle.size() && std::equal(expectedMulti.begin(), expectedMulti.end(), multi), "indexed indirect multi draw packet mismatch");
    check(storage.buffer.cursor_up == storage.words.data() + expectedSingle.size() + expectedMulti.size(), "incorrect indexed indirect cursor advance");
    const auto before = storage.words;
    expectFailure([&] { sceAgcDcbDrawIndexIndirect(&storage.buffer, 2, 0); });
    expectFailure([&] { sceAgcDcbDrawIndexIndirectMulti(&storage.buffer, 0, 1, 8, &count, 16, 0); });
    expectFailure([&] { sceAgcDcbDrawIndexIndirectMulti(&storage.buffer, 0, 0, 8, &count, 20, 0); });
    check(storage.words == before, "invalid indexed indirect draw modified packet memory");
}

void testMarkers() {
    Storage dcb;
    Storage acb;
    const auto* dcbPush = sceAgcDcbPushMarker(&dcb.buffer, "frame", 0xff0000u);
    const auto* acbPush = sceAgcAcbPushMarker(&acb.buffer, "frame", 0x00ff00u);
    check(acbPush == acb.words.data() && acbPush[0] == Agc::Command::Header(0x10, 3, 0x0bu << 2u), "ACB push marker header mismatch");
    check(std::strcmp(reinterpret_cast<const char*>(acbPush + 1), "frame") == 0, "ACB push marker text mismatch");
    const auto* acbPop = sceAgcAcbPopMarker(&acb.buffer);
    check(acbPop == acb.words.data() + 3 && acbPop[0] == Agc::Command::Header(0x10, 2, 0x0cu << 2u) && acbPop[1] == 0, "ACB pop marker mismatch");
    sceAgcDcbPopMarker(&dcb.buffer);
    const auto* acbSet = sceAgcAcbSetMarker(&acb.buffer, nullptr, 0);
    const auto* dcbSet = sceAgcDcbSetMarker(&dcb.buffer, nullptr, 0);
    check(acbSet == acb.words.data() + 5 && dcbSet == dcb.words.data() + 5, "set marker did not return its push packet");
    check(acbSet[0] == Agc::Command::Header(0x10, 2, 0x0bu << 2u) && acbSet[1] == 0 && acbSet[2] == Agc::Command::Header(0x10, 2, 0x0cu << 2u), "ACB set marker is not a push and pop pair");
    check(dcbPush == dcb.words.data() && dcb.words == acb.words, "ACB and DCB markers differ");
    check(acb.buffer.cursor_up == acb.words.data() + 9, "incorrect ACB marker cursor advance");
    expectFailure([] { sceAgcAcbPushMarker(nullptr, "frame", 0); });
    expectFailure([] { sceAgcAcbPopMarker(nullptr); });
    expectFailure([] { sceAgcAcbSetMarker(nullptr, "frame", 0); });
}

void testMarkerSpans() {
    Storage dcb;
    Storage acb;
    dcb.words.fill(0xdeadbeefu);
    acb.words.fill(0xdeadbeefu);
    const char text[] = "framex";
    const auto* dcbPush = sceAgcDcbPushMarkerSpan(&dcb.buffer, text, 5, 0xff0000u);
    const auto* acbPush = sceAgcAcbPushMarkerSpan(&acb.buffer, text, 5, 0x00ff00u);
    check(dcbPush == dcb.words.data() && acbPush == acb.words.data(), "push marker span did not start at the cursor");
    check(acbPush[0] == Agc::Command::Header(0x10, 3, 0x0bu << 2u), "push marker span header mismatch");
    check(std::memcmp(acbPush + 1, "frame\0\0\0", 8) == 0, "push marker span text is not the first length bytes padded with zeros");
    const auto* full = sceAgcAcbPushMarkerSpan(&acb.buffer, "abcdefgh", 8, 0);
    sceAgcDcbPushMarkerSpan(&dcb.buffer, "abcdefgh", 8, 0);
    check(full == acb.words.data() + 3 && full[0] == Agc::Command::Header(0x10, 4, 0x0bu << 2u), "a dword-multiple span is not followed by a terminating dword");
    check(std::memcmp(full + 1, "abcdefgh", 8) == 0 && full[3] == 0, "dword-multiple marker span text mismatch");
    const auto* empty = sceAgcAcbPushMarkerSpan(&acb.buffer, nullptr, 0, 0);
    sceAgcDcbPushMarkerSpan(&dcb.buffer, nullptr, 0, 0);
    check(empty == acb.words.data() + 7 && empty[0] == Agc::Command::Header(0x10, 2, 0x0bu << 2u) && empty[1] == 0, "empty marker span mismatch");
    const auto* acbSet = sceAgcAcbSetMarkerSpan(&acb.buffer, "ab", 2, 0);
    const auto* dcbSet = sceAgcDcbSetMarkerSpan(&dcb.buffer, "ab", 2, 0);
    check(acbSet == acb.words.data() + 9 && dcbSet == dcb.words.data() + 9, "set marker span did not return its push packet");
    check(acbSet[0] == Agc::Command::Header(0x10, 2, 0x0bu << 2u) && std::memcmp(acbSet + 1, "ab\0\0", 4) == 0 && acbSet[2] == Agc::Command::Header(0x10, 2, 0x0cu << 2u) && acbSet[3] == 0, "set marker span is not a push and pop pair");
    check(acb.buffer.cursor_up == acb.words.data() + 13 && acb.words[13] == 0xdeadbeefu, "incorrect marker span cursor advance");
    check(dcb.words == acb.words, "ACB and DCB marker spans differ");

    AgcDriver::QueueState queue;
    const std::array<std::pair<std::size_t, std::size_t>, 3> pushes{{{0, 3}, {3, 4}, {7, 2}}};
    for (const auto& [offset, words] : pushes) {
        const std::span<const std::uint32_t> packet(dcb.words.data() + offset, words);
        AgcDriver::Pm4::Validate(packet, 0);
        AgcDriver::Pm4::Execute(packet, queue);
    }
    check(queue.markers.size() == 3 && queue.markers[0] == "frame" && queue.markers[1] == "abcdefgh" && queue.markers[2].empty(), "driver read the wrong marker span text");

    const auto before = acb.words;
    expectFailure([&] { sceAgcAcbPushMarkerSpan(&acb.buffer, nullptr, 1, 0); });
    expectFailure([&] { sceAgcDcbSetMarkerSpan(&acb.buffer, nullptr, 4, 0); });
    expectFailure([] { sceAgcDcbPushMarkerSpan(nullptr, "frame", 5, 0); });
    expectFailure([] { sceAgcAcbSetMarkerSpan(nullptr, "frame", 5, 0); });
    check(acb.words == before && acb.buffer.cursor_up == acb.words.data() + 13, "a rejected marker span modified the buffer");
}

void testWorkloads() {
    Storage dcb;
    const std::uint32_t ids[2] = {3, 4};
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, ids, 2) == nullptr, "workloads active packet for an unregistered stream");
    check(sceAgcDcbSetWorkloadComplete(&dcb.buffer, 1, 3) == nullptr, "workload complete packet for an unregistered stream");
    check(dcb.buffer.cursor_up == dcb.words.data(), "unregistered stream advanced the cursor");
    const std::uint32_t record[8] = {};
    check(sceAgcDriverRegisterWorkloadStream(1, record) == 0, "workload stream registration failed");
    const auto* active = sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, ids, 2);
    check(active == dcb.words.data() && active[0] == Agc::Command::Header(0x10, 18, 0) && active[1] == 1 && active[2] == 0x18 && active[3] == 0, "workloads active packet");
    for (std::uint32_t word = 4; word < 18; ++word) check(active[word] == 0, "workloads active packet tail is not zero");
    const auto* complete = sceAgcDcbSetWorkloadComplete(&dcb.buffer, 1, 33);
    check(complete == dcb.words.data() + 18 && complete[0] == Agc::Command::Header(0x10, 12, 0) && complete[1] == 1 && complete[2] == 33 && complete[3] == 0xffffffffu && complete[4] == 0xfffffffdu, "workload complete packet");
    for (std::uint32_t word = 5; word < 12; ++word) check(complete[word] == 0, "workload complete packet tail is not zero");
    check(dcb.buffer.cursor_up == dcb.words.data() + 30, "incorrect workload cursor advance");
    const std::uint32_t duplicate[2] = {5, 5};
    const std::uint32_t outOfRange[1] = {64};
    const std::uint32_t top[1] = {63};
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, duplicate, 2) == nullptr, "duplicate workload ids accepted");
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, outOfRange, 1) == nullptr, "workload id 64 accepted");
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, top, 1) != nullptr, "workload id 63 rejected");
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, ids, 0) == nullptr, "workload count 0 accepted");
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 1, nullptr, 2) == nullptr, "null workload ids accepted");
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 0, ids, 2) == nullptr, "stream id 0 accepted");
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, 32, ids, 2) == nullptr, "stream id 32 accepted");
    check(sceAgcDcbSetWorkloadComplete(&dcb.buffer, 1, 64) == nullptr, "workload complete id 64 accepted");
    check(sceAgcDcbSetWorkloadsActive(nullptr, 1, ids, 2) == nullptr, "null command buffer accepted");
    check(sceAgcDcbSetWorkloadComplete(nullptr, 1, 3) == nullptr, "null command buffer accepted for complete");
    check(sceAgcDriverUnregisterWorkloadStream(1) == 0, "workload stream unregistration failed");
    check(sceAgcDcbSetWorkloadComplete(&dcb.buffer, 1, 3) == nullptr, "workload complete packet after unregistration");
}

void testWorkloadStreams() {
    struct Vector {
        std::uint32_t streamId;
        std::uintptr_t workloadIds;
        std::uint32_t workloadCount;
        std::uint32_t workloadId;
    };
    const std::array<Vector, 3> vectors{{
        {0x11111111u, 0x22222222u, 0x33333333u, 0x22222222u},
        {1, 2, 3, 2},
        {0x22222222u, static_cast<std::uintptr_t>(0x3333333344444444ull), 0, 0x44444444u}}};
    for (const auto& vector : vectors) {
        Storage storage;
        storage.words.fill(0xdeadbeefu);
        const auto before = storage.words;
        const auto* ids = reinterpret_cast<const std::uint32_t*>(vector.workloadIds);
        check(sceAgcDcbSetWorkloadsActive(&storage.buffer, vector.streamId, ids, vector.workloadCount) == nullptr, "DCB workloads active returned a packet for an unregistered stream");
        check(sceAgcDcbSetWorkloadComplete(&storage.buffer, vector.streamId, vector.workloadId) == nullptr, "DCB workload complete returned a packet for an unregistered stream");
        check(sceAgcDcbSetWorkloadStreamInactive(&storage.buffer, vector.streamId) == nullptr, "DCB workload stream inactive returned a packet for an unregistered stream");
        check(sceAgcAcbSetWorkloadsActive(&storage.buffer, vector.streamId, ids, vector.workloadCount) == nullptr, "ACB workloads active returned a packet for an unregistered stream");
        check(sceAgcAcbSetWorkloadComplete(&storage.buffer, vector.streamId, vector.workloadId) == nullptr, "ACB workload complete returned a packet for an unregistered stream");
        check(sceAgcAcbSetWorkloadStreamInactive(&storage.buffer, vector.streamId) == nullptr, "ACB workload stream inactive returned a packet for an unregistered stream");
        check(storage.words == before && storage.buffer.cursor_up == storage.words.data(), "a workload call for an unregistered stream wrote to the buffer");
    }

    const std::uint32_t ids[2] = {3, 4};
    Storage exhausted;
    exhausted.buffer.cursor_down = exhausted.words.data() + 3;
    const auto before = exhausted.words;
    check(sceAgcDcbSetWorkloadStreamInactive(&exhausted.buffer, 1) == nullptr && sceAgcAcbSetWorkloadsActive(&exhausted.buffer, 1, ids, 2) == nullptr &&
        sceAgcAcbSetWorkloadComplete(&exhausted.buffer, 1, 3) == nullptr && sceAgcAcbSetWorkloadStreamInactive(&exhausted.buffer, 1) == nullptr,
        "a workload call for an unregistered stream on an exhausted buffer returned a packet");
    check(exhausted.words == before && exhausted.buffer.cursor_up == exhausted.words.data(), "a workload call for an unregistered stream modified an exhausted buffer");
    check(sceAgcDcbSetWorkloadStreamInactive(nullptr, 1) == nullptr && sceAgcAcbSetWorkloadsActive(nullptr, 1, ids, 2) == nullptr &&
        sceAgcAcbSetWorkloadComplete(nullptr, 1, 3) == nullptr && sceAgcAcbSetWorkloadStreamInactive(nullptr, 1) == nullptr,
        "a workload call with a null command buffer returned a packet");
}

void testRegisteredWorkloadStreams() {
    constexpr std::uint32_t stream = 5;
    const std::uint32_t record[8] = {};
    check(sceAgcDriverRegisterWorkloadStream(stream, record) == 0, "workload stream registration failed");
    const std::uint32_t ids[3] = {0, 33, 63};
    Storage dcb;
    Storage acb;
    dcb.words.fill(0xdeadbeefu);
    acb.words.fill(0xdeadbeefu);
    check(sceAgcDcbSetWorkloadsActive(&dcb.buffer, stream, ids, 3) == dcb.words.data(), "DCB workloads active packet for a registered stream");
    check(sceAgcAcbSetWorkloadsActive(&acb.buffer, stream, ids, 3) == acb.words.data(), "ACB workloads active packet for a registered stream");
    check(sceAgcDcbSetWorkloadComplete(&dcb.buffer, stream, 33) == dcb.words.data() + 18, "DCB workload complete packet for a registered stream");
    check(sceAgcAcbSetWorkloadComplete(&acb.buffer, stream, 33) == acb.words.data() + 18, "ACB workload complete packet for a registered stream");
    const auto* inactive = sceAgcDcbSetWorkloadStreamInactive(&dcb.buffer, stream);
    check(inactive == dcb.words.data() + 30, "DCB workload stream inactive packet for a registered stream");
    check(sceAgcAcbSetWorkloadStreamInactive(&acb.buffer, stream) == acb.words.data() + 30, "ACB workload stream inactive packet for a registered stream");
    check(dcb.buffer.cursor_up == dcb.words.data() + 39 && acb.buffer.cursor_up == acb.words.data() + 39, "incorrect workload cursor advance for a registered stream");
    check(dcb.words[2] == 1 && dcb.words[3] == 0x80000002u, "workloads active mask for ids 0, 33 and 63");
    check(inactive[0] == Agc::Command::Header(0x10, 9, 0) && inactive[1] == stream, "workload stream inactive packet");
    for (std::uint32_t word = 2; word < 9; ++word) check(inactive[word] == 0, "workload stream inactive packet tail is not zero");
    check(dcb.words[39] == 0xdeadbeefu, "a workload packet wrote past its end");
    check(acb.words == dcb.words, "the ACB and DCB workload IT_NOP packets differ for the same arguments");

    AgcDriver::QueueState graphics;
    AgcDriver::QueueState compute;
    const std::array<std::pair<std::size_t, std::size_t>, 3> packets{{{0, 18}, {18, 12}, {30, 9}}};
    for (const auto& [offset, words] : packets) {
        const std::span<const std::uint32_t> dcbPacket(dcb.words.data() + offset, words);
        const std::span<const std::uint32_t> acbPacket(acb.words.data() + offset, words);
        AgcDriver::Pm4::Validate(dcbPacket, 0);
        AgcDriver::Pm4::Execute(dcbPacket, graphics);
        AgcDriver::Pm4::Validate(acbPacket, 0x20);
        AgcDriver::Pm4::Execute(acbPacket, compute);
    }

    Storage rejected;
    rejected.words.fill(0xdeadbeefu);
    const auto untouched = rejected.words;
    const std::uint32_t duplicate[2] = {5, 5};
    const std::uint32_t outOfRange[1] = {64};
    check(sceAgcAcbSetWorkloadsActive(&rejected.buffer, stream, duplicate, 2) == nullptr, "ACB duplicate workload ids accepted");
    check(sceAgcAcbSetWorkloadsActive(&rejected.buffer, stream, outOfRange, 1) == nullptr, "ACB workload id 64 accepted");
    check(sceAgcAcbSetWorkloadsActive(&rejected.buffer, stream, ids, 0) == nullptr, "ACB workload count 0 accepted");
    check(sceAgcAcbSetWorkloadsActive(&rejected.buffer, stream, ids, 64) == nullptr, "ACB workload count 64 accepted");
    check(sceAgcAcbSetWorkloadsActive(&rejected.buffer, stream, nullptr, 2) == nullptr, "ACB null workload ids accepted");
    check(sceAgcAcbSetWorkloadComplete(&rejected.buffer, stream, 64) == nullptr, "ACB workload complete id 64 accepted");
    check(rejected.words == untouched && rejected.buffer.cursor_up == rejected.words.data(), "a rejected ACB workload call wrote to the buffer");
    check(sceAgcDcbSetWorkloadStreamInactive(nullptr, stream) == nullptr && sceAgcAcbSetWorkloadsActive(nullptr, stream, ids, 3) == nullptr &&
        sceAgcAcbSetWorkloadComplete(nullptr, stream, 3) == nullptr && sceAgcAcbSetWorkloadStreamInactive(nullptr, stream) == nullptr,
        "a workload call with a null command buffer for a registered stream returned a packet");
    check(sceAgcDriverUnregisterWorkloadStream(stream) == 0, "workload stream unregistration failed");
    check(sceAgcDcbSetWorkloadStreamInactive(&rejected.buffer, stream) == nullptr && sceAgcAcbSetWorkloadsActive(&rejected.buffer, stream, ids, 3) == nullptr &&
        sceAgcAcbSetWorkloadComplete(&rejected.buffer, stream, 3) == nullptr && sceAgcAcbSetWorkloadStreamInactive(&rejected.buffer, stream) == nullptr,
        "a workload call after unregistration returned a packet");
    check(rejected.words == untouched && rejected.buffer.cursor_up == rejected.words.data(), "a workload call after unregistration wrote to the buffer");
}

void testIndexBuffer() {
    Storage storage;
    alignas(4) std::uint16_t indices[2]{};
    const auto address = reinterpret_cast<std::uintptr_t>(indices);
    const auto* bound = sceAgcDcbSetIndexBuffer(&storage.buffer, address);
    check(bound[1] == static_cast<std::uint32_t>(address) && bound[2] == static_cast<std::uint32_t>(address >> 32u), "index buffer address mismatch");
    const auto* unbound = sceAgcDcbSetIndexBuffer(&storage.buffer, 0);
    check(unbound == bound + 3 && unbound[0] == bound[0] && unbound[1] == 0 && unbound[2] == 0, "index buffer was not unbound");
    const auto before = storage.words;
    try {
        sceAgcDcbSetIndexBuffer(&storage.buffer, 0x1001);
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find("0x1001") != std::string::npos, "misaligned index buffer error omits the address");
        check(storage.words == before, "misaligned index buffer modified packet memory");
        return;
    }
    throw std::runtime_error("misaligned index buffer was accepted");
}

void testIndirectBase() {
    Storage storage;
    alignas(8) std::uint32_t arguments[4]{};
    const auto address = reinterpret_cast<std::uintptr_t>(arguments);
    const auto* bound = sceAgcDcbSetBaseIndirectArgs(&storage.buffer, 0, arguments);
    check(bound[1] == 1 && bound[2] == static_cast<std::uint32_t>(address) && bound[3] == static_cast<std::uint32_t>(address >> 32u), "indirect base address mismatch");
    const auto* cleared = sceAgcDcbSetBaseIndirectArgs(&storage.buffer, 1, nullptr);
    check(cleared == bound + 4 && (cleared[0] & 2u) != 0 && cleared[1] == 1 && cleared[2] == 0 && cleared[3] == 0, "indirect base was not cleared");
    const auto before = storage.words;
    expectFailure([&] { sceAgcDcbSetBaseIndirectArgs(&storage.buffer, 0, reinterpret_cast<const volatile void*>(address + 4)); });
    check(storage.words == before, "misaligned indirect base modified packet memory");
}

AgcDriver::Pm4::DrawParameters resolveIndirectDraw(const Storage& storage, const std::uint32_t* draw) {
    auto queue = std::make_unique<AgcDriver::QueueState>();
    for (const auto* packet = storage.words.data(); packet != draw; packet += AgcDriver::Pm4::PacketWords(*packet)) {
        const std::span<const std::uint32_t> words(packet, AgcDriver::Pm4::PacketWords(*packet));
        AgcDriver::Pm4::Validate(words, 0);
        AgcDriver::Pm4::Execute(words, *queue);
    }
    const auto resolved = AgcDriver::Pm4::ResolveDraw(std::span<const std::uint32_t>(draw, AgcDriver::Pm4::PacketWords(*draw)), *queue);
    check(resolved.indirect.has_value(), "DRAW_INDEX_INDIRECT did not resolve to an indirect draw");
    return resolved;
}

void testIndexIndirectArgs() {
    alignas(16) static std::array<std::uint32_t, 16> indices{};
    alignas(16) static std::array<std::uint32_t, 8> records{};
    alignas(8) static std::array<std::uint32_t, 5> arguments{6, 1, 2, 3, 0};
    const auto indexAddress = reinterpret_cast<std::uintptr_t>(indices.data());
    records.fill(0xdeadbeefu);
    records[4] = static_cast<std::uint32_t>(indexAddress);
    records[5] = static_cast<std::uint32_t>(indexAddress >> 32u);
    records[6] = 12;
    records[7] = 1;

    Storage indirect;
    indirect.words.fill(0xdeadbeefu);
    const auto* attributes = sceAgcDcbSetIndexIndirectArgs(&indirect.buffer, records.data(), 1);
    const auto base = reinterpret_cast<std::uintptr_t>(records.data());
    const std::array<std::uint32_t, 4> expected{0xc0029100u, static_cast<std::uint32_t>(base), static_cast<std::uint32_t>(base >> 32u), 1};
    check(attributes == indirect.words.data() && std::equal(expected.begin(), expected.end(), attributes), "INDEX_ATTRIBUTES_INDIRECT packet mismatch");
    const auto size = sceAgcDcbSetIndexIndirectArgsGetSize();
    check(size == AgcDriver::Pm4::PacketWords(attributes[0]) * sizeof(std::uint32_t) && indirect.buffer.cursor_up == attributes + size / sizeof(std::uint32_t) && *indirect.buffer.cursor_up == 0xdeadbeefu, "index indirect size differs from the packet it writes");
    sceAgcDcbSetBaseIndirectArgs(&indirect.buffer, 0, arguments.data());
    const auto* draw = sceAgcDcbDrawIndexIndirect(&indirect.buffer, 0, 0);

    Storage direct;
    sceAgcDcbSetIndexBuffer(&direct.buffer, indexAddress);
    sceAgcDcbSetIndexCount(&direct.buffer, 12);
    sceAgcDcbSetIndexSize(&direct.buffer, 1, 0);
    sceAgcDcbSetBaseIndirectArgs(&direct.buffer, 0, arguments.data());
    const auto* directDraw = sceAgcDcbDrawIndexIndirect(&direct.buffer, 0, 0);

    const auto fromRecord = resolveIndirectDraw(indirect, draw);
    const auto fromPackets = resolveIndirectDraw(direct, directDraw);
    check(fromRecord.indexAddress == indexAddress && fromRecord.indexCount == 12 && fromRecord.indexSize == 4 && fromRecord.indirect->arguments == reinterpret_cast<std::uintptr_t>(arguments.data()), "DRAW_INDEX_INDIRECT after the index indirect arguments binds the wrong index buffer");
    check(fromRecord.indexAddress == fromPackets.indexAddress && fromRecord.indexCount == fromPackets.indexCount && fromRecord.indexSize == fromPackets.indexSize &&
        fromRecord.flags == fromPackets.flags && fromRecord.indexed == fromPackets.indexed && fromRecord.indirect->arguments == fromPackets.indirect->arguments &&
        fromRecord.indirect->recordBytes == fromPackets.indirect->recordBytes && fromRecord.indirect->baseVertexLocation == fromPackets.indirect->baseVertexLocation,
        "DRAW_INDEX_INDIRECT after the index indirect arguments differs from the same draw after the direct index packets");
    const auto record = AgcDriver::Pm4::ReadDrawArguments(*fromRecord.indirect, 0);
    check(record.count == 6 && record.instances == 1 && record.firstVertexOrIndex == 2 && record.vertexOffset == 3, "DRAW_INDEX_INDIRECT did not read its arguments from the indirect base");

    Storage wide;
    const auto* index = sceAgcDcbSetIndexIndirectArgs(&wide.buffer, records.data(), 0xfedcu);
    check(index[3] == 0xfedcu && wide.buffer.cursor_up == index + size / sizeof(std::uint32_t), "attribute index is not the packet's fourth dword");
    const auto before = wide.words;
    expectFailure([&] { sceAgcDcbSetIndexIndirectArgs(&wide.buffer, records.data() + 2, 0); });
    expectFailure([&] { sceAgcDcbSetIndexIndirectArgs(&wide.buffer, nullptr, 0); });
    expectFailure([&] { sceAgcDcbSetIndexIndirectArgs(nullptr, records.data(), 0); });
    check(wide.words == before && wide.buffer.cursor_up == index + size / sizeof(std::uint32_t), "invalid index indirect arguments modified the buffer");
}

void testContextState() {
    const std::array<std::array<std::uint32_t, 6>, 4> sizes{{{5}, {5, 8, 9, 3, 2}, {3, 5, 8, 9, 2}, {5, 8, 9, 3, 2, 5}}};
    const std::array<std::array<std::uint32_t, 4>, 4> reservations{{{5}, {22, 3, 2}, {3, 22, 2}, {22, 3, 2, 5}}};
    const std::array<std::uint32_t, 4> totals{5, 27, 27, 32};
    for (std::uint32_t operation = 0; operation < sizes.size(); ++operation) {
        for (std::uint32_t capacity = 0; capacity <= totals[operation]; ++capacity) {
            Storage source;
            source.words.fill(0xdeadbeefu);
            std::uint32_t split = 0;
            std::uint32_t requested = 0;
            for (const auto count : reservations[operation]) {
                if (count > capacity - split) {
                    requested = count;
                    break;
                }
                split += count;
            }
            ContextGrowth growth{{}, source.words.data() + split, requested + 2};
            growth.destination.words.fill(0xdeadbeefu);
            source.buffer.cursor_down = source.words.data() + capacity + 2;
            source.buffer.reserved_dw = 2;
            source.buffer.callback = growContext;
            source.buffer.user_data = &growth;
            auto* first = sceAgcDcbContextStateOp_0100(&source.buffer, operation);
            check(first == (split == 0 ? growth.destination.words.data() : source.words.data()), "incorrect first context packet address");
            check(growth.calls == (requested == 0 ? 0u : 1u), "incorrect context callback count");
            auto* end = requested == 0 ? source.words.data() + totals[operation] : growth.destination.words.data() + totals[operation] - split;
            check(source.buffer.cursor_up == end, "incorrect context cursor advance");
            check(*end == 0xdeadbeefu && source.words[split] == 0xdeadbeefu, "context allocation overwrote adjacent memory");
            std::uint32_t offset = 0;
            for (const auto count : sizes[operation]) {
                if (count == 0) {
                    break;
                }
                const auto* packet = offset < split ? source.words.data() + offset : growth.destination.words.data() + offset - split;
                const auto header = 0xc0001000u | ((count - 2u) << 16u) | (offset == 0 ? 0x68u : 0u);
                check(packet[0] == header, "incorrect context packet header");
                for (std::uint32_t i = 1; i < count; ++i) {
                    check(packet[i] == (offset == 0 && i == 1 ? operation : 0u), "incorrect context packet payload");
                }
                offset += count;
            }
        }
    }
    expectFailure([] { sceAgcDcbContextStateOp_0100(nullptr, 0); });
    Storage invalid;
    expectFailure([&] { sceAgcDcbContextStateOp_0100(&invalid.buffer, 4); });
    check(invalid.buffer.cursor_up == invalid.words.data(), "invalid context operation advanced cursor");
    invalid.buffer.cursor_up = invalid.words.data() + 1;
    invalid.buffer.cursor_down = invalid.words.data();
    expectFailure([&] { sceAgcDcbContextStateOp_0100(&invalid.buffer, 0); });
    invalid.buffer.cursor_up = invalid.words.data();
    expectFailure([&] { sceAgcDcbContextStateOp_0100(&invalid.buffer, 1); });
    ContextGrowth growth{{}, invalid.words.data(), 22};
    invalid.buffer.callback = growContext;
    invalid.buffer.user_data = &growth;
    growth.success = false;
    expectFailure([&] { sceAgcDcbContextStateOp_0100(&invalid.buffer, 1); });
    growth.calls = 0;
    growth.success = true;
    growth.destination.buffer.cursor_down = growth.destination.words.data() + 21;
    expectFailure([&] { sceAgcDcbContextStateOp_0100(&invalid.buffer, 1); });
    check(invalid.buffer.cursor_up == growth.destination.words.data(), "failed reservation advanced cursor");
}

void testFlip() {
    Storage storage;
    storage.words.fill(0xdeadbeefu);
    auto* packet = sceAgcDcbSetFlip(&storage.buffer, 0xfedcba98u, -2, 0x12345678u, -0x123456789abcdefLL);
    const std::array<std::uint32_t, 6> expected{0xc004105cu, 0xfedcba98u, 0xfffffffeu, 0x12345678u, 0x76543211u, 0xfedcba98u};
    check(packet == storage.words.data(), "flip returned wrong packet address");
    check(std::equal(expected.begin(), expected.end(), packet), "flip packet lost argument bits");
    check(storage.buffer.cursor_up == packet + 6 && packet[6] == 0xdeadbeefu, "flip packet overran allocation");
    expectFailure([] { sceAgcDcbSetFlip(nullptr, 1, 0, 1, 0); });
    Storage exhausted;
    exhausted.buffer.cursor_down = exhausted.words.data() + 5;
    expectFailure([&] { sceAgcDcbSetFlip(&exhausted.buffer, 1, 0, 1, 0); });
    check(exhausted.buffer.cursor_up == exhausted.words.data(), "failed flip allocation advanced cursor");
    Storage acb;
    acb.words.fill(0xdeadbeefu);
    auto* wait = sceAgcAcbWaitUntilSafeForRendering(&acb.buffer, 0xfedcba98u, 2);
    const std::array<std::uint32_t, 4> expectedWait{0xc0021018u, 0xfedcba98u, 2, 0};
    check(wait == acb.words.data() && std::equal(expectedWait.begin(), expectedWait.end(), wait), "compute rendering wait packet mismatch");
    check(acb.buffer.cursor_up == wait + 4 && wait[4] == 0xdeadbeefu, "compute rendering wait overran allocation");
    expectFailure([] { sceAgcAcbWaitUntilSafeForRendering(nullptr, 1, 0); });
    Storage full;
    full.buffer.cursor_down = full.words.data() + 3;
    expectFailure([&] { sceAgcAcbWaitUntilSafeForRendering(&full.buffer, 1, 0); });
    check(full.buffer.cursor_up == full.words.data(), "failed compute rendering wait advanced cursor");
    check(sceAgcSuspendPoint() == 0, "empty suspend failed");
}

void testRegisters() {
    Storage storage;
    const std::array<ShaderRegister, 3> registers{{{0x10, 7}, {0x11, 8}, {0x20, 9}}};
    Agc::Command::WriteRegisters(&storage.buffer, 0x76u, registers.data(), registers.size(), true, __func__);
    const std::array<std::uint32_t, 7> expected{0xc0027600u, 0x10, 7, 8, 0xc0017600u, 0x20, 9};
    check(std::equal(expected.begin(), expected.end(), storage.words.begin()), "register run packet mismatch");
    auto* packet = Agc::Command::WriteIndirectRegisters(&storage.buffer, 0x63u, registers.data(), 0x3ffeu, __func__);
    Agc::Command::PatchIndirectCount(packet, 0x63u, 1, __func__);
    check(packet[4] == 0x3fffu, "indirect register count mismatch");
    const auto before = storage.words;
    expectFailure([&] { Agc::Command::PatchIndirectCount(packet, 0x63u, 1, __func__); });
    expectFailure([&] { Agc::Command::PatchIndirectAddress(packet, 0x64u, registers.data(), __func__); });
    check(storage.words == before, "invalid indirect patch modified memory");
}

void testRegisterRange() {
    Storage storage;
    storage.words.fill(0xdeadbeefu);
    auto* packet = sceAgcCbSetShRegisterRangeDirect(&storage.buffer, 0x8c, nullptr, 4);
    auto expected = storage.words;
    expected.fill(0xdeadbeefu);
    expected[0] = 0xc0047600u;
    expected[1] = 0x8c;
    check(packet == storage.words.data(), "incorrect register range packet address");
    check(storage.buffer.cursor_up == storage.words.data() + 6, "incorrect register range allocation");
    check(storage.words == expected, "null register values modified payload or adjacent memory");
    const std::array<std::uint32_t, 4> values{1, 2, 3, 4};
    packet = sceAgcCbSetShRegisterRangeDirect(&storage.buffer, 0x90, values.data(), values.size());
    expected[6] = 0xc0047600u;
    expected[7] = 0x90;
    std::copy(values.begin(), values.end(), expected.begin() + 8);
    check(packet == storage.words.data() + 6 && storage.buffer.cursor_up == storage.words.data() + 12, "incorrect populated register range allocation");
    check(storage.words == expected, "register values were not copied correctly");
    const auto* misaligned = reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const unsigned char*>(values.data()) + 1);
    expectFailure([&] { sceAgcCbSetShRegisterRangeDirect(&storage.buffer, 0x8c, misaligned, 4); });
    check(storage.words == expected && storage.buffer.cursor_up == storage.words.data() + 12, "misaligned register values modified command buffer");
}

void testPacketPayloadAddress() {
    Storage storage;
    auto* packet = sceAgcCbSetShRegisterRangeDirect(&storage.buffer, 0x8c, nullptr, 4);
    std::uint32_t* payload = nullptr;
    check(sceAgcGetDataPacketPayloadAddress_0090(&payload, packet, 1) == 0 && payload == packet + 2, "incorrect register packet payload address");
    const std::array<std::uint32_t, 4> values{11, 22, 33, 44};
    std::copy(values.begin(), values.end(), payload);
    const std::array<std::uint32_t, 6> expected{0xc0047600u, 0x8c, 11, 22, 33, 44};
    check(std::equal(expected.begin(), expected.end(), packet), "payload write corrupted register packet");
    check(sceAgcGetDataPacketPayloadAddress_0090(&payload, packet, 0) == 0 && payload == packet + 1, "incorrect generic packet payload address");
    packet[0] = 0xffff1000u;
    check(sceAgcGetDataPacketPayloadAddress_0090(&payload, packet, 0) == 0 && payload == nullptr, "empty payload marker was not recognized");
    check(sceAgcGetDataPacketPayloadAddress_0090(&payload, packet, -1) == 0 && payload == packet + 2, "nonzero payload type did not skip two words");
    expectFailure([&] { sceAgcGetDataPacketPayloadAddress_0090(nullptr, packet, 1); });
    expectFailure([&] { sceAgcGetDataPacketPayloadAddress_0090(&payload, nullptr, 1); });
    auto* misaligned = reinterpret_cast<std::uint32_t*>(reinterpret_cast<unsigned char*>(packet) + 1);
    expectFailure([&] { sceAgcGetDataPacketPayloadAddress_0090(&payload, misaligned, 0); });
    check(payload == packet + 2, "invalid packet changed output address");
}

void testMemory() {
    Storage storage;
    Agc::Command::WriteDma(&storage.buffer, false, 1, 0, 0, 0x2000, 2, 0, 0x12345678, 16, 0, 1, 1, __func__);
    const std::array<std::uint32_t, 7> expected{0xc0055000u, 0xc0000001u, 0x12345678, 0, 0x2000, 0, 0x80000010u};
    check(std::equal(expected.begin(), expected.end(), storage.words.begin()), "DMA packet mismatch");
    std::uint64_t value = 0;
    auto* packet = Agc::Command::WriteWait(&storage.buffer, 1, 3, 0, 0, &value, 0x1122334455667788ull, 0xffffffffffffffffull, 32, __func__);
    check(packet[0] == 0xc0027901u && packet[4] == 0xc0079300u && packet[5] == 0x13u, "wait packet header mismatch");
    check(packet[8] == 0x55667788u && packet[9] == 0x11223344u && packet[12] == 2u, "wait reference or poll interval mismatch");
    sceAgcWaitRegMemPatchReference(packet, 7);
    check(packet[8] == 7 && packet[9] == 0x11223344u, "reference patch changed the high word");
    const auto before = storage.words;
    expectFailure([&] { sceAgcWaitRegMemPatchReference(packet, 0x100000000ull); });
    check(storage.words == before, "invalid memory operation modified packet memory");
    auto* truncated = Agc::Command::WriteWait(&storage.buffer, 0, 3, 0, 0, &value, 0x100000000ull, 0xffffffff00000001ull, 32, __func__);
    check(truncated[8] == 0u && truncated[9] == 1u, "32-bit wait did not keep the low halves of the reference and mask");
    sceAgcWaitRegMemPatchMask(packet, 0x0f0f0f0fu);
    check(packet[10] == 0x0f0f0f0fu && packet[11] == 0xffffffffu && packet[8] == 7, "64-bit mask patch changed the wrong word");
    sceAgcWaitRegMemPatchMask(truncated, 0xff00u);
    check(truncated[9] == 0xff00u && truncated[8] == 0u && truncated[10] == 2u, "32-bit mask patch changed the wrong word");
    const auto beforeMask = storage.words;
    expectFailure([&] { sceAgcWaitRegMemPatchMask(packet, 0x100000000ull); });
    expectFailure([&] { sceAgcWaitRegMemPatchMask(truncated, 0x100000000ull); });
    expectFailure([&] { sceAgcWaitRegMemPatchMask(packet + 4, 1); });
    check(storage.words == beforeMask, "invalid mask patch modified packet memory");
}

struct AtomicGdsAllocation {
    Storage storage;
    std::uint32_t capacity = 15;
    std::uint32_t calls = 0;
    bool accepted = true;
};

bool APS5_VABI growAtomicGds(CommandBuffer* buffer, std::uint32_t count, void* userData) {
    auto& allocation = *static_cast<AtomicGdsAllocation*>(userData);
    check(count == 15, "atomic GDS allocation omitted packet or reserved words");
    ++allocation.calls;
    if (!allocation.accepted) return false;
    buffer->bottom = allocation.storage.words.data();
    buffer->top = buffer->bottom + allocation.capacity;
    buffer->cursor_up = buffer->bottom;
    buffer->cursor_down = buffer->top;
    return true;
}

void testAtomicGds() {
    Storage storage;
    storage.words.fill(0xdeadbeefu);
    storage.buffer.cursor_up += 2;
    auto* packet = sceAgcAcbAtomicGds_0900(&storage.buffer, 0xffffffffu, 1, 0xffffffffu, 0xffffffffu,
        0xabcd, 0xfedc, 0x12345678u, 0x87654321u, 0xfedcba9876543210ull, 0x0123456789abcdefull);
    const std::array<std::uint32_t, 11> expected{
        0xc0091d00u, 0x001500ffu, 0x13fu, 0xabcdu, 0xfedcu, 0x00340078u,
        0x87654321u, 0x76543210u, 0xfedcba98u, 0x89abcdefu, 0x01234567u};
    check(packet == storage.words.data() + 2, "atomic GDS returned the wrong packet address");
    check(std::equal(expected.begin(), expected.end(), packet), "atomic GDS packet or guest ABI mismatch");
    check(storage.buffer.cursor_up == packet + expected.size(), "atomic GDS did not advance by eleven words");
    check(storage.words[0] == 0xdeadbeefu && storage.words[1] == 0xdeadbeefu &&
        std::all_of(storage.words.begin() + 13, storage.words.end(), [](auto word) { return word == 0xdeadbeefu; }),
        "atomic GDS wrote outside its packet");

    const std::array<std::array<std::uint32_t, 3>, 9> controls{{
        {0, 0, 0}, {0, 1, 0x00020000u}, {0, 2, 0}, {0, 0xffffffffu, 0},
        {0x20u, 1, 0x00040020u}, {0x40u, 1, 0x00120040u},
        {0x200u, 1, 0x00030000u}, {0xfffffd00u, 0, 0}, {0xffu, 1, 0x001400ffu}}};
    for (const auto& entry : controls) {
        Storage controlStorage;
        auto* controlPacket = sceAgcAcbAtomicGds_0900(&controlStorage.buffer, entry[0], entry[1],
            0xfffffffeu, 0xffffffc0u, 0, 0, 0xff00ff00u, 0, 0, 0);
        std::array<std::uint32_t, 11> controlExpected{};
        controlExpected[0] = 0xc0091d00u;
        controlExpected[1] = entry[2];
        check(std::equal(controlExpected.begin(), controlExpected.end(), controlPacket), "atomic GDS control or field masking mismatch");
    }

    auto emit = [](CommandBuffer* buffer) {
        return sceAgcAcbAtomicGds_0900(buffer, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    };
    Storage exact;
    exact.buffer.cursor_down = exact.words.data() + 15;
    exact.buffer.reserved_dw = 4;
    check(emit(&exact.buffer) == exact.words.data() && exact.buffer.cursor_up == exact.words.data() + 11,
        "atomic GDS rejected an exact fit with reserved space");
    const auto exactBefore = exact.words;
    expectFailure([&] { emit(&exact.buffer); });
    check(exact.words == exactBefore && exact.buffer.cursor_up == exact.words.data() + 11,
        "atomic GDS overwrote reserved space");
    Storage exhausted;
    exhausted.buffer.cursor_down = exhausted.words.data() + 10;
    const auto before = exhausted.words;
    expectFailure([&] { emit(&exhausted.buffer); });
    check(exhausted.words == before && exhausted.buffer.cursor_up == exhausted.words.data(),
        "failed atomic GDS allocation modified the buffer");
    expectFailure([&] { emit(nullptr); });

    Storage noDown;
    noDown.buffer.top = noDown.words.data() + 11;
    noDown.buffer.cursor_down = nullptr;
    check(emit(&noDown.buffer) == noDown.words.data() && noDown.buffer.cursor_up == noDown.buffer.top,
        "atomic GDS did not honor a buffer with no down cursor");

    AtomicGdsAllocation allocation;
    CommandBuffer growing{nullptr, nullptr, nullptr, nullptr, growAtomicGds, &allocation, 4};
    check(emit(&growing) == allocation.storage.words.data() && allocation.calls == 1 &&
        growing.cursor_up == allocation.storage.words.data() + 11, "atomic GDS did not use the guest allocation callback");
    const std::array<std::uint32_t, 11> zeroPacket{0xc0091d00u};
    check(std::equal(zeroPacket.begin(), zeroPacket.end(), allocation.storage.words.begin()),
        "atomic GDS callback allocation produced the wrong packet");
    for (const bool accepted : {false, true}) {
        AtomicGdsAllocation failure;
        failure.accepted = accepted;
        failure.capacity = 14;
        const auto failureBefore = failure.storage.words;
        CommandBuffer failed{nullptr, nullptr, nullptr, nullptr, growAtomicGds, &failure, 4};
        expectFailure([&] { emit(&failed); });
        check(failure.calls == 1 && failure.storage.words == failureBefore &&
            failed.cursor_up == (accepted ? failure.storage.words.data() : nullptr),
            "atomic GDS wrote a packet after failed or insufficient allocation");
    }
}

void testDebugRaiseException() {
    int result = -1;
    try {
        result = sceAgcDebugRaiseException(0x1234abcdu);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("debug exception stopped the caller: ") + error.what());
    }
    check(result == 0, "debug exception did not return 0");
}

void testDefaults() {
    std::uint32_t state = 0x12345678;
    check(sceAgcInit_0090(&state, 8) == 0 && state == 0x12345678, "AGC initialization failed or modified caller state");
    check(sceAgcInit_0090(&state, 13) == 0 && state == 0x12345678, "AGC version 13 initialization changed caller state");
    expectFailure([] { sceAgcInit_0090(nullptr, 8); });
    check(sceAgcInit_0090(&state, 14) == 0 && state == 0x12345678, "out-of-range AGC initialization version changed caller state");
    check(sceAgcInit(8) == 0, "AGC version initialization failed");
    check(sceAgcInit(13) == 0, "AGC version 13 initialization failed");
    check(sceAgcInit(14) == 0, "out-of-range AGC version did not return success");
    check(sceAgcInit(0xffffffffu) == 0, "maximum AGC version did not return success");
    check(sceAgcInit(0xffffc278u) == 0, "pointer-valued AGC argument did not return success");
    for (std::uint32_t version = 0; version < 14; ++version) {
        for (const bool internal : {false, true}) {
            auto* first = Agc::Command::GetRegisterDefaults(version, internal, __func__);
            check(first != nullptr && first == Agc::Command::GetRegisterDefaults(version, internal, __func__), "unstable register defaults pointer");
        }
    }
    auto* internalDefaults = sceAgcGetRegisterDefaultsInternal();
    check(internalDefaults != nullptr && internalDefaults == Agc::Command::GetRegisterDefaults(0, true, __func__) && internalDefaults == sceAgcGetRegisterDefaults2Internal(0), "internal register defaults are not the baseline internal table");
    check(internalDefaults != sceAgcGetRegisterDefaults(), "internal register defaults returned the public table");
    constexpr int kInvalidArgument = static_cast<int>(0x80890001u);
    std::array<std::uint32_t, 512> flatCxBuffer{};
    check(sceAgcGetDefaultCxStateFlat(nullptr, sizeof(flatCxBuffer)) == kInvalidArgument, "null destination was accepted");
    check(sceAgcGetDefaultCxStateFlat(flatCxBuffer.data(), 0) == 0, "zero size copy failed");
    check(sceAgcGetDefaultCxStateFlat(flatCxBuffer.data(), sizeof(flatCxBuffer)) == 0, "flat CX state default retrieval failed");
    check(flatCxBuffer[0x001] == 0x11000100u, "flat CX state register 0x001 mismatch");
    check(flatCxBuffer[0x007] == 0x0000ffffu, "flat CX state register 0x007 mismatch");
    check(flatCxBuffer[0x12d] == 0x3f800000u, "flat CX state register 0x12d mismatch");
    check(flatCxBuffer[0x1e0] == 0x20010001u, "flat CX state register 0x1e0 mismatch");
    std::array<std::uint32_t, 513> overBuffer{};
    overBuffer[512] = 0x12345678u;
    check(sceAgcGetDefaultCxStateFlat(overBuffer.data(), sizeof(overBuffer)) == 0, "oversized copy failed");
    check(overBuffer[512] == 0x12345678u, "oversized copy overflowed buffer");
    check(Agc::Command::GetDefaultCxStateFlat(nullptr, sizeof(flatCxBuffer)) == kInvalidArgument, "null command destination was accepted");
    check(Agc::Command::GetDefaultCxStateFlat(flatCxBuffer.data(), sizeof(flatCxBuffer)) == 0, "command flat CX state retrieval failed");
    expectFailure([] { Agc::Command::GetRegisterDefaults(14, false, __func__); });
    expectFailure([] { Agc::Command::GetRegisterDefaults(0xffffffffu, true, __func__); });
}

void testEventWrite() {
    Storage storage;
    const auto* samplePacket = sceAgcDcbEventWrite(&storage.buffer, 0x38, reinterpret_cast<const void*>(0x1000));
    check(samplePacket == storage.words.data(), "incorrect event 0x38 return pointer");
    check(storage.words[0] == 0xc0024600u, "incorrect event 0x38 packet header");
    check(storage.words[1] == (0x100u | 0x38u), "incorrect event 0x38 first payload word");
    check(storage.words[2] == 0x1000u, "incorrect event 0x38 low address word");
    check(storage.words[3] == 0u, "incorrect event 0x38 high address word");
    check(sceAgcDcbEventWriteGetSize(0x38) == 16, "incorrect event 0x38 size");
    check(sceAgcDcbEventWriteGetSize(0x39) == 16, "incorrect event 0x39 size");

    Storage sample39;
    sceAgcDcbEventWrite(&sample39.buffer, 0x39, reinterpret_cast<const void*>(0x2008));
    check(sample39.words[0] == 0xc0024600u && sample39.words[1] == (0x100u | 0x39u) && sample39.words[2] == 0x2008u && sample39.words[3] == 0u, "incorrect event 0x39 packet");

    expectFailure([&] { sceAgcDcbEventWrite(&storage.buffer, 0x38, reinterpret_cast<const void*>(0x1001)); });
    expectFailure([&] { sceAgcDcbEventWrite(&storage.buffer, 0x40, nullptr); });

    Storage partialFlush;
    sceAgcDcbEventWrite(&partialFlush.buffer, 7, nullptr);
    check(partialFlush.words[0] == 0xc0004600u && partialFlush.words[1] == 0x407u, "incorrect event 7 partial flush packet");
    check(sceAgcDcbEventWriteGetSize(7) == 8, "incorrect event 7 size");

    Storage partialFlush15;
    sceAgcDcbEventWrite(&partialFlush15.buffer, 15, nullptr);
    check(partialFlush15.words[0] == 0xc0004600u && partialFlush15.words[1] == 0x40fu, "incorrect event 15 partial flush packet");

    Storage partialFlush16;
    sceAgcDcbEventWrite(&partialFlush16.buffer, 16, nullptr);
    check(partialFlush16.words[0] == 0xc0004600u && partialFlush16.words[1] == 0x410u, "incorrect event 16 partial flush packet");

    Storage cacheFlushNull;
    sceAgcDcbEventWrite(&cacheFlushNull.buffer, 0x16, nullptr);
    check(cacheFlushNull.words[0] == 0xc0004600u && cacheFlushNull.words[1] == 0x16u, "incorrect event 0x16 null address packet");
    check(sceAgcDcbEventWriteGetSize(0x16) == 8, "incorrect event 0x16 size");

    Storage cacheFlushWithAddress;
    sceAgcDcbEventWrite(&cacheFlushWithAddress.buffer, 0x16, reinterpret_cast<const void*>(0x2d2a10000ull));
    check(cacheFlushWithAddress.words[0] == 0xc0004600u && cacheFlushWithAddress.words[1] == 0x16u, "incorrect event 0x16 with address packet");
}

}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::strcmp(argv[1], "defaults") == 0) {
            testDefaults();
            LibcRunShutdown_nid_postfix();
            std::puts("AGC initialization tests passed");
            return 0;
        }
        testPackets();
        testEventWrite();
        testNop();
        testClearState();
        testIndexedIndirectDraws();
        testMarkers();
        testMarkerSpans();
        testWorkloads();
        testWorkloadStreams();
        testRegisteredWorkloadStreams();
        testIndexBuffer();
        testIndirectBase();
        testIndexIndirectArgs();
        testContextState();
        testFlip();
        testRegisters();
        testRegisterRange();
        testPacketPayloadAddress();
        testMemory();
        testAtomicGds();
        testDebugRaiseException();
        testDefaults();
        LibcRunShutdown_nid_postfix();
        std::puts("AGC command tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        try { LibcRunShutdown_nid_postfix(); }
        catch (const std::exception& shutdown) { std::fprintf(stderr, "shutdown: %s\n", shutdown.what()); }
        return 1;
    }
}
