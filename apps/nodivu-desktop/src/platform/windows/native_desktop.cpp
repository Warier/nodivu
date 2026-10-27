#include "platform/windows/native_desktop.hpp"
#include <QByteArray>
#include <QScopeGuard>
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <shobjidl.h>
// clang-format on

namespace nodivu::windows {
namespace {
QByteArray userToken(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
        return {};
    const auto close = qScopeGuard([token] { CloseHandle(token); });
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0 || bytes > 4096)
        return {};
    QByteArray data(static_cast<qsizetype>(bytes), '\0');
    if (!GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes))
        return {};
    return data;
}
} // namespace
void initializeDesktopIdentity() {
    SetCurrentProcessExplicitAppUserModelID(L"org.nodivu.desktop");
}
void applyWindowTheme(quintptr handle) {
    const BOOL dark = TRUE;
    // NOLINTNEXTLINE(performance-no-int-to-ptr): Qt WId encodes this owned HWND as quintptr.
    const auto window = reinterpret_cast<HWND>(handle);
    if (FAILED(DwmSetWindowAttribute(window, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(window, 19, &dark, sizeof(dark));
}
QString legacyProfileConflict() {
    const auto current = userToken(GetCurrentProcess());
    if (current.isEmpty())
        return QStringLiteral("Não foi possível verificar a identidade do perfil Windows.");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return QStringLiteral("Não foi possível verificar a versão anterior em execução.");
    const auto closeSnapshot = qScopeGuard([snapshot] { CloseHandle(snapshot); });
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int inspected = 0;
    for (BOOL found = Process32FirstW(snapshot, &entry); found;
         found = Process32NextW(snapshot, &entry)) {
        if (++inspected > 16384)
            return QStringLiteral("Limite de processos excedido ao verificar o perfil.");
        if (_wcsicmp(entry.szExeFile, L"nodivu-electron.exe") != 0)
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
            continue;
        const auto closeProcess = qScopeGuard([process] { CloseHandle(process); });
        const auto candidate = userToken(process);
        // TokenUser returns naturally aligned TOKEN_USER storage containing a
        // SID pointer into the same buffer. Both buffers outlive EqualSid.
        if (!candidate.isEmpty() &&
            EqualSid(reinterpret_cast<const TOKEN_USER*>(current.constData())->User.Sid,
                     reinterpret_cast<const TOKEN_USER*>(candidate.constData())->User.Sid))
            return QStringLiteral(
                "Encerre a versão Electron antes de abrir o Qt neste perfil. Para avaliar em "
                "paralelo, use a opção portátil com perfil separado.");
    }
    return {};
}
} // namespace nodivu::windows
