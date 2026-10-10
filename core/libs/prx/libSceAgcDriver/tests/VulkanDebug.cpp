#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/VulkanDebug.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using AgcDriver::Graphics::Context;
using AgcDriver::Graphics::DebugLabel;

namespace {

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

struct Calls {
    std::vector<std::string> names;
    std::vector<VkObjectType> types;
    std::vector<std::uint64_t> handles;
    std::vector<std::string> labels;
    int ends = 0;
    int faultQueries = 0;
};

Calls calls;

VKAPI_ATTR VkResult VKAPI_CALL setName(VkDevice, const VkDebugUtilsObjectNameInfoEXT* info) {
    calls.names.emplace_back(info->pObjectName);
    calls.types.push_back(info->objectType);
    calls.handles.push_back(info->objectHandle);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL beginLabel(VkCommandBuffer, const VkDebugUtilsLabelEXT* label) {
    calls.labels.emplace_back(label->pLabelName);
}

VKAPI_ATTR void VKAPI_CALL endLabel(VkCommandBuffer) {
    ++calls.ends;
}

constexpr std::array<std::uint8_t, 5> VendorBinary{0x56, 0x4b, 0x44, 0x4d, 0x50};

VKAPI_ATTR VkResult VKAPI_CALL faultInfo(VkDevice, VkDeviceFaultCountsEXT* counts, VkDeviceFaultInfoEXT* info) {
    ++calls.faultQueries;
    if (info == nullptr) {
        counts->addressInfoCount = 1;
        counts->vendorInfoCount = 1;
        counts->vendorBinarySize = VendorBinary.size();
        return VK_SUCCESS;
    }
    std::snprintf(info->description, sizeof(info->description), "test fault");
    info->pAddressInfos[0] = {VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT, 0x123456789aull, 0x1000};
    std::snprintf(info->pVendorInfos[0].description, sizeof(info->pVendorInfos[0].description), "vendor detail");
    info->pVendorInfos[0].vendorFaultCode = 7;
    info->pVendorInfos[0].vendorFaultData = 9;
    std::memcpy(info->pVendorBinaryData, VendorBinary.data(), VendorBinary.size());
    return VK_SUCCESS;
}

VkDevice fakeDevice() {
    return reinterpret_cast<VkDevice>(static_cast<std::uintptr_t>(0x1000));
}

VkCommandBuffer fakeCommands() {
    return reinterpret_cast<VkCommandBuffer>(static_cast<std::uintptr_t>(0x2000));
}

void namesAndLabelsTests() {
    Context context{};
    context.device = fakeDevice();
    AgcDriver::Graphics::NameObject(context, VK_OBJECT_TYPE_IMAGE, 0x55, "storage 0x%llx", 0x1234ull);
    {
        const DebugLabel label(context, fakeCommands(), "draw %u", 3u);
    }
    Expect(calls.names.empty() && calls.labels.empty() && calls.ends == 0, "debug names or labels were recorded without VK_EXT_debug_utils");

    context.setObjectName = setName;
    context.beginLabel = beginLabel;
    context.endLabel = endLabel;
    AgcDriver::Graphics::NameObject(context, VK_OBJECT_TYPE_IMAGE, 0x55, "storage 0x%llx %ux%u", 0x1234ull, 256u, 128u);
    AgcDriver::Graphics::NameObject(context, VK_OBJECT_TYPE_PIPELINE, 0, "null handle");
    Expect(calls.names.size() == 1 && calls.names[0] == "storage 0x1234 256x128" && calls.types[0] == VK_OBJECT_TYPE_IMAGE && calls.handles[0] == 0x55, "an image was not named with its formatted guest address");
    {
        const DebugLabel outer(context, fakeCommands(), "dispatch cs 0x%llx", 0xabcdull);
        Expect(calls.labels.size() == 1 && calls.labels[0] == "dispatch cs 0xabcd" && calls.ends == 0, "a label was not begun with its formatted text");
        const DebugLabel inner(context, VK_NULL_HANDLE, "ignored");
    }
    Expect(calls.labels.size() == 1 && calls.ends == 1, "labels were not ended once each, or a null command buffer was labelled");
}

void deviceFaultTests() {
    const auto directory = std::filesystem::current_path();
    const auto dumps = [&] {
        std::vector<std::filesystem::path> found;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename().string().rfind("aps5-device-fault-", 0) == 0) found.push_back(entry.path());
        }
        return found;
    };
    for (const auto& stale : dumps()) std::filesystem::remove(stale);
    AgcDriver::Graphics::RegisterDeviceFaultSource(fakeDevice(), faultInfo, true);
    AgcDriver::Graphics::ReportDeviceLost("vkQueueSubmit");
    AgcDriver::Graphics::ReportDeviceLost("vkWaitForFences");
    AgcDriver::Graphics::UnregisterDeviceFaultSource(fakeDevice());
    Expect(calls.faultQueries == 2, "a device loss did not query the fault counts and then the fault, once per process");
    const auto written = dumps();
    Expect(written.size() == 1, "the vendor crash dump was not written");
    if (written.size() == 1) {
        std::ifstream file(written[0], std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        file.close();
        Expect(bytes.size() == VendorBinary.size() && std::memcmp(bytes.data(), VendorBinary.data(), VendorBinary.size()) == 0, "the vendor crash dump does not hold the driver's bytes");
        std::filesystem::remove(written[0]);
    }
}

void validationMessageTests() {
    VkDebugUtilsMessengerCallbackDataEXT data{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
    data.pMessageIdName = "VUID-test";
    data.messageIdNumber = 42;
    data.pMessage = "test message";
    for (int i = 0; i < 15; ++i) {
        Expect(AgcDriver::Graphics::VulkanValidationMessage(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, &data, nullptr) == VK_FALSE, "the validation callback asked to abort the call");
    }
    const auto info = AgcDriver::Graphics::VulkanValidationMessengerInfo();
    Expect(info.pfnUserCallback != nullptr && (info.messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 && (info.messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) == 0, "the messenger does not report errors through the callback without verbose messages");
}

}

int main() {
    namesAndLabelsTests();
    deviceFaultTests();
    validationMessageTests();
    if (failures != 0) return 1;
    std::puts("Vulkan debug tests passed");
    return 0;
}
