#include "platform/windows/confirmation_dialog.hpp"
#include <QDebug>
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

namespace nodivu::windows {
namespace {
constexpr int ContinueEditing = 100;
constexpr int Proceed = 101;
HRESULT CALLBACK validateButton(HWND, UINT message, WPARAM button, LPARAM, LONG_PTR) {
    // Only an explicit, configured choice can authorize discarding edits.
    if (message == TDN_BUTTON_CLICKED && button != ContinueEditing && button != Proceed &&
        button != IDCANCEL)
        return S_FALSE;
    return S_OK;
}
} // namespace
std::optional<bool> showConfirmation(quintptr parent, const QString& message, const QString& cancel,
                                     const QString& proceed) {
    const auto content = message.toStdWString();
    const auto cancelText = cancel.toStdWString();
    const auto proceedText = proceed.toStdWString();
    const TASKDIALOG_BUTTON buttons[]{{ContinueEditing, cancelText.c_str()},
                                      {Proceed, proceedText.c_str()}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hInstance = GetModuleHandleW(nullptr);
    config.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
    const auto owner = reinterpret_cast<HWND>(parent);
    if (owner && IsWindowEnabled(owner)) {
        config.hwndParent = owner;
        config.dwFlags |= TDF_POSITION_RELATIVE_TO_WINDOW;
    }
    config.pszWindowTitle = L"Nodivu";
    config.pszMainIcon = TD_INFORMATION_ICON;
    config.pszContent = content.c_str();
    config.cButtons = 2;
    config.pButtons = buttons;
    config.nDefaultButton = ContinueEditing;
    config.pfCallback = validateButton;
    int selected = IDCANCEL;
    const auto result = TaskDialogIndirect(&config, &selected, nullptr, nullptr);
    if (FAILED(result)) {
        qWarning("Native confirmation failed (HRESULT 0x%08lx); using Qt fallback.",
                 static_cast<unsigned long>(result));
        return std::nullopt;
    }
    return selected == Proceed;
}
} // namespace nodivu::windows
