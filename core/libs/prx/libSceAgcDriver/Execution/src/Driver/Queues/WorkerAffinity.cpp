#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Queues/WorkerAffinity.hpp"
#include "prx/libSceAgcDriver/Execution/include/TracyZone.hpp"
#include "prx/libc/include/CpuTopology.hpp"
#include "prx/libc/include/general/ThreadName.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>

namespace AgcDriver::DriverDetail {

std::uint64_t WorkerAffinityMask() {
    static const std::uint64_t mask = [] {
        if (std::getenv("APS5_NO_WORKER_AFFINITY") != nullptr) return std::uint64_t{0};
        const auto requested = CpuTopology::MaskFromEnvironment("APS5_WORKER_AFFINITY_MASK");
        if (requested != 0) return requested;
#ifndef __linux__
        if (std::getenv("APS5_WORKER_AFFINITY") == nullptr) return std::uint64_t{0};
#endif
        const auto& layout = CpuTopology::Get();
        return layout.hybrid ? layout.performant : std::uint64_t{0};
    }();
    return mask;
}

#ifdef _WIN32
// Fork-only APS5_DRAW_CORE=1: queue worker 0x0 records every draw and is the frame's critical path,
// while a title's spinning job threads (Demon's Souls: 13 "BPE JobWorkerThread" at full load) share
// its cores. The worker gets the first core of the highest efficiency class to itself through CPU
// Sets: its own selection holds that core's first logical processor, and the process default (every
// thread without a selection, host and guest, created before or after) holds every other core.
// CPU Sets are scheduler preferences, so a hard affinity mask set elsewhere still applies.
void ReserveDrawCore() {
    using CpuSetInformationFn = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
    using ProcessDefaultCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
    using ThreadSelectedCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
    const HMODULE kernel = GetModuleHandleA("kernel32.dll");
    const auto GetSystemCpuSetInformation = reinterpret_cast<CpuSetInformationFn>(reinterpret_cast<void*>(GetProcAddress(kernel, "GetSystemCpuSetInformation")));
    const auto SetProcessDefaultCpuSets = reinterpret_cast<ProcessDefaultCpuSetsFn>(reinterpret_cast<void*>(GetProcAddress(kernel, "SetProcessDefaultCpuSets")));
    const auto SetThreadSelectedCpuSets = reinterpret_cast<ThreadSelectedCpuSetsFn>(reinterpret_cast<void*>(GetProcAddress(kernel, "SetThreadSelectedCpuSets")));
    if (GetSystemCpuSetInformation == nullptr || SetProcessDefaultCpuSets == nullptr || SetThreadSelectedCpuSets == nullptr) return;
    ULONG length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (length == 0) return;
    std::vector<unsigned char> buffer(length);
    auto* first = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data());
    if (!GetSystemCpuSetInformation(first, length, &length, GetCurrentProcess(), 0)) return;
    struct Entry {
        ULONG id;
        WORD group;
        BYTE logical;
        BYTE core;
        BYTE efficiency;
    };
    std::vector<Entry> entries;
    for (ULONG offset = 0; offset + sizeof(DWORD) * 2 <= length;) {
        const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
        if (info->Size == 0) break;
        if (info->Type == CpuSetInformation) entries.push_back({info->CpuSet.Id, info->CpuSet.Group, info->CpuSet.LogicalProcessorIndex, info->CpuSet.CoreIndex, info->CpuSet.EfficiencyClass});
        offset += info->Size;
    }
    if (entries.size() < 4) return;
    const Entry* chosen = nullptr;
    for (const auto& entry : entries) {
        if (chosen == nullptr || entry.efficiency > chosen->efficiency || (entry.efficiency == chosen->efficiency && (entry.group < chosen->group || (entry.group == chosen->group && entry.logical < chosen->logical)))) chosen = &entry;
    }
    std::vector<ULONG> others;
    for (const auto& entry : entries) {
        if (entry.group != chosen->group || entry.core != chosen->core) others.push_back(entry.id);
    }
    const bool process = SetProcessDefaultCpuSets(GetCurrentProcess(), others.data(), static_cast<ULONG>(others.size())) != 0;
    const bool thread = SetThreadSelectedCpuSets(GetCurrentThread(), &chosen->id, 1) != 0;
    std::fprintf(stderr, "[affinity] APS5_DRAW_CORE: queue worker 0x0 -> logical processor %u (core %u, efficiency class %u)%s; every other thread -> %zu of %zu CPU sets%s\n", static_cast<unsigned>(chosen->logical), static_cast<unsigned>(chosen->core), static_cast<unsigned>(chosen->efficiency), thread ? "" : " (selection refused)", others.size(), entries.size(), process ? "" : " (process default refused)");
}
#endif

void PinWorkerThread(const char* role) {
    TracyZone::NameThread(role);
#ifdef _WIN32
    const HRESULT nameResult = ThreadName::SetCurrent(role);
    if (FAILED(nameResult)) std::fprintf(stderr, "[thread-name] could not name %s (0x%08lx)\n", role, static_cast<unsigned long>(nameResult));
    static const bool drawCore = std::getenv("APS5_DRAW_CORE") != nullptr;
    if (drawCore && std::strcmp(role, "queue worker 0x0") == 0) ReserveDrawCore();
#endif
    const auto mask = WorkerAffinityMask();
    if (mask == 0) return;
    static std::once_flag summary;
    std::call_once(summary, [mask] {
        const auto& layout = CpuTopology::Get();
        std::fprintf(stderr, "[affinity] queue workers and presenter -> 0x%llx (hybrid=%d efficient=0x%llx performant=0x%llx process=0x%llx)\n", static_cast<unsigned long long>(mask), layout.hybrid ? 1 : 0, static_cast<unsigned long long>(layout.efficient), static_cast<unsigned long long>(layout.performant), static_cast<unsigned long long>(layout.process));
    });
    CpuTopology::PinTraced(role, nullptr, mask);
}

}
