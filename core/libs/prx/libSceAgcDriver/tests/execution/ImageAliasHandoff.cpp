#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace {
using AgcDriver::Graphics::Require;
using AgcDriver::Graphics::StorageTexture;
constexpr std::size_t BlockBytes = 2 * 1024 * 1024;
constexpr std::size_t KeysOffset = BlockBytes - 65536;
constexpr std::uint32_t Threads = 8;
alignas(256) std::array<std::uint32_t, Threads * 4> Input{}, Output{};
alignas(256) constexpr std::array<std::uint32_t, 10> StoreCode{
    0x34020084, 0xe0381000, 0x80001401, 0xbf8c3f70, 0x7e3c0300, 0x7e3e0280, 0x7e400280, 0xf0200108, 0x0001141e, 0xbf810000,
};
alignas(256) constexpr std::array<std::uint32_t, 10> LoadCode{
    0x34020084, 0x7e3c0300, 0x7e3e0280, 0x7e400280, 0xf0001f08, 0x0001141e, 0xbf8c3f70, 0xe0781000, 0x80001401, 0xbf810000,
};

std::uint64_t AddressOf(const void* data) { return reinterpret_cast<std::uintptr_t>(data); }

class GuestBlock {
public:
    explicit GuestBlock(bool watch, bool shared = false) : watched(watch) {
#ifdef _WIN32
        if (watched) {
            block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
            if (shared) {
                Require(block != nullptr, "cannot reserve shared test memory");
                const auto section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, BlockBytes, nullptr);
                Require(section != nullptr, "cannot create shared test memory");
                GuestArena::GuestArenaMap_nid_postfix(block, BlockBytes, section, 0, PAGE_READWRITE);
                CloseHandle(section);
            } else if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
        } else {
            block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        }
#else
        Require(!shared, "shared test memory requires Windows");
        if (watched) {
            constexpr std::uintptr_t alignment = 65536;
            void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapped != MAP_FAILED) {
                const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
                const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
                if (aligned != begin) munmap(mapped, aligned - begin);
                if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
                block = reinterpret_cast<std::uint8_t*>(aligned);
                GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
            }
        } else {
            block = static_cast<std::uint8_t*>(std::aligned_alloc(65536, BlockBytes));
        }
#endif
        Require(block != nullptr, "image alias: cannot allocate the guest block");
        Require(!watched || AgcDriver::GuestMemory::Watched(AddressOf(block), BlockBytes), "image alias: the watched guest block is not write-watched");
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        if (watched) {
            GuestArena::GuestArenaReset_nid_postfix(block, BlockBytes);
            GuestArena::GuestArenaRelease_nid_postfix(block, BlockBytes);
        } else {
            VirtualFree(block, 0, MEM_RELEASE);
        }
#else
        if (watched) {
            munmap(block, BlockBytes);
            GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, BlockBytes);
        } else {
            std::free(block);
        }
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* Data() { return block; }

private:
    bool watched;
    std::uint8_t* block = nullptr;
};

struct Surface {
    std::uint32_t format = 20;
    std::uint32_t width = 256;
    std::uint32_t height = 256;
    std::uint32_t mips = 1;
    std::uint32_t layers = 1;
    std::uint32_t baseLayer = 0;
    std::uint32_t tile = 0x1b;
    std::uint32_t type = 9;
    std::uint32_t offset = 0;
    bool dcc = false;
};

std::array<std::uint32_t, 8> Descriptor(std::uint8_t* block, const Surface& s) {
    const auto address = AddressOf(block + s.offset);
    const auto meta = s.dcc ? AddressOf(block + KeysOffset) >> 8 : 0;
    return {static_cast<std::uint32_t>(address >> 8), static_cast<std::uint32_t>((address >> 40) & 255) | (s.format << 20) | (((s.width - 1) & 3) << 30),
            ((s.width - 1) >> 2) | ((s.height - 1) << 14), 0xfacu | (s.tile << 20) | (s.type << 28),
            (s.layers - 1) | (s.baseLayer << 16), (s.mips - 1) << 4,
            s.dcc ? (1u << 20) | (1u << 21) | (static_cast<std::uint32_t>(meta & 255) << 24) : 0u, static_cast<std::uint32_t>(meta >> 8)};
}

void Dispatch(AgcDriver::VulkanDevice& device, std::uint8_t* block, const Surface& surface, bool write) {
    const auto& data = write ? Input : Output;
    const auto address = AddressOf(data.data());
    const std::array<std::uint32_t, 4> buffer{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32) & 65535), static_cast<std::uint32_t>(data.size() * 4), 0x31016facu};
    const auto texture = Descriptor(block, surface);
    std::vector<std::uint32_t> userData(16);
    std::copy(buffer.begin(), buffer.end(), userData.begin());
    std::copy(texture.begin(), texture.end(), userData.begin() + 4);
    const std::span<const std::uint32_t> code = write ? std::span(StoreCode) : std::span(LoadCode);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{AddressOf(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{{ShaderRecompiler::ShaderStage::Compute, AddressOf(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory}, device.Target(), {0, 0, 0, 128}};
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    {
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        device.Dispatch(result, 1, 1, 1, {}, AddressOf(code.data()));
    }
    device.WaitIdle();
}

void Flush(AgcDriver::VulkanDevice& device, std::uint8_t* block) {
    StorageTexture::FlushPending(AddressOf(block), BlockBytes, nullptr, "alias test");
    device.WaitIdle();
}

void Clear(AgcDriver::VulkanDevice& device) {
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    AgcDriver::Graphics::ClearCachedTextures(device.Device());
}

struct Result {
    std::array<std::uint32_t, Threads * 4> values;
    std::array<std::uint32_t, Threads * 4> reverse;
    std::vector<std::uint8_t> bytes;
};

Result Run(AgcDriver::VulkanDevice& device, std::uint8_t* block, const Surface& source, const Surface& alias, bool reference, bool cpu, bool keys) {
    std::memset(block, 0xa5, BlockBytes);
    std::memset(block + KeysOffset, 0xff, 65536);
    AgcDriver::GuestMemory::MarkWritten(AddressOf(block), BlockBytes);
    Input.fill(0x12345678);
    Dispatch(device, block, source, true);
    const auto from = AgcDriver::Graphics::DecodeTextureResource(Descriptor(block, source));
    const auto image = StorageTexture::FindPending(from.baseAddress, AgcDriver::Graphics::DescribeSurface(from).guestBytes);
    Require(image != nullptr, "producer did not leave pending GPU results");
    if (cpu) {
        std::memset(block + source.offset, 0x5a, 65536);
        AgcDriver::GuestMemory::MarkWritten(AddressOf(block + source.offset), 65536);
        AgcDriver::GuestMemory::CollectWritesUncached(AddressOf(block), BlockBytes);
    }
    if (keys) {
        std::memset(block + KeysOffset, 0, 65536);
        AgcDriver::GuestMemory::MarkWritten(AddressOf(block + KeysOffset), 65536);
    }
    if (reference) Flush(device, block);
    Output.fill(0);
    Dispatch(device, block, alias, false);
    const auto values = Output;
    if (reference) Flush(device, block);
    Input.fill(0x23456789);
    Dispatch(device, block, alias, true);
    if (reference) Flush(device, block);
    Output.fill(0);
    Dispatch(device, block, source, false);
    Flush(device, block);
    Result result{values, Output, {block, block + BlockBytes}};
    Clear(device);
    return result;
}

void Check(AgcDriver::VulkanDevice& device, std::uint8_t* block, const char* name, Surface source, Surface alias, StorageTexture::AliasKind expected, bool cpu = false, bool keys = false) {
    const auto from = AgcDriver::Graphics::DecodeTextureResource(Descriptor(block, source));
    const auto to = AgcDriver::Graphics::DecodeTextureResource(Descriptor(block, alias));
    Require(StorageTexture::PlanAlias(from, to).kind == expected, std::string(name) + ": unexpected layout classification");
    const auto before = StorageTexture::AliasCounters();
    const auto actual = Run(device, block, source, alias, false, cpu, keys);
    const auto after = StorageTexture::AliasCounters();
    const auto reference = Run(device, block, source, alias, true, cpu, keys);
    Require(actual.values == reference.values, std::string(name) + ": dispatch read differs from guest-memory reference");
    Require(actual.reverse == reference.reverse, std::string(name) + ": reverse dispatch read differs from guest-memory reference");
    Require(actual.bytes == reference.bytes, std::string(name) + ": tiled bytes or metadata differ from guest-memory reference");
    if (StorageTexture::GpuAliasHandoffEnabled() && !cpu && !keys && expected != StorageTexture::AliasKind::Layout) {
        Require(after.gpu[static_cast<std::size_t>(expected)] > before.gpu[static_cast<std::size_t>(expected)], std::string(name) + ": no GPU hand-off was exercised");
    }
    std::printf("%s: texel and guest-byte exact\n", name);
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
#ifdef _WIN32
        GuestBlock block(true, true);
#else
        GuestBlock block(true);
#endif
        GuestAllocations::Mutation().Add(Input.data(), sizeof(Input), true, true, true);
        GuestAllocations::Mutation().Add(Output.data(), sizeof(Output), true, true, true);
        Surface source, alias;
        alias.format = 21;
        Check(*device, block.Data(), "format reinterpretation", source, alias, StorageTexture::AliasKind::Format);
        source.layers = alias.layers = 2;
        source.type = alias.type = 13;
        alias.format = source.format;
        alias.baseLayer = 1;
        Check(*device, block.Data(), "identical layout different layer key", source, alias, StorageTexture::AliasKind::Identical);
        source = {}; alias = source; alias.height = 128;
        Check(*device, block.Data(), "extent subset", source, alias, StorageTexture::AliasKind::Subset);
        source.layers = 2; source.type = 13;
        alias = source; alias.layers = 1;
        Check(*device, block.Data(), "layer subset", source, alias, StorageTexture::AliasKind::Subset);
        source = {}; source.mips = 2;
        alias = {}; alias.offset = static_cast<std::uint32_t>(AgcDriver::Graphics::DescribeSurface(AgcDriver::Graphics::DecodeTextureResource(Descriptor(block.Data(), source))).mips[0].tiledOffset);
        Check(*device, block.Data(), "rebased mip subset", source, alias, StorageTexture::AliasKind::Subset);
        source = {}; alias = source; alias.format = 21;
        Check(*device, block.Data(), "interleaved CPU write", source, alias, StorageTexture::AliasKind::Format, true);
        source.dcc = alias.dcc = true;
        Check(*device, block.Data(), "uncompressed DCC metadata", source, alias, StorageTexture::AliasKind::Format);
        source.format = alias.format = 56;
        alias.height = 128;
        Check(*device, block.Data(), "DCC clear invalidation", source, alias, StorageTexture::AliasKind::Subset, false, true);
        source = {}; alias = source; alias.tile = 0;
        Check(*device, block.Data(), "different tiling fallback", source, alias, StorageTexture::AliasKind::Layout);
        GuestAllocations::Mutation().Remove(Input.data());
        GuestAllocations::Mutation().Remove(Output.data());
        std::puts("image alias hand-off tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
