#include "prx/libSceAgcDriver/Graphics/include/VulkanDebug.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

namespace {

constexpr std::uint32_t ReportsPerMessage = 10;

bool switchOn(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) return false;
    if (std::strcmp(value, "1") == 0) return true;
    throw std::runtime_error(std::string(name) + "=" + value + ": expected 1");
}

const char* severityName(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) return "error";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) return "warning";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0) return "info";
    return "verbose";
}

const char* faultAddressName(VkDeviceFaultAddressTypeEXT type) {
    switch (type) {
        case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT: return "none";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT: return "invalid read";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT: return "invalid write";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT: return "invalid execute";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT: return "instruction pointer (unknown)";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT: return "instruction pointer (invalid)";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT: return "instruction pointer (fault)";
        default: return "unknown";
    }
}

std::string formatted(const char* format, va_list arguments) {
    char text[256];
    std::vsnprintf(text, sizeof(text), format, arguments);
    return text;
}

struct FaultSource {
    VkDevice device;
    PFN_vkGetDeviceFaultInfoEXT query;
    bool vendorBinary;
};

struct FaultSources {
    std::mutex mutex;
    std::vector<FaultSource> sources;
};

FaultSources& faultSources() {
    static FaultSources sources;
    return sources;
}

void reportFault(const FaultSource& source, const char* operation) {
    VkDeviceFaultCountsEXT counts{VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    const auto counted = source.query(source.device, &counts, nullptr);
    if (counted != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] device lost in %s; vkGetDeviceFaultInfoEXT returned %d\n", operation, static_cast<int>(counted));
        return;
    }
    if (!source.vendorBinary) counts.vendorBinarySize = 0;
    std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
    std::vector<std::byte> binary(static_cast<std::size_t>(counts.vendorBinarySize));
    VkDeviceFaultInfoEXT info{VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT};
    info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
    info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
    info.pVendorBinaryData = binary.empty() ? nullptr : binary.data();
    const auto queried = source.query(source.device, &counts, &info);
    if (queried != VK_SUCCESS && queried != VK_INCOMPLETE) {
        std::fprintf(stderr, "[vulkan] device lost in %s; vkGetDeviceFaultInfoEXT returned %d\n", operation, static_cast<int>(queried));
        return;
    }
    addresses.resize(counts.addressInfoCount);
    vendors.resize(counts.vendorInfoCount);
    binary.resize(static_cast<std::size_t>(counts.vendorBinarySize));
    std::fprintf(stderr, "[vulkan] device lost in %s: %s\n", operation, info.description);
    for (const auto& address : addresses) {
        const auto mask = address.addressPrecision > 1 ? ~(address.addressPrecision - 1) : ~VkDeviceSize{0};
        const auto low = address.reportedAddress & mask;
        const auto high = low + (address.addressPrecision > 1 ? address.addressPrecision - 1 : 0);
        std::fprintf(stderr, "[vulkan]   %s at 0x%llx (range 0x%llx..0x%llx)\n", faultAddressName(address.addressType), static_cast<unsigned long long>(address.reportedAddress), static_cast<unsigned long long>(low), static_cast<unsigned long long>(high));
    }
    for (const auto& vendor : vendors) {
        std::fprintf(stderr, "[vulkan]   vendor fault: %s (code 0x%llx, data 0x%llx)\n", vendor.description, static_cast<unsigned long long>(vendor.vendorFaultCode), static_cast<unsigned long long>(vendor.vendorFaultData));
    }
    if (binary.empty()) return;
    char name[64];
    std::snprintf(name, sizeof(name), "aps5-device-fault-%lld.bin", static_cast<long long>(std::time(nullptr)));
    if (std::FILE* file = std::fopen(name, "wb")) {
        std::fwrite(binary.data(), 1, binary.size(), file);
        std::fclose(file);
        std::fprintf(stderr, "[vulkan]   vendor crash dump: %s (%zu bytes)\n", name, binary.size());
    } else {
        std::fprintf(stderr, "[vulkan]   vendor crash dump of %zu bytes could not be written to %s\n", binary.size(), name);
    }
}

}

bool VulkanValidationRequested() {
    static const bool requested = switchOn("APS5_VULKAN_VALIDATION");
    return requested;
}

bool VulkanDebugNamesRequested() {
    static const bool requested = switchOn("APS5_VULKAN_DEBUG");
    return requested;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    const char* id = data->pMessageIdName != nullptr ? data->pMessageIdName : "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0 && std::strstr(id, "DEBUG-PRINTF") == nullptr) return VK_FALSE;
    static std::mutex mutex;
    static std::map<std::pair<std::int32_t, std::string>, std::uint32_t> seen;
    std::uint32_t count = 0;
    {
        std::lock_guard lock(mutex);
        count = ++seen[{data->messageIdNumber, id}];
    }
    if (count > ReportsPerMessage + 1) return VK_FALSE;
    if (count == ReportsPerMessage + 1) {
        std::fprintf(stderr, "[vulkan] %s %s: reported %u times, further reports suppressed\n", severityName(severity), id, ReportsPerMessage);
        return VK_FALSE;
    }
    std::fprintf(stderr, "[vulkan] %s %s: %s\n", severityName(severity), id, data->pMessage != nullptr ? data->pMessage : "");
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VulkanValidationMessengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = VulkanValidationMessage;
    return info;
}

void NameObject(const Context& context, VkObjectType type, std::uint64_t handle, const char* format, ...) {
    if (context.setObjectName == nullptr || handle == 0) return;
    va_list arguments;
    va_start(arguments, format);
    const auto name = formatted(format, arguments);
    va_end(arguments);
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    context.setObjectName(context.device, &info);
}

DebugLabel::DebugLabel(const Context& context, VkCommandBuffer commands, const char* format, ...) {
    if (context.beginLabel == nullptr || context.endLabel == nullptr || commands == VK_NULL_HANDLE) return;
    va_list arguments;
    va_start(arguments, format);
    const auto name = formatted(format, arguments);
    va_end(arguments);
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    label.pLabelName = name.c_str();
    context.beginLabel(commands, &label);
    end = context.endLabel;
    this->commands = commands;
}

DebugLabel::~DebugLabel() {
    if (end != nullptr) end(commands);
}

void RegisterDeviceFaultSource(VkDevice device, PFN_vkGetDeviceFaultInfoEXT query, bool vendorBinary) {
    auto& registry = faultSources();
    std::lock_guard lock(registry.mutex);
    registry.sources.push_back({device, query, vendorBinary});
}

void UnregisterDeviceFaultSource(VkDevice device) {
    auto& registry = faultSources();
    std::lock_guard lock(registry.mutex);
    std::erase_if(registry.sources, [&](const FaultSource& source) { return source.device == device; });
}

void ReportDeviceLost(const char* operation) {
    static std::atomic<bool> reported{false};
    if (reported.exchange(true)) return;
    auto& registry = faultSources();
    std::lock_guard lock(registry.mutex);
    if (registry.sources.empty()) {
        std::fprintf(stderr, "[vulkan] device lost in %s; the device does not report faults (VK_EXT_device_fault)\n", operation);
        return;
    }
    for (const auto& source : registry.sources) reportFault(source, operation);
}

}
