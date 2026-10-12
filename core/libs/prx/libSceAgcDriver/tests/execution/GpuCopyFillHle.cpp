#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using namespace AgcDriver;
using Graphics::Require;
using ShaderRecompiler::ShaderStage;
constexpr std::size_t Bytes = 65536;
bool resident = false;

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        if (resident) {
            data = GuestArena::GuestArenaAllocate_nid_postfix(Bytes, Bytes);
            GuestArena::GuestArenaCommit_nid_postfix(data, Bytes, PAGE_READWRITE, Bytes);
        } else data = VirtualAlloc(nullptr, Bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        data = std::aligned_alloc(Bytes, Bytes);
#endif
        Require(data != nullptr, "guest allocation failed");
        std::memset(data, 0x55, Bytes);
        GuestAllocations::Mutation().Add(data, Bytes, true, true, true);
    }
    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(data);
#ifdef _WIN32
        if (resident) GuestArena::GuestArenaRelease_nid_postfix(data, Bytes);
        else VirtualFree(data, 0, MEM_RELEASE);
#else
        std::free(data);
#endif
    }
    std::uint64_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }
private:
    void* data = nullptr;
};

std::array<std::uint32_t, 4> Descriptor(std::uint64_t address, std::uint32_t bytes) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u), bytes, 0x31016facu};
}

void DispatchCode(VulkanDevice& device, std::span<const std::uint32_t> code, const std::vector<std::uint32_t>& data) {
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(std::span(code))}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderRecompiler::ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, data, compute, std::nullopt, std::nullopt, memory},
        device.Target(), {0, 0, 0, 128},
    };
    request.useCache = false;
    const auto shader = ShaderRecompiler::Recompile(request);
    device.Dispatch(shader, 1, 1, 1);
}

void LoadTexels(VulkanDevice& device, std::uint64_t texels, std::uint64_t input, std::uint64_t output) {
    alignas(256) static constexpr std::array<std::uint32_t, 10> code{
        0x34020084, 0xe0381000, 0x80000201, 0xbf8c3f70, 0xf0001f08, 0x00020602, 0xbf8c3f70, 0xe0781000, 0x80010601, 0xbf810000,
    };
    std::vector<std::uint32_t> data(16);
    const auto in = Descriptor(input, 32 * 16), out = Descriptor(output, 32 * 16);
    std::copy(in.begin(), in.end(), data.begin());
    std::copy(out.begin(), out.end(), data.begin() + 4);
    const std::array<std::uint32_t, 8> texture{
        static_cast<std::uint32_t>(texels >> 8u), static_cast<std::uint32_t>(texels >> 40u) | (56u << 20u) | (3u << 30u),
        (127u >> 2u) | (127u << 14u), 0xfacu | (0x1bu << 20u) | (9u << 28u), 0, 0, 0, 0,
    };
    std::copy(texture.begin(), texture.end(), data.begin() + 8);
    DispatchCode(device, code, data);
}

alignas(256) constexpr std::array<std::uint32_t, 78> LoadsCode{
    0x34020084, 0x34040086, 0xdc388000, 0x04080001, 0xbf8c3f70, 0x7e1402ff, 0x5a5a0000, 0x7e1602ff,
    0x5a5a0001, 0x7e1802ff, 0x5a5a0002, 0x7e1a02ff, 0x5a5a0003, 0x7e1c02ff, 0x5a5a0004, 0x7e1e02ff,
    0x5a5a0005, 0x7e2002ff, 0x5a5a0006, 0x7e2202ff, 0x5a5a0007, 0x7e2402ff, 0x5a5a0008, 0x7e2602ff,
    0x5a5a0009, 0x7e2802ff, 0x5a5a000a, 0x7e2a02ff, 0x5a5a000b, 0x7e2c02ff, 0x5a5a000c, 0x7e2e02ff,
    0x5a5a000d, 0x7e3002ff, 0x5a5a000e, 0x7e3202ff, 0x5a5a000f, 0x4a4008ff, 0x00000a00, 0x4a420eff,
    0x00000200, 0xd70f6a22, 0x02024008, 0x7e460209, 0x50464680, 0x4a4c0aff, 0x00000200, 0xd70f6a24,
    0x02024c08, 0x7e4a0209, 0x504a4a80, 0x36500c81, 0x7daa5080, 0xdc208800, 0x0a080020, 0xdc2487ff,
    0x0b080020, 0xdc288ffd, 0x0c080020, 0xdc2c8001, 0x0d7d0022, 0xdc308fff, 0x0e080020, 0xdc308000,
    0x18080021, 0xdc208800, 0x197d0022, 0xbf8c0070, 0xbefe04c1, 0xe0781000, 0x80010a02, 0xe0781010,
    0x80010e02, 0xe0781020, 0x80011202, 0xe0781030, 0x80011602, 0xbf810000
};

void RecordDispatch(AgcDriver::VulkanDevice& device, const GuestBlock& input, const GuestBlock& output) {
    std::vector<std::uint32_t> userData(10, 0u);
    const auto address = output.Address();
    const std::array<std::uint32_t, 4> descriptor{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), 2048u, 0x31016facu};
    std::copy(descriptor.begin(), descriptor.end(), userData.begin() + 4);
    userData[8] = static_cast<std::uint32_t>(input.Address());
    userData[9] = static_cast<std::uint32_t>(input.Address() >> 32u);
    const std::span<const std::uint32_t> code(LoadsCode);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{32, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
}

void WriteTexels(VulkanDevice& device, std::uint64_t address) {
    alignas(256) static constexpr std::array<std::uint32_t, 8> code{
        0x7e020280, 0x7e0402f2, 0x7e0602f2, 0x7e0802f2, 0x7e0a02f2, 0xf0201f08, 0x00010200, 0xbf810000,
    };
    std::vector<std::uint32_t> data(16);
    const std::array<std::uint32_t, 8> texture{
        static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u) | (56u << 20u) | (3u << 30u),
        (127u >> 2u) | (127u << 14u), 0xfacu | (0x1bu << 20u) | (9u << 28u), 0, 0, 0, 0,
    };
    std::copy(texture.begin(), texture.end(), data.begin() + 4);
    DispatchCode(device, code, data);
}

void Fill(VulkanDevice& device, const GuestBlock& block, const std::array<std::uint32_t, 4>& pattern) {
    auto prepared = device.PrepareCopyFill(block.Address(), 0, Bytes);
    std::lock_guard lock(GuestMemory::GpuMutex());
    const auto* recorder = Graphics::Recorder::Active();
    const auto submitted = recorder->Submissions();
    const auto refusal = device.RecordCopyFill(prepared, pattern, 0, {});
    Require(refusal == nullptr, std::string("prepared fill refused: ") + (refusal ? refusal : ""));
    Require(recorder->Submissions() == submitted && recorder->OpenWriteOverlaps(block.Address(), Bytes), "fill submitted or failed to note its pending write");
}

std::array<std::uint32_t, Bytes / 4> Read(const GuestBlock& block) {
    std::array<std::uint32_t, Bytes / 4> words;
    GuestMemory::Read(block.Address(), std::as_writable_bytes(std::span(words)));
    return words;
}
}

int main(int argc, char**) {
    struct ProfileDrain { ~ProfileDrain() { AgcDriverShutdown_nid_postfix(); } } drain;
    try {
        resident = argc > 1;
#ifdef _WIN32
        if (resident) _putenv_s("APS5_HOST_IMPORT_MIB", "0");
#else
        if (resident) return VulkanTestSkipped;
#endif
        GuestBlock source, destination, input, output;
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (resident) {
            std::memset(reinterpret_cast<void*>(input.Address()), 0, Bytes);
            RecordDispatch(*device, input, output);
        }
        const std::array<std::uint32_t, 4> zero{};
        {
            std::lock_guard lock(GuestMemory::GpuMutex());
            if (!device->FillBuffer(source.Address(), Bytes, zero) || !device->FillBuffer(destination.Address(), Bytes, zero)) return VulkanTestSkipped;
        }
        Read(source);
        Read(destination);
        Fill(*device, source, {0x12345678, 0x12345678, 0x12345678, 0x12345678});
        const auto filled = Read(source);
        Require(std::all_of(filled.begin(), filled.end(), [](auto word) { return word == 0x12345678; }), "CPU read missed the uniform GPU fill");
        const std::array<std::uint32_t, 4> pattern{1, 0xdeadbeef, 0x80000000, 0xffffffff};
        Fill(*device, source, pattern);
        auto prepared = device->PrepareCopyFill(destination.Address(), source.Address(), Bytes);
        bool journal = false;
        {
            std::lock_guard lock(GuestMemory::GpuMutex());
            auto* recorder = Graphics::Recorder::Active();
            const auto submitted = recorder->Submissions();
            const auto outcome = device->CopyBuffer(destination.Address(), source.Address(), Bytes, Bytes, std::numeric_limits<std::size_t>::max(), Bytes, 0, 0, [&](auto value, auto generation) {
                Require(value.empty() && generation == 0, "GPU copy read source payload for its journal");
                journal = true;
            }, prepared);
            Require(outcome.path == 1 && !outcome.synced, "copy used CPU bytes or synchronized");
            Require(recorder->Submissions() == submitted && recorder->OpenWriteOverlaps(destination.Address(), Bytes), "copy submitted or failed to note its pending write");
        }
        Require(journal, "copy writer was not noted");
        const auto copied = Read(destination);
        for (std::size_t i = 0; i < copied.size(); ++i) Require(copied[i] == pattern[i % 4], "CPU read missed patterned fill/copy ordering");
        {
            std::lock_guard lock(GuestMemory::GpuMutex());
            const auto outcome = device->CopyBuffer(source.Address() + 16, source.Address(), 256, Bytes, Bytes, Bytes, 0, 0, {});
            Require(outcome.path == 6 && !outcome.synced, "overlapping copy was substituted for the original kernel");
        }
        {
            std::lock_guard lock(GuestMemory::GpuMutex());
            Require(device->FillBuffer(input.Address(), Bytes, zero), "coordinate fill was not imported");
        }
        RecordDispatch(*device, input, source);
        prepared = device->PrepareCopyFill(destination.Address(), source.Address(), 2048);
        {
            std::lock_guard lock(GuestMemory::GpuMutex());
            const auto refusal = device->RecordCopyFill(prepared, {}, 0, {});
            Require(refusal == nullptr, std::string("dispatch-to-copy refused: ") + (refusal ? refusal : ""));
        }
        const auto produced = Read(destination), original = Read(source);
        Require(std::equal(produced.begin(), produced.begin() + 512, original.begin()), "copy missed a recorded dispatch producer");
        for (const auto color : {0xffffffffu, 0xff0000ffu, 0xff00ff00u}) {
            WriteTexels(*device, source.Address());
            Fill(*device, source, {color, color, color, color});
            LoadTexels(*device, source.Address(), input.Address(), output.Address());
            std::array<float, 32 * 4> pixels;
            GuestMemory::Read(output.Address(), std::as_writable_bytes(std::span(pixels)));
            for (std::size_t i = 0; i < pixels.size(); ++i) Require(pixels[i] == static_cast<float>((color >> ((i % 4) * 8)) & 255) / 255.0f, "cached image load missed the GPU fill");
        }
        if (resident) {
            Fill(*device, source, pattern);
            {
                std::lock_guard lock(GuestMemory::GpuMutex());
                device->WaitIdle();
                Require(Graphics::ResidentMirrorWriteOverlaps(source.Address(), 8), "completed resident write was treated as current CPU metadata");
            }
            Read(source);
            std::lock_guard lock(GuestMemory::GpuMutex());
            Require(!Graphics::ResidentMirrorWriteOverlaps(source.Address(), 8), "resident metadata stayed pending after a CPU read");
        }
        std::puts("GPU copy/fill HLE tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
