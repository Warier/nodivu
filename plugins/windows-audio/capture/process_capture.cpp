#include "process_capture.h"
#include <audioclientactivationparams.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/implements.h>
#include <atomic>

namespace nodivu::capture {
using Microsoft::WRL::ComPtr;
namespace {
uint64_t creation_time(const FILETIME& value) noexcept {
    return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
HRESULT read_identity(HANDLE process, uint64_t& result) noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        return HRESULT_FROM_WIN32(GetLastError());
    result = creation_time(created);
    return S_OK;
}
}

// Self-contained COM callback: NEVER borrows ProcessCapture or caller stack.
// Windows retains this handler during activation. On timeout, a late callback
// can finish safely after the caller has returned. open() pins this module
// image until process exit, so a late callback cannot enter unloaded code.
class Activation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
    PROPVARIANT variant{};
    std::atomic<bool> completed{false};
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override {
        HRESULT activation_result = E_UNEXPECTED;
        ComPtr<IUnknown> unknown;
        result = operation->GetActivateResult(&activation_result, &unknown);
        if (SUCCEEDED(result)) result = activation_result;
        if (SUCCEEDED(result)) result = unknown ? unknown.As(&client) : E_POINTER;
        completed.store(true, std::memory_order_release);
        SetEvent(ready.get());
        return S_OK;
    }
};

ProcessCapture::ProcessCapture() = default;
ProcessCapture::~ProcessCapture() { close(); }

HRESULT identify(DWORD process_id, Target& target) noexcept {
    target = {};
    if (!process_id) return E_INVALIDARG;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, process_id));
    if (!process.get()) return HRESULT_FROM_WIN32(GetLastError());
    uint64_t created = 0;
    HRESULT hr = read_identity(process.get(), created);
    if (FAILED(hr)) return hr;
    if (WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT)
        return HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);
    target = {process_id, created};
    return S_OK;
}

HRESULT ProcessCapture::fail(HRESULT result) noexcept {
    close();
    error_ = result;
    state_ = State::failed;
    return result;
}

HRESULT ProcessCapture::open(Target target, Mode mode, DWORD timeout_ms, HANDLE cancel) noexcept {
    close();
    // One outstanding asynchronous activation per instance, even when callers
    // repeatedly retry after a timeout. No growing queue of detached callbacks.
    if (pending_activation_) {
        if (!pending_activation_->completed.load(std::memory_order_acquire)) return fail(E_PENDING);
        pending_activation_.Reset();
    }
    metrics_ = {};
    error_ = S_OK;
    stage_ = Stage::identity;
    if (!QueryPerformanceFrequency(&qpc_frequency_) || qpc_frequency_.QuadPart <= 0)
        return fail(E_UNEXPECTED);
    if (!target.process_id || !target.creation_time || !timeout_ms || timeout_ms > 10000 ||
        (mode != Mode::include_tree && mode != Mode::exclude_tree)) return fail(E_INVALIDARG);
    target_.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, target.process_id));
    if (!target_.get()) return fail(HRESULT_FROM_WIN32(GetLastError()));
    uint64_t created = 0;
    HRESULT hr = read_identity(target_.get(), created);
    if (FAILED(hr)) return fail(hr);
    if (created != target.creation_time) return fail(HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER));
    if (WaitForSingleObject(target_.get(), 0) != WAIT_TIMEOUT)
        return fail(HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED));
    event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event_.get()) return fail(HRESULT_FROM_WIN32(GetLastError()));
    auto activation = Microsoft::WRL::Make<Activation>();
    if (!activation) return fail(E_OUTOFMEMORY);
    if (!activation->ready.get()) return fail(HRESULT_FROM_WIN32(GetLastError()));
    auto& params = activation->parameters;
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = target.process_id;
    params.ProcessLoopbackParams.ProcessLoopbackMode = mode == Mode::include_tree
        ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    activation->variant.vt = VT_BLOB;
    activation->variant.blob.cbSize = sizeof(params);
    activation->variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    state_ = State::starting;
    stage_ = Stage::activation;
    const ULONGLONG began = GetTickCount64();
    // Windows owns completion callbacks after timeout/cancel. Pin this small
    // module image until process exit: instance resources still join/release,
    // but host FreeLibrary cannot unmap callback code on a COM thread. Reload
    // already requires app restart. No mutable global callback registry.
    HMODULE image = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&identify), &image)) return fail(HRESULT_FROM_WIN32(GetLastError()));
    pending_activation_ = activation;
    hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
        &activation->variant, activation.Get(), &operation);
    if (FAILED(hr)) {
        pending_activation_.Reset();
        return fail(hr);
    }
    const HANDLE waits[] = {activation->ready.get(), cancel};
    const DWORD wait = WaitForMultipleObjects(cancel ? 2 : 1, waits, FALSE, timeout_ms);
    metrics_.activation_ms = static_cast<DWORD>(GetTickCount64() - began);
    if (wait != WAIT_OBJECT_0) return fail(HRESULT_FROM_WIN32(wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : wait == WAIT_OBJECT_0 + 1 ? ERROR_CANCELLED : GetLastError()));
    if (!activation->completed.load(std::memory_order_acquire)) return fail(E_UNEXPECTED);
    pending_activation_.Reset();
    if (FAILED(activation->result)) return fail(activation->result);
    client_ = activation->client;
    WAVEFORMATEXTENSIBLE format{};
    format.Format = {WAVE_FORMAT_EXTENSIBLE, 2, 48000, 48000 * 8, 8, 32, 22};
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    stage_ = Stage::initialize;
    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        // Request 100 ms capacity, not an intentional 100 ms delay;
        // packets are drained immediately and their actual cadence is measured.
        1000000, 0, &format.Format, nullptr);
    if (FAILED(hr)) return fail(hr);
    // This virtual client returned unusable GetBufferSize values on Win10
    // 19045. Do not use that endpoint-oriented query to bound packet access.
    // We validate actual packet lengths against our explicit 4,800-frame cap.
    stage_ = Stage::service;
    hr = client_->GetService(IID_PPV_ARGS(&capture_));
    if (FAILED(hr)) return fail(hr);
    stage_ = Stage::event;
    hr = client_->SetEventHandle(event_.get());
    if (FAILED(hr)) return fail(hr);
    stage_ = Stage::start;
    hr = client_->Start();
    if (FAILED(hr)) return fail(hr);
    state_ = State::capturing;
    return S_OK;
}

HRESULT ProcessCapture::drain(PacketSink sink, void* context) noexcept {
    if (!sink) return E_POINTER;
    if (state_ != State::capturing) return FAILED(error_) ? error_ : E_UNEXPECTED;
    stage_ = Stage::read;
    const DWORD target_status = WaitForSingleObject(target_.get(), 0);
    if (target_status == WAIT_OBJECT_0) {
        close();
        state_ = State::target_exited;
        error_ = HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);
        return error_;
    }
    if (target_status == WAIT_FAILED) return fail(HRESULT_FROM_WIN32(GetLastError()));
    UINT32 total = 0;
    for (unsigned packet = 0; packet < 32; ++packet) {
        UINT32 available = 0;
        HRESULT hr = capture_->GetNextPacketSize(&available);
        if (FAILED(hr)) return fail(hr);
        if (available > 4800) return fail(E_INVALIDARG);
        if (!available || total + available > 48000) return S_OK;
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 position = 0, qpc = 0;
        hr = capture_->GetBuffer(&data, &frames, &flags, &position, &qpc);
        if (FAILED(hr)) return fail(hr);
        LARGE_INTEGER received_at{};
        const bool received_time_valid = QueryPerformanceCounter(&received_at) != FALSE;
        const bool valid = frames <= available && frames <= 4800 &&
            ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || data);
        if (valid) {
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            sink(context, {silent ? nullptr : reinterpret_cast<const float*>(data), frames, flags, position, qpc});
            metrics_.frames += frames;
            ++metrics_.packets;
            if (frames > metrics_.max_packet_frames) metrics_.max_packet_frames = frames;
            if (silent) metrics_.silent_frames += frames;
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++metrics_.discontinuities;
            if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) ++metrics_.timestamp_errors;
            else {
                if (!metrics_.first_qpc_100ns) metrics_.first_qpc_100ns = qpc;
                metrics_.last_qpc_100ns = qpc;
                if (received_time_valid) {
                    const uint64_t now_100ns = static_cast<uint64_t>(double(received_at.QuadPart) * 10000000.0 / double(qpc_frequency_.QuadPart));
                    if (now_100ns >= qpc) {
                        const uint64_t age = now_100ns - qpc;
                        metrics_.packet_age_sum_100ns += age;
                        if (age > metrics_.packet_age_max_100ns) metrics_.packet_age_max_100ns = age;
                        ++metrics_.timed_packets;
                    } else ++metrics_.future_timestamps;
                }
            }
        }
        // Always release the WASAPI borrow, including malformed packets.
        hr = capture_->ReleaseBuffer(frames);
        if (FAILED(hr)) return fail(hr);
        if (!valid) return fail(E_INVALIDARG);
        total += frames;
    }
    return S_OK;
}

void ProcessCapture::close() noexcept {
    if (client_) client_->Stop();
    capture_.Reset();
    client_.Reset();
    event_.reset();
    target_.reset();
    state_ = State::closed;
}
}
