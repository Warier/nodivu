// Minimal update handoff. This executable has no Qt or audio dependencies.
#include <array>
#include <cerrno>
// Windows SDK headers require this order (BCrypt and Shell API use Win32 types).
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
// clang-format on
#include <string>
#include <vector>

namespace {
bool matchesHash(const wchar_t* path, const std::wstring& expected) {
    const DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
        return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD returned = 0;
    bool valid = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (valid)
        valid = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
                                  &returned, 0) >= 0;
    std::vector<unsigned char> object(objectBytes);
    if (valid)
        valid = BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{};
    unsigned long long total = 0;
    while (valid) {
        DWORD bytes = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
            valid = false;
            break;
        }
        if (bytes == 0)
            break;
        total += bytes;
        if (total > 256ULL * 1024 * 1024 || BCryptHashData(hash, buffer.data(), bytes, 0) < 0)
            valid = false;
    }
    std::array<unsigned char, 32> digest{};
    if (valid)
        valid = total > 0 &&
                BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    if (algorithm)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!valid)
        return false;
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring actual;
    for (auto byte : digest) {
        actual += hex[byte >> 4];
        actual += hex[byte & 15];
    }
    return actual == expected;
}
} // namespace
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count != 4) {
        if (args)
            LocalFree(args);
        return 2;
    }
    const std::wstring pidText(args[1]);
    const std::wstring installer(args[2]);
    const std::wstring hash(args[3]);
    LocalFree(args);
    if (pidText.empty() || pidText.size() > 10 ||
        pidText.find_first_not_of(L"0123456789") != std::wstring::npos || hash.size() != 64 ||
        hash.find_first_not_of(L"0123456789abcdef") != std::wstring::npos || installer.size() < 4 ||
        installer.size() > 32000 || installer[1] != L':' ||
        installer.find(L'"') != std::wstring::npos ||
        installer.substr(installer.size() - 4) != L".exe")
        return 2;
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long pid = wcstoul(pidText.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != L'\0' || pid == 0)
        return 2;
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (parent) {
        const DWORD waited = WaitForSingleObject(parent, 60000);
        CloseHandle(parent);
        if (waited != WAIT_OBJECT_0)
            return 3;
    } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
        return 3;
    }
    if (!matchesHash(installer.c_str(), hash)) {
        MessageBoxW(
            nullptr,
            L"O instalador mudou ou não pôde ser verificado. A versão atual foi preservada.",
            L"Nodivu", MB_OK | MB_ICONERROR);
        return 4;
    }
    // An interactive per-user installer is launched only after explicit GUI consent.
    std::wstring command = L"\"" + installer + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(installer.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &child))
        return 5;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return 0;
}
