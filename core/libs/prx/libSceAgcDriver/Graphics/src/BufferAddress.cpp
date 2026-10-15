#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include <cstdio>
#include <cstdlib>

namespace AgcDriver::Graphics {

VkDeviceAddress Buffer::DeviceAddress() const {
    Require(deviceAddress != 0, "buffer has no device address");
    return deviceAddress;
}

void Buffer::initializeAddress(VkBufferUsageFlags usage) {
    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0) return;
    const VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, buffer};
    deviceAddress = context.Function<PFN_vkGetBufferDeviceAddressKHR>("vkGetBufferDeviceAddressKHR")(context.device, &info);
    Require(deviceAddress != 0, "vkGetBufferDeviceAddressKHR returned a null address");
    // Fork-only APS5_TRACE_DEVICE_ADDRESSES: device ranges of addressable buffers, to match a
    // [device-fault] address against them.
    static const bool trace = std::getenv("APS5_TRACE_DEVICE_ADDRESSES") != nullptr;
    if (trace) std::fprintf(stderr, "[bda] buffer device 0x%llx+0x%llx (capacity 0x%llx, offset 0x%llx)\n", static_cast<unsigned long long>(deviceAddress), static_cast<unsigned long long>(size), static_cast<unsigned long long>(capacity), static_cast<unsigned long long>(offset));
}

}
