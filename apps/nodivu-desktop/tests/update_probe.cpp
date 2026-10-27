// Inert installer fixture: no registry, DLL loading, network or installation.
#include <array>
#include <windows.h>

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) {
        Sleep(wcscmp(argv[1], L"--hold-long") == 0 ? 10000 : 700);
        return 0;
    }
    std::array<wchar_t, 32768> path{};
    const auto length = GetEnvironmentVariableW(L"NODIVU_UPDATE_PROBE_MARKER", path.data(),
                                                static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return 2;
    HANDLE file = CreateFileW(path.data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 3;
    constexpr char marker[] = "verified installer fixture launched";
    DWORD written = 0;
    const bool saved = WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr) &&
                       written == sizeof(marker) - 1;
    CloseHandle(file);
    return saved ? 0 : 4;
}
