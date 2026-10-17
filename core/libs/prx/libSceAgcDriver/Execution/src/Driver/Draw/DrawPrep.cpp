#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPrep.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Queues/WorkerAffinity.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace AgcDriver::DriverDetail {

namespace {

using Range = std::pair<std::uint64_t, std::uint64_t>;

// More preparations than this in flight make the worker wait: each holds a copy of the queue state.
constexpr std::uint64_t MaxOutstanding = 64;

struct Ticket {
    bool declared = false;
    bool writesMemory = false;
    std::vector<Range> ranges;
};

struct Pool {
    std::mutex mutex;
    std::condition_variable work;
    std::condition_variable changed;
    std::deque<std::pair<std::uint64_t, std::function<void()>>> jobs;
    // tickets[i] is ticket finished + i; preparations finish in ticket order (ServeDrawPrepTurn).
    std::deque<Ticket> tickets;
    // Written by the queue worker only, under the mutex.
    std::uint64_t issued = 0;
    std::atomic<std::uint64_t> finished{0};
    std::exception_ptr failure;
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    // APS5_PROFILE_DRAW report.
    std::uint64_t submitted = 0, outstandingSum = 0, fullWaits = 0, drains = 0, idleDrains = 0, hazardWaits = 0, turnWaits = 0, writers = 0;
    std::array<std::atomic<std::uint64_t>, 3> kept{};
    std::uint64_t prepNs = 0, fullWaitNs = 0, drainWaitNs = 0, hazardWaitNs = 0, turnWaitNs = 0;
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
};

Pool& pool() {
    static auto* instance = new Pool();
    return *instance;
}

bool profile() {
    static const bool enabled = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return enabled;
}

std::uint64_t elapsedNs(std::chrono::steady_clock::time_point since) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - since).count());
}

thread_local std::uint64_t currentTicket = 0;
thread_local bool ticketActive = false;
thread_local bool ticketDeclared = false;
thread_local bool ticketServed = false;

void declareLocked(Pool& state, bool writesMemory, std::vector<Range> ranges) {
    auto& ticket = state.tickets.at(currentTicket - state.finished.load(std::memory_order_relaxed));
    ticket.declared = true;
    ticket.writesMemory = writesMemory;
    ticket.ranges = std::move(ranges);
    ticketDeclared = true;
    if (writesMemory) ++state.writers;
}

void reportLocked(Pool& state) {
    const auto now = std::chrono::steady_clock::now();
    if (now - state.lastReport < std::chrono::seconds(10)) return;
    state.lastReport = now;
    const auto ms = [](std::uint64_t ns) { return static_cast<double>(ns) / 1e6; };
    ProfilePrint_nid_no_patch("[draw-prep] 10 s, %zu threads: %llu draws prepared at %.1f us each (turn waits included), %.1f in flight at submit, %llu declared memory writes; worker waited %.1f ms for room (%llu), %.1f ms in %llu drains (%llu idle); preparations waited %.1f ms for earlier writers (%llu), %.1f ms for their turn (%llu); draws kept on the worker: %llu predicated, %llu indirect, %llu behind deferred labels\n", state.threads.size(), static_cast<unsigned long long>(state.submitted), state.submitted == 0 ? 0.0 : static_cast<double>(state.prepNs) / 1e3 / static_cast<double>(state.submitted), state.submitted == 0 ? 0.0 : static_cast<double>(state.outstandingSum) / static_cast<double>(state.submitted), static_cast<unsigned long long>(state.writers), ms(state.fullWaitNs), static_cast<unsigned long long>(state.fullWaits), ms(state.drainWaitNs), static_cast<unsigned long long>(state.drains), static_cast<unsigned long long>(state.idleDrains), ms(state.hazardWaitNs), static_cast<unsigned long long>(state.hazardWaits), ms(state.turnWaitNs), static_cast<unsigned long long>(state.turnWaits), static_cast<unsigned long long>(state.kept[0].exchange(0)), static_cast<unsigned long long>(state.kept[1].exchange(0)), static_cast<unsigned long long>(state.kept[2].exchange(0)));
    state.submitted = state.outstandingSum = state.fullWaits = state.drains = state.idleDrains = state.hazardWaits = state.turnWaits = state.writers = 0;
    state.prepNs = state.fullWaitNs = state.drainWaitNs = state.hazardWaitNs = state.turnWaitNs = 0;
}

void run() {
    PinWorkerThread("draw prep");
    DrawPipeline::Active() = true;
    auto& state = pool();
    std::unique_lock lock(state.mutex);
    for (;;) {
        state.work.wait(lock, [&] { return !state.jobs.empty(); });
        // Jobs leave the queue in ticket order, so the oldest unfinished preparation always runs.
        auto [ticket, job] = std::move(state.jobs.front());
        state.jobs.pop_front();
        currentTicket = ticket;
        const bool skip = state.failure != nullptr;
        lock.unlock();
        ticketActive = true;
        ticketDeclared = false;
        ticketServed = false;
        std::exception_ptr error;
        const auto started = std::chrono::steady_clock::now();
        if (!skip) {
            try {
                job();
            } catch (...) {
                error = std::current_exception();
            }
        }
        job = nullptr;
        if (!ticketDeclared) {
            std::lock_guard declareLock(state.mutex);
            declareLocked(state, false, {});
            state.changed.notify_all();
        }
        ServeDrawPrepTurn();
        ticketActive = false;
        const auto spent = elapsedNs(started);
        lock.lock();
        state.prepNs += spent;
        if (error != nullptr && state.failure == nullptr) {
            state.failure = error;
            state.failed.store(true, std::memory_order_release);
        }
        state.tickets.pop_front();
        state.finished.fetch_add(1, std::memory_order_release);
        state.changed.notify_all();
    }
}

}

std::size_t DrawPrepThreads() {
    static const std::size_t threads = [] {
        const char* text = std::getenv("APS5_PARALLEL_PREP");
        if (text == nullptr || DrawPipeline::Depth() == 0) return std::size_t{0};
        return static_cast<std::size_t>(std::min<unsigned long long>(std::strtoull(text, nullptr, 10), 16));
    }();
    return threads;
}

bool DrawPrepThread() {
    return ticketActive;
}

bool DrawPrepBusy() {
    auto& state = pool();
    std::lock_guard lock(state.mutex);
    return state.issued != state.finished.load(std::memory_order_relaxed);
}

void SubmitDrawPrep(std::function<void()> job) {
    auto& state = pool();
    std::unique_lock lock(state.mutex);
    if (state.threads.empty()) {
        for (std::size_t index = 0; index < DrawPrepThreads(); ++index) state.threads.emplace_back(run);
        std::fprintf(stderr, "[draw-prep] APS5_PARALLEL_PREP: queue 0's direct draws prepared on %zu threads\n", state.threads.size());
    }
    if (state.issued - state.finished.load(std::memory_order_relaxed) >= MaxOutstanding) {
        const auto start = std::chrono::steady_clock::now();
        state.changed.wait(lock, [&] { return state.issued - state.finished.load(std::memory_order_relaxed) < MaxOutstanding; });
        ++state.fullWaits;
        state.fullWaitNs += elapsedNs(start);
    }
    state.outstandingSum += state.issued - state.finished.load(std::memory_order_relaxed);
    ++state.submitted;
    state.tickets.emplace_back();
    state.jobs.emplace_back(state.issued++, std::move(job));
    state.work.notify_one();
    if (profile()) reportLocked(state);
}

void NoteDrawPrepKept(std::size_t reason) {
    pool().kept[reason].fetch_add(1, std::memory_order_relaxed);
}

void DeclareDrawPrep(bool writesMemory, std::vector<Range> ranges) {
    if (!ticketActive || ticketDeclared) return;
    auto& state = pool();
    std::lock_guard lock(state.mutex);
    declareLocked(state, writesMemory, std::move(ranges));
    state.changed.notify_all();
}

void AwaitDrawPrepHazard(std::uint64_t address, std::size_t bytes) {
    if (!ticketActive || ticketServed) return;
    auto& state = pool();
    if (state.finished.load(std::memory_order_acquire) == currentTicket) return;
    const auto end = address + bytes;
    std::unique_lock lock(state.mutex);
    const auto clear = [&] {
        const auto finished = state.finished.load(std::memory_order_relaxed);
        for (std::uint64_t index = 0; finished + index < currentTicket; ++index) {
            const auto& ticket = state.tickets[index];
            if (!ticket.declared || ticket.writesMemory) return false;
            if (std::any_of(ticket.ranges.begin(), ticket.ranges.end(), [&](const Range& range) { return address < range.second && range.first < end; })) return false;
        }
        return true;
    };
    if (clear()) return;
    const auto start = std::chrono::steady_clock::now();
    state.changed.wait(lock, clear);
    ++state.hazardWaits;
    state.hazardWaitNs += elapsedNs(start);
}

void ServeDrawPrepTurn() {
    if (!ticketActive || ticketServed) return;
    auto& state = pool();
    ticketServed = true;
    if (state.finished.load(std::memory_order_acquire) == currentTicket) return;
    std::unique_lock lock(state.mutex);
    const auto start = std::chrono::steady_clock::now();
    state.changed.wait(lock, [&] { return state.finished.load(std::memory_order_relaxed) == currentTicket; });
    ++state.turnWaits;
    state.turnWaitNs += elapsedNs(start);
}

void DrainDrawPrep() {
    if (DrawPrepThreads() == 0) return;
    auto& state = pool();
    // Only the queue worker submits and drains, so it reads its own issued count without the mutex.
    if (state.issued == state.finished.load(std::memory_order_acquire) && !state.failed.load(std::memory_order_acquire)) return;
    std::unique_lock lock(state.mutex);
    ++state.drains;
    if (state.issued == state.finished.load(std::memory_order_relaxed)) {
        ++state.idleDrains;
    } else {
        const auto start = std::chrono::steady_clock::now();
        state.changed.wait(lock, [&] { return state.issued == state.finished.load(std::memory_order_relaxed); });
        state.drainWaitNs += elapsedNs(start);
    }
    if (state.failure == nullptr) return;
    auto error = std::exchange(state.failure, nullptr);
    state.failed.store(false, std::memory_order_release);
    lock.unlock();
    std::rethrow_exception(error);
}

}
