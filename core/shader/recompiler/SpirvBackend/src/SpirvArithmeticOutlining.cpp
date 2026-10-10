#include "SpirvBackend/SpirvArithmeticOutlining.hpp"
#include <algorithm>

namespace ShaderRecompiler {
namespace {

bool arithmetic(const IrValue& value) {
    if (value.HasImmediate()) return false;
    if (value.Type() != IrType::U32 && value.Type() != IrType::F32 && value.Type() != IrType::U1) return false;
    switch (value.Opcode()) {
    case IrOpcode::IAdd32:
    case IrOpcode::ISub32:
    case IrOpcode::IMul32:
    case IrOpcode::UMulHi:
    case IrOpcode::BitCastU32F32:
    case IrOpcode::BitCastF32U32:
    case IrOpcode::BitwiseAnd32:
    case IrOpcode::BitwiseOr32:
    case IrOpcode::BitwiseXor32:
    case IrOpcode::BitwiseNot32:
    case IrOpcode::BitFieldUExtract:
    case IrOpcode::BitFieldSExtract:
    case IrOpcode::ShiftLeftLogical32:
    case IrOpcode::ShiftRightLogical32:
    case IrOpcode::ShiftRightArithmetic32:
    case IrOpcode::IEqual32:
    case IrOpcode::INotEqual32:
    case IrOpcode::ULessThan32:
    case IrOpcode::UGreaterThan32:
    case IrOpcode::ULessThanEqual32:
    case IrOpcode::UGreaterThanEqual32:
    case IrOpcode::LogicalAnd:
    case IrOpcode::LogicalOr:
    case IrOpcode::LogicalNot:
    case IrOpcode::SelectU32:
    case IrOpcode::SelectF32:
    case IrOpcode::SelectU1:
    case IrOpcode::FPAdd32:
    case IrOpcode::FPSub32:
    case IrOpcode::FPMul32:
    case IrOpcode::FPFma32:
    case IrOpcode::FPMad32:
        return true;
    default:
        return false;
    }
}

void append(std::string& key, std::uint64_t value) {
    for (std::uint32_t byte = 0; byte < 8u; ++byte) key.push_back(static_cast<char>(value >> (byte * 8u)));
}

SpirvArithmeticGraph graphFor(const IrValue* root, const std::unordered_set<const IrValue*>& assigned,
                             const std::unordered_map<const IrValue*, std::size_t>& positions) {
    std::unordered_set<const IrValue*> candidates;
    candidates.insert(root);
    std::vector<const IrValue*> order{root};
    bool changed;
    do {
        changed = false;
        std::vector<const IrValue*> additions;
        for (const auto* value : order) {
            for (const auto* argument : value->Arguments()) {
                argument = argument->Resolve();
                if (argument->Parent() != root->Parent() || assigned.contains(argument) || candidates.contains(argument) || !arithmetic(*argument)) continue;
                if (std::all_of(argument->Uses().begin(), argument->Uses().end(), [&](const auto* user) { return candidates.contains(user); })) additions.push_back(argument);
            }
        }
        for (const auto* value : additions) {
            if (candidates.size() >= 64u) break;
            if (candidates.insert(value).second) {
                order.push_back(value);
                changed = true;
            }
        }
    } while (changed);
    SpirvArithmeticGraph graph;
    std::unordered_set<const IrValue*> visited;
    const auto reachable = [&](const auto& self, const IrValue* value) -> void {
        value = value->Resolve();
        if (!candidates.contains(value) || !visited.insert(value).second) return;
        for (const auto* argument : value->Arguments()) self(self, argument);
        graph.instructions.push_back(value);
    };
    reachable(reachable, root);
    if (graph.instructions.size() < 12u) return {};
    std::sort(graph.instructions.begin(), graph.instructions.end(), [&](const auto* lhs, const auto* rhs) { return positions.at(lhs) < positions.at(rhs); });
    std::unordered_map<const IrValue*, std::size_t> ids;
    for (std::size_t index = 0; index < graph.instructions.size(); ++index) ids.emplace(graph.instructions[index], index);
    for (const auto* value : graph.instructions) {
        append(graph.key, static_cast<std::uint64_t>(value->Opcode()));
        append(graph.key, static_cast<std::uint64_t>(value->Type()));
        append(graph.key, value->Flags<std::uint64_t>());
        append(graph.key, value->ArgumentCount());
        for (const auto* argument : value->Arguments()) {
            argument = argument->Resolve();
            append(graph.key, static_cast<std::uint64_t>(argument->Type()));
            if (argument->HasImmediate()) {
                append(graph.key, 0u);
                append(graph.key, argument->ImmediateU64());
            } else if (const auto found = ids.find(argument); found != ids.end()) {
                append(graph.key, 1u);
                append(graph.key, found->second);
            } else {
                if (argument->Type() != IrType::U32 && argument->Type() != IrType::F32 && argument->Type() != IrType::U1) return {};
                const auto input = std::find(graph.inputs.begin(), graph.inputs.end(), argument);
                append(graph.key, 2u);
                append(graph.key, std::distance(graph.inputs.begin(), input));
                if (input == graph.inputs.end()) graph.inputs.push_back(argument);
            }
        }
    }
    if (graph.inputs.size() > 8u) return {};
    return graph;
}

}

SpirvArithmeticPlan PlanArithmeticOutlining(const IrProgram& program, std::uint32_t laneCount,
                                          const std::unordered_set<const IrValue*>& sharedValues) {
    std::vector<SpirvArithmeticGraph> graphs;
    std::unordered_set<const IrValue*> assigned;
    struct Cost {
        std::size_t calls = 0;
        std::size_t inlineWords = 0;
    };
    std::unordered_map<std::string, Cost> costs;
    for (const auto* block : program.BlockOrder()) {
        std::unordered_map<const IrValue*, std::size_t> positions;
        const auto& instructions = block->Instructions();
        std::size_t position = 0;
        for (const auto* instruction : instructions) positions.emplace(instruction, position++);
        for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
            if (assigned.contains(*it) || !arithmetic(**it)) continue;
            auto graph = graphFor(*it, assigned, positions);
            if (graph.instructions.empty()) continue;
            for (const auto* value : graph.instructions) assigned.insert(value);
            auto& cost = costs[graph.key];
            cost.calls += sharedValues.contains(graph.instructions.back()) ? 1u : laneCount;
            for (const auto* value : graph.instructions) cost.inlineWords += 4u * (sharedValues.contains(value) ? 1u : laneCount);
            graphs.push_back(std::move(graph));
        }
    }
    SpirvArithmeticPlan plan;
    for (auto& graph : graphs) {
        const auto cost = costs.at(graph.key);
        const auto outlinedWords = graph.instructions.size() * 4u + 32u + graph.inputs.size() * 4u + cost.calls * (5u + graph.inputs.size());
        if (cost.calls < 2u || cost.inlineWords <= outlinedWords) continue;
        const auto* root = graph.instructions.back();
        for (const auto* value : graph.instructions) {
            if (value != root) plan.interiors.insert(value);
        }
        plan.roots.emplace(root, std::move(graph));
    }
    return plan;
}

}
