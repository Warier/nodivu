// Real Windows gate. Writes only measurements, never records PCM. The only
// rendered audio is two synthetic tones on the caller's explicit endpoint.
#include "../capture/process_capture.h"
#include <ks.h>
#include <ksmedia.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <stdexcept>

using namespace nodivu::capture;
using Microsoft::WRL::ComPtr;
namespace {
constexpr double pi = 3.14159265358979323846;
void check(HRESULT hr, const char* stage) {
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s: HRESULT 0x%08lX\n", stage, static_cast<unsigned long>(hr));
        throw std::runtime_error(stage);
    }
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct ComScope {
    ComScope() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM MTA"); }
    ~ComScope() { CoUninitialize(); }
};
std::wstring quote(const std::wstring& s) {
    // Inputs here are executable path, opaque endpoint ID and our event names.
    // Reject quotes/trailing backslash rather than accept ambiguous arguments.
    require(s.find(L'"') == std::wstring::npos && !s.empty() && s.back() != L'\\', "Argumento invalido");
    return L"\"" + s + L"\"";
}

int emit(const wchar_t* endpoint, unsigned frequency, const wchar_t* stop_name, const wchar_t* ready_name, DWORD duration_ms = 30000) {
    Handle stop(OpenEventW(SYNCHRONIZE, FALSE, stop_name));
    const std::wstring play_name = std::wstring(stop_name) + L"-play";
    Handle play(OpenEventW(SYNCHRONIZE, FALSE, play_name.c_str()));
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name));
    require(stop.get() && play.get() && ready.get(), "Eventos do emissor ausentes");
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "enumerator");
    ComPtr<IMMDevice> device;
    check(enumerator->GetDevice(endpoint, &device), "endpoint explicito");
    ComPtr<IMMEndpoint> endpoint_info;
    check(device.As(&endpoint_info), "endpoint direction");
    EDataFlow flow;
    check(endpoint_info->GetDataFlow(&flow), "endpoint flow");
    require(flow == eRender, "Endpoint precisa ser de reproducao");
    ComPtr<IAudioClient> client;
    check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "render activate");
    WAVEFORMATEXTENSIBLE format{};
    format.Format = {WAVE_FORMAT_EXTENSIBLE, 2, 48000, 384000, 8, 32, 22};
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        0, 0, &format.Format, nullptr), "render initialize");
    Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    require(event.get() != nullptr, "Render event");
    check(client->SetEventHandle(event.get()), "render event");
    UINT32 capacity = 0;
    check(client->GetBufferSize(&capacity), "render buffer size");
    ComPtr<IAudioRenderClient> render;
    check(client->GetService(IID_PPV_ARGS(&render)), "render service");
    uint64_t cursor = 0;
    auto fill = [&](UINT32 frames) {
        BYTE* raw = nullptr;
        check(render->GetBuffer(frames, &raw), "render get buffer");
        auto* data = reinterpret_cast<float*>(raw);
        const bool playing = WaitForSingleObject(play.get(), 0) == WAIT_OBJECT_0;
        for (UINT32 i = 0; i < frames; ++i, ++cursor) {
            const float value = playing ? static_cast<float>(0.04 * std::sin(2 * pi * frequency * double(cursor % 48000) / 48000)) : 0;
            data[2 * i] = data[2 * i + 1] = value;
        }
        check(render->ReleaseBuffer(frames, 0), "render release");
    };
    fill(capacity);
    check(client->Start(), "render start");
    SetEvent(ready.get());
    const HANDLE events[] = {stop.get(), event.get()};
    const ULONGLONG deadline = GetTickCount64() + duration_ms;
    while (GetTickCount64() < deadline) {
        const DWORD wait = WaitForMultipleObjects(2, events, FALSE, 1000);
        if (wait == WAIT_OBJECT_0) break;
        require(wait == WAIT_OBJECT_0 + 1 || wait == WAIT_TIMEOUT, "Render wait failed");
        if (wait == WAIT_TIMEOUT) continue;
        UINT32 padding = 0;
        check(client->GetCurrentPadding(&padding), "render padding");
        require(padding <= capacity, "Render padding invalido");
        if (capacity > padding) fill(capacity - padding);
    }
    check(client->Stop(), "render stop");
    return 0;
}

struct Child {
    Handle process, ready;
    DWORD pid = 0;
    void start(const std::wstring& executable, const std::wstring& endpoint, unsigned frequency,
               const std::wstring& stop_name, const std::wstring& ready_name, HANDLE job) {
        ready.reset(CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str()));
        require(ready.get() != nullptr, "Child ready event");
        std::wstring command = quote(executable) + L" --emit " + quote(endpoint) + L" " + std::to_wstring(frequency)
            + L" " + quote(stop_name) + L" " + quote(ready_name);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info) != FALSE, "CreateProcess emissor");
        process.reset(info.hProcess);
        Handle thread(info.hThread);
        pid = info.dwProcessId;
        // Assign before resuming: parent exceptions/crash cannot orphan tones.
        if (!AssignProcessToJobObject(job, process.get())) {
            TerminateProcess(process.get(), 1); // only our still-suspended test child
            throw std::runtime_error("AssignProcessToJobObject");
        }
        require(ResumeThread(thread.get()) != DWORD(-1), "ResumeThread");
        HANDLE events[] = {ready.get(), process.get()};
        require(WaitForMultipleObjects(2, events, FALSE, 5000) == WAIT_OBJECT_0, "Emissor nao ficou pronto");
    }
    void joined() {
        require(WaitForSingleObject(process.get(), 3000) == WAIT_OBJECT_0, "Emissor nao encerrou");
        DWORD code = 1;
        require(GetExitCodeProcess(process.get(), &code) && code == 0, "Emissor falhou");
    }
};

struct Spectrum {
    uint64_t frames = 0, invalid = 0;
    double energy = 0;
    std::array<double, 2> sine{}, cosine{};
    static void receive(void* context, const Packet& packet) noexcept {
        auto& s = *static_cast<Spectrum*>(context);
        for (UINT32 i = 0; i < packet.frames; ++i, ++s.frames) {
            double sample = packet.interleaved ? packet.interleaved[i * 2] : 0;
            if (!std::isfinite(sample)) { ++s.invalid; sample = 0; }
            s.energy += sample * sample;
            // Deliberately test-only analysis. Not a production DSP callback.
            for (size_t f = 0; f < 2; ++f) {
                const double angle = 2 * pi * (f ? 1733 : 997) * double(s.frames % 48000) / 48000;
                s.sine[f] += sample * std::sin(angle);
                s.cosine[f] += sample * std::cos(angle);
            }
        }
    }
    double amplitude(size_t f) const {
        return frames ? 2 * std::hypot(sine[f], cosine[f]) / double(frames) : 0;
    }
};
void discard(void*, const Packet&) noexcept {}
void sample(std::array<ProcessCapture, 5>& captures, std::array<Spectrum, 5>* spectra, DWORD ms) {
    const auto deadline = GetTickCount64() + ms;
    std::array<HANDLE, 5> events{};
    for (size_t i = 0; i < captures.size(); ++i) events[i] = captures[i].ready_event();
    while (GetTickCount64() < deadline) {
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, 20);
        require(wait != WAIT_FAILED, "Capture event wait");
        for (size_t i = 0; i < captures.size(); ++i)
            check(captures[i].drain(spectra ? Spectrum::receive : discard, spectra ? &(*spectra)[i] : nullptr), "capture drain");
    }
}

int isolation(const std::wstring& executable, const std::wstring& endpoint, bool verify_exclusion) {
    const auto prefix = L"Local\\NodivuCaptureTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto stop_name = prefix + L"-stop";
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, stop_name.c_str()));
    Handle play(CreateEventW(nullptr, TRUE, FALSE, (stop_name + L"-play").c_str()));
    require(stop.get() != nullptr, "Stop event");
    require(play.get() != nullptr, "Play event");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    require(job.get() != nullptr, "Test job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "Test job limits");
    Child a, b;
    a.start(executable, endpoint, 997, stop_name, prefix + L"-a", job.get());
    b.start(executable, endpoint, 1733, stop_name, prefix + L"-b", job.get());
    Target ta{}, tb{}, parent{};
    check(identify(a.pid, ta), "identify A");
    check(identify(b.pid, tb), "identify B");
    check(identify(GetCurrentProcessId(), parent), "identify parent");
    ProcessCapture invalid;
    Target wrong = ta;
    ++wrong.creation_time;
    require(FAILED(invalid.open(wrong, Mode::include_tree)), "Identidade antiga aceita");
    require(FAILED(invalid.open({}, Mode::include_tree)), "PID zero aceito");
    std::array<ProcessCapture, 5> captures;
    const HRESULT first = captures[0].open(ta, Mode::include_tree);
    if (FAILED(first)) std::fprintf(stderr, "stage=%d activation_ms=%lu\n",
        static_cast<int>(captures[0].stage()), captures[0].metrics().activation_ms);
    check(first, "include A");
    check(captures[1].open(tb, Mode::include_tree), "include B");
    check(captures[2].open(parent, Mode::include_tree), "include parent tree");
    check(captures[3].open(ta, Mode::exclude_tree), "exclude A");
    check(captures[4].open(parent, Mode::exclude_tree), "exclude parent tree");
    sample(captures, nullptr, 300); // discard activation transients, explicitly
    std::array<Spectrum, 5> baseline{};
    sample(captures, &baseline, 1000);
    require(baseline[0].energy < 1e-10 && baseline[1].energy < 1e-10 && baseline[2].energy < 1e-10,
        "Captura isolada recebeu audio antes dos tons");
    std::printf("{\"baseline_exclude_parent\":true,\"a_997\":%.9f,\"b_1733\":%.9f,\"rms\":%.9f}\n",
        baseline[4].amplitude(0), baseline[4].amplitude(1), baseline[4].frames ? std::sqrt(baseline[4].energy / double(baseline[4].frames)) : 0);
    SetEvent(play.get());
    sample(captures, nullptr, 300); // tone onset is outside the spectral window
    std::array<Spectrum, 5> spectra{};
    sample(captures, &spectra, 2500);
    const char* names[] = {"include_a", "include_b", "include_parent", "exclude_a", "exclude_parent"};
    for (size_t i = 0; i < captures.size(); ++i) {
        const auto& s = spectra[i];
        const auto& m = captures[i].metrics();
        std::printf("{\"diagnostic\":\"%s\",\"rms\":%.9f,\"silent_frames\":%llu}\n", names[i],
            s.frames ? std::sqrt(s.energy / double(s.frames)) : 0, static_cast<unsigned long long>(m.silent_frames));
        std::printf("{\"timing\":\"%s\",\"packet_age_mean_ms\":%.3f,\"packet_age_max_ms\":%.3f,\"future_timestamps\":%llu}\n", names[i],
            m.timed_packets ? double(m.packet_age_sum_100ns) / double(m.timed_packets) / 10000.0 : 0,
            double(m.packet_age_max_100ns) / 10000.0, static_cast<unsigned long long>(m.future_timestamps));
        std::printf("{\"case\":\"%s\",\"frames\":%llu,\"a_997\":%.9f,\"b_1733\":%.9f,\"activation_ms\":%lu,\"max_packet_frames\":%u,\"discontinuities\":%llu,\"timestamp_errors\":%llu,\"qpc_first_100ns\":%llu,\"qpc_last_100ns\":%llu}\n",
            names[i], static_cast<unsigned long long>(s.frames), s.amplitude(0), s.amplitude(1), m.activation_ms, m.max_packet_frames,
            static_cast<unsigned long long>(m.discontinuities), static_cast<unsigned long long>(m.timestamp_errors),
            static_cast<unsigned long long>(m.first_qpc_100ns), static_cast<unsigned long long>(m.last_qpc_100ns));
        require(s.invalid == 0, "PCM nao finito");
        require(s.frames >= 96000 && s.frames <= 144000, "Cobertura de captura fora de 2-3 segundos");
        require(m.first_qpc_100ns != 0 && m.last_qpc_100ns > m.first_qpc_100ns && m.timestamp_errors == 0, "Timestamps invalidos");
    }
    const double signal_a = spectra[0].amplitude(0), signal_b = spectra[1].amplitude(1);
    require(signal_a > 0.005 && signal_b > 0.005, "Tons nao chegaram");
    require(spectra[0].amplitude(1) < signal_b * .01 && spectra[1].amplitude(0) < signal_a * .01, "Vazamento entre processos maior que -40 dB");
    require(spectra[2].amplitude(0) > signal_a * .8 && spectra[2].amplitude(1) > signal_b * .8, "Arvore nao incluiu filhos");
    const bool exclusion_passed = spectra[3].amplitude(0) < signal_a * .01 && spectra[3].amplitude(1) > signal_b * .8 &&
        spectra[4].amplitude(0) < signal_a * .01 && spectra[4].amplitude(1) < signal_b * .01;
    // Exclusion includes unrelated system audio, including copies replayed by
    // other processes. Do not silently call that test PASS on a busy desktop.
    std::printf("{\"system_exclusion\":\"%s\",\"threshold_db\":-40}\n", exclusion_passed ? "PASS" : "INCONCLUSIVE");
    if (!exclusion_passed) std::fputs("Exclusao inconclusiva: sinal residual no audio global; verificar audio concorrente/reemissao externa. Nenhum app foi alterado.\n", stderr);

    SetEvent(stop.get());
    a.joined(); b.joined();
    require(FAILED(captures[0].drain(discard, nullptr)) && captures[0].state() == State::target_exited, "Processo encerrado sem estado explicito");
    for (auto& capture : captures) capture.close();
    // The parent has no audio of its own now. Capturing a live silent target
    // must remain operational, not masquerade as a failed target.
    check(captures[0].open(parent, Mode::include_tree), "reopen silent parent");
    Spectrum silence;
    const auto deadline = GetTickCount64() + 350;
    while (GetTickCount64() < deadline) {
        require(WaitForSingleObject(captures[0].ready_event(), 20) != WAIT_FAILED, "Silent event wait");
        check(captures[0].drain(Spectrum::receive, &silence), "silent drain");
    }
    require(silence.energy < 1e-10 && captures[0].state() == State::capturing, "Alvo silencioso com audio residual");
    std::puts("{\"result\":\"PASS\",\"scope\":\"process_isolation_and_lifecycle\",\"isolated_processes\":2,\"simultaneous_captures\":5,\"tree_include\":true,\"pid_identity_rejected\":true,\"target_exit\":true,\"silent_reopen\":true}");
    if (verify_exclusion) require(exclusion_passed, "Gate estrito de exclusao global nao passou");
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        ComScope com;
        if (argc == 5 && std::wstring(argv[1]) == L"--tone") {
            const std::wstring frequency = argv[3];
            require(frequency == L"997" || frequency == L"1733", "Frequencia de teste invalida");
            wchar_t* end = nullptr;
            const auto duration = wcstoul(argv[4], &end, 10);
            require(end && !*end && duration >= 100 && duration <= 120000, "Duracao de teste invalida");
            const auto name = L"Local\\NodivuSourceTone-" + std::to_wstring(GetCurrentProcessId());
            Handle stop(CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
            Handle ready(CreateEventW(nullptr, TRUE, FALSE, (name + L"-ready").c_str()));
            Handle play(CreateEventW(nullptr, TRUE, TRUE, (name + L"-play").c_str()));
            require(stop.get() && ready.get() && play.get(), "Tone events");
            return emit(argv[2], frequency == L"997" ? 997 : 1733, name.c_str(), (name + L"-ready").c_str(), duration);
        }
        if (argc == 6 && std::wstring(argv[1]) == L"--emit") {
            const std::wstring frequency = argv[3];
            require(frequency == L"997" || frequency == L"1733", "Frequencia de teste invalida");
            return emit(argv[2], frequency == L"997" ? 997 : 1733, argv[4], argv[5]);
        }
        if ((argc == 3 || (argc == 4 && std::wstring(argv[3]) == L"--verify-exclusion")) && std::wstring(argv[1]) == L"--render-id") {
            std::array<wchar_t, 32768> executable{};
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            require(length && length < executable.size(), "Executable path");
            return isolation(executable.data(), argv[2], argc == 4);
        }
        std::fputs("Uso: process-capture-test --render-id <ID explicito de saida; preferir VB-CABLE> [--verify-exclusion]\n", stderr);
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
