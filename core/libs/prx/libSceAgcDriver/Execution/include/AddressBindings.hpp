#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_ADDRESSBINDINGS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_ADDRESSBINDINGS_HPP

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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

inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL TracedDeviceProc(VkDevice device, const char* name) {
    auto& trace = ImageTrace();
    if (name != nullptr && std::strcmp(name, "vkCreateImage") == 0) {
        if (trace.createImage == nullptr) trace.createImage = reinterpret_cast<PFN_vkCreateImage>(trace.deviceProc(device, name));
        if (trace.createImage != nullptr) return reinterpret_cast<PFN_vkVoidFunction>(TracedCreateImage);
    }
    return trace.deviceProc(device, name);
}

// Wraps the device function resolver so vkCreateImage is traced; returns the resolver to use.
inline PFN_vkGetDeviceProcAddr TraceImageCreation(PFN_vkGetDeviceProcAddr deviceProc) {
    if (!AddressBindingsEnabled() || deviceProc == nullptr) return deviceProc;
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
            ++liveHits;
        }
    }
    const std::size_t kept = std::min<std::size_t>(log.unboundNext, AddressBindingLog::UnboundCapacity);
    std::size_t unboundHits = 0;
    for (std::size_t i = 0; i < kept && unboundHits < 16; ++i) {
        const auto& entry = log.unbound[(log.unboundNext - 1 - i) % AddressBindingLog::UnboundCapacity];
        if (overlaps(entry)) {
            lines.emplace_back("[address-bindings] unbound: " + AddressBindingText(entry, address, now));
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
