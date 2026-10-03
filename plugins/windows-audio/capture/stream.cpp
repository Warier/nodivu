#include "stream.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nodivu::capture {
Stream::Stream() : wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
    QueryPerformanceFrequency(&qpc_frequency_);
}
Stream::~Stream() { clear(); }
bool Stream::configure(uint32_t mode, const nodivu_capture_target_t* target) noexcept {
    const auto reject = [this](HRESULT error = E_INVALIDARG) { clear(); error_.store(error); status_.store(5); return false; };
    if (!mode) { clear(); return true; }
    if (mode > 2 || !target || !wake_.get() || qpc_frequency_.QuadPart <= 0 ||
        target->reserved || !std::memchr(target->executable, 0, sizeof(target->executable))) return reject();
    try {
        Request next; next.mode = mode;
        next.target = {target->process_id, target->creation_time};
        next.executable = wide(target->executable);
        if (mode == 1 && next.executable.empty()) return reject();
        if (next.target.process_id) {
            Target live{};
            if (FAILED(identify(next.target.process_id, live)) || (next.target.creation_time && live.creation_time != next.target.creation_time) ||
                (mode == 1 && !same_path(next.executable, executable_path(live.process_id)))) return reject();
            if (mode == 1 && !next.target.creation_time) return reject();
            next.target = live;
        } else if (mode == 2 || next.target.creation_time) return reject();
        if (finished_.load() && owner_.joinable()) owner_.join();
        if (!owner_.joinable()) {
            finished_.store(false);
            stopping_.store(false);
            owner_ = std::thread([this] { run(); });
        }
        {
            std::lock_guard<std::mutex> lock(request_mutex_);
            next.generation = generation_.load() + 1;
            request_ = std::move(next);
            generation_.store(request_.generation);
        }
        error_.store(0); status_.store(1); pid_.store(0);
        SetEvent(wake_.get());
        return true;
    } catch (...) { return reject(E_OUTOFMEMORY); }
}
void Stream::clear() noexcept {
    generation_.fetch_add(1); // immediately invalidates PCM, before any join
    stopping_.store(true);
    if (wake_.get()) SetEvent(wake_.get());
    if (owner_.joinable()) owner_.join();
    { std::lock_guard<std::mutex> lock(request_mutex_); request_ = {}; }
    status_.store(0); error_.store(0); pid_.store(0);
}
void Stream::reset() noexcept {
    read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
    fraction_ = 0; correction_ = 0; primed_ = false;
}
void Stream::receive(void* context, const Packet& packet) noexcept {
    auto& s = *static_cast<Stream*>(context);
    if (s.producing_generation_ != s.generation_.load()) return;
    const uint64_t write = s.write_.load(std::memory_order_relaxed);
    const uint64_t read = s.read_.load(std::memory_order_acquire);
    s.captured_.fetch_add(packet.frames, std::memory_order_relaxed);
    if (packet.flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) s.discontinuities_.fetch_add(1, std::memory_order_relaxed);
    if (packet.frames > capacity - (write - read)) {
        s.dropped_.fetch_add(packet.frames, std::memory_order_relaxed); return;
    }
    for (UINT32 i = 0; i < packet.frames; ++i) {
        float left = packet.interleaved ? packet.interleaved[i * 2] : 0;
        float right = packet.interleaved ? packet.interleaved[i * 2 + 1] : 0;
        if (!std::isfinite(left)) left = 0;
        if (!std::isfinite(right)) right = 0;
        s.ring_[(write + i) % capacity] = {left, right, s.producing_generation_,
            (packet.flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ? 0 : packet.qpc_100ns + uint64_t(i) * 10000000 / 48000};
    }
    s.write_.store(write + packet.frames, std::memory_order_release);
}
void Stream::process(float* left, float* right, uint32_t frames) noexcept {
    // Host guarantees stereo buffers <=480. No COM, lock, heap or owner join.
    std::fill_n(left, frames, 0.0f); std::fill_n(right, frames, 0.0f);
    const uint64_t generation = generation_.load();
    if (generation != consuming_generation_) { primed_ = false; fraction_ = 0; correction_ = 0; consuming_generation_ = generation; }
    uint64_t read = read_.load(std::memory_order_relaxed);
    const uint64_t write = write_.load(std::memory_order_acquire);
    LARGE_INTEGER counter{}; QueryPerformanceCounter(&counter);
    const uint64_t now = static_cast<uint64_t>(double(counter.QuadPart) * 10000000.0 / double(qpc_frequency_.QuadPart));
    // Consumer owns discards. Skips are bounded by fixed queue capacity; after
    // a disconnected graph, old buffered sound must not play on reconnect.
    while (read < write) {
        const auto& value = ring_[read % capacity];
        if (value.generation == generation && (!value.qpc || now <= value.qpc + 600000)) break;
        ++read; dropped_.fetch_add(1, std::memory_order_relaxed); primed_ = false;
    }
    if (write - read > 2400) { dropped_.fetch_add(write - read - target_frames); read = write - target_frames; primed_ = false; fraction_ = 0; }
    if (status_.load() != 2) { read_.store(write, std::memory_order_release); primed_ = false; return; }
    if (!primed_ && write - read >= target_frames) primed_ = true;
    if (!primed_) { read_.store(read, std::memory_order_release); underflow_.fetch_add(frames); return; }
    // Bounded drift correction (±1000 ppm), smoothed occupancy error. Large
    // stalls use explicit drops/reprime, never an unbounded latency backlog.
    const double error = (double(write - read) - target_frames) / target_frames;
    correction_ += (std::clamp(error * .002, -.001, .001) - correction_) * .01;
    for (uint32_t i = 0; i < frames; ++i) {
        if (read + 1 >= write || ring_[read % capacity].generation != generation || ring_[(read + 1) % capacity].generation != generation) {
            underflow_.fetch_add(frames - i); primed_ = false; fraction_ = 0; break;
        }
        const auto& a = ring_[read % capacity]; const auto& b = ring_[(read + 1) % capacity];
        left[i] = a.left + static_cast<float>(fraction_) * (b.left - a.left);
        right[i] = a.right + static_cast<float>(fraction_) * (b.right - a.right);
        fraction_ += 1.0 + correction_;
        const auto step = static_cast<uint64_t>(fraction_);
        read += step; fraction_ -= double(step);
    }
    read_.store(read, std::memory_order_release);
}
nodivu_capture_state_t Stream::snapshot() const noexcept {
    // Independent metrics are approximate, not a transaction with the DSP.
    const auto read = read_.load(); const auto write = write_.load();
    return {status_.load(), error_.load(), pid_.load(), static_cast<uint32_t>(std::min<uint64_t>(write >= read ? write - read : 0, capacity)),
        captured_.load(), dropped_.load(), underflow_.load(), discontinuities_.load(), age_.load(), max_age_.load(), target_frames, 48000};
}
void Stream::run() noexcept {
    struct Finished { std::atomic<bool>& flag; ~Finished() { flag.store(true); } } finished{finished_};
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) { error_.store(com); status_.store(5); return; }
    try {
        ProcessCapture capture;
        uint64_t seen = 0;
        Request current;
        ULONGLONG retry_at = 0;
        while (!stopping_.load()) {
            if (generation_.load() != seen) {
                WaitForSingleObject(wake_.get(), 0);
                std::lock_guard<std::mutex> lock(request_mutex_);
                current = request_; seen = current.generation;
                capture.close(); retry_at = 0; producing_generation_ = seen;
            }
            if (!current.mode) { WaitForSingleObject(wake_.get(), 100); continue; }
            if (capture.state() != State::capturing && GetTickCount64() >= retry_at) {
                Target target = current.target;
                if (current.mode == 1) {
                    Target live{};
                    if (!target.process_id || FAILED(identify(target.process_id, live)) || live.creation_time != target.creation_time) {
                        // Once bound, never silently switch to another same-image process.
                        // A new explicit choice (or project reopen) resolves identity again.
                        if (target.process_id) { pid_.store(0); status_.store(3); retry_at = GetTickCount64() + 1000; WaitForSingleObject(wake_.get(), 50); continue; }
                        std::vector<Application> apps;
                        const HRESULT found = applications(apps);
                        if (FAILED(found)) { error_.store(found); status_.store(5); retry_at = GetTickCount64() + 1000; continue; }
                        size_t matches = 0;
                        for (const auto& app : apps) if (same_path(app.executable, current.executable)) { target = app.identity; ++matches; }
                        if (matches != 1) { pid_.store(0); status_.store(matches ? 4 : 3); retry_at = GetTickCount64() + 1000; WaitForSingleObject(wake_.get(), 50); continue; }
                    }
                }
                status_.store(1);
                // Consume the wake for this request before using it to cancel
                // activation; a later configure/clear signals it again.
                if (generation_.load() != seen) continue;
                const HRESULT hr = capture.open(target, current.mode == 1 ? Mode::include_tree : Mode::exclude_tree, 3000, wake_.get());
                if (generation_.load() != seen || stopping_.load()) continue;
                if (FAILED(hr)) { error_.store(hr); status_.store(5); retry_at = GetTickCount64() + 1000; }
                else { current.target = target; pid_.store(target.process_id); error_.store(0); status_.store(2); }
            }
            if (capture.state() == State::capturing) {
                const HANDLE events[] = {wake_.get(), capture.ready_event()};
                const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 50);
                if (wait == WAIT_OBJECT_0 || generation_.load() != seen) continue;
                if (wait == WAIT_FAILED) { error_.store(HRESULT_FROM_WIN32(GetLastError())); status_.store(5); capture.close(); continue; }
                const HRESULT hr = capture.drain(receive, this);
                if (FAILED(hr)) { error_.store(hr); pid_.store(0); status_.store(capture.state() == State::target_exited ? 3 : 5); retry_at = GetTickCount64() + 1000; }
                const auto& m = capture.metrics();
                age_.store(m.timed_packets ? m.packet_age_sum_100ns / m.timed_packets / 10 : 0);
                max_age_.store(m.packet_age_max_100ns / 10);
            } else WaitForSingleObject(wake_.get(), 50);
        }
    } catch (...) { error_.store(E_OUTOFMEMORY); status_.store(5); }
    CoUninitialize();
}
}
