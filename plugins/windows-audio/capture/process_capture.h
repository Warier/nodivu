#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <cstdint>

namespace nodivu::capture {
class Activation;
// Owner-thread API, not a DSP/CLAP API. COM must be initialized as MTA by
// the owner, and all methods/destruction must run there. One object per source.
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (value_) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};

enum class Mode { include_tree, exclude_tree };
enum class State { closed, starting, capturing, target_exited, failed };
enum class Stage { identity, activation, initialize, service, event, start, read };
struct Target { DWORD process_id; uint64_t creation_time; };
// Read and later revalidate process creation time: a reused PID is not the target.
HRESULT identify(DWORD process_id, Target& target) noexcept;
struct Packet {
    const float* interleaved; // borrowed until callback returns; null on silence
    UINT32 frames;           // fixed stereo float32 / 48 kHz
    DWORD flags;
    uint64_t device_position_frames;
    uint64_t qpc_100ns;      // Windows timestamp, not raw QPC ticks
};
using PacketSink = void (*)(void*, const Packet&) noexcept;
struct Metrics {
    uint64_t frames = 0, packets = 0, silent_frames = 0;
    uint64_t discontinuities = 0, timestamp_errors = 0;
    uint64_t first_qpc_100ns = 0, last_qpc_100ns = 0;
    uint64_t packet_age_sum_100ns = 0, packet_age_max_100ns = 0, timed_packets = 0;
    uint64_t future_timestamps = 0;
    DWORD activation_ms = 0;
    UINT32 max_packet_frames = 0;
};

class ProcessCapture {
public:
    ProcessCapture();
    ~ProcessCapture();
    ProcessCapture(const ProcessCapture&) = delete;
    ProcessCapture& operator=(const ProcessCapture&) = delete;
    // Control only. Bounded activation wait; no implicit PID/endpoint fallback.
    HRESULT open(Target target, Mode mode, DWORD timeout_ms = 3000, HANDLE cancel = nullptr) noexcept;
    // Nonblocking, at most 32 packets / 1 second of frames per call. The sink
    // must do bounded work and cannot retain packet pointers or call back here.
    HRESULT drain(PacketSink sink, void* context) noexcept;
    // Borrowed handle for owner-side event waiting; invalid after close/failure.
    // Waiting here is outside DSP. A scheduler must also handle stop/target exit.
    HANDLE ready_event() const noexcept { return event_.get(); }
    void close() noexcept;
    State state() const noexcept { return state_; }
    HRESULT error() const noexcept { return error_; }
    Stage stage() const noexcept { return stage_; }
    const Metrics& metrics() const noexcept { return metrics_; }
private:
    HRESULT fail(HRESULT result) noexcept;
    Microsoft::WRL::ComPtr<IAudioClient> client_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_;
    Microsoft::WRL::ComPtr<Activation> pending_activation_;
    Handle event_, target_;
    State state_ = State::closed;
    HRESULT error_ = S_OK;
    Stage stage_ = Stage::identity;
    Metrics metrics_;
    LARGE_INTEGER qpc_frequency_{};
};
}
