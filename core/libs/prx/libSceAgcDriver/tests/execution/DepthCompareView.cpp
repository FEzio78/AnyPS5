#include "prx/libSceAgcDriver/Execution/include/BdaFeatures.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureDetiler.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureFormat.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureCache.hpp"
#include "RuntimeAbi.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DccMetadata.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <bit>
#include <SDL_loadso.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;

class Device {
public:
    Device() {
#ifdef _WIN32
        library = SDL_LoadObject("vulkan-1.dll");
#else
        library = SDL_LoadObject("libvulkan.so.1");
#endif
        Require(library != nullptr, "cannot load Vulkan");
        try {
            instanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
            Require(instanceProc != nullptr, "missing Vulkan instance resolver");
            VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            application.apiVersion = VK_API_VERSION_1_1;
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            info.pApplicationInfo = &application;
            Check(function<PFN_vkCreateInstance>("vkCreateInstance")(&info, nullptr, &instance), "vkCreateInstance");
            std::uint32_t count = 0;
            const auto enumerate = function<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
            Check(enumerate(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
            Require(count != 0, "no Vulkan device");
            std::vector<VkPhysicalDevice> devices(count);
            Check(enumerate(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
            context.physical = devices.front();
            const auto extensions = function<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
            Check(extensions(context.physical, nullptr, &count, nullptr), "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> available(count);
            Check(extensions(context.physical, nullptr, &count, available.data()), "vkEnumerateDeviceExtensionProperties");
            const auto hasExtension = [&](const char* name) {
                for (const auto& extension : available) {
                    if (std::strcmp(extension.extensionName, name) == 0) return true;
                }
                return false;
            };
            auto bytes = AgcDriver::QueryBdaByteFeatures(context.physical, function<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2"), available);
            auto address = AgcDriver::QueryBdaFeatures(context.physical, function<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2"), available);
            const auto queues = function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
            queues(context.physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            queues(context.physical, &count, families.data());
            std::uint32_t family = 0;
            while (family < count && (families[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ++family;
            Require(family < count, "no Vulkan compute queue");
            const float priority = 1;
            VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            VkPhysicalDeviceFeatures enabled{};
            enabled.shaderInt64 = VK_TRUE;
            address.pNext = &bytes;
            VkPhysicalDeviceRobustness2FeaturesEXT robustness{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
            VkPhysicalDeviceFeatures2 queried{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &robustness};
            function<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(context.physical, &queried);
            Require(robustness.nullDescriptor && hasExtension(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME), "depth resource test requires null descriptors");
            robustness.robustBufferAccess2 = false;
            robustness.robustImageAccess2 = false;
            bytes.pNext = &robustness;
            context.nullDescriptors = true;
            std::vector<const char*> extensionsEnabled{VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, VK_KHR_8BIT_STORAGE_EXTENSION_NAME};
            extensionsEnabled.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
            if (hasExtension(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)) {
                extensionsEnabled.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
                VkPhysicalDeviceExternalMemoryHostPropertiesEXT hostProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
                VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &hostProperties};
                function<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(context.physical, &properties);
                context.hostImportAlignment = hostProperties.minImportedHostPointerAlignment;
            }
            if (hasExtension(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME)) {
                extensionsEnabled.push_back(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
                context.depthRangeUnrestricted = true;
            }
            VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &address};
            device.queueCreateInfoCount = 1;
            device.pQueueCreateInfos = &queue;
            device.enabledExtensionCount = static_cast<std::uint32_t>(extensionsEnabled.size());
            device.ppEnabledExtensionNames = extensionsEnabled.data();
            device.pEnabledFeatures = &enabled;
            Check(function<PFN_vkCreateDevice>("vkCreateDevice")(context.physical, &device, nullptr, &context.device), "vkCreateDevice");
            context.deviceProc = function<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
            function<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(context.physical, &context.memory);
            VkPhysicalDeviceProperties properties{};
            function<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(context.physical, &properties);
            context.limits = properties.limits;
            context.bufferDeviceAddress = true;
            context.formatProperties = function<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties");
            context.imageFormatProperties = function<PFN_vkGetPhysicalDeviceImageFormatProperties>("vkGetPhysicalDeviceImageFormatProperties");
            if (hasExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) context.memoryProperties2 = function<PFN_vkGetPhysicalDeviceMemoryProperties2>("vkGetPhysicalDeviceMemoryProperties2");
            context.Function<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(context.device, family, 0, &context.queue);
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.queueFamilyIndex = family;
            Check(context.Function<PFN_vkCreateCommandPool>("vkCreateCommandPool")(context.device, &pool, nullptr, &context.pool), "vkCreateCommandPool");
        } catch (...) {
            release();
            throw;
        }
    }

    ~Device() { release(); }
    const Context& GetContext() const { return context; }

private:
    template<typename TFunction>
    TFunction function(const char* name) const {
        const auto result = reinterpret_cast<TFunction>(instanceProc(instance, name));
        Require(result != nullptr, name);
        return result;
    }

    void release() noexcept {
        if (context.pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(context.device, context.pool, nullptr);
        context.bufferPool.reset();
        if (context.device != VK_NULL_HANDLE) {
            ClearImageMirrors(context.device);
            ClearHostImports(context.device);
            DestroyShadows(context.device);
            function<PFN_vkDestroyDevice>("vkDestroyDevice")(context.device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) function<PFN_vkDestroyInstance>("vkDestroyInstance")(instance, nullptr);
        if (library != nullptr) SDL_UnloadObject(library);
    }

    void* library = nullptr;
    PFN_vkGetInstanceProcAddr instanceProc = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    Context context{};
};

std::array<std::uint32_t, 8> Descriptor(std::uint64_t address, std::uint32_t format, std::uint32_t side, std::uint32_t layers, std::uint32_t levels) {
    return {static_cast<std::uint32_t>(address >> 8),
            static_cast<std::uint32_t>((address >> 40) & 255) | (format << 20) | (((side - 1) & 3) << 30),
            ((side - 1) >> 2) | ((side - 1) << 14),
            0x924u | (24u << 20) | (13u << 28) | ((levels - 1) << 16),
            layers - 1, 0, 0, 0};
}

std::vector<VkBufferImageCopy> Regions(const GuestTextureResource& resource, VkImageAspectFlags aspect) {
    const auto geometry = DescribeSurface(resource);
    std::vector<VkBufferImageCopy> regions;
    for (std::uint32_t layer = 0; layer < geometry.layers; ++layer) {
        for (std::uint32_t level = 0; level < resource.mipCount; ++level) {
            VkBufferImageCopy region{};
            region.bufferOffset = geometry.LinearLayerOffset(layer) + geometry.mips[level].linearOffset;
            region.bufferRowLength = geometry.mips[level].pitchBytes / BytesPerElement(resource.format);
            region.imageSubresource = {aspect, level, layer, 1};
            region.imageExtent = {std::max(resource.width >> level, 1u), std::max(resource.height >> level, 1u), 1};
            regions.push_back(region);
        }
    }
    return regions;
}

std::vector<std::byte> ReadDepth(const Context& context, const Texture& texture, const GuestTextureResource& resource) {
    const auto geometry = DescribeSurface(resource);
    Buffer readback(context, geometry.sliceLinearBytes * geometry.layers, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    CommandBatch batch(context);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = texture.Layout();
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.Image();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, resource.mipCount, 0, geometry.imageLayers};
    const auto pipelineBarrier = context.Function<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier");
    pipelineBarrier(batch.Handle(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    const auto regions = Regions(resource, VK_IMAGE_ASPECT_DEPTH_BIT);
    context.Function<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(batch.Handle(), barrier.image, barrier.newLayout, readback.Handle(), regions.size(), regions.data());
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    std::swap(barrier.oldLayout, barrier.newLayout);
    pipelineBarrier(batch.Handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    batch.SubmitAndWait();
    return {readback.Bytes().begin(), readback.Bytes().end()};
}

void Run(const Context& context, std::uint32_t format, std::uint32_t side, std::uint32_t layers, std::uint32_t levels, std::uint32_t sourceFormat = 0) {
    auto words = Descriptor(0x100000, format, side, layers, levels);
    auto resource = DecodeTextureResource(words);
    const auto geometry = DescribeSurface(resource);
    const auto bytes = (geometry.guestBytes + 65535) & ~std::uint64_t{65535};
#ifdef _WIN32
    auto* memory = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
#else
    auto* memory = std::aligned_alloc(65536, bytes);
#endif
    Require(memory != nullptr, "cannot allocate guest surface");
    GuestAllocations::Mutation().Add(memory, bytes, true, true, true);
    std::memset(memory, 0, bytes);
    words = Descriptor(reinterpret_cast<std::uint64_t>(memory), format, side, layers, levels);
    resource = DecodeTextureResource(words);
    {
        Recorder recorder(context);
        recorder.Activate();
        const auto fallback = CachedSampledTexture(context, words, true);
        Require(fallback->HasDepthCopy() || fallback->StorageSource() == nullptr, "surface without an imported GPU source did not use a snapshot");
        auto sourceResource = resource;
        if (sourceFormat != 0) sourceResource.format = sourceFormat;
        auto source = CachedStorageSurface(context, sourceResource);
        Require(Texture::CanDepthCopyFrom(context, *source, resource), "test source is ineligible for a depth copy");
        const auto elementBytes = BytesPerElement(format);
        const auto regions = Regions(resource, VK_IMAGE_ASPECT_COLOR_BIT);
        auto input = std::make_shared<Buffer>(context, geometry.sliceLinearBytes * layers, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        ShaderRecompiler::RecompileResult program;
        ShaderRecompiler::DescriptorBinding binding;
        binding.kind = ShaderRecompiler::DescriptorKind::SampledImage;
        binding.role = ShaderRecompiler::DescriptorRole::GuestImages;
        binding.binding = ShaderRecompiler::RuntimeAbi::FirstComparisonImageBinding;
        binding.count = 1;
        binding.imageShape = ShaderRecompiler::DescriptorImageShape::Image2DArray;
        binding.imageSamplers = {0};
        binding.imageDepthCompare = {true};
        binding.guestDescriptor.assign(words.begin(), words.end());
        program.bindings.push_back(binding);
        const CompiledShader compiled{ShaderRecompiler::ShaderStage::Compute, &program, 0};
        std::unique_ptr<ShaderResources> reused;
        std::vector<std::byte> previous;
        for (std::uint32_t pass = 0; pass < 2; ++pass) {
            for (std::size_t i = 0; i < input->Bytes().size() / elementBytes; ++i) {
                if (format == 22) {
                    constexpr std::array<std::uint32_t, 8> edges{0u, 0x80000000u, 1u, 0x007fffffu, 0x00800000u, 0x3f000000u, 0x3f7fffffu, 0x3f800000u};
                    const auto value = i % 257 < edges.size() ? edges[(i + pass) % edges.size()] : std::bit_cast<std::uint32_t>(static_cast<float>((i * 37 + pass * 19) % 65536) / 65535.0f);
                    std::memcpy(input->Bytes().data() + i * 4, &value, 4);
                } else {
                    const auto value = static_cast<std::uint16_t>(i * 37 + pass * 19);
                    std::memcpy(input->Bytes().data() + i * 2, &value, 2);
                }
            }
            const auto before = std::vector<std::byte>(static_cast<std::byte*>(memory), static_cast<std::byte*>(memory) + bytes);
            const auto commands = recorder.Commands();
            recorder.Keep(input);
            RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
            context.Function<PFN_vkCmdCopyBufferToImage>("vkCmdCopyBufferToImage")(commands, input->Handle(), source->Image(), VK_IMAGE_LAYOUT_GENERAL, regions.size(), regions.data());
            source->MarkDirty();
            const auto version = source->Version();
            if (reused != nullptr) {
                Require(reused->Revalidate(compiled), "depth copy resource template was not reusable");
                Require(!reused->SampledTextures().front()->RefreshDepthCopy(), "resource revalidation missed a source version change");
            }
            auto gpu = CachedSampledTexture(context, words, true);
            Require(gpu->HasDepthCopy() && gpu->StorageSource() == source.get(), "pending image fell back to a snapshot");
            Require(source->Version() == version && StorageTexture::FindPending(resource.baseAddress, geometry.guestBytes) == source, "sampling flushed or refreshed the pending source");
            Require(std::memcmp(memory, before.data(), bytes) == 0, "GPU lookup changed guest memory");
            Require(!gpu->RefreshDepthCopy(), "unchanged source version made another copy");
            Require(CachedSampledTexture(context, words, true) == gpu, "depth compare cache missed an unchanged version");
            if (reused == nullptr) reused = std::make_unique<ShaderResources>(context, compiled);
            Require(reused->SampledTextures().front() == gpu && reused->ReadsImage(source.get()), "resource binding lost the depth copy source");
            const auto images = reused->StorageImages();
            Require(std::find(images.begin(), images.end(), std::pair{source->Image(), false}) != images.end() && std::find(images.begin(), images.end(), std::pair{gpu->Image(), false}) != images.end(), "sampled depth copy images were not registered for hazards");
            recorder.Sync();
            const auto actual = ReadDepth(context, *gpu, resource);
            if (!previous.empty()) Require(actual != previous, "new source version did not update the depth image");
            previous = actual;
            StorageTexture::FlushPending(resource.baseAddress, geometry.guestBytes);
            recorder.Sync();
            std::vector<std::byte> snapshot(geometry.guestBytes);
            ReadTextureSurface(resource, DccKeys::Uncompressed, snapshot);
            Texture cpu(context, *context.detiler, resource, VkComponentMapping{}, snapshot, true);
            recorder.Sync();
            const auto expected = ReadDepth(context, cpu, resource);
            for (const auto& region : regions) {
                for (std::uint32_t y = 0; y < region.imageExtent.height; ++y) {
                    const auto offset = region.bufferOffset + y * region.bufferRowLength * elementBytes;
                    const auto rowBytes = region.imageExtent.width * elementBytes;
                    if (std::memcmp(actual.data() + offset, expected.data() + offset, rowBytes) != 0) {
                        std::size_t first = 0;
                        while (first < rowBytes && actual[offset + first] == expected[offset + first]) ++first;
                        std::fprintf(stderr, "mismatch layer %u mip %u row %u byte %zu: GPU %02x snapshot %02x producer %02x guest0 %02x\n", region.imageSubresource.baseArrayLayer, region.imageSubresource.mipLevel, y, first, static_cast<unsigned>(actual[offset + first]), static_cast<unsigned>(expected[offset + first]), static_cast<unsigned>(input->Bytes()[offset + first]), *static_cast<unsigned char*>(memory));
                        ClearCachedTextures(context.device);
                        throw std::runtime_error("GPU and snapshot depth texels differ");
                    }
                    Require(std::memcmp(actual.data() + offset, input->Bytes().data() + offset, rowBytes) == 0, "depth copy changed producer texel bits");
                }
            }
            Require(CachedSampledTexture(context, words, true) == gpu, "resident source lost its depth copy after flush");
            auto mismatched = resource;
            mismatched.tileMode = TextureTileMode::kR64KBX;
            Require(!Texture::CanDepthCopyFrom(context, *source, mismatched), "mismatched swizzle was accepted");
            mismatched = resource;
            mismatched.format = 20;
            Require(!Texture::CanDepthCopyFrom(context, *source, mismatched), "integer sampled format was accepted for depth compare");
            Require(gpu->SampledViewRange(false).layers == layers && gpu->SampledViewRange(true).layers == 1, "depth array view ranges differ from snapshot views");
        }
        recorder.Sync();
        ClearCachedTextures(context.device);
        source.reset();
        recorder.Sync();
    }
    GuestAllocations::Mutation().Remove(memory);
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    std::free(memory);
#endif
    std::printf("depth compare equivalence passed: format %u, %ux%u, %u layers, %u levels\n", format, side, side, layers, levels);
}


void RunImport(const Context& context) {
    Require(context.hostImportAlignment != 0, "import depth test requires host-imported memory");
    auto words = Descriptor(0x100000, 22, 1024, 4, 1);
    auto resource = DecodeTextureResource(words);
    const auto geometry = DescribeSurface(resource);
    const auto bytes = geometry.guestBytes;
#ifdef _WIN32
    auto* memory = GuestArena::GuestArenaAllocate_nid_postfix(bytes, 65536);
    const auto section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, static_cast<DWORD>(bytes), nullptr);
    Require(section != nullptr, "cannot create imported atlas backing");
    GuestArena::GuestArenaMap_nid_postfix(memory, bytes, section, 0, PAGE_READWRITE);
    CloseHandle(section);
#else
    auto* raw = mmap(nullptr, bytes + 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Require(raw != MAP_FAILED, "cannot map imported atlas");
    const auto begin = reinterpret_cast<std::uintptr_t>(raw);
    const auto aligned = (begin + 65535) & ~std::uintptr_t{65535};
    if (aligned != begin) munmap(raw, aligned - begin);
    munmap(reinterpret_cast<void*>(aligned + bytes), begin + 65536 - aligned);
    auto* memory = reinterpret_cast<void*>(aligned);
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(memory, bytes);
#endif
    Require(memory != nullptr, "cannot allocate imported atlas");
    GuestAllocations::Mutation().Add(memory, bytes, true, true, true);
    std::memset(memory, 0, bytes);
    words = Descriptor(reinterpret_cast<std::uint64_t>(memory), 22, 1024, 4, 1);
    resource = DecodeTextureResource(words);
    const auto watch = PrepareImportWatch(context);
    SetImportWatch(context, ImportWatch::Watch);
    const auto* import = HostImportFor(context, resource.baseAddress, bytes);
    Require(import != nullptr && AgcDriver::GuestMemory::Watched(resource.baseAddress, bytes), "import test requires watched memory for unchanged reuse");
    {
        Recorder recorder(context);
        recorder.Activate();
        auto input = std::make_shared<Buffer>(context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        for (std::size_t i = 0; i < bytes / 4; ++i) {
            constexpr std::array<std::uint32_t, 8> edges{0u, 0x80000000u, 1u, 0x007fffffu, 0x00800000u, 0x3f000000u, 0x3f7fffffu, 0x3f800000u};
            const auto value = i % 257 < edges.size() ? edges[i % edges.size()] : std::bit_cast<std::uint32_t>(static_cast<float>((i * 37) % 65536) / 65535.0f);
            std::memcpy(input->Bytes().data() + i * 4, &value, 4);
        }
        ShaderRecompiler::RecompileResult program;
        ShaderRecompiler::DescriptorBinding binding;
        binding.kind = ShaderRecompiler::DescriptorKind::SampledImage;
        binding.role = ShaderRecompiler::DescriptorRole::GuestImages;
        binding.binding = ShaderRecompiler::RuntimeAbi::FirstComparisonImageBinding;
        binding.count = 1;
        binding.imageShape = ShaderRecompiler::DescriptorImageShape::Image2DArray;
        binding.imageSamplers = {0};
        binding.imageDepthCompare = {true};
        binding.guestDescriptor.assign(words.begin(), words.end());
        program.bindings.push_back(binding);
        const CompiledShader compiled{ShaderRecompiler::ShaderStage::Compute, &program, 0};
        std::unique_ptr<ShaderResources> reused;
        std::shared_ptr<Texture> gpu;
        std::vector<std::byte> previous;
        for (unsigned pass = 0; pass < 5; ++pass) {
            AgcDriver::GuestMemory::BumpCollectEpoch();
            auto expectedKind = Recorder::WriterKind::Unknown;
            if (pass < 3) {
                Require(StorageTexture::FindPending(resource.baseAddress, bytes) == nullptr, "import atlas unexpectedly has a pending image");
                if (pass == 0) Require(StorageTexture::FindLive(resource.baseAddress, bytes) == nullptr, "initial imported atlas has a resident image");
                const auto commands = recorder.Commands();
                RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
                if (pass < 2) {
                    recorder.Keep(input);
                    CopyBuffer(context, commands, input->Handle(), 0, import->buffer, resource.baseAddress - import->base, bytes);
                    expectedKind = Recorder::WriterKind::Copy;
                    recorder.NotePendingWrite(resource.baseAddress, bytes, expectedKind);
                } else {
                    context.Function<PFN_vkCmdFillBuffer>("vkCmdFillBuffer")(commands, import->buffer, resource.baseAddress - import->base, bytes, 0x3f000000);
                    expectedKind = Recorder::WriterKind::Fill;
                    recorder.NotePendingWrite(resource.baseAddress, bytes, expectedKind);
                }
                if (pass != 1) AgcDriver::GuestMemory::MarkWritten(resource.baseAddress, bytes);
                else recorder.Submit();
                const auto notes = recorder.PendingWritesOver(resource.baseAddress, bytes);
                Require(!notes.empty() && notes.back().kind == expectedKind, "recorded writer kind was lost");
                Require(Recorder::SnapshotWriteOverlaps(resource.baseAddress, bytes), "recorded buffer writer is absent from the snapshot union");
                recorder.TraceDepthCompareWrites(resource.baseAddress, bytes, resource.format, resource.width, resource.height);
            } else {
                if (pass == 4) AgcDriver::GuestMemory::Unwatch(resource.baseAddress, bytes);
                const auto value = pass == 3 ? 0x3f400000u : 0x3e800000u;
                for (std::size_t i = 0; i < bytes / 4; ++i) std::memcpy(static_cast<std::byte*>(memory) + i * 4, &value, 4);
            }
            const auto versionBefore = gpu != nullptr ? gpu->StorageSource()->Version() : 0;
            const auto waitsBefore = Recorder::ThreadHookWaits();
            const auto readsBefore = AgcDriver::GuestMemory::ThreadCpuReadBytes();
            const auto submitsBefore = recorder.Submissions();
            {
                if (reused != nullptr) {
                    Require(reused->Revalidate(compiled), "import depth resource template did not refresh in place");
                    gpu = reused->SampledTextures().front();
                } else {
                    gpu = CachedSampledTexture(context, words, true);
                    reused = std::make_unique<ShaderResources>(context, compiled);
                }
                Require(gpu->HasDepthCopy() && gpu->StorageSource()->ImportRefreshed(), "import atlas fell back to a snapshot");
                Require(gpu->StorageSource()->Version() > versionBefore, "import writer did not advance the storage version");
                Require(!gpu->RefreshDepthCopy(), "import lookup did not copy the refreshed storage version");
                Require(!gpu->StorageSource()->GuestSnapshotValid(), "import path kept a CPU snapshot");
                Require(reused->ReadsImage(gpu->StorageSource()), "import resource lost its storage hazard");
                Require(Recorder::ThreadHookWaits() == waitsBefore, "import lookup waited for the recorded producer");
                if (pass < 4) {
                    const auto version = gpu->StorageSource()->Version();
                    Require(CachedSampledTexture(context, words, true) == gpu && reused->Revalidate(compiled), "unchanged import view was not reusable");
                    Require(gpu->StorageSource()->Version() == version && !gpu->RefreshDepthCopy(), "unchanged import reuse refreshed or copied");
                }
                Require(AgcDriver::GuestMemory::ThreadCpuReadBytes() == readsBefore, "import depth lookup read or compared guest bytes on the CPU");
                Require(recorder.Submissions() == submitsBefore, "import depth lookup submitted or drained recorded work");
            }
            recorder.Sync();
            const auto actual = ReadDepth(context, *gpu, resource);
            std::vector<std::byte> snapshot(bytes);
            ReadTextureSurface(resource, DccKeys::Uncompressed, snapshot);
            Require(AgcDriver::GuestMemory::ThreadCpuReadBytes() > readsBefore, "CPU read instrumentation did not see the snapshot path");
            if (pass < 2) Require(std::equal(snapshot.begin(), snapshot.end(), input->Bytes().begin()), "recorded buffer write did not reach the imported atlas");
            Texture cpu(context, *context.detiler, resource, VkComponentMapping{}, snapshot, true);
            recorder.Sync();
            const auto expected = ReadDepth(context, cpu, resource);
            for (const auto& region : Regions(resource, VK_IMAGE_ASPECT_DEPTH_BIT)) {
                for (std::uint32_t y = 0; y < region.imageExtent.height; ++y) {
                    const auto offset = region.bufferOffset + y * region.bufferRowLength * 4;
                    Require(std::memcmp(actual.data() + offset, expected.data() + offset, region.imageExtent.width * 4) == 0, "import depth texels differ from the snapshot path");
                }
            }
            if (pass == 1) Require(actual == previous, "identical recorded buffer write changed depth texels");
            else if (!previous.empty()) Require(actual != previous, "changed import bytes left stale depth texels");
            previous = actual;
        }
        recorder.Sync();
        ClearCachedTextures(context.device);
        reused.reset();
        gpu.reset();
        recorder.Sync();
    }
    SetImportWatch(context, watch);
    GuestAllocations::Mutation().Remove(memory);
    static_cast<void>(HostImportFor(context, resource.baseAddress, bytes));
#ifdef _WIN32
    GuestArena::GuestArenaReset_nid_postfix(memory, bytes);
    GuestArena::GuestArenaRelease_nid_postfix(memory, bytes);
#else
    GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(memory, bytes);
    munmap(memory, bytes);
#endif
    std::puts("import depth equivalence passed: recorded buffer copy/fill, journal-only write, unchanged reuse, watched and untracked CPU writes; no CPU surface reads or waits");
}

}

int main() {
    struct Shutdown {
        ~Shutdown() {
            AgcDriverShutdown_nid_postfix();
            AgcDriver::StopProfileOutput_nid_no_patch();
        }
    } shutdown;
    try {
#ifdef _WIN32
        _putenv_s("APS5_DUMP_DISPATCH_IMAGES", "1");
        _putenv_s("APS5_PROFILE_IMAGES", "1");
        _putenv_s("APS5_PROFILE_DRAW", "1");
        _putenv_s("APS5_TRACE_DEPTH_COMPARE", "1");
#else
        setenv("APS5_DUMP_DISPATCH_IMAGES", "1", 1);
        setenv("APS5_PROFILE_IMAGES", "1", 1);
        setenv("APS5_PROFILE_DRAW", "1", 1);
        setenv("APS5_TRACE_DEPTH_COMPARE", "1", 1);
#endif
        std::unique_ptr<Device> device;
        try {
            device = std::make_unique<Device>();
        } catch (const std::exception& error) {
            if (std::getenv("ANYPS5_REQUIRE_VULKAN") != nullptr) throw;
            std::printf("skipped, no Vulkan device: %s\n", error.what());
            return VulkanTestSkipped;
        }
        auto context = device->GetContext();
        TextureDetiler detiler(context);
        context.detiler = &detiler;
        TextureCache textureCache(context);
        context.textureCache = &textureCache;
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        RunImport(context);
        Run(context, 22, 1024, 4, 1);
        Run(context, 22, 128, 4, 4);
        Run(context, 7, 128, 4, 4);
        Run(context, 22, 128, 4, 1, 20);
        Run(context, 22, 128, 4, 1, 56);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

