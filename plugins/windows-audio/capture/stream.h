#pragma once
#include "targets.h"
#include "../adapter/capture_source.h"
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
namespace nodivu::capture {
static_assert(std::atomic<uint64_t>::is_always_lock_free, "RT requires lock-free counters");
// Single producer (capture owner), single consumer (CLAP audio). Main only
// changes the desired generation; neither main nor producer resets read_.
#pragma warning(push)
#pragma warning(disable:4324) // Intentional cache-line isolation of SPSC cursors.
class Stream {
public:
    Stream();
    ~Stream();
    bool configure(uint32_t mode, const nodivu_capture_target_t* target) noexcept;
    void clear() noexcept; // main; cancels activation and joins owner
    void reset() noexcept; // audio; consumer-side flush only
    void process(float* left, float* right, uint32_t frames) noexcept;
    nodivu_capture_state_t snapshot() const noexcept;
private:
    struct Frame { float left, right; uint64_t generation, qpc; };
    struct Request { uint32_t mode = 0; Target target{}; std::wstring executable; uint64_t generation = 0; };
    static constexpr uint32_t capacity = 4800, target_frames = 960;
    std::array<Frame, capacity> ring_{};
    alignas(64) std::atomic<uint64_t> read_{0};
    alignas(64) std::atomic<uint64_t> write_{0};
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> stopping_{false}, finished_{false};
    std::atomic<int32_t> status_{0}, error_{0};
    std::atomic<uint32_t> pid_{0};
    std::atomic<uint64_t> captured_{0}, dropped_{0}, underflow_{0}, discontinuities_{0}, age_{0}, max_age_{0};
    Handle wake_;
    std::mutex request_mutex_; // main/owner only, never process/reset
    Request request_;
    std::thread owner_;
    uint64_t producing_generation_ = 0; // producer only
    uint64_t consuming_generation_ = 0; // consumer only
    double fraction_ = 0, correction_ = 0;
    bool primed_ = false;
    LARGE_INTEGER qpc_frequency_{};
    void run() noexcept;
    static void receive(void* context, const Packet& packet) noexcept;
};
#pragma warning(pop)
}
