#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t Width = 64;
alignas(256) std::array<std::uint32_t, Threads * 4> Input{};
alignas(256) std::array<std::uint32_t, Threads * 4> Output{};
alignas(4096) std::array<std::uint8_t, 1048576> Texels{};

constexpr std::array<std::uint32_t, 10> Gather{
    0x34020084, 0xe0381000, 0x80000201, 0xbf8c3f70, 0xf15c0108, 0x00820602,
    0xbf8c3f70, 0xe0781000, 0x80010601, 0xbf810000,
};
constexpr std::array<std::uint32_t, 10> Load{
    0x34020084, 0xe0381000, 0x80000201, 0xbf8c3f70, 0xf0000308, 0x00020603,
    0xbf8c3f70, 0xe0781000, 0x80010601, 0xbf810000,
};
constexpr std::array<std::uint32_t, 7> Store{
    0x34020084, 0xe0381000, 0x80000201, 0xbf8c3f70, 0xf0200108, 0x00020503, 0xbf810000,
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::array<std::uint32_t, 8> TextureDescriptor(std::uint32_t format, std::uint32_t height) {
    const auto address = reinterpret_cast<std::uintptr_t>(Texels.data());
    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (format << 20u) | ((Width - 1u) << 30u),
            ((Width - 1u) >> 2u) | ((height - 1u) << 14u), 0x90000facu, 0u, 0u, 0u, 0u};
}

std::uint32_t Pixel(std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
    return (x * 13u + y * 17u + channel * 101u + 3u) & 255u;
}

void Run(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::uint32_t format, std::uint32_t height) {
    std::vector<std::uint32_t> user(20, 0u);
    const auto input = BufferDescriptor(Input.data(), sizeof(Input));
    const auto output = BufferDescriptor(Output.data(), sizeof(Output));
    const auto texture = TextureDescriptor(format, height);
    std::copy(input.begin(), input.end(), user.begin());
    std::copy(output.begin(), output.end(), user.begin() + 4);
    std::copy(texture.begin(), texture.end(), user.begin() + 8);
    user[16] = 0x92u;
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderRecompiler::ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {64, 0, user, compute, std::nullopt, std::nullopt, memory}, device.Target(), {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
}

void Check(AgcDriver::VulkanDevice& device, std::uint32_t format, std::uint32_t height, std::uint32_t channels) {
    Texels.fill(0xeeu);
    const auto resource = AgcDriver::Graphics::DecodeTextureResource(TextureDescriptor(format, height));
    const auto mip = AgcDriver::Graphics::DescribeSurface(resource).mips.at(0);
    Require(static_cast<std::uint64_t>(mip.pitchBytes) * (height + 32u) <= Texels.size(), "edge test allocation is too small");
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) {
            for (std::uint32_t c = 0; c < channels; ++c) Texels[y * mip.pitchBytes + x * channels + c] = static_cast<std::uint8_t>(Pixel(x, y, c));
        }
    }
    constexpr std::array<int, 4> offsets{-32, -1, 0, 31};
    const std::array<int, 8> rows{-1, 0, 1, static_cast<int>(height) - 1, static_cast<int>(height), static_cast<int>(height) + 1, static_cast<int>(height) + 15, static_cast<int>(height) + 16};
    const std::array<int, 8> columns{-1, 0, 1, 31, 32, 63, 64, 65};
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        const auto x = columns[lane % columns.size()];
        const auto y = rows[lane / columns.size()];
        Input[lane * 4] = (static_cast<std::uint32_t>(offsets[lane % 4]) & 63u) | ((static_cast<std::uint32_t>(offsets[(lane / 4) % 4]) & 63u) << 8u);
        Input[lane * 4 + 1] = std::bit_cast<std::uint32_t>((static_cast<float>(x) + 0.75f) / Width);
        Input[lane * 4 + 2] = std::bit_cast<std::uint32_t>((static_cast<float>(y) + 0.75f) / height);
    }
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto code = Gather;
        code[4] = (code[4] & ~0xf00u) | (1u << (8u + channel));
        Run(device, code, format, height);
        for (std::uint32_t lane = 0; lane < Threads; ++lane) {
            const auto x = columns[lane % columns.size()] + offsets[lane % 4];
            const auto y = rows[lane / columns.size()] + offsets[(lane / 4) % 4];
            const std::array<int, 4> dx{0, 1, 1, 0};
            const std::array<int, 4> dy{1, 1, 0, 0};
            for (std::uint32_t tap = 0; tap < 4; ++tap) {
                const auto expected = Pixel(std::clamp(x + dx[tap], 0, static_cast<int>(Width) - 1), std::clamp(y + dy[tap], 0, static_cast<int>(height) - 1), channel);
                Require(Output[lane * 4 + tap] == expected, "integer edge gather mismatch at lane " + std::to_string(lane) + ", tap " + std::to_string(tap));
            }
        }
    }
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        Input[lane * 4 + 1] = static_cast<std::uint32_t>(columns[lane % columns.size()]);
        Input[lane * 4 + 2] = static_cast<std::uint32_t>(rows[lane / columns.size()]);
        Input[lane * 4 + 3] = 0xabu;
    }
    Run(device, Load, format, height);
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        const auto x = columns[lane % columns.size()];
        const auto y = rows[lane / columns.size()];
        for (std::uint32_t c = 0; c < channels; ++c) {
            const auto expected = x < 0 || x >= Width || y < 0 || y >= height ? 0u : Pixel(x, y, c);
            Require(Output[lane * 4 + c] == expected, "integer edge load mismatch at lane " + std::to_string(lane));
        }
    }
    std::vector<std::uint8_t> expected(Texels.begin(), Texels.end());
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        const auto x = columns[lane % columns.size()];
        const auto y = rows[lane / columns.size()];
        if (x >= 0 && x < Width && y >= 0 && y < height) {
            expected[y * mip.pitchBytes + x * channels] = 0xabu;
            if (channels == 2) expected[y * mip.pitchBytes + x * channels + 1] = 0u;
        }
    }
    Run(device, Store, format, height);
    AgcDriver::Graphics::StorageTexture::FlushPending(reinterpret_cast<std::uintptr_t>(Texels.data()), Texels.size(), nullptr, "test");
    device.WaitIdle();
    Require(std::ranges::equal(Texels, expected), "integer edge stores changed a texel or allocation padding unexpectedly");
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestAllocations::Mutation().Add(Texels.data(), Texels.size(), true, true, true);
        Check(*device, 5, 2160, 1);
        Check(*device, 18, 1080, 2);
        GuestAllocations::Mutation().Remove(Texels.data());
        std::puts("wave64 integer plane edge tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
