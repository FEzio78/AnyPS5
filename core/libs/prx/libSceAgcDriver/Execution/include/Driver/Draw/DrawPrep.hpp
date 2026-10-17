#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_DRAW_DRAWPREP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_DRAW_DRAWPREP_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace AgcDriver::DriverDetail {

// Fork-only APS5_PARALLEL_PREP=<threads> (default 0, off): queue 0's direct draws are prepared
// (decode, lookup, capture, materialize: Driver::draw) on a pool of threads, each on a copy of the
// queue state, after the port of tibzejoker's parallel preparation (tz-poc34 b340b74c2). A ticket
// per draw keeps packet order where it matters:
// - a preparation declares, once its programs are known, whether they may write memory and the
//   ranges its targets write (DeclareDrawPrep);
// - a capture (queryPendingWrite) first waits until every earlier preparation has declared and
//   every earlier one that may write the captured range has finished (AwaitDrawPrepHazard);
// - a preparation hands its commit to the draw pipeline, or records synchronously, only when every
//   earlier preparation has finished (ServeDrawPrepTurn), so preparations finish in ticket order.
// The queue worker drains the pool (DrainDrawPrep) before every packet other than a direct draw or
// a register write, and before a submission ends.
std::size_t DrawPrepThreads();
bool DrawPrepThread();
bool DrawPrepBusy();
void SubmitDrawPrep(std::function<void()> job);
void DeclareDrawPrep(bool writesMemory, std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges);
void AwaitDrawPrepHazard(std::uint64_t address, std::size_t bytes);
void ServeDrawPrepTurn();
void DrainDrawPrep();
// A draw packet the worker kept: 0 predicated, 1 indirect, 2 deferred labels pending.
void NoteDrawPrepKept(std::size_t reason);

}

#endif
