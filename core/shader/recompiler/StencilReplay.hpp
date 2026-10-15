#ifndef CORE_SHADER_RECOMPILER_STENCILREPLAY_HPP
#define CORE_SHADER_RECOMPILER_STENCILREPLAY_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include <vector>

namespace ShaderRecompiler {

[[nodiscard]] std::vector<std::uint32_t> StencilReferenceValues(const IrProgram& program);
[[nodiscard]] bool StencilReplaySafe(const IrProgram& program);
[[nodiscard]] bool StencilReferenceDefinedForKeptInvocations(const IrProgram& program);
// TODO(debt): fork-only fallback for devices without VK_EXT_shader_stencil_export. A stencil
// reference export whose replay cannot be proven is dropped and the draw keeps the fixed-function
// reference from DB_STENCILREFMASK, so draws that write a per-pixel reference store the wrong value.
// Opt-in with APS5_STENCIL_EXPORT_FALLBACK=1; without it the refusal stands.
[[nodiscard]] bool StencilReplayProvable(const IrProgram& program, bool earlyTests, bool orderedPixels);
[[nodiscard]] bool StencilExportFallbackEnabled();

}

#endif
