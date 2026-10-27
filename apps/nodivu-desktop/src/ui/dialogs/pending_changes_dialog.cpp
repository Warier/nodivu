#include "ui/dialogs/pending_changes_dialog.hpp"
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>
#ifdef Q_OS_WIN
#include "platform/windows/confirmation_dialog.hpp"
#endif

namespace nodivu {
PendingChangesText pendingChangesText(PendingChangesAction action) {
    switch (action) {
    case PendingChangesAction::Open:
        return {QStringLiteral("Abrir outro projeto e descartar alterações não salvas?"),
                QStringLiteral("Cancelar"), QStringLiteral("Abrir outro")};
    case PendingChangesAction::Restore:
        return {QStringLiteral("Restaurar a cópia e descartar as alterações abertas?"),
                QStringLiteral("Cancelar"), QStringLiteral("Restaurar cópia")};
    case PendingChangesAction::Close:
        return {QStringLiteral("Fechar e descartar alterações não salvas?"),
                QStringLiteral("Continuar editando"), QStringLiteral("Descartar e fechar")};
    }
    return {};
}
bool confirmPendingChanges(QWidget* parent, PendingChangesAction action) {
    const auto text = pendingChangesText(action);
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        const auto result = windows::showConfirmation(parent ? parent->window()->winId() : 0,
                                                      text.message, text.cancel, text.proceed);
        if (result.has_value())
            return *result;
    }
#endif
    // Portable/offscreen fallback keeps the same choices and safe default.
    QMessageBox dialog(QMessageBox::Information, QStringLiteral("Nodivu"), text.message,
                       QMessageBox::NoButton, parent);
    dialog.setObjectName(QStringLiteral("pendingChangesDialog"));
    auto* cancel = dialog.addButton(text.cancel, QMessageBox::RejectRole);
    auto* proceed = dialog.addButton(text.proceed, QMessageBox::DestructiveRole);
    cancel->setObjectName(QStringLiteral("pendingChangesCancel"));
    proceed->setObjectName(QStringLiteral("pendingChangesProceed"));
    dialog.setDefaultButton(cancel);
    dialog.setEscapeButton(cancel);
    dialog.exec();
    return dialog.clickedButton() == proceed;
}
} // namespace nodivu
