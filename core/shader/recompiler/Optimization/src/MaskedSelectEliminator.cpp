#include "Optimization/MaskedSelectEliminator.hpp"
#include "Optimization/DeadCodeEliminator.hpp"
#include "RdnaDecoder/RdnaInstruction.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ShaderRecompiler {
namespace {

constexpr std::size_t VisitLimit = 4096;
constexpr std::uint32_t DepthLimit = 8;

class VisitedValues {
public:
    void Reset() {
        if (slots.empty()) {
            slots.assign(Capacity, nullptr);
            stamps.assign(Capacity, 0u);
        }
        if (++epoch == 0u) {
            std::fill(stamps.begin(), stamps.end(), 0u);
            epoch = 1u;
        }
        count = 0;
    }
    bool Insert(const IrValue* value) {
        auto index = (reinterpret_cast<std::uintptr_t>(value) >> 4u) * 0x9e3779b97f4a7c15ull >> (64u - CapacityBits);
        while (stamps[index] == epoch) {
            if (slots[index] == value) return false;
            index = (index + 1u) & (Capacity - 1u);
        }
        slots[index] = value;
        stamps[index] = epoch;
        ++count;
        return true;
    }
    bool Contains(const IrValue* value) const {
        auto index = (reinterpret_cast<std::uintptr_t>(value) >> 4u) * 0x9e3779b97f4a7c15ull >> (64u - CapacityBits);
        while (stamps[index] == epoch) {
            if (slots[index] == value) return true;
            index = (index + 1u) & (Capacity - 1u);
        }
        return false;
    }
    std::size_t Size() const { return count; }

private:
    static constexpr std::size_t CapacityBits = 14;
    static constexpr std::size_t Capacity = std::size_t{1} << CapacityBits;
    static_assert(Capacity >= 2u * (VisitLimit + 2u));
    std::vector<const IrValue*> slots;
    std::vector<std::uint32_t> stamps;
    std::uint32_t epoch = 0;
    std::size_t count = 0;
};

bool isSelect(IrOpcode opcode) {
    return opcode == IrOpcode::SelectU1 || opcode == IrOpcode::SelectU32 || opcode == IrOpcode::SelectF32;
}

bool computeLaneLocal(IrOpcode opcode) {
    if (IrOpcodeHasSideEffects(opcode)) return false;
    switch (opcode) {
        case IrOpcode::Identity:
        case IrOpcode::IAdd32:
        case IrOpcode::ISub32:
        case IrOpcode::IMul32:
        case IrOpcode::SelectU1:
        case IrOpcode::SelectU32:
        case IrOpcode::SelectF32:
        case IrOpcode::BitCastU16F16:
        case IrOpcode::BitCastF16U16:
        case IrOpcode::BitCastU32F32:
        case IrOpcode::BitCastF32U32:
        case IrOpcode::ConvertU16U32:
        case IrOpcode::ConvertU32U16:
        case IrOpcode::ConvertU8U32:
        case IrOpcode::ConvertU32U8:
        case IrOpcode::ConvertF32F16:
        case IrOpcode::ConvertF16F32:
        case IrOpcode::ConvertS32F32:
        case IrOpcode::ConvertU32F32:
        case IrOpcode::ConvertF32S32:
        case IrOpcode::ConvertF32U32:
        case IrOpcode::ConvertF32F64:
        case IrOpcode::ConvertF64F32:
        case IrOpcode::ConvertF64S32:
        case IrOpcode::ConvertF64U32:
        case IrOpcode::ConvertS32F64:
        case IrOpcode::ConvertU32F64:
        case IrOpcode::CompositeConstructU64:
        case IrOpcode::CompositeConstructU32x2:
        case IrOpcode::CompositeConstructU32x3:
        case IrOpcode::CompositeConstructF32x2:
        case IrOpcode::CompositeConstructU32x4:
        case IrOpcode::CompositeExtractU64:
        case IrOpcode::CompositeExtractU32x2:
        case IrOpcode::CompositeExtractU32x3:
        case IrOpcode::CompositeExtractU32x4:
        case IrOpcode::PackHalf2x16:
        case IrOpcode::PackFloat2x16Rtz:
        case IrOpcode::BitFieldInsert:
        case IrOpcode::BitFieldUExtract:
        case IrOpcode::BitFieldSExtract:
        case IrOpcode::IAdd64:
        case IrOpcode::IAddCarry32:
        case IrOpcode::ISub64:
        case IrOpcode::IMul64:
        case IrOpcode::SMulHi:
        case IrOpcode::UMulHi:
        case IrOpcode::IAbs32:
        case IrOpcode::ShiftLeftLogical32:
        case IrOpcode::ShiftLeftLogical64:
        case IrOpcode::ShiftRightLogical32:
        case IrOpcode::ShiftRightLogical64:
        case IrOpcode::ShiftRightArithmetic32:
        case IrOpcode::ShiftRightArithmetic64:
        case IrOpcode::BitwiseAnd32:
        case IrOpcode::BitwiseAnd64:
        case IrOpcode::BitwiseOr32:
        case IrOpcode::BitwiseXor32:
        case IrOpcode::BitwiseNot32:
        case IrOpcode::BitReverse32:
        case IrOpcode::BitCount32:
        case IrOpcode::BitCount64:
        case IrOpcode::FindUMsb32:
        case IrOpcode::FindUMsb64:
        case IrOpcode::FindILsb32:
        case IrOpcode::SMin32:
        case IrOpcode::UMin32:
        case IrOpcode::SMax32:
        case IrOpcode::UMax32:
        case IrOpcode::SMinTri32:
        case IrOpcode::UMinTri32:
        case IrOpcode::SMaxTri32:
        case IrOpcode::UMaxTri32:
        case IrOpcode::SMedTri32:
        case IrOpcode::UMedTri32:
        case IrOpcode::SLessThan32:
        case IrOpcode::SLessThan64:
        case IrOpcode::ULessThan32:
        case IrOpcode::ULessThan64:
        case IrOpcode::IEqual32:
        case IrOpcode::IEqual64:
        case IrOpcode::SLessThanEqual32:
        case IrOpcode::ULessThanEqual32:
        case IrOpcode::SGreaterThan32:
        case IrOpcode::UGreaterThan32:
        case IrOpcode::UGreaterThan64:
        case IrOpcode::INotEqual32:
        case IrOpcode::INotEqual64:
        case IrOpcode::SGreaterThanEqual32:
        case IrOpcode::UGreaterThanEqual32:
        case IrOpcode::LogicalOr:
        case IrOpcode::LogicalAnd:
        case IrOpcode::LogicalXor:
        case IrOpcode::LogicalNot:
        case IrOpcode::FPOrdEqual32:
        case IrOpcode::FPUnordEqual32:
        case IrOpcode::FPOrdNotEqual32:
        case IrOpcode::FPUnordNotEqual32:
        case IrOpcode::FPOrdLessThan32:
        case IrOpcode::FPUnordLessThan32:
        case IrOpcode::FPOrdGreaterThan32:
        case IrOpcode::FPUnordGreaterThan32:
        case IrOpcode::FPOrdLessThanEqual32:
        case IrOpcode::FPUnordLessThanEqual32:
        case IrOpcode::FPOrdGreaterThanEqual32:
        case IrOpcode::FPUnordGreaterThanEqual32:
        case IrOpcode::FPIsNan32:
        case IrOpcode::FPCmpClass32:
        case IrOpcode::FPAbs32:
        case IrOpcode::FPNeg32:
        case IrOpcode::FPSaturate32:
        case IrOpcode::FPAdd32:
        case IrOpcode::FPSub32:
        case IrOpcode::FPFma32:
        case IrOpcode::FPMad32:
        case IrOpcode::FPMul32:
        case IrOpcode::FPMin32:
        case IrOpcode::FPMax32:
        case IrOpcode::FPMinTri32:
        case IrOpcode::FPMaxTri32:
        case IrOpcode::FPMedTri32:
        case IrOpcode::FPRecip32:
        case IrOpcode::FPRecipIFlag32:
        case IrOpcode::FPRecipSqrt32:
        case IrOpcode::FPSqrt:
        case IrOpcode::FPSin:
        case IrOpcode::FPCos:
        case IrOpcode::FPExp2:
        case IrOpcode::FPLog2:
        case IrOpcode::FPRoundEven32:
        case IrOpcode::FPFloor32:
        case IrOpcode::FPCeil32:
        case IrOpcode::FPTrunc32:
        case IrOpcode::FPFract32:
        case IrOpcode::FPAdd64:
        case IrOpcode::FPMul64:
        case IrOpcode::FPFma64:
        case IrOpcode::FPMin64:
        case IrOpcode::FPMax64:
        case IrOpcode::FPSaturate64:
        case IrOpcode::FPLdexp64:
        case IrOpcode::FPRoundEven64:
        case IrOpcode::FPFloor64:
        case IrOpcode::FPCeil64:
        case IrOpcode::FPTrunc64:
        case IrOpcode::FPFract64:
        case IrOpcode::FPFrexpMant64:
        case IrOpcode::FPFrexpExp64:
        case IrOpcode::MakeImageAddress:
            return true;
        default:
            return false;
    }
}

bool computeReadsOnlyWhereActive(IrOpcode opcode) {
    switch (opcode) {
        case IrOpcode::LoadBufferU8:
        case IrOpcode::LoadBufferU16:
        case IrOpcode::LoadBufferU32:
        case IrOpcode::LoadBufferU32x2:
        case IrOpcode::LoadBufferU32x3:
        case IrOpcode::LoadBufferU32x4:
        case IrOpcode::StoreBufferU8:
        case IrOpcode::StoreBufferU16:
        case IrOpcode::StoreBufferU32:
        case IrOpcode::StoreBufferU32x2:
        case IrOpcode::StoreBufferU32x3:
        case IrOpcode::StoreBufferU32x4:
        case IrOpcode::LoadSharedU8:
        case IrOpcode::LoadSharedU16:
        case IrOpcode::LoadSharedU32:
        case IrOpcode::LoadSharedU32x2:
        case IrOpcode::LoadSharedU32x3:
        case IrOpcode::LoadSharedU32x4:
        case IrOpcode::WriteSharedU8:
        case IrOpcode::WriteSharedU16:
        case IrOpcode::WriteSharedU32:
        case IrOpcode::WriteSharedU32x2:
        case IrOpcode::WriteSharedU32x3:
        case IrOpcode::WriteSharedU32x4:
        case IrOpcode::SetAttribute:
            return true;
        default:
            return false;
    }
}

struct OpcodeTables {
    std::array<std::uint8_t, static_cast<std::size_t>(IrOpcode::Count)> laneLocal{};
    std::array<std::uint8_t, static_cast<std::size_t>(IrOpcode::Count)> readsOnlyWhereActive{};
    OpcodeTables() {
        for (std::size_t index = 0; index < laneLocal.size(); ++index) {
            laneLocal[index] = computeLaneLocal(static_cast<IrOpcode>(index)) ? 1u : 0u;
            readsOnlyWhereActive[index] = computeReadsOnlyWhereActive(static_cast<IrOpcode>(index)) ? 1u : 0u;
        }
    }
};

const OpcodeTables& opcodeTables() {
    static const OpcodeTables tables;
    return tables;
}

bool isLaneLocal(IrOpcode opcode) {
    return opcodeTables().laneLocal[static_cast<std::size_t>(opcode)] != 0u;
}

bool readsOnlyWhereActive(IrOpcode opcode) {
    return opcodeTables().readsOnlyWhereActive[static_cast<std::size_t>(opcode)] != 0u;
}

bool isExplicitLodSample(const IrProgram& program, const IrValue& value) {
    if (value.Opcode() != IrOpcode::ImageSampleRaw) return false;
    if (program.Resources().stage != IrShaderStage::Pixel) return true;
    const auto index = value.Flags<MemoryFlags>().index;
    if (index >= program.Resources().memoryInfo.size()) return false;
    return (program.Resources().memoryInfo[index].imageSampleFlags & (RdnaImageSampleFlagDerivative | RdnaImageSampleFlagLod | RdnaImageSampleFlagLevelZero)) != 0u;
}

bool isImmediate(const IrValue* value, std::uint32_t immediate) {
    return value->HasImmediate() && value->Type() == IrType::U32 && value->ImmediateU32() == immediate;
}

const IrValue* otherArgument(const IrValue* value, std::uint32_t immediate) {
    if (value->ArgumentCount() != 2u) return nullptr;
    const IrValue* left = value->Argument(0)->Resolve();
    const IrValue* right = value->Argument(1)->Resolve();
    if (isImmediate(right, immediate)) return left;
    if (isImmediate(left, immediate)) return right;
    return nullptr;
}

struct ThreadBit {
    const IrValue* low = nullptr;
    const IrValue* high = nullptr;
};

bool isLane(const IrValue* value) {
    return value->Resolve()->Opcode() == IrOpcode::LaneId;
}

std::optional<ThreadBit> threadBit(const IrValue* value, std::uint32_t waveSize) {
    value = value->Resolve();
    if (value->Opcode() != IrOpcode::INotEqual32) return std::nullopt;
    const IrValue* bit = otherArgument(value, 0u);
    if (bit == nullptr || bit->Opcode() != IrOpcode::BitwiseAnd32) return std::nullopt;
    const IrValue* shifted = otherArgument(bit, 1u);
    if (shifted == nullptr || shifted->Opcode() != IrOpcode::ShiftRightLogical32) return std::nullopt;
    const IrValue* amount = shifted->Argument(1)->Resolve();
    if (amount->Opcode() != IrOpcode::BitwiseAnd32) return std::nullopt;
    const IrValue* lane = otherArgument(amount, 31u);
    if (lane == nullptr || !isLane(lane)) return std::nullopt;
    const IrValue* word = shifted->Argument(0)->Resolve();
    if (waveSize == 32u || word->HasImmediate()) return ThreadBit{word, word};
    if (word->Opcode() != IrOpcode::SelectU32) return std::nullopt;
    const IrValue* half = word->Argument(0)->Resolve();
    if (half->Opcode() != IrOpcode::ULessThan32 || !isLane(half->Argument(0)) || !isImmediate(half->Argument(1)->Resolve(), 32u)) return std::nullopt;
    return ThreadBit{word->Argument(1)->Resolve(), word->Argument(2)->Resolve()};
}

bool alwaysTrue(const IrValue* value, std::uint32_t waveSize, std::uint32_t depth = 0) {
    value = value->Resolve();
    if (value->HasImmediate()) return value->Type() == IrType::Bool && value->ImmediateBool();
    if (depth > DepthLimit) return false;
    if (value->Opcode() == IrOpcode::LogicalAnd) return alwaysTrue(value->Argument(0), waveSize, depth + 1u) && alwaysTrue(value->Argument(1), waveSize, depth + 1u);
    const auto bit = threadBit(value, waveSize);
    return bit && isImmediate(bit->low, 0xffffffffu) && isImmediate(bit->high, 0xffffffffu);
}

bool implies(const IrValue* condition, const IrValue* mask, std::uint32_t waveSize, std::uint32_t depth = 0);

bool wordsImply(const IrValue* low, const IrValue* high, const IrValue* mask, std::uint32_t waveSize, std::uint32_t depth) {
    if (depth > DepthLimit) return false;
    if (isImmediate(low, 0u) && (waveSize == 32u || isImmediate(high, 0u))) return true;
    if (low->Opcode() == IrOpcode::CompositeExtractU32x4 && isImmediate(low->Argument(1)->Resolve(), 0u)) {
        const IrValue* ballot = low->Argument(0)->Resolve();
        const bool sameBallot = waveSize == 32u || (high->Opcode() == IrOpcode::CompositeExtractU32x4 && high->Argument(0)->Resolve() == ballot && isImmediate(high->Argument(1)->Resolve(), 1u));
        if (ballot->Opcode() == IrOpcode::Ballot && sameBallot && implies(ballot->Argument(0), mask, waveSize, depth + 1u)) return true;
    }
    if (low->Opcode() != IrOpcode::BitwiseAnd32) return false;
    if (waveSize == 32u) return wordsImply(low->Argument(0)->Resolve(), high, mask, waveSize, depth + 1u) || wordsImply(low->Argument(1)->Resolve(), high, mask, waveSize, depth + 1u);
    if (high->Opcode() != IrOpcode::BitwiseAnd32) return false;
    for (std::size_t lowIndex = 0; lowIndex < 2u; ++lowIndex) {
        for (std::size_t highIndex = 0; highIndex < 2u; ++highIndex) {
            if (wordsImply(low->Argument(lowIndex)->Resolve(), high->Argument(highIndex)->Resolve(), mask, waveSize, depth + 1u)) return true;
        }
    }
    return false;
}

bool implies(const IrValue* condition, const IrValue* mask, std::uint32_t waveSize, std::uint32_t depth) {
    condition = condition->Resolve();
    if (condition == mask || alwaysTrue(mask, waveSize)) return true;
    if (condition->HasImmediate()) return condition->Type() == IrType::Bool && !condition->ImmediateBool();
    if (depth > DepthLimit) return false;
    if (condition->Opcode() == IrOpcode::LogicalAnd) return implies(condition->Argument(0), mask, waveSize, depth + 1u) || implies(condition->Argument(1), mask, waveSize, depth + 1u);
    const auto bit = threadBit(condition, waveSize);
    return bit && wordsImply(bit->low, bit->high, mask, waveSize, depth + 1u);
}

std::unordered_set<const IrBlock*> blocksOutsideLoops(const IrProgram& program) {
    const auto& order = program.BlockOrder();
    if (order.empty()) return {};
    std::unordered_map<const IrBlock*, int> state;
    std::unordered_set<const IrBlock*> looping;
    std::vector<std::pair<const IrBlock*, std::size_t>> stack;
    state[order.front()] = 1;
    stack.emplace_back(order.front(), 0u);
    while (!stack.empty()) {
        auto& [block, next] = stack.back();
        if (next == block->Successors().size()) {
            state[block] = 2;
            stack.pop_back();
            continue;
        }
        const IrBlock* source = block;
        const IrBlock* successor = block->Successors()[next++];
        int& successorState = state[successor];
        if (successorState == 0) {
            successorState = 1;
            stack.emplace_back(successor, 0u);
        } else if (successorState == 1) {
            std::unordered_set<const IrBlock*> body{successor};
            std::vector<const IrBlock*> pending{source};
            while (!pending.empty()) {
                const IrBlock* member = pending.back();
                pending.pop_back();
                if (!body.insert(member).second) continue;
                for (const IrBlock* predecessor : member->Predecessors()) pending.push_back(predecessor);
            }
            looping.insert(body.begin(), body.end());
        }
    }
    std::unordered_set<const IrBlock*> result;
    for (const auto& [block, visited] : state) {
        if (visited == 2 && !looping.contains(block)) result.insert(block);
    }
    return result;
}

struct Witness {
    const IrValue* user = nullptr;
    std::uint32_t operand = 0;
};

constexpr std::size_t WitnessHopLimit = 1u << 20;

class WalkState {
public:
    void Reset(const IrProgram& program) {
        std::uint32_t maxValueId = 0;
        std::uint32_t maxBlockId = 0;
        for (const IrBlock* block : program.BlockOrder()) {
            maxBlockId = std::max(maxBlockId, block->Id());
            for (const IrValue* inst : block->Instructions()) maxValueId = std::max(maxValueId, inst->Id());
        }
        witnesses.assign(static_cast<std::size_t>(maxValueId) + 1u, Witness{});
        rejectedStamp.assign(static_cast<std::size_t>(maxValueId) + 1u, 0u);
        queuedStamp.assign(static_cast<std::size_t>(maxValueId) + 1u, 0u);
        walk = 0;
        once.assign(static_cast<std::size_t>(maxBlockId) + 1u, 0u);
        for (const IrBlock* block : blocksOutsideLoops(program)) once[block->Id()] = 1u;
    }
    bool Once(const IrBlock* block) const { return block != nullptr && block->Id() < once.size() && once[block->Id()] != 0u; }
    Witness* WitnessOf(const IrValue* value) { return value->Id() < witnesses.size() ? &witnesses[value->Id()] : nullptr; }
    void BeginWalk() { ++walk; }
    bool Rejected(const IrValue* value) const { return value->Id() < rejectedStamp.size() && rejectedStamp[value->Id()] == walk; }
    void Reject(const IrValue* value) { if (value->Id() < rejectedStamp.size()) rejectedStamp[value->Id()] = walk; }
    bool Queued(const IrValue* value) const { return value->Id() < queuedStamp.size() && queuedStamp[value->Id()] == walk; }
    void MarkQueued(const IrValue* value) { if (value->Id() < queuedStamp.size()) queuedStamp[value->Id()] = walk; }

    VisitedValues visited;
    std::vector<const IrValue*> pending;
    std::vector<const IrValue*> reverseQueue;
    std::vector<const IrValue*> chain;

private:
    std::vector<Witness> witnesses;
    std::vector<std::uint32_t> rejectedStamp;
    std::vector<std::uint32_t> queuedStamp;
    std::vector<std::uint8_t> once;
    std::uint32_t walk = 0;
};

const IrValue* edgeControl(const IrValue* user, std::size_t operand) {
    if (isSelect(user->Opcode()) && operand == 1u) return user->Argument(0);
    if (user->Opcode() == IrOpcode::LogicalAnd && operand < 2u) return user->Argument(1u - operand);
    if (readsOnlyWhereActive(user->Opcode()) && operand + 1u < user->ArgumentCount()) return user->Argument(user->ArgumentCount() - 1u);
    return nullptr;
}

bool isBadUser(const IrProgram& program, const IrValue& user) {
    return !user.IsPhi() && !isLaneLocal(user.Opcode()) && !isExplicitLodSample(program, user);
}

bool witnessProvesFailure(const IrProgram& program, WalkState& state, const IrValue* start, const IrValue* mask) {
    const auto waveSize = program.WaveSize();
    auto& chain = state.chain;
    chain.clear();
    const IrValue* value = start;
    bool proven = false;
    for (std::size_t hops = 0; hops < WitnessHopLimit; ++hops) {
        if (state.Rejected(value)) break;
        const Witness* witness = state.WitnessOf(value);
        if (witness == nullptr || witness->user == nullptr) break;
        chain.push_back(value);
        const IrValue* user = witness->user;
        if (witness->operand >= user->ArgumentCount() || user->Argument(witness->operand) != value) break;
        if (!state.Once(user->Parent())) { proven = true; break; }
        const IrValue* control = edgeControl(user, witness->operand);
        if (control != nullptr && implies(control, mask, waveSize)) break;
        if (isBadUser(program, *user)) { proven = true; break; }
        value = user;
    }
    if (!proven) {
        for (const IrValue* rejected : chain) state.Reject(rejected);
    }
    return proven;
}

void recordWitnesses(WalkState& state, const IrValue* from, const IrValue* user, std::size_t operand, const IrValue* mask, std::uint32_t waveSize) {
    auto& queue = state.reverseQueue;
    queue.clear();
    if (Witness* witness = state.WitnessOf(from)) *witness = Witness{user, static_cast<std::uint32_t>(operand)};
    state.MarkQueued(from);
    queue.push_back(from);
    for (std::size_t next = 0; next < queue.size(); ++next) {
        const IrValue* target = queue[next];
        const auto count = target->ArgumentCount();
        for (std::size_t index = 0; index < count; ++index) {
            const IrValue* argument = target->Argument(index);
            if (argument == nullptr || !state.visited.Contains(argument)) continue;
            Witness* witness = state.WitnessOf(argument);
            if (witness == nullptr || state.Queued(argument)) continue;
            const IrValue* control = edgeControl(target, index);
            if (control != nullptr && implies(control, mask, waveSize)) continue;
            state.MarkQueued(argument);
            *witness = Witness{target, static_cast<std::uint32_t>(index)};
            queue.push_back(argument);
        }
    }
}

bool unobservedWhereMasked(const IrProgram& program, WalkState& state, const IrValue& select, const IrValue* mask) {
    const auto waveSize = program.WaveSize();
    state.BeginWalk();
    if (witnessProvesFailure(program, state, &select, mask)) return false;
    auto& pending = state.pending;
    auto& visited = state.visited;
    pending.clear();
    pending.push_back(&select);
    visited.Reset();
    visited.Insert(&select);
    while (!pending.empty()) {
        const IrValue* value = pending.back();
        pending.pop_back();
        for (const IrUse& use : value->OperandUses()) {
            const IrValue* user = use.user;
            if (!state.Once(user->Parent())) {
                recordWitnesses(state, value, user, use.operand, mask, waveSize);
                return false;
            }
            const IrValue* control = edgeControl(user, use.operand);
            if (control != nullptr && implies(control, mask, waveSize)) continue;
            if (isBadUser(program, *user)) {
                recordWitnesses(state, value, user, use.operand, mask, waveSize);
                return false;
            }
            if (!visited.Contains(user) && witnessProvesFailure(program, state, user, mask)) {
                recordWitnesses(state, value, user, use.operand, mask, waveSize);
                return false;
            }
            if (visited.Insert(user)) {
                if (visited.Size() > VisitLimit) return false;
                pending.push_back(user);
            }
        }
    }
    return true;
}

}

MaskedSelectEliminationStats MaskedSelectEliminator::Eliminate(IrProgram& program) const {
    MaskedSelectEliminationStats stats;
    static thread_local WalkState state;
    state.Reset(program);
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto blockIt = program.BlockOrder().rbegin(); blockIt != program.BlockOrder().rend(); ++blockIt) {
            const bool single = state.Once(*blockIt);
            auto& instructions = (*blockIt)->Instructions();
            auto it = instructions.end();
            while (it != instructions.begin()) {
                --it;
                IrValue* inst = *it;
                if (!isSelect(inst->Opcode()) || inst->ArgumentCount() != 3u || !inst->HasUses()) continue;
                const IrValue* mask = inst->Argument(0)->Resolve();
                if (!alwaysTrue(mask, program.WaveSize()) && (!single || !unobservedWhereMasked(program, state, *inst, mask))) continue;
                inst->ReplaceAllUsesWith(inst->Argument(1));
                inst->Invalidate();
                inst->SetParent(nullptr);
                it = instructions.erase(it);
                ++stats.removedSelects;
                changed = true;
            }
        }
        if (changed) DeadCodeEliminator{}.Eliminate(program);
    }
    return stats;
}

}
