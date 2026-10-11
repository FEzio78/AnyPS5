#include "VulkanTestDevice.hpp"
#include "MovedDispatch_spv.h"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/Recipe.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace AgcDriver;
using Graphics::Require;
constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t Words = 16;

struct Block {
    Block() {
#ifdef _WIN32
        data = static_cast<std::uint32_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        data = static_cast<std::uint32_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(data != nullptr, "cannot allocate the dispatch test block");
        std::memset(data, 0, BlockBytes);
        GuestAllocations::Mutation().Add(data, BlockBytes, true, true, true);
    }
    ~Block() {
        GuestAllocations::Mutation().Remove(data);
#ifdef _WIN32
        VirtualFree(data, 0, MEM_RELEASE);
#else
        std::free(data);
#endif
    }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    std::uint32_t* data = nullptr;
};

std::vector<std::uint32_t> Descriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uint64_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u) & 0xffffu, bytes, 0x31016facu};
}

ShaderRecompiler::RecompileResult Shader(const void* first, const void* second, void* output, std::uint32_t constant = 7) {
    ShaderRecompiler::RecompileResult shader;
    shader.variantId = 0x4d4f564544;
    shader.spirv = std::vector<std::uint32_t>(std::begin(MOVED_DISPATCH_SPV), std::end(MOVED_DISPATCH_SPV));
    ShaderRecompiler::DescriptorBinding inputs{};
    inputs.kind = ShaderRecompiler::DescriptorKind::StorageBuffer;
    inputs.role = ShaderRecompiler::DescriptorRole::GuestBuffers;
    inputs.binding = 0;
    inputs.count = 2;
    inputs.bufferWritten = {false, false};
    inputs.guestDescriptor = Descriptor(first, Words * 4);
    const auto other = Descriptor(second, Words * 4);
    inputs.guestDescriptor.insert(inputs.guestDescriptor.end(), other.begin(), other.end());
    shader.bindings.push_back(inputs);
    auto written = inputs;
    written.binding = 1;
    written.count = 1;
    written.bufferWritten = {true};
    written.guestDescriptor = Descriptor(output, Words * 4);
    shader.bindings.push_back(written);
    auto data = written;
    data.binding = 2;
    data.role = ShaderRecompiler::DescriptorRole::FlattenedSrt;
    data.bufferWritten.clear();
    data.guestDescriptor = {constant};
    shader.bindings.push_back(data);
    return shader;
}

void Expect(const std::uint32_t* output, const std::uint32_t* first, const std::uint32_t* second, std::uint32_t constant) {
    for (std::size_t index = 0; index < Words; ++index) Require(output[index] == first[index] + second[index] + constant, "dispatch saw stale moved-buffer data at word " + std::to_string(index));
}

}

int main() {
    try {
        const Block first, second, output;
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::unique_lock gpu(GuestMemory::GpuMutex());
        const auto prepare = [&](const ShaderRecompiler::RecompileResult& shader) {
            gpu.unlock();
            auto result = device->PrepareDispatch(shader, {});
            gpu.lock();
            return result;
        };
        const auto prepareRecipe = [&](const std::shared_ptr<const Recipe>& recipe) {
            gpu.unlock();
            auto result = device->PrepareRecipe(recipe, false);
            gpu.lock();
            return result;
        };
        for (std::size_t index = 0; index < 4096; ++index) {
            first.data[index] = static_cast<std::uint32_t>(index * 3 + 1);
            second.data[index] = static_cast<std::uint32_t>(index * 5 + 2);
        }
        const auto original = Shader(first.data, second.data, output.data);
        std::shared_ptr<const Recipe> initial;
        device->Dispatch(original, 1, 1, 1, {}, 0, prepare(original), &initial);
        device->WaitIdle();
        Expect(output.data, first.data, second.data, 7);
        if (initial == nullptr) {
            std::cout << "skipped, imported dispatch templates are unavailable\n";
            return VulkanTestSkipped;
        }
        const auto kept = initial->templateRef.lock();
        Require(kept != nullptr, "the initial dispatch template disappeared");
        const bool movable = std::getenv("APS5_NO_MOVED_DISPATCH_BUFFERS") == nullptr;
        for (const unsigned selected : {1u, 2u, 3u}) {
            const auto* liveFirst = first.data + ((selected & 1u) ? 1024 : 0);
            const auto* liveSecond = second.data + ((selected & 2u) ? 2048 : 0);
            const auto live = Shader(liveFirst, liveSecond, output.data);
            std::shared_ptr<const Recipe> recipe;
            device->Dispatch(live, 1, 1, 1, {}, 0, prepare(live), &recipe);
            device->WaitIdle();
            Expect(output.data, liveFirst, liveSecond, 7);
            Require(recipe != nullptr, "a moved imported dispatch lost its recipe");
            Require((recipe->templateRef.lock() == kept) == movable, "the dispatch address-key opt-out was not respected");
            const auto hit = prepareRecipe(recipe);
            Require(hit != nullptr, "the moved dispatch recipe pre-check failed");
            VulkanDevice::IndirectOutcome outcome{};
            Require(device->DispatchRecipe(live, 1, 1, 1, 0, 0, hit, outcome, prepare(live)) == RecipeOutcome::Recorded, "a moved dispatch recipe did not record");
            device->WaitIdle();
            Expect(output.data, liveFirst, liveSecond, 7);
            auto fresh = live;
            fresh.variantId = 0;
            device->Dispatch(fresh, 1, 1, 1);
            device->WaitIdle();
            Expect(output.data, liveFirst, liveSecond, 7);
            auto different = live;
            different.bindings[1].guestDescriptor = Descriptor(output.data + 1024, Words * 4);
            Require(device->DispatchRecipe(different, 1, 1, 1, 0, 0, hit, outcome) == RecipeOutcome::Rebuild, "a recipe accepted a different written buffer");
        }
        alignas(256) std::array<std::uint32_t, Words> copied{};
        copied.fill(0x12345678u);
        const auto live = Shader(copied.data(), second.data + 2048, output.data, 13);
        std::shared_ptr<const Recipe> copiedRecipe;
        device->Dispatch(live, 1, 1, 1, {}, 0, prepare(live), &copiedRecipe);
        device->WaitIdle();
        Expect(output.data, copied.data(), second.data + 2048, 13);
        const bool refresh = std::getenv("APS5_NO_TEMPLATE_DATA_REFRESH") == nullptr;
        if (movable && refresh) {
            Require(copiedRecipe != nullptr && copiedRecipe->templateRef.lock() == kept, "a copied move rebuilt the dispatch template");
            const auto hit = prepareRecipe(copiedRecipe);
            Require(hit != nullptr, "the copied moved-buffer recipe pre-check failed");
            copied.fill(0x76543210u);
            VulkanDevice::IndirectOutcome outcome{};
            Require(device->DispatchRecipe(live, 1, 1, 1, 0, 0, hit, outcome, prepare(live)) == RecipeOutcome::Recorded, "the copied moved-buffer recipe did not record");
            device->WaitIdle();
            Expect(output.data, copied.data(), second.data + 2048, 13);
        }
        auto fresh = live;
        fresh.variantId = 0;
        device->Dispatch(fresh, 1, 1, 1);
        device->WaitIdle();
        Expect(output.data, copied.data(), second.data + 2048, 13);
        Graphics::SharedResourceCache().Clear();
        device->WaitIdle();
        std::cout << "Movable dispatch descriptor, recipe and fresh-build equivalence tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
