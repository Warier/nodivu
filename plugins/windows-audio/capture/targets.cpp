#include "targets.h"
#include <audiopolicy.h>
#include <tlhelp32.h>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace nodivu::capture {
using Microsoft::WRL::ComPtr;
std::wstring executable_path(DWORD pid) {
    Handle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!handle.get()) return {};
    std::wstring path(32768, L'\0');
    DWORD count = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(handle.get(), 0, path.data(), &count)) return {};
    path.resize(count);
    return path;
}
bool same_path(const std::wstring& a, const std::wstring& b) noexcept {
    return !a.empty() && !b.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("UTF-8");
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr)) throw std::runtime_error("UTF-8");
    return result;
}
std::wstring wide(const char* value) {
    if (!value || !*value) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (!size || size > 32768) throw std::runtime_error("Caminho UTF-8 invalido");
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), size)) throw std::runtime_error("UTF-8");
    result.resize(size - 1);
    return result;
}
HRESULT applications(std::vector<Application>& result) noexcept {
    result.clear();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return com;
    HRESULT hr = S_OK;
    try {
        std::map<DWORD, DWORD> parents;
        const HANDLE raw = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (raw == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(GetLastError());
        else {
            Handle snapshot(raw);
            PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
            const bool first = Process32FirstW(snapshot.get(), &entry) != FALSE;
            if (!first) hr = HRESULT_FROM_WIN32(GetLastError());
            if (first) do {
                if (parents.size() >= 8192) { hr = HRESULT_FROM_WIN32(ERROR_MORE_DATA); break; }
                parents.emplace(entry.th32ProcessID, entry.th32ParentProcessID);
            } while (Process32NextW(snapshot.get(), &entry));
        }
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED(hr)) hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        ComPtr<IMMDeviceCollection> devices;
        if (SUCCEEDED(hr)) hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        UINT device_count = 0;
        if (SUCCEEDED(hr)) hr = devices->GetCount(&device_count);
        if (device_count > 128) hr = HRESULT_FROM_WIN32(ERROR_MORE_DATA);
        UINT scanned = 0; HRESULT last_failure = S_OK;
        std::set<DWORD> seen;
        for (UINT d = 0; SUCCEEDED(hr) && d < device_count && d < 128; ++d) {
            ComPtr<IMMDevice> device;
            ComPtr<IAudioSessionManager2> manager;
            ComPtr<IAudioSessionEnumerator> sessions;
            HRESULT endpoint_hr = devices->Item(d, &device);
            if (SUCCEEDED(endpoint_hr)) endpoint_hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager);
            if (SUCCEEDED(endpoint_hr)) endpoint_hr = manager->GetSessionEnumerator(&sessions);
            int count = 0;
            if (SUCCEEDED(endpoint_hr)) endpoint_hr = sessions->GetCount(&count);
            if (FAILED(endpoint_hr)) { last_failure = endpoint_hr; continue; } // endpoint may disappear
            ++scanned;
            if (count < 0 || count > 1024) { hr = HRESULT_FROM_WIN32(ERROR_MORE_DATA); break; }
            for (int i = 0; i < count && i < 1024; ++i) {
                ComPtr<IAudioSessionControl> session;
                ComPtr<IAudioSessionControl2> control;
                DWORD pid = 0;
                if (FAILED(sessions->GetSession(i, &session)) || FAILED(session.As(&control)) || FAILED(control->GetProcessId(&pid)) || !pid) continue;
                auto path = executable_path(pid);
                Target identity{};
                if (path.empty() || FAILED(identify(pid, identity))) continue;
                // Browser audio often lives in a child executable. Climb only
                // exact same image, with creation-time ordering to reject PID reuse.
                for (unsigned depth = 0; depth < 32; ++depth) {
                    const auto p = parents.find(pid);
                    if (p == parents.end() || !p->second || p->second == pid) break;
                    Target parent{};
                    if (FAILED(identify(p->second, parent)) || parent.creation_time >= identity.creation_time || !same_path(path, executable_path(p->second))) break;
                    identity = parent; pid = parent.process_id;
                }
                if (seen.insert(pid).second) {
                    if (result.size() == 256) { hr = HRESULT_FROM_WIN32(ERROR_MORE_DATA); break; }
                    result.push_back({identity, std::move(path)});
                }
            }
        }
        if (SUCCEEDED(hr) && device_count && !scanned && FAILED(last_failure)) hr = last_failure;
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.executable < b.executable || (a.executable == b.executable && a.identity.process_id < b.identity.process_id); });
    } catch (...) { hr = E_OUTOFMEMORY; }
    if (SUCCEEDED(com)) CoUninitialize();
    if (FAILED(hr)) result.clear();
    return hr;
}
}
