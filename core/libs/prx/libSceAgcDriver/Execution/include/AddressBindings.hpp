#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_ADDRESSBINDINGS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_ADDRESSBINDINGS_HPP

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace AgcDriver {

// Fork-only APS5_TRACE_ADDRESS_BINDINGS=1: records every GPU virtual address range the driver binds
// and unbinds (VK_EXT_device_address_binding_report), so a [device-fault] address can be matched
// against the object that owned it, live or already unbound.
inline bool AddressBindingsEnabled() {
    static const bool enabled = std::getenv("APS5_TRACE_ADDRESS_BINDINGS") != nullptr;
    return enabled;
}

struct AddressBinding {
    VkDeviceAddress base = 0;
    VkDeviceSize size = 0;
    VkObjectType type = VK_OBJECT_TYPE_UNKNOWN;
    std::uint64_t handle = 0;
    bool internal = false;
    double boundMs = 0;
    double unboundMs = -1;
};

struct AddressBindingLog {
    static constexpr std::size_t UnboundCapacity = 1u << 18;
    std::mutex mutex;
    std::multimap<VkDeviceAddress, AddressBinding> live;
    std::vector<AddressBinding> unbound = std::vector<AddressBinding>(UnboundCapacity);
    std::size_t unboundNext = 0;
    std::uint64_t binds = 0, unbinds = 0, unmatched = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

inline AddressBindingLog& AddressBindings() {
    static auto* log = new AddressBindingLog;
    return *log;
}

// What vkCreateImage was asked for, by image handle, with the creating call stack (return
// addresses; symbolize offline with addr2line against the module the address falls in).
struct TracedImage {
    VkImageCreateInfo info{};
    std::array<void*, 10> stack{};
    std::uint16_t frames = 0;
};

struct TracedImages {
    std::mutex mutex;
    std::map<std::uint64_t, TracedImage> images;
    PFN_vkGetDeviceProcAddr deviceProc = nullptr;
    PFN_vkCreateImage createImage = nullptr;
};

inline TracedImages& ImageTrace() {
    static auto* trace = new TracedImages;
    return *trace;
}

inline VKAPI_ATTR VkResult VKAPI_CALL TracedCreateImage(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* allocator, VkImage* image) {
    auto& trace = ImageTrace();
    const auto result = trace.createImage(device, info, allocator, image);
    if (result != VK_SUCCESS || info == nullptr || image == nullptr) return result;
    TracedImage entry;
    entry.info = *info;
    entry.info.pNext = nullptr;
    entry.info.pQueueFamilyIndices = nullptr;
#ifdef _WIN32
    entry.frames = RtlCaptureStackBackTrace(1, static_cast<DWORD>(entry.stack.size()), entry.stack.data(), nullptr);
#endif
    std::lock_guard lock(trace.mutex);
    trace.images[reinterpret_cast<std::uint64_t>(*image)] = entry;
    return result;
}

// Fork-only APS5_TRACE_VK_MEMORY=1: live vkAllocateMemory bytes by memory type and by allocating
// call site (host-pointer imports counted apart: they map guest memory instead of adding to it),
// printed every 30 s as [vk-memory].
inline bool VkMemoryTraceEnabled() {
    static const bool enabled = std::getenv("APS5_TRACE_VK_MEMORY") != nullptr;
    return enabled;
}

struct TracedMemory {
    VkDeviceSize bytes = 0;
    std::uint32_t type = 0;
    bool imported = false;
    std::uint64_t site = 0;
};

struct MemorySite {
    std::array<void*, 6> stack{};
    std::uint16_t frames = 0;
    VkDeviceSize liveBytes = 0;
    std::uint64_t liveCount = 0;
    std::uint64_t totalCount = 0;
};

struct MemoryTrace {
    std::mutex mutex;
    std::map<std::uint64_t, TracedMemory> live;
    std::map<std::uint64_t, MemorySite> sites;
    std::array<VkDeviceSize, VK_MAX_MEMORY_TYPES> typeBytes{};
    VkDeviceSize importedBytes = 0;
    PFN_vkAllocateMemory allocate = nullptr;
    PFN_vkFreeMemory free = nullptr;
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
};

inline MemoryTrace& VkMemory() {
    static auto* trace = new MemoryTrace;
    return *trace;
}

inline std::string StackText(const void* const* stack, std::uint16_t frames) {
    std::string text;
    char part[160];
    for (std::uint16_t i = 0; i < frames; ++i) {
        const auto address = reinterpret_cast<std::uintptr_t>(stack[i]);
#ifdef _WIN32
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module) && module != nullptr) {
            GetModuleFileNameA(module, name, sizeof(name));
            const char* base = std::strrchr(name, '\\');
            std::snprintf(part, sizeof(part), " %s+0x%llx", base != nullptr ? base + 1 : name, static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
            text += part;
            continue;
        }
#endif
        std::snprintf(part, sizeof(part), " 0x%llx", static_cast<unsigned long long>(address));
        text += part;
    }
    return text;
}

inline void ReportVkMemoryLocked(MemoryTrace& trace) {
    const auto now = std::chrono::steady_clock::now();
    if (now - trace.lastReport < std::chrono::seconds(30)) return;
    trace.lastReport = now;
    std::string types;
    char part[96];
    for (std::uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
        if (trace.typeBytes[i] == 0) continue;
        std::snprintf(part, sizeof(part), " type%u %.0f MiB", i, static_cast<double>(trace.typeBytes[i]) / 1048576.0);
        types += part;
    }
    std::fprintf(stderr, "[vk-memory] %zu live allocations:%s; host-pointer imports %.0f MiB (not counted above)\n", trace.live.size(), types.c_str(), static_cast<double>(trace.importedBytes) / 1048576.0);
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        const double privateMiB = static_cast<double>(counters.PrivateUsage) / 1048576.0;
        const double arenaMiB = static_cast<double>(GuestMemory::ArenaCommittedBytes()) / 1048576.0;
        std::fprintf(stderr, "[vk-memory] process private %.0f MiB: guest arena committed %.0f MiB, the rest %.0f MiB (driver heap, Vulkan system memory, loader)\n", privateMiB, arenaMiB, privateMiB - arenaMiB);
    }
    // Fork-only APS5_MIMALLOC_STATS=1: every 5 minutes, mimalloc's own statistics (what the
    // allocator that serves malloc and new holds, against the process private bytes above).
    static const bool mimallocStats = std::getenv("APS5_MIMALLOC_STATS") != nullptr;
    static auto lastMimallocStats = std::chrono::steady_clock::time_point{};
    if (mimallocStats && now - lastMimallocStats >= std::chrono::minutes(5)) {
        lastMimallocStats = now;
        using OutputFn = void (*)(const char*, void*);
        using StatsPrintFn = void (*)(OutputFn, void*);
        static const auto statsPrint = [] {
            const HMODULE module = GetModuleHandleA("mimalloc.dll");
            return module != nullptr ? reinterpret_cast<StatsPrintFn>(reinterpret_cast<void*>(GetProcAddress(module, "mi_stats_print_out"))) : nullptr;
        }();
        if (statsPrint != nullptr) {
            std::fprintf(stderr, "[mimalloc-stats] begin\n");
            statsPrint([](const char* text, void*) { std::fputs(text, stderr); }, nullptr);
            std::fprintf(stderr, "[mimalloc-stats] end\n");
        }
    }
    // Where "the rest" lives: committed bytes of each Win32 heap (the CRT's malloc is the process
    // heap; drivers create their own), and private committed memory outside every heap
    // (VirtualAlloc'd regions, the guest arena among them).
    struct HeapSummaryInfo {
        DWORD cb;
        SIZE_T cbAllocated;
        SIZE_T cbCommitted;
        SIZE_T cbReserved;
        SIZE_T cbMaxReserve;
    };
    using HeapSummaryFn = BOOL(WINAPI*)(HANDLE, DWORD, HeapSummaryInfo*);
    static const auto heapSummary = reinterpret_cast<HeapSummaryFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "HeapSummary")));
    std::vector<HANDLE> heaps(GetProcessHeaps(0, nullptr) + 16);
    heaps.resize(GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data()));
    std::vector<std::pair<SIZE_T, std::string>> heapLines;
    SIZE_T heapCommitted = 0;
    const HANDLE processHeap = GetProcessHeap();
    for (const auto heap : heaps) {
        HeapSummaryInfo summary{sizeof(HeapSummaryInfo), 0, 0, 0, 0};
        if (heapSummary == nullptr || !heapSummary(heap, 0, &summary)) continue;
        heapCommitted += summary.cbCommitted;
        char text[160];
        std::snprintf(text, sizeof(text), " %p%s %.0f/%.0f MiB", heap, heap == processHeap ? " (process heap, malloc)" : "", static_cast<double>(summary.cbAllocated) / 1048576.0, static_cast<double>(summary.cbCommitted) / 1048576.0);
        heapLines.emplace_back(summary.cbCommitted, text);
    }
    std::sort(heapLines.begin(), heapLines.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string heapText;
    for (std::size_t i = 0; i < heapLines.size() && i < 5; ++i) heapText += heapLines[i].second;
    SIZE_T privateCommitted = 0;
    std::uint64_t regions = 0;
    MEMORY_BASIC_INFORMATION region{};
    for (auto* at = static_cast<char*>(nullptr); VirtualQuery(at, &region, sizeof(region)) == sizeof(region); at = static_cast<char*>(region.BaseAddress) + region.RegionSize) {
        if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE) {
            privateCommitted += region.RegionSize;
            ++regions;
        }
        if (region.RegionSize == 0) break;
    }
    std::fprintf(stderr, "[vk-memory] %zu heaps %.0f MiB committed (allocated/committed, largest:%s); private committed outside heaps and the guest arena %.0f MiB in %llu regions\n", heapLines.size(), static_cast<double>(heapCommitted) / 1048576.0, heapText.c_str(), (static_cast<double>(privateCommitted) - static_cast<double>(heapCommitted)) / 1048576.0 - static_cast<double>(GuestMemory::ArenaCommittedBytes()) / 1048576.0, static_cast<unsigned long long>(regions));
#endif
    std::vector<const MemorySite*> ranked;
    for (const auto& [key, site] : trace.sites) if (site.liveBytes != 0) ranked.push_back(&site);
    std::sort(ranked.begin(), ranked.end(), [](const MemorySite* a, const MemorySite* b) { return a->liveBytes > b->liveBytes; });
    for (std::size_t i = 0; i < ranked.size() && i < 10; ++i) {
        std::fprintf(stderr, "[vk-memory]   %.0f MiB in %llu live (%llu made):%s\n", static_cast<double>(ranked[i]->liveBytes) / 1048576.0, static_cast<unsigned long long>(ranked[i]->liveCount), static_cast<unsigned long long>(ranked[i]->totalCount), StackText(ranked[i]->stack.data(), ranked[i]->frames).c_str());
    }
}

inline VKAPI_ATTR VkResult VKAPI_CALL TracedAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory) {
    auto& trace = VkMemory();
    const auto result = trace.allocate(device, info, allocator, memory);
    if (result != VK_SUCCESS || info == nullptr || memory == nullptr) return result;
    TracedMemory entry{info->allocationSize, info->memoryTypeIndex, false, 0};
    for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next != nullptr; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) entry.imported = true;
    }
    MemorySite site;
#ifdef _WIN32
    site.frames = RtlCaptureStackBackTrace(1, static_cast<DWORD>(site.stack.size()), site.stack.data(), nullptr);
#endif
    std::uint64_t key = 0xcbf29ce484222325ull ^ (entry.imported ? 1u : 0u);
    for (std::uint16_t i = 0; i < site.frames; ++i) key = (key ^ reinterpret_cast<std::uintptr_t>(site.stack[i])) * 0x100000001b3ull;
    entry.site = key;
    std::lock_guard lock(trace.mutex);
    auto& kept = trace.sites[key];
    if (kept.totalCount == 0) {
        kept.stack = site.stack;
        kept.frames = site.frames;
    }
    ++kept.totalCount;
    if (!entry.imported) {
        kept.liveBytes += entry.bytes;
        ++kept.liveCount;
        if (entry.type < VK_MAX_MEMORY_TYPES) trace.typeBytes[entry.type] += entry.bytes;
    } else {
        trace.importedBytes += entry.bytes;
    }
    trace.live[reinterpret_cast<std::uint64_t>(*memory)] = entry;
    ReportVkMemoryLocked(trace);
    return result;
}

inline VKAPI_ATTR void VKAPI_CALL TracedFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator) {
    auto& trace = VkMemory();
    {
        std::lock_guard lock(trace.mutex);
        if (const auto found = trace.live.find(reinterpret_cast<std::uint64_t>(memory)); found != trace.live.end()) {
            const auto& entry = found->second;
            if (entry.imported) trace.importedBytes -= entry.bytes;
            else {
                if (entry.type < VK_MAX_MEMORY_TYPES) trace.typeBytes[entry.type] -= entry.bytes;
                auto& site = trace.sites[entry.site];
                site.liveBytes -= entry.bytes;
                --site.liveCount;
            }
            trace.live.erase(found);
        }
    }
    trace.free(device, memory, allocator);
}

// With APS5_TRACE_ADDRESS_BINDINGS: the creating and destroying call stacks of every buffer and
// device memory, and a memory's imported host pointer (the guest address of a host import), kept
// after destruction so a fault in an unbound range names the code that made and freed its owner.
// Handles are reused, so an entry created after the binding it is asked about is reported as such.
struct ObjectLife {
    VkDeviceSize size = 0;
    std::uint32_t usage = 0;
    std::uint32_t memoryType = 0;
    std::uint64_t hostPointer = 0;
    bool imported = false;
    double createdMs = 0;
    double destroyedMs = -1;
    std::array<void*, 8> created{};
    std::array<void*, 8> destroyed{};
    std::uint16_t createdFrames = 0;
    std::uint16_t destroyedFrames = 0;
};

struct LifeTrace {
    std::mutex mutex;
    std::map<std::uint64_t, ObjectLife> objects;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkAllocateMemory allocate = nullptr;
    PFN_vkFreeMemory free = nullptr;
};

inline LifeTrace& ObjectLives() {
    static auto* trace = new LifeTrace;
    return *trace;
}

inline double AddressBindingNowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - AddressBindings().start).count();
}

inline void RecordCreated(std::uint64_t handle, ObjectLife life) {
    life.createdMs = AddressBindingNowMs();
#ifdef _WIN32
    life.createdFrames = RtlCaptureStackBackTrace(1, static_cast<DWORD>(life.created.size()), life.created.data(), nullptr);
#endif
    auto& trace = ObjectLives();
    std::lock_guard lock(trace.mutex);
    trace.objects[handle] = life;
}

inline void RecordDestroyed(std::uint64_t handle) {
    std::array<void*, 8> stack{};
    std::uint16_t frames = 0;
#ifdef _WIN32
    frames = RtlCaptureStackBackTrace(1, static_cast<DWORD>(stack.size()), stack.data(), nullptr);
#endif
    const double now = AddressBindingNowMs();
    auto& trace = ObjectLives();
    std::lock_guard lock(trace.mutex);
    const auto found = trace.objects.find(handle);
    if (found == trace.objects.end()) return;
    found->second.destroyedMs = now;
    found->second.destroyed = stack;
    found->second.destroyedFrames = frames;
}

inline VKAPI_ATTR VkResult VKAPI_CALL LifeCreateBuffer(VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* allocator, VkBuffer* buffer) {
    const auto result = ObjectLives().createBuffer(device, info, allocator, buffer);
    if (result != VK_SUCCESS || info == nullptr || buffer == nullptr) return result;
    ObjectLife life;
    life.size = info->size;
    life.usage = static_cast<std::uint32_t>(info->usage);
    RecordCreated(reinterpret_cast<std::uint64_t>(*buffer), life);
    return result;
}

inline VKAPI_ATTR void VKAPI_CALL LifeDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator) {
    if (buffer != VK_NULL_HANDLE) RecordDestroyed(reinterpret_cast<std::uint64_t>(buffer));
    ObjectLives().destroyBuffer(device, buffer, allocator);
}

inline VKAPI_ATTR VkResult VKAPI_CALL LifeAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory) {
    const auto result = VkMemoryTraceEnabled() ? TracedAllocateMemory(device, info, allocator, memory) : ObjectLives().allocate(device, info, allocator, memory);
    if (result != VK_SUCCESS || info == nullptr || memory == nullptr) return result;
    ObjectLife life;
    life.size = info->allocationSize;
    life.memoryType = info->memoryTypeIndex;
    for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next != nullptr; next = next->pNext) {
        if (next->sType != VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) continue;
        life.imported = true;
        life.hostPointer = reinterpret_cast<std::uint64_t>(reinterpret_cast<const VkImportMemoryHostPointerInfoEXT*>(next)->pHostPointer);
    }
    RecordCreated(reinterpret_cast<std::uint64_t>(*memory), life);
    return result;
}

inline VKAPI_ATTR void VKAPI_CALL LifeFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator) {
    if (memory != VK_NULL_HANDLE) RecordDestroyed(reinterpret_cast<std::uint64_t>(memory));
    if (VkMemoryTraceEnabled()) TracedFreeMemory(device, memory, allocator);
    else ObjectLives().free(device, memory, allocator);
}

// The traced life of `handle` as asked about for a binding made at `boundMs`.
inline std::string ObjectLifeText(std::uint64_t handle, double boundMs) {
    auto& trace = ObjectLives();
    std::lock_guard lock(trace.mutex);
    const auto found = trace.objects.find(handle);
    if (found == trace.objects.end()) return "  (creation not traced)";
    const auto& life = found->second;
    char line[256];
    std::snprintf(line, sizeof(line), "  size 0x%llx usage 0x%x memory type %u%s host 0x%llx; created %.0f ms%s; destroyed %s", static_cast<unsigned long long>(life.size), life.usage, life.memoryType, life.imported ? " IMPORTED" : "", static_cast<unsigned long long>(life.hostPointer), life.createdMs, boundMs >= 0 && life.createdMs > boundMs + 1 ? " (AFTER the binding: handle reused, this is a later object)" : "", life.destroyedMs < 0 ? "no" : "at");
    std::string text = line;
    if (life.destroyedMs >= 0) {
        std::snprintf(line, sizeof(line), " %.0f ms", life.destroyedMs);
        text += line;
    }
    text += "\n[address-bindings]    created by:" + StackText(life.created.data(), life.createdFrames);
    if (life.destroyedMs >= 0) text += "\n[address-bindings]    destroyed by:" + StackText(life.destroyed.data(), life.destroyedFrames);
    return text;
}

inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL TracedDeviceProc(VkDevice device, const char* name) {
    auto& trace = ImageTrace();
    if (name != nullptr && AddressBindingsEnabled()) {
        auto& lives = ObjectLives();
        if (std::strcmp(name, "vkCreateBuffer") == 0) {
            if (lives.createBuffer == nullptr) lives.createBuffer = reinterpret_cast<PFN_vkCreateBuffer>(trace.deviceProc(device, name));
            if (lives.createBuffer != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(LifeCreateBuffer);
        }
        if (std::strcmp(name, "vkDestroyBuffer") == 0) {
            if (lives.destroyBuffer == nullptr) lives.destroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(trace.deviceProc(device, name));
            if (lives.destroyBuffer != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(LifeDestroyBuffer);
        }
        if (std::strcmp(name, "vkAllocateMemory") == 0) {
            if (lives.allocate == nullptr) lives.allocate = reinterpret_cast<PFN_vkAllocateMemory>(trace.deviceProc(device, name));
            if (VkMemoryTraceEnabled() && VkMemory().allocate == nullptr) VkMemory().allocate = lives.allocate;
            if (lives.allocate != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(LifeAllocateMemory);
        }
        if (std::strcmp(name, "vkFreeMemory") == 0) {
            if (lives.free == nullptr) lives.free = reinterpret_cast<PFN_vkFreeMemory>(trace.deviceProc(device, name));
            if (VkMemoryTraceEnabled() && VkMemory().free == nullptr) VkMemory().free = lives.free;
            if (lives.free != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(LifeFreeMemory);
        }
    }
    if (name != nullptr && VkMemoryTraceEnabled()) {
        auto& memory = VkMemory();
        if (std::strcmp(name, "vkAllocateMemory") == 0) {
            if (memory.allocate == nullptr) memory.allocate = reinterpret_cast<PFN_vkAllocateMemory>(trace.deviceProc(device, name));
            if (memory.allocate != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(TracedAllocateMemory);
        }
        if (std::strcmp(name, "vkFreeMemory") == 0) {
            if (memory.free == nullptr) memory.free = reinterpret_cast<PFN_vkFreeMemory>(trace.deviceProc(device, name));
            if (memory.free != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(TracedFreeMemory);
        }
    }
    if (name != nullptr && AddressBindingsEnabled() && std::strcmp(name, "vkCreateImage") == 0) {
        if (trace.createImage == nullptr) trace.createImage = reinterpret_cast<PFN_vkCreateImage>(trace.deviceProc(device, name));
        if (trace.createImage != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(TracedCreateImage);
    }
    return trace.deviceProc(device, name);
}

// Wraps the device function resolver so vkCreateImage is traced; returns the resolver to use.
inline PFN_vkGetDeviceProcAddr TraceImageCreation(PFN_vkGetDeviceProcAddr deviceProc) {
    if ((!AddressBindingsEnabled() && !VkMemoryTraceEnabled()) || deviceProc == nullptr) return deviceProc;
    ImageTrace().deviceProc = deviceProc;
    return TracedDeviceProc;
}

inline std::string TracedImageText(std::uint64_t handle) {
    auto& trace = ImageTrace();
    std::lock_guard lock(trace.mutex);
    const auto found = trace.images.find(handle);
    if (found == trace.images.end()) return "  (image creation not traced)";
    const auto& info = found->second.info;
    char line[512];
    int used = std::snprintf(line, sizeof(line), "  created as type %d %ux%ux%u mips %u layers %u format %d samples %d usage 0x%x flags 0x%x tiling %d; stack", static_cast<int>(info.imageType), info.extent.width, info.extent.height, info.extent.depth, info.mipLevels, info.arrayLayers, static_cast<int>(info.format), static_cast<int>(info.samples), static_cast<unsigned>(info.usage), static_cast<unsigned>(info.flags), static_cast<int>(info.tiling));
    for (std::uint16_t i = 0; i < found->second.frames && used > 0 && used < static_cast<int>(sizeof(line)) - 40; ++i) {
        const auto address = reinterpret_cast<std::uintptr_t>(found->second.stack[i]);
#ifdef _WIN32
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module) && module != nullptr) {
            GetModuleFileNameA(module, name, sizeof(name));
            const char* base = std::strrchr(name, '\\');
            used += std::snprintf(line + used, sizeof(line) - used, " %s+0x%llx", base != nullptr ? base + 1 : name, static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
            continue;
        }
#endif
        used += std::snprintf(line + used, sizeof(line) - used, " 0x%llx", static_cast<unsigned long long>(address));
    }
    return line;
}

inline const char* AddressBindingObjectName(VkObjectType type) {
    switch (type) {
    case VK_OBJECT_TYPE_BUFFER: return "buffer";
    case VK_OBJECT_TYPE_IMAGE: return "image";
    case VK_OBJECT_TYPE_DEVICE_MEMORY: return "memory";
    case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR: return "acceleration structure";
    default: return "object";
    }
}

inline VKAPI_ATTR VkBool32 VKAPI_CALL AddressBindingCallback(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if ((types & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) == 0 || data == nullptr) return VK_FALSE;
    const VkDeviceAddressBindingCallbackDataEXT* binding = nullptr;
    for (auto* next = static_cast<const VkBaseInStructure*>(data->pNext); next != nullptr; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT) binding = reinterpret_cast<const VkDeviceAddressBindingCallbackDataEXT*>(next);
    }
    if (binding == nullptr) return VK_FALSE;
    auto& log = AddressBindings();
    const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - log.start).count();
    AddressBinding entry{binding->baseAddress, binding->size, VK_OBJECT_TYPE_UNKNOWN, 0, (binding->flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT) != 0, now, -1};
    if (data->objectCount != 0 && data->pObjects != nullptr) {
        entry.type = data->pObjects[0].objectType;
        entry.handle = data->pObjects[0].objectHandle;
    }
    std::lock_guard lock(log.mutex);
    if (binding->bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT) {
        ++log.binds;
        log.live.emplace(entry.base, entry);
        return VK_FALSE;
    }
    ++log.unbinds;
    auto [first, last] = log.live.equal_range(entry.base);
    auto found = last;
    for (auto it = first; it != last; ++it) {
        if (it->second.size == entry.size && (entry.handle == 0 || it->second.handle == entry.handle)) {
            found = it;
            break;
        }
    }
    if (found != last) {
        entry.boundMs = found->second.boundMs;
        if (entry.handle == 0) {
            entry.type = found->second.type;
            entry.handle = found->second.handle;
        }
        log.live.erase(found);
    } else {
        ++log.unmatched;
        entry.boundMs = -1;
    }
    entry.unboundMs = now;
    log.unbound[log.unboundNext % AddressBindingLog::UnboundCapacity] = entry;
    ++log.unboundNext;
    return VK_FALSE;
}

inline std::string AddressBindingText(const AddressBinding& entry, VkDeviceAddress address, double now) {
    char line[320];
    const long long offset = static_cast<long long>(address) - static_cast<long long>(entry.base);
    if (entry.unboundMs < 0) {
        std::snprintf(line, sizeof(line), "%s 0x%llx at 0x%llx+0x%llx (fault at %+lld), bound %.0f ms before the report%s", AddressBindingObjectName(entry.type), static_cast<unsigned long long>(entry.handle), static_cast<unsigned long long>(entry.base), static_cast<unsigned long long>(entry.size), offset, now - entry.boundMs, entry.internal ? ", driver-internal" : "");
    } else {
        std::snprintf(line, sizeof(line), "%s 0x%llx at 0x%llx+0x%llx (fault at %+lld), UNBOUND %.0f ms before the report (bound for %.0f ms)%s", AddressBindingObjectName(entry.type), static_cast<unsigned long long>(entry.handle), static_cast<unsigned long long>(entry.base), static_cast<unsigned long long>(entry.size), offset, now - entry.unboundMs, entry.boundMs < 0 ? -1.0 : entry.unboundMs - entry.boundMs, entry.internal ? ", driver-internal" : "");
    }
    return line;
}

// Every live or recently unbound range that covers [address, address + precision), plus the nearest
// live neighbours on each side when nothing covers it.
inline std::vector<std::string> AddressBindingReport(VkDeviceAddress address, VkDeviceSize precision) {
    std::vector<std::string> lines;
    if (!AddressBindingsEnabled()) return lines;
    auto& log = AddressBindings();
    std::lock_guard lock(log.mutex);
    const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - log.start).count();
    const VkDeviceAddress end = address + (precision != 0 ? precision : 1);
    const auto overlaps = [&](const AddressBinding& entry) { return entry.base < end && address < entry.base + entry.size; };
    char line[256];
    std::snprintf(line, sizeof(line), "[address-bindings] fault 0x%llx+0x%llx; %llu binds, %llu unbinds (%llu unmatched), %zu live", static_cast<unsigned long long>(address), static_cast<unsigned long long>(precision), static_cast<unsigned long long>(log.binds), static_cast<unsigned long long>(log.unbinds), static_cast<unsigned long long>(log.unmatched), log.live.size());
    lines.emplace_back(line);
    std::size_t liveHits = 0;
    for (const auto& [base, entry] : log.live) {
        if (overlaps(entry)) {
            lines.emplace_back("[address-bindings] live:    " + AddressBindingText(entry, address, now));
            if (entry.type == VK_OBJECT_TYPE_IMAGE) lines.emplace_back("[address-bindings]" + TracedImageText(entry.handle));
            else if (entry.type == VK_OBJECT_TYPE_BUFFER || entry.type == VK_OBJECT_TYPE_DEVICE_MEMORY) lines.emplace_back("[address-bindings]" + ObjectLifeText(entry.handle, entry.boundMs));
            ++liveHits;
        }
    }
    const std::size_t kept = std::min<std::size_t>(log.unboundNext, AddressBindingLog::UnboundCapacity);
    std::size_t unboundHits = 0;
    for (std::size_t i = 0; i < kept && unboundHits < 16; ++i) {
        const auto& entry = log.unbound[(log.unboundNext - 1 - i) % AddressBindingLog::UnboundCapacity];
        if (overlaps(entry)) {
            lines.emplace_back("[address-bindings] unbound: " + AddressBindingText(entry, address, now));
            if (entry.type == VK_OBJECT_TYPE_BUFFER || entry.type == VK_OBJECT_TYPE_DEVICE_MEMORY) lines.emplace_back("[address-bindings]" + ObjectLifeText(entry.handle, entry.boundMs));
            ++unboundHits;
        }
    }
    if (kept != 0) {
        const auto& oldest = log.unbound[(log.unboundNext - kept) % AddressBindingLog::UnboundCapacity];
        std::snprintf(line, sizeof(line), "[address-bindings] unbound history covers the last %.0f ms (%zu entries)", now - oldest.unboundMs, kept);
        lines.emplace_back(line);
    }
    if (liveHits == 0) {
        auto above = log.live.lower_bound(address);
        auto below = above;
        for (int i = 0; i < 3 && below != log.live.begin(); ++i) {
            --below;
            lines.emplace_back("[address-bindings] below:   " + AddressBindingText(below->second, address, now));
            if (below->second.type == VK_OBJECT_TYPE_IMAGE) lines.emplace_back("[address-bindings]" + TracedImageText(below->second.handle));
        }
        for (int i = 0; i < 3 && above != log.live.end(); ++i, ++above) {
            lines.emplace_back("[address-bindings] above:   " + AddressBindingText(above->second, address, now));
            if (above->second.type == VK_OBJECT_TYPE_IMAGE) lines.emplace_back("[address-bindings]" + TracedImageText(above->second.handle));
        }
    }
    if (liveHits == 0 && unboundHits == 0) lines.emplace_back("[address-bindings] no live or remembered binding covers the fault address");
    return lines;
}

}

#endif
