#include "BdaAbi.hpp"
#include "Recompiler.hpp"
#include "Optimization/ResourceProgram.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <span>
#include <spirv/unified1/spirv.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ShaderRecompiler;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

SpirvTarget Target(bool sharedDescriptors = false) {
    static constexpr std::array<std::uint32_t, 4> capabilities{
        spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess,
        spv::CapabilityStorageBufferArrayDynamicIndexing,
    };
    static constexpr std::array<std::string_view, 4> extensions{
        "SPV_EXT_descriptor_indexing", "SPV_KHR_8bit_storage", "SPV_KHR_float_controls", "SPV_KHR_physical_storage_buffer",
    };
    return {0x00401000u, 0x00010300u, 32, BdaAbi::Version, std::span(capabilities).first(sharedDescriptors ? 4u : 3u), extensions, false,
            {1024, 1024, 64}, 1024, 32768, {}, {}};
}

struct Counts {
    std::size_t words = 0;
    std::size_t switches = 0;
    std::size_t calls = 0;
    std::size_t functions = 0;
    std::size_t notInlined = 0;
};

Counts Count(std::span<const std::uint32_t> words) {
    Check(words.size() >= 5 && words[0] == spv::MagicNumber, "formatted buffer once: missing SPIR-V module");
    Counts counts{words.size()};
    for (std::size_t offset = 5; offset < words.size();) {
        const auto count = words[offset] >> spv::WordCountShift;
        Check(count != 0 && count <= words.size() - offset, "formatted buffer once: malformed SPIR-V instruction");
        const auto opcode = words[offset] & spv::OpCodeMask;
        if (opcode == spv::OpSwitch) ++counts.switches;
        if (opcode == spv::OpFunctionCall) ++counts.calls;
        if (opcode == spv::OpFunction) {
            ++counts.functions;
            if (count > 3 && (words[offset + 3] & spv::FunctionControlDontInlineMask) != 0) ++counts.notInlined;
        }
        offset += count;
    }
    return counts;
}

Counts Compile(std::size_t loads, bool formattedStores = false, bool gpu = false, bool sharedDescriptors = false, bool multipleResources = false, bool prepared = false) {
    std::vector<std::uint32_t> code;
    if (gpu) code.push_back(0x7e160500u);
    for (std::uint32_t load = 0; load < loads; ++load) code.insert(code.end(), {0xe0000000u | (load * 4u), (multipleResources && load % 2u != 0u ? 0x80010000u : 0x80020000u) | ((10u + load) << 8u)});
    code.push_back(0xbf8c3f70u);
    for (std::uint32_t load = 0; load < loads; ++load) code.insert(code.end(), {(formattedStores ? 0xe0100000u : 0xe0700000u) | (load * 4u), 0x80010000u | ((10u + load) << 8u)});
    code.push_back(0xbf810000u);
    const std::span<const std::uint32_t> words(code);
    const std::array<MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(words)}}};
    const std::array<std::uint32_t, 12> userData{0u, 0u, 0u, 0u, 0x10000000u, 0u, 0x1000u, 0x31016facu, 0x20000000u, 0u, 0x1000u, 0x31038facu};
    RecompileRequest request{};
    request.shader = {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), words, 0, {}};
    request.target = Target(sharedDescriptors);
    request.layout = {0, 0, 0, 128};
    request.context.waveSize = 32;
    request.context.userDataBaseRegister = 0;
    request.context.userData = userData;
    request.context.compute = ShaderComputeStageInfo{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    request.context.memory = memory;
    request.useCache = false;
    if (prepared) return Count(GetPreparedArtifact(*PrepareShader(request)).spirv.Words());
    return Count(Recompile(request).spirv.Words());
}

}

int main() {
    try {
        const auto none = Compile(0);
        const auto one = Compile(1);
        const auto three = Compile(3);
        Check(one.calls > none.calls && one.functions == none.functions + 5, "formatted buffer once: expected one format dispatcher, one address helper and three memory helpers");
        Check(three.calls == one.calls + 2 && three.functions == one.functions, "formatted buffer once: two more formatted loads from the same descriptor emitted " + std::to_string(three.calls - one.calls) + " calls and " + std::to_string(three.functions - one.functions) + " functions more, expected two calls and no function");
        Check(three.switches == one.switches, "formatted buffer once: two more formatted loads from the same descriptor emitted " + std::to_string(three.switches - one.switches) + " more format switches");
        Check(one.notInlined == none.notInlined + 5, "formatted buffer once: the shared formatted access functions may be inlined back into their callers");
        const auto storeOne = Compile(1, true);
        const auto storeThree = Compile(3, true);
        Check(storeOne.functions == none.functions + 9 && storeOne.calls > none.calls, "formatted buffer once: expected shared format, address and memory helpers for loads and stores");
        Check(storeThree.functions == storeOne.functions && storeThree.calls == storeOne.calls + 4 && storeThree.switches == storeOne.switches, "formatted buffer once: two more formatted loads and stores emitted " + std::to_string(storeThree.functions - storeOne.functions) + " functions, " + std::to_string(storeThree.calls - storeOne.calls) + " calls and " + std::to_string(storeThree.switches - storeOne.switches) + " switches more, expected four calls only");
        Check(three.words - one.words < one.words / 10u, "formatted buffer once: 2 more formatted loads added " + std::to_string(three.words - one.words) + " SPIR-V words to a " + std::to_string(one.words) + "-word module");
        const auto gpuOne = Compile(1, false, true);
        const auto gpuThree = Compile(3, false, true);
        Check(gpuThree.functions == gpuOne.functions && gpuThree.calls > gpuOne.calls, "formatted buffer once: two more formatted loads from a GPU-selected descriptor emitted " + std::to_string(gpuThree.functions - gpuOne.functions) + " more functions, expected calls to the shared one");
        Check(gpuThree.switches == gpuOne.switches, "formatted buffer once: two more formatted loads from a GPU-selected descriptor emitted " + std::to_string(gpuThree.switches - gpuOne.switches) + " more format switches");
        Check(gpuThree.words - gpuOne.words < gpuOne.words / 10u, "formatted buffer once: 2 more formatted loads from a GPU-selected descriptor added " + std::to_string(gpuThree.words - gpuOne.words) + " SPIR-V words to a " + std::to_string(gpuOne.words) + "-word module");
        const auto sharedOne = Compile(1, false, false, true, false, true);
        const auto sharedThree = Compile(3, false, false, true, true, true);
        const auto separateThree = Compile(3, false, false, false, true, true);
        Check(sharedThree.functions == sharedOne.functions && sharedThree.switches == sharedOne.switches, "formatted buffer once: another descriptor duplicated the format dispatcher");
        Check(sharedThree.calls == sharedOne.calls + 2u && sharedThree.words - sharedOne.words < sharedOne.words / 10u, "formatted buffer once: shared descriptors did not add only small call sites");
        Check(separateThree.functions == sharedThree.functions && separateThree.switches == sharedThree.switches && separateThree.words == sharedThree.words, "formatted buffer once: sharing depends on dynamic descriptor indexing support");
        const auto sharedStoreOne = Compile(1, true, false, true, false, true);
        const auto sharedStoreThree = Compile(3, true, false, true, true, true);
        Check(sharedStoreThree.functions == sharedStoreOne.functions && sharedStoreThree.switches == sharedStoreOne.switches, "formatted buffer once: shared loads and stores duplicated a dispatcher");
        std::printf("formatted buffer once tests passed (%zu words for 1 load, %zu for 3; GPU-selected %zu and %zu)\n", one.words, three.words, gpuOne.words, gpuThree.words);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
