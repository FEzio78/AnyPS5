#pragma once

#include "IntermediateRepresentation/IrProgram.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ShaderRecompiler {

struct SpirvArithmeticGraph {
    std::vector<const IrValue*> instructions;
    std::vector<const IrValue*> inputs;
    std::string key;
};

struct SpirvArithmeticPlan {
    std::unordered_map<const IrValue*, SpirvArithmeticGraph> roots;
    std::unordered_set<const IrValue*> interiors;
};

SpirvArithmeticPlan PlanArithmeticOutlining(const IrProgram& program, std::uint32_t laneCount,
                                          const std::unordered_set<const IrValue*>& sharedValues = {});

}
