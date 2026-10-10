#include "IntermediateRepresentation/IrProgram.hpp"
#include "SpirvBackend/SpirvArithmeticOutlining.hpp"
#include "Recompiler.hpp"
#include "Optimization/ResourceProgram.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <spirv/unified1/spirv.hpp>

using namespace ShaderRecompiler;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Builder {
    IrProgram program;
    IrBlock* block;

    Builder() : block(&newBlock()) { program.SetEntryBlock(*block); }

    IrBlock& newBlock() {
        auto& created = program.CreateBlock();
        program.BlockOrder().push_back(&created);
        return created;
    }

    IrValue& constant(std::uint32_t bits) {
        auto& value = program.CreateValue(IrOpcode::Void, IrType::U32);
        value.SetImmediateU32(bits);
        return value;
    }

    IrValue& emit(IrOpcode opcode, std::initializer_list<IrValue*> arguments, IrType type = IrType::U32) {
        auto& value = program.CreateValue(opcode, type);
        for (auto* argument : arguments) value.AddArgument(argument);
        block->AppendInstruction(&value);
        return value;
    }

    IrValue& chain(IrValue& input, std::uint32_t bits = 1u, std::uint64_t flags = 0u) {
        auto* value = &input;
        for (std::uint32_t index = 0; index < 16u; ++index) {
            value = &emit(IrOpcode::BitwiseXor32, {value, &constant(bits + index)});
            value->SetFlags(flags);
        }
        emit(IrOpcode::ReferenceU32, {value}, IrType::Void);
        return *value;
    }
};

void testGraphBoundaries() {
    Builder b;
    auto& lane = b.emit(IrOpcode::LaneId, {});
    auto& loaded = b.emit(IrOpcode::LoadSharedU32, {&lane});
    auto& first = b.chain(loaded);
    auto& second = b.chain(lane);
    const auto plan = PlanArithmeticOutlining(b.program, 1u);
    require(plan.roots.size() == 2u && plan.roots.at(&first).key == plan.roots.at(&second).key, "equivalent arithmetic with different inputs was not shared");
    require(!plan.interiors.contains(&lane) && !plan.interiors.contains(&loaded), "memory or lane operations were outlined");
    for (const auto& [root, graph] : plan.roots) {
        std::unordered_set<const IrValue*> members(graph.instructions.begin(), graph.instructions.end());
        for (const auto* value : graph.instructions) {
            if (value == root) continue;
            for (const auto* user : value->Uses()) require(members.contains(user), "an outlined interior has an external user");
        }
    }
}

void testKeysAndBlocks() {
    Builder b;
    auto& lane = b.emit(IrOpcode::LaneId, {});
    auto& first = b.chain(lane);
    auto& constantChanged = b.chain(lane, 2u);
    auto& flagsChanged = b.chain(lane, 1u, 4u);
    auto& other = b.newBlock();
    b.block = &other;
    auto& acrossBlocks = b.chain(first);
    const auto plan = PlanArithmeticOutlining(b.program, 2u);
    require(plan.roots.at(&first).key != plan.roots.at(&constantChanged).key, "constant bits were omitted from the helper key");
    require(plan.roots.at(&first).key != plan.roots.at(&flagsChanged).key, "flags were omitted from the helper key");
    require(plan.roots.at(&acrossBlocks).inputs.front() == &first, "arithmetic crossed a block boundary");
    for (const auto* value : plan.roots.at(&acrossBlocks).instructions) require(value->Parent() == &other, "an instruction moved between blocks");
}

void testExternalUses() {
    Builder b;
    auto& lane = b.emit(IrOpcode::LaneId, {});
    auto& shared = b.emit(IrOpcode::IAdd32, {&lane, &b.constant(7u)});
    b.chain(shared);
    b.emit(IrOpcode::ReferenceU32, {&shared}, IrType::Void);
    const auto plan = PlanArithmeticOutlining(b.program, 2u);
    require(!plan.interiors.contains(&shared), "a value used outside a graph was removed from its caller");
    require(plan.roots.size() == 1u && plan.roots.begin()->second.inputs.front() == &shared, "a shared value was not retained as a parameter");
}

void testUniformValuesAndAliasing() {
    Builder b;
    auto& lane = b.emit(IrOpcode::LaneId, {});
    auto& other = b.emit(IrOpcode::LoadSharedU32, {&lane});
    auto& aliased = b.chain(b.emit(IrOpcode::IAdd32, {&lane, &lane}));
    auto& distinct = b.chain(b.emit(IrOpcode::IAdd32, {&lane, &other}));
    const auto plan = PlanArithmeticOutlining(b.program, 2u);
    require(plan.roots.at(&aliased).key != plan.roots.at(&distinct).key, "input aliasing was omitted from the helper key");
    std::unordered_set<const IrValue*> shared;
    for (const auto* value : b.block->Instructions()) shared.insert(value);
    require(PlanArithmeticOutlining(b.program, 2u, shared).roots.empty(), "uniform arithmetic was counted twice for wave64");
}

std::vector<std::uint32_t> compile(std::uint32_t copies, std::uint32_t waveSize) {
    std::vector<std::uint32_t> code{0xe0381000u, 0x80000400u, 0xbf8c3f70u};
    for (std::uint32_t index = 0; index < copies; ++index) code.push_back(0x10000b04u | ((10u + index) << 17u));
    for (std::uint32_t index = 0; index < copies; ++index) code.insert(code.end(), {0xe0701000u | (index * 4u), 0x80010000u | ((10u + index) << 8u)});
    code.push_back(0xbf810000u);
    const std::array<std::uint32_t, 8> user{0x10000000u, 0u, 4096u, 0x31016facu, 0x20000000u, 0u, 4096u, 0x31016facu};
    RecompileRequest request{};
    request.shader = {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}};
    request.target = {0x00402000u, 0x00010300u, 32u};
    request.context.waveSize = waveSize;
    request.context.userData = user;
    request.context.compute = ShaderComputeStageInfo{{waveSize, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    request.layout = {0u, 0u, 0u, 128u};
    request.useCache = false;
    return GetPreparedArtifact(*PrepareShader(request)).spirv.Words();
}

std::pair<std::size_t, std::size_t> count(std::span<const std::uint32_t> words) {
    std::size_t helpers = 0, calls = 0;
    for (std::size_t offset = 5u; offset < words.size();) {
        const auto size = words[offset] >> spv::WordCountShift;
        require(size != 0u && size <= words.size() - offset, "malformed outlined module");
        const auto opcode = words[offset] & spv::OpCodeMask;
        if (opcode == spv::OpFunction && (words[offset + 3u] & spv::FunctionControlDontInlineMask) != 0u) ++helpers;
        if (opcode == spv::OpFunctionCall) ++calls;
        offset += size;
    }
    return {helpers, calls};
}

std::size_t countOpcode(std::span<const std::uint32_t> words, spv::Op opcode) {
    std::size_t result = 0;
    for (std::size_t offset = 5u; offset < words.size(); offset += words[offset] >> spv::WordCountShift) {
        if ((words[offset] & spv::OpCodeMask) == opcode) ++result;
    }
    return result;
}

void testModuleSharing() {
    for (const auto waveSize : {32u, 64u}) {
        const auto two = compile(2u, waveSize);
        const auto sixteen = compile(16u, waveSize);
        const auto [twoHelpers, twoCalls] = count(two);
        const auto [sixteenHelpers, sixteenCalls] = count(sixteen);
        std::printf("wave%u: %zu -> %zu words, %zu -> %zu helpers, %zu -> %zu calls\n", waveSize, two.size(), sixteen.size(), twoHelpers, sixteenHelpers, twoCalls, sixteenCalls);
        require(twoHelpers != 0u && sixteenHelpers == twoHelpers, "repeated float arithmetic duplicated helper functions");
        require(sixteenCalls > twoCalls, "repeated float arithmetic did not call shared helpers");
        require(countOpcode(sixteen, spv::OpFMul) == countOpcode(two, spv::OpFMul), "repeated arithmetic duplicated floating multiply instructions");
        require(countOpcode(sixteen, spv::OpUMulExtended) == countOpcode(two, spv::OpUMulExtended), "repeated arithmetic duplicated significand calculations");
    }
}

}

int main() {
    try {
        testGraphBoundaries();
        testKeysAndBlocks();
        testExternalUses();
        testUniformValuesAndAliasing();
        testModuleSharing();
        std::puts("arithmetic outlining tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
