#pragma once
#include "ui/controls/search_text.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include <QAction>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
namespace nodivu {
inline void showCommandPalette(QWidget* window, QMenuBar* menus) {
    struct Command {
        QString label;
        QPointer<QAction> action;
    };
    QList<Command> commands;
    const auto append = [&](auto&& self, QMenu* menu, const QString& prefix) -> void {
        for (auto* action : menu->actions()) {
            if (action->isSeparator() || !action->isVisible())
                continue;
            auto title = action->text();
            title.remove(QLatin1Char('&'));
            if (action->menu())
                self(self, action->menu(), prefix + title + QStringLiteral(" › "));
            else if (action->objectName() != QStringLiteral("commandPalette"))
                commands.append({prefix + title, action});
        }
    };
    for (auto* action : menus->actions())
        if (action->menu()) {
            auto title = action->text();
            title.remove(QLatin1Char('&'));
            append(append, action->menu(), title + QStringLiteral(" › "));
        }
    QDialog dialog(window);
    dialog.setWindowTitle(QStringLiteral("Buscar comando"));
    dialog.setObjectName(QStringLiteral("commandPaletteDialog"));
    dialog.setStyleSheet(studioPanelStyle());
    dialog.resize(620, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("commandSearch"));
    search->setAccessibleName(QStringLiteral("Buscar comando"));
    search->setPlaceholderText(
        QStringLiteral("O que deseja fazer? Ex.: salvar, escuta, dispositivos…"));
    layout->addWidget(search);
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(QStringLiteral("Comandos disponíveis"));
    layout->addWidget(list);
    const auto filter = [&] {
        list->clear();
        for (qsizetype i = 0; i < commands.size(); ++i) {
            const auto& c = commands[i];
            if (!c.action || !matchesSearchText(c.label, search->text()))
                continue;
            auto* row = new QListWidgetItem(
                c.label + (c.action->shortcut().isEmpty()
                               ? QString()
                               : QStringLiteral("    ") +
                                     c.action->shortcut().toString(QKeySequence::NativeText)),
                list);
            row->setData(Qt::UserRole, i);
            row->setToolTip(c.action->toolTip());
            if (!c.action->isEnabled())
                row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
        }
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->flags().testFlag(Qt::ItemIsEnabled)) {
                list->setCurrentRow(i);
                break;
            }
        if (list->count() == 0) {
            auto* row = new QListWidgetItem(QStringLiteral("Nenhum comando encontrado."), list);
            row->setFlags(Qt::NoItemFlags);
        }
    };
    QPointer<QAction> chosen;
    const auto activate = [&] {
        auto* row = list->currentItem();
        if (!row || !row->flags().testFlag(Qt::ItemIsEnabled))
            return;
        const auto index = row->data(Qt::UserRole).toInt();
        if (index >= 0 && index < commands.size()) {
            chosen = commands[index].action;
            dialog.accept();
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&] { filter(); });
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, activate);
    QObject::connect(list, &QListWidget::itemActivated, &dialog, [&] { activate(); });
    auto* next = new QAction(&dialog);
    next->setShortcut(QKeySequence(Qt::Key_Down));
    dialog.addAction(next);
    QObject::connect(next, &QAction::triggered, &dialog, [&] {
        list->setFocus();
        if (list->currentRow() < 0)
            list->setCurrentRow(0);
    });
    filter();
    search->setFocus();
    dialog.exec();
    if (chosen && chosen->isEnabled())
        QTimer::singleShot(0, chosen, &QAction::trigger);
}
} // namespace nodivu
