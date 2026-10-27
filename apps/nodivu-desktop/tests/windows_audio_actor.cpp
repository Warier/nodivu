// Hardware acceptance fixture only. Never shipped or loaded by the GUI.
#include "process_capture.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <ks.h>
#include <ksmedia.h>
#include <stdexcept>
#include <string>

namespace {
using namespace nodivu::capture;
using Microsoft::WRL::ComPtr;
constexpr double Pi = 3.14159265358979323846;
void check(HRESULT result, const char* stage) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: HRESULT 0x%08lX\n", stage, static_cast<unsigned long>(result));
        throw std::runtime_error(stage);
    }
}
struct ComScope {
    ComScope() {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM MTA");
    }
    ~ComScope() {
        CoUninitialize();
    }
};
struct Spectrum {
    uint64_t frames = 0, invalid = 0;
    double energy = 0;
    std::array<double, 2> real{}, imaginary{};
    std::array<double, 2> windowMagnitudes{};
    static void receive(void* context, const Packet& packet) noexcept {
        auto& state = *static_cast<Spectrum*>(context);
        for (UINT32 index = 0; index < packet.frames; ++index) {
            const double value =
                packet.interleaved
                    ? (packet.interleaved[index * 2] + packet.interleaved[index * 2 + 1]) * 0.5
                    : 0;
            if (!std::isfinite(value)) {
                ++state.invalid;
                continue;
            }
            state.energy += value * value;
            const auto frame = state.frames++;
            for (size_t band = 0; band < 2; ++band) {
                const double angle =
                    2 * Pi * (band == 0 ? 997 : 1733) * static_cast<double>(frame % 48000) / 48000;
                state.real[band] += value * std::cos(angle);
                state.imaginary[band] += value * std::sin(angle);
            }
            if (state.frames % 4800 == 0) {
                for (size_t band = 0; band < 2; ++band) {
                    state.windowMagnitudes[band] +=
                        2 * std::hypot(state.real[band], state.imaginary[band]);
                    state.real[band] = state.imaginary[band] = 0;
                }
            }
        }
    }
    double amplitude(size_t band) const {
        // The existing asynchronous capture resampler corrects clock drift.
        // Average 100 ms magnitudes so its small frequency shift does not look
        // like attenuation through phase cancellation over the entire sample.
        const auto partial = frames % 4800;
        const auto usable = frames - partial + (partial >= 2400 ? partial : 0);
        const auto magnitude = windowMagnitudes[band] +
                               (partial >= 2400 ? 2 * std::hypot(real[band], imaginary[band]) : 0);
        return usable ? magnitude / static_cast<double>(usable) : 0;
    }
};
void discard(void*, const Packet&) noexcept {}
struct Onsets {
    std::array<uint64_t, 32> times{};
    size_t count = 0;
    UINT32 quiet = 4800;
    bool active = false;
    static void receive(void* context, const Packet& packet) noexcept {
        auto& state = *static_cast<Onsets*>(context);
        if (!packet.qpc_100ns || (packet.flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR))
            return;
        for (UINT32 index = 0; index < packet.frames; ++index) {
            const float value = packet.interleaved ? packet.interleaved[index * 2] : 0;
            if (std::abs(value) > 0.003f) {
                if (!state.active && state.quiet >= 4800 && state.count < state.times.size())
                    state.times[state.count++] =
                        packet.qpc_100ns + static_cast<uint64_t>(index) * 10000000 / 48000;
                state.active = true;
                state.quiet = 0;
            } else if (state.quiet < 4800) {
                if (++state.quiet >= 4800)
                    state.active = false;
            }
        }
    }
};
int latency(DWORD sourcePid, DWORD outputPid) {
    Target sourceTarget{}, outputTarget{};
    check(identify(sourcePid, sourceTarget), "latency source identity");
    check(identify(outputPid, outputTarget), "latency output identity");
    ProcessCapture source, output;
    check(source.open(sourceTarget, Mode::include_tree), "latency source capture");
    check(output.open(outputTarget, Mode::include_tree), "latency output capture");
    Onsets sourceOnsets, outputOnsets;
    std::cout << "{\"ready\":true}" << std::endl;
    HANDLE events[]{source.ready_event(), output.ready_event()};
    const auto deadline = GetTickCount64() + 15000;
    while (GetTickCount64() < deadline && (sourceOnsets.count < 12 || outputOnsets.count < 12)) {
        if (WaitForMultipleObjects(2, events, FALSE, 20) == WAIT_FAILED)
            throw std::runtime_error("latency capture wait");
        check(source.drain(Onsets::receive, &sourceOnsets), "latency source read");
        check(output.drain(Onsets::receive, &outputOnsets), "latency output read");
    }
    // Both taps share Windows QPC timestamps. Ignore partial attachment pulses;
    // pair interior onsets one-to-one, within half the one-second pulse period.
    std::array<double, 32> delays{};
    size_t count = 0, outputIndex = 0;
    for (size_t index = 1; index + 1 < sourceOnsets.count; ++index) {
        const auto start = sourceOnsets.times[index];
        while (outputIndex < outputOnsets.count && outputOnsets.times[outputIndex] < start)
            ++outputIndex;
        if (outputIndex < outputOnsets.count && outputOnsets.times[outputIndex] - start < 5000000) {
            delays[count++] =
                static_cast<double>(outputOnsets.times[outputIndex++] - start) / 10000;
        }
    }
    if (count < 8 || source.metrics().timestamp_errors || output.metrics().timestamp_errors)
        throw std::runtime_error("latency requires eight valid paired pulse timestamps");
    std::printf("{\"source_onsets\":%zu,\"output_onsets\":%zu,\"delays_ms\":[", sourceOnsets.count,
                outputOnsets.count);
    for (size_t index = 0; index < count; ++index)
        std::printf("%s%.6f", index ? "," : "", delays[index]);
    std::printf("],\"timestamp_errors\":0,\"scope\":\"Source process tap to backend output process "
                "tap; software path, not acoustic or physical roundtrip\"}\n");
    std::fflush(stdout);
    return 0;
}
int receiver(DWORD pid) {
    Target target{};
    check(identify(pid, target), "receiver target identity");
    ProcessCapture capture;
    check(capture.open(target, Mode::include_tree), "receiver process capture");
    std::cout << "{\"ready\":true}" << std::endl;
    std::string command;
    int requests = 0;
    while (std::getline(std::cin, command)) {
        if (command == "quit")
            break;
        if (command != "sample" || ++requests > 128)
            return 2;
        // Drain old packets outside the collection window after a control change.
        const auto prepareUntil = GetTickCount64() + 300;
        while (GetTickCount64() < prepareUntil) {
            WaitForSingleObject(capture.ready_event(), 10);
            check(capture.drain(discard, nullptr), "receiver warmup");
        }
        Spectrum spectrum;
        const auto until = GetTickCount64() + 700;
        while (GetTickCount64() < until) {
            if (WaitForSingleObject(capture.ready_event(), 20) == WAIT_FAILED)
                throw std::runtime_error("receiver event failed");
            check(capture.drain(Spectrum::receive, &spectrum), "receiver collection");
        }
        const auto& timing = capture.metrics();
        std::printf(
            "{\"frames\":%llu,\"a997\":%.9f,\"b1733\":%.9f,\"rms\":%.9f,"
            "\"nonfinite\":%llu,\"packet_age_mean_ms\":%.4f,\"discontinuities\":%llu}\n",
            static_cast<unsigned long long>(spectrum.frames), spectrum.amplitude(0),
            spectrum.amplitude(1),
            spectrum.frames ? std::sqrt(spectrum.energy / static_cast<double>(spectrum.frames)) : 0,
            static_cast<unsigned long long>(spectrum.invalid),
            timing.timed_packets ? static_cast<double>(timing.packet_age_sum_100ns) /
                                       static_cast<double>(timing.timed_packets) / 10000
                                 : 0,
            static_cast<unsigned long long>(timing.discontinuities));
        std::fflush(stdout);
    }
    return 0;
}
int emitter(const wchar_t* endpoint, unsigned frequency, bool pulsed = false) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&enumerator)),
          "emitter enumerator");
    ComPtr<IMMDevice> device;
    check(enumerator->GetDevice(endpoint, &device), "explicit endpoint");
    ComPtr<IMMEndpoint> direction;
    check(device.As(&direction), "endpoint direction");
    EDataFlow flow{};
    check(direction->GetDataFlow(&flow), "endpoint flow");
    if (flow != eRender)
        throw std::runtime_error("An explicit render endpoint is required");
    ComPtr<IAudioClient> client;
    check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client),
          "emitter activate");
    WAVEFORMATEXTENSIBLE format{};
    format.Format = {WAVE_FORMAT_EXTENSIBLE, 2, 48000, 384000, 8, 32, 22};
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                             0, 0, &format.Format, nullptr),
          "emitter initialize");
    Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event.get())
        throw std::runtime_error("emitter event");
    check(client->SetEventHandle(event.get()), "emitter set event");
    UINT32 capacity = 0;
    check(client->GetBufferSize(&capacity), "emitter capacity");
    ComPtr<IAudioRenderClient> render;
    check(client->GetService(IID_PPV_ARGS(&render)), "emitter render service");
    uint64_t cursor = 0;
    const auto fill = [&](UINT32 count) {
        BYTE* raw = nullptr;
        check(render->GetBuffer(count, &raw), "emitter buffer");
        auto* pcm = reinterpret_cast<float*>(raw);
        for (UINT32 index = 0; index < count; ++index, ++cursor) {
            const float value =
                pulsed && cursor % 48000 >= 4800
                    ? 0
                    : static_cast<float>(0.02 *
                                         std::sin(2 * Pi * frequency *
                                                  static_cast<double>(cursor % 48000) / 48000));
            pcm[index * 2] = pcm[index * 2 + 1] = value;
        }
        check(render->ReleaseBuffer(count, 0), "emitter release");
    };
    fill(capacity);
    check(client->Start(), "emitter start");
    std::cout << "{\"ready\":true}" << std::endl;
    const auto until = GetTickCount64() + 120000;
    while (GetTickCount64() < until) {
        const auto wait = WaitForSingleObject(event.get(), 1000);
        if (wait == WAIT_TIMEOUT)
            continue;
        if (wait != WAIT_OBJECT_0)
            throw std::runtime_error("emitter wait");
        UINT32 padding = 0;
        check(client->GetCurrentPadding(&padding), "emitter padding");
        if (padding > capacity)
            throw std::runtime_error("emitter capacity exceeded");
        if (padding < capacity)
            fill(capacity - padding);
    }
    check(client->Stop(), "emitter stop");
    return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try {
        ComScope com;
        if (argc == 4 && std::wstring(argv[1]) == L"--latency") {
            wchar_t* sourceEnd = nullptr;
            wchar_t* outputEnd = nullptr;
            const auto sourcePid = wcstoul(argv[2], &sourceEnd, 10);
            const auto outputPid = wcstoul(argv[3], &outputEnd, 10);
            if (!sourceEnd || *sourceEnd || !outputEnd || *outputEnd || !sourcePid || !outputPid)
                return 2;
            return latency(sourcePid, outputPid);
        }
        if (argc == 3 && std::wstring(argv[1]) == L"--pulse")
            return emitter(argv[2], 997, true);
        if (argc == 3 && std::wstring(argv[1]) == L"--receiver") {
            wchar_t* end = nullptr;
            const auto pid = wcstoul(argv[2], &end, 10);
            if (!end || *end || pid == 0)
                return 2;
            return receiver(pid);
        }
        if (argc == 4 && std::wstring(argv[1]) == L"--emit") {
            const std::wstring frequency(argv[3]);
            if (frequency != L"997" && frequency != L"1733")
                return 2;
            return emitter(argv[2], frequency == L"997" ? 997 : 1733);
        }
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
