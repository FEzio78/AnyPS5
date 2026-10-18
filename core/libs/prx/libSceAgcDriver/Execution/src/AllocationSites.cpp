// Fork-only APS5_ALLOC_SITES=1: the driver library's operator new records the caller of every
// allocation of at least APS5_ALLOC_SITES bytes (default and 1: 256 KiB), and the [alloc-sites] report (every 5 minutes, from the first
// such allocation after that) lists the call sites holding the most live bytes, as module offsets
// (symbolize with nm on the unpatched library). Off, operator new and delete are malloc and free.
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::size_t trackedBytes() {
    static const std::size_t bytes = [] {
        const char* text = std::getenv("APS5_ALLOC_SITES");
        const auto value = text != nullptr ? std::strtoull(text, nullptr, 10) : 0ull;
        return static_cast<std::size_t>(value > 1 ? value : 256u * 1024u);
    }();
    return bytes;
}

bool tracking() {
    static const bool enabled = std::getenv("APS5_ALLOC_SITES") != nullptr;
    return enabled;
}

struct Site {
    std::size_t liveBytes = 0;
    std::size_t liveCount = 0;
    std::size_t totalCount = 0;
};

struct Tracker {
    std::mutex mutex;
    std::unordered_map<void*, std::pair<void*, std::size_t>> blocks;
    std::unordered_map<void*, Site> sites;
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
    bool reporting = false;
};

thread_local bool inside = false;

struct Inside {
    Inside() { inside = true; }
    ~Inside() { inside = false; }
};

Tracker& tracker() {
    static auto* instance = new Tracker();
    return *instance;
}

void report(Tracker& state) {
    std::vector<std::pair<void*, Site>> sorted(state.sites.begin(), state.sites.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.liveBytes > b.second.liveBytes; });
    std::size_t live = 0;
    for (const auto& [site, data] : sorted) live += data.liveBytes;
    std::fprintf(stderr, "[alloc-sites] %.0f MiB live in %zu blocks of at least %zu bytes, %zu call sites\n", static_cast<double>(live) / 1048576.0, state.blocks.size(), trackedBytes(), sorted.size());
    for (std::size_t i = 0; i < sorted.size() && i < 25; ++i) {
        const auto& [site, data] = sorted[i];
        if (data.liveBytes == 0) break;
#ifdef _WIN32
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(site), &module) && module != nullptr) GetModuleFileNameA(module, name, sizeof(name));
        const char* leaf = name;
        for (const char* at = name; *at != 0; ++at) if (*at == '\\' || *at == '/') leaf = at + 1;
        std::fprintf(stderr, "[alloc-sites]   %9.1f MiB in %6zu live (%8zu made) %s+0x%llx\n", static_cast<double>(data.liveBytes) / 1048576.0, data.liveCount, data.totalCount, leaf, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(site) - reinterpret_cast<std::uintptr_t>(module)));
#else
        std::fprintf(stderr, "[alloc-sites]   %9.1f MiB in %6zu live (%8zu made) %p\n", static_cast<double>(data.liveBytes) / 1048576.0, data.liveCount, data.totalCount, site);
#endif
    }
}

void* allocate(std::size_t bytes, void* caller) {
    void* block = std::malloc(bytes != 0 ? bytes : 1);
    if (block == nullptr) throw std::bad_alloc();
    if (!tracking() || bytes < trackedBytes() || inside) return block;
    const Inside guard;
    auto& state = tracker();
    std::lock_guard lock(state.mutex);
    if (state.reporting) return block;
    state.blocks[block] = {caller, bytes};
    auto& site = state.sites[caller];
    site.liveBytes += bytes;
    ++site.liveCount;
    ++site.totalCount;
    const auto now = std::chrono::steady_clock::now();
    if (now - state.lastReport >= std::chrono::minutes(5)) {
        state.lastReport = now;
        state.reporting = true;
        report(state);
        state.reporting = false;
    }
    return block;
}

void release(void* block) {
    if (block == nullptr) return;
    if (tracking() && !inside) {
        const Inside guard;
        auto& state = tracker();
        std::lock_guard lock(state.mutex);
        if (const auto found = state.blocks.find(block); found != state.blocks.end()) {
            auto& site = state.sites[found->second.first];
            site.liveBytes -= found->second.second;
            --site.liveCount;
            state.blocks.erase(found);
        }
    }
    std::free(block);
}

}

void* operator new(std::size_t bytes) { return allocate(bytes, __builtin_return_address(0)); }
void* operator new[](std::size_t bytes) { return allocate(bytes, __builtin_return_address(0)); }
void operator delete(void* block) noexcept { release(block); }
void operator delete[](void* block) noexcept { release(block); }
void operator delete(void* block, std::size_t) noexcept { release(block); }
void operator delete[](void* block, std::size_t) noexcept { release(block); }
