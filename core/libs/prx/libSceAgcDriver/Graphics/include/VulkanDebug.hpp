#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_VULKANDEBUG_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_VULKANDEBUG_HPP

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <cstdint>

namespace AgcDriver::Graphics {

struct Context;

bool VulkanValidationRequested();
bool VulkanDebugNamesRequested();

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);
VkDebugUtilsMessengerCreateInfoEXT VulkanValidationMessengerInfo();

[[gnu::format(printf, 4, 5)]] void NameObject(const Context& context, VkObjectType type, std::uint64_t handle, const char* format, ...);

class DebugLabel {
public:
    [[gnu::format(printf, 4, 5)]] DebugLabel(const Context& context, VkCommandBuffer commands, const char* format, ...);
    ~DebugLabel();
    DebugLabel(const DebugLabel&) = delete;
    DebugLabel& operator=(const DebugLabel&) = delete;

private:
    PFN_vkCmdEndDebugUtilsLabelEXT end = nullptr;
    VkCommandBuffer commands = VK_NULL_HANDLE;
};

void RegisterDeviceFaultSource(VkDevice device, PFN_vkGetDeviceFaultInfoEXT query, bool vendorBinary);
void UnregisterDeviceFaultSource(VkDevice device);
void ReportDeviceLost(const char* operation);

}

#endif
