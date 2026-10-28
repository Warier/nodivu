#include "features/store/store_window.hpp"
#include "features/store/readme_view.hpp"
#include "ui/controls/action_availability.hpp"
#include "ui/controls/message_presentation.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include "ui/theme/theme.hpp"
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <algorithm>

namespace nodivu {
StoreWindow::StoreWindow(StoreController& controller, QWidget* parent)
    : QDialog(parent), controller_(controller) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Plugins — Nodivu"));
    setStyleSheet(studioPanelStyle());
    resize(1000, 680);
    auto* layout = new QVBoxLayout(this);
    auto* title =
        new QLabel(QStringLiteral("Plugins\nDescubra blocos e gerencie sua biblioteca"), this);
    title->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: 600; padding: 8px;"));
    layout->addWidget(title);
    tabs_ = new QTabBar(this);
    tabs_->setAccessibleName(QStringLiteral("Seções de plugins"));
    for (const auto& text :
         {QStringLiteral("Instalados"), QStringLiteral("Explorar"), QStringLiteral("Atualizações")})
        tabs_->addTab(text);
    layout->addWidget(tabs_);
    search_ = new QLineEdit(this);
    search_->setAccessibleName(QStringLiteral("Buscar plugins"));
    search_->setPlaceholderText(QStringLiteral("Buscar por nome, autor ou descrição"));
    layout->addWidget(search_);
    auto* toolbar = new QHBoxLayout;
    auto* folder = new QPushButton(QStringLiteral("Adicionar pasta…"), this);
    auto* zip = new QPushButton(QStringLiteral("Adicionar arquivo…"), this);
    auto* refresh = new QPushButton(QStringLiteral("Atualizar lista"), this);
    auto* clean = new QPushButton(QStringLiteral("Limpar downloads"), this);
    for (auto* button : {folder, zip, clean})
        button->hide();
    auto* install = new QToolButton(this);
    install->setObjectName(QStringLiteral("pluginImportMenu"));
    install->setText(QStringLiteral("Instalar pacote"));
    install->setIcon(studioIcon(StudioIcon::Plus));
    install->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    install->setPopupMode(QToolButton::InstantPopup);
    auto* imports = new QMenu(install);
    for (auto* button : {zip, folder}) {
        auto* option = imports->addAction(button->text());
        connect(option, &QAction::triggered, button, &QPushButton::click);
    }
    install->setMenu(imports);
    toolbar->addWidget(install);
    toolbar->addWidget(refresh);
    auto* utilities = new QToolButton(this);
    utilities->setText(QStringLiteral("Opções"));
    utilities->setPopupMode(QToolButton::InstantPopup);
    auto* utilityMenu = new QMenu(utilities);
    auto* cleanAction = utilityMenu->addAction(clean->text());
    connect(cleanAction, &QAction::triggered, clean, &QPushButton::click);
    utilities->setMenu(utilityMenu);
    toolbar->addWidget(utilities);
    toolbar->addStretch();
    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("pluginsCount"));
    toolbar->addWidget(count_);
    layout->addLayout(toolbar);
    message_ = new QLabel(this);
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    layout->addWidget(message_);
    errorDetails_ = new QToolButton(this);
    errorDetails_->setText(QStringLiteral("Detalhes do diagnóstico"));
    errorDetails_->setObjectName(QStringLiteral("pluginErrorDetails"));
    layout->addWidget(errorDetails_, 0, Qt::AlignLeft);
    connect(errorDetails_, &QToolButton::clicked, this, [this] {
        QMessageBox dialog(QMessageBox::Warning, QStringLiteral("Detalhes do diagnóstico"),
                           friendlyError(controller_.state().error), QMessageBox::Ok, this);
        dialog.setDetailedText(controller_.state().error);
        dialog.exec();
    });
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    list_ = new QTreeWidget(splitter);
    list_->setAccessibleName(QStringLiteral("Plugins disponíveis e instalados"));
    list_->setObjectName(QStringLiteral("pluginsList"));
    list_->setHeaderLabels({QStringLiteral("Plugin / autor"), QStringLiteral("Versão"),
                            QStringLiteral("Estado / consumo")});
    list_->setRootIsDecorated(false);
    list_->setIconSize(QSize(40, 40));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    readme_ = new ReadmeView(splitter);
    readme_->setAccessibleName(QStringLiteral("Documentação do plugin"));
    readme_->setPlaceholderText(
        QStringLiteral("Selecione um plugin e use Documentação para consultar seus detalhes."));
    splitter->addWidget(list_);
    splitter->addWidget(readme_);
    splitter->setSizes({550, 450});
    layout->addWidget(splitter, 1);
    selectionInfo_ = new QLabel(this);
    selectionInfo_->setObjectName(QStringLiteral("pluginSelectionInfo"));
    selectionInfo_->setTextFormat(Qt::PlainText);
    selectionInfo_->setWordWrap(true);
    layout->addWidget(selectionInfo_);
    auto* actions = new QHBoxLayout;
    action_ = new QPushButton(QStringLiteral("Documentação"), this);
    details_ = new QPushButton(QStringLiteral("Documentação"), this);
    enable_ = new QPushButton(QStringLiteral("Ativar / desativar"), this);
    remove_ = new QPushButton(QStringLiteral("Remover / cancelar remoção"), this);
    rollback_ = new QPushButton(QStringLiteral("Retornar versão…"), this);
    cancelUpdate_ = new QPushButton(QStringLiteral("Cancelar atualização"), this);
    action_->setObjectName(QStringLiteral("pluginAction"));
    details_->setObjectName(QStringLiteral("pluginDetails"));
    enable_->setObjectName(QStringLiteral("pluginEnable"));
    remove_->setObjectName(QStringLiteral("pluginRemove"));
    rollback_->setObjectName(QStringLiteral("pluginRollback"));
    cancelUpdate_->setObjectName(QStringLiteral("pluginCancelUpdate"));
    action_->setProperty("role", QStringLiteral("primary"));
    actions->addWidget(action_);
    actions->addWidget(details_);
    for (auto* button : {enable_, remove_, rollback_, cancelUpdate_})
        button->hide();
    more_ = new QToolButton(this);
    more_->setObjectName(QStringLiteral("pluginMoreActions"));
    more_->setText(QStringLiteral("Mais opções"));
    more_->setIcon(studioIcon(StudioIcon::More));
    more_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more_->setPopupMode(QToolButton::InstantPopup);
    auto* secondaryMenu = new QMenu(more_);
    for (auto* button : {remove_, rollback_, cancelUpdate_}) {
        auto* option = secondaryMenu->addAction(button->text());
        secondary_.append({option, button});
        connect(option, &QAction::triggered, button, &QPushButton::click);
    }
    more_->setMenu(secondaryMenu);
    actions->addStretch();
    actions->addWidget(more_);
    layout->addLayout(actions);
    progress_ = new QProgressBar(this);
    progress_->setAccessibleName(QStringLiteral("Progresso da operação"));
    cancel_ = new QPushButton(QStringLiteral("Cancelar operação"), this);
    layout->addWidget(progress_);
    layout->addWidget(cancel_);
    auto* note = new QLabel(
        QStringLiteral("Ativação, atualização e remoção são aplicadas ao reabrir. Plugins nativos "
                       "executam no backend com seus direitos; assinatura não fornece sandbox."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    connect(&controller_, &StoreController::changed, this, &StoreWindow::render);
    connect(&controller_, &StoreController::readmeReady, readme_, &ReadmeView::showContent);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        render();
        if (index == 0)
            controller_.refreshInstalled();
        else
            controller_.refresh(false);
    });
    connect(search_, &QLineEdit::textChanged, this, &StoreWindow::render);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &StoreWindow::updateSelection);
    connect(folder, &QPushButton::clicked, this, [this] {
        if (controller_.busy())
            return;
        const auto path =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Pasta do plugin CLAP"));
        if (!path.isEmpty() &&
            QMessageBox::question(this, QStringLiteral("Confiar no pacote"),
                                  QStringLiteral("Instalar código nativo desta pasta? Confirme "
                                                 "somente se você confia no autor.")) ==
                QMessageBox::Yes)
            controller_.importFolder(path);
    });
    connect(zip, &QPushButton::clicked, this, [this] {
        if (controller_.busy())
            return;
        const auto path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Adicionar pacote"), {},
                                         QStringLiteral("Plugin (*.zip *.nodivu-plugin)"));
        if (!path.isEmpty() &&
            QMessageBox::question(this, QStringLiteral("Confiar no pacote"),
                                  QStringLiteral("Instalar código nativo deste arquivo? Confirme "
                                                 "somente se você confia no autor.")) ==
                QMessageBox::Yes)
            controller_.importZip(path);
    });
    connect(refresh, &QPushButton::clicked, this, [this] {
        if (tabs_->currentIndex() == 0)
            controller_.refreshInstalled();
        else
            controller_.refresh();
    });
    connect(clean, &QPushButton::clicked, &controller_, &StoreController::cleanDownloads);
    connect(action_, &QPushButton::clicked, this, &StoreWindow::selectedAction);
    connect(details_, &QPushButton::clicked, this, [this] {
        if (!list_->selectedItems().isEmpty())
            controller_.details(list_->selectedItems().first()->data(0, Qt::UserRole).toString(),
                                tabs_->currentIndex() == 0);
    });
    connect(enable_, &QPushButton::clicked, this, [this] {
        if (!list_->selectedItems().isEmpty()) {
            auto* item = list_->selectedItems().first();
            controller_.setEnabled(item->data(0, Qt::UserRole).toString(),
                                   !item->data(0, Qt::UserRole + 1).toBool());
        }
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (!list_->selectedItems().isEmpty()) {
            auto* item = list_->selectedItems().first();
            controller_.markRemoved(item->data(0, Qt::UserRole).toString(),
                                    !item->data(0, Qt::UserRole + 2).toBool());
        }
    });
    connect(rollback_, &QPushButton::clicked, this, [this] {
        if (!list_->selectedItems().isEmpty())
            controller_.listVersions(
                list_->selectedItems().first()->data(0, Qt::UserRole).toString());
    });
    connect(&controller_, &StoreController::versionsReady, this,
            [this](const QString& key, const QVector<PackageRecord>& versions) {
                if (versions.isEmpty()) {
                    QMessageBox::information(
                        this, QStringLiteral("Versões arquivadas"),
                        QStringLiteral("Não há versões anteriores arquivadas para este pacote."));
                    return;
                }
                QStringList labels;
                for (const auto& version : versions)
                    labels.append(version.version + QStringLiteral(" · ") + version.key);
                bool accepted = false;
                const auto selected =
                    QInputDialog::getItem(this, QStringLiteral("Retornar versão"),
                                          QStringLiteral("Versão a ativar na próxima abertura:"),
                                          labels, 0, false, &accepted);
                if (accepted && labels.contains(selected))
                    controller_.rollback(key, versions[labels.indexOf(selected)].key);
            });
    connect(cancelUpdate_, &QPushButton::clicked, this, [this] {
        if (!list_->selectedItems().isEmpty())
            controller_.cancelUpdate(
                list_->selectedItems().first()->data(0, Qt::UserRole).toString());
    });
    connect(cancel_, &QPushButton::clicked, &controller_, &StoreController::cancel);
    progressTimer_.setInterval(250);
    connect(&progressTimer_, &QTimer::timeout, this, &StoreWindow::renderProgress);
    progressTimer_.start();
    render();
}
void StoreWindow::render() {
    const auto& state = controller_.state();
    const auto query = search_->text().trimmed();
    QString selected;
    if (!list_->selectedItems().isEmpty())
        selected = list_->selectedItems().first()->data(0, Qt::UserRole).toString();
    const QSignalBlocker blocker(list_);
    list_->clear();
    const bool local = tabs_->currentIndex() == 0;
    const bool updates = tabs_->currentIndex() == 2;
    for (auto* button : {enable_, remove_, rollback_, cancelUpdate_})
        button->hide();
    more_->setVisible(local);
    action_->setText(local ? QStringLiteral("Documentação")
                           : QStringLiteral("Instalar / atualizar"));
    details_->setVisible(true);
    if (local) {
        for (const auto& package : state.installed) {
            if (!query.isEmpty() &&
                !(package.title + package.author + package.id).contains(query, Qt::CaseInsensitive))
                continue;
            const auto status = state.packageStatus.value(package.key);
            QStringList labels;
            labels.append(package.removed   ? QStringLiteral("Remoção pendente")
                          : package.enabled ? QStringLiteral("Habilitado")
                                            : QStringLiteral("Desativado"));
            if (!status.pendingVersion.isEmpty())
                labels.append(QStringLiteral("Versão %1 agendada").arg(status.pendingVersion));
            if (status.pending)
                labels.append(QStringLiteral("Aguardando reinício"));
            if (!package.error.isEmpty())
                labels.append(QStringLiteral("Falha no manifesto: ") + package.error);
            if (!status.loadError.isEmpty())
                labels.append(QStringLiteral("Indisponível na última abertura: ") +
                              status.loadError);
            const auto kind = package.manifest.contains(QStringLiteral("library"))
                                  ? QStringLiteral("CLAP nativo")
                                  : QStringLiteral("Worker");
            labels.append(kind + QStringLiteral(" · ") +
                          (PackageService::isProtected(package.key)
                               ? QStringLiteral("Pacote do app")
                               : QStringLiteral("Instalação local")));
            auto* item =
                new QTreeWidgetItem(list_, {package.title + QLatin1Char('\n') + package.author,
                                            package.version, labels.join(QLatin1Char('\n'))});
            item->setIcon(0, studioIcon(status.pending                    ? StudioIcon::Warning
                                        : package.enabled && package.ok() ? StudioIcon::Check
                                                                          : StudioIcon::Plugin,
                                        status.pending                    ? QColor(240, 212, 155)
                                        : package.enabled && package.ok() ? QColor(112, 217, 189)
                                                                          : QColor(170, 182, 199)));
            item->setData(0, Qt::UserRole, package.key);
            item->setData(0, Qt::UserRole + 1, package.enabled);
            item->setData(0, Qt::UserRole + 2, package.removed);
            if (package.key == selected)
                item->setSelected(true);
        }
    } else {
        for (const auto& value : state.catalog) {
            const auto entry = value.toObject();
            const auto id = entry.value(QStringLiteral("id")).toString();
            if (!query.isEmpty() && !(entry.value(QStringLiteral("title")).toString() +
                                      entry.value(QStringLiteral("author")).toString() +
                                      entry.value(QStringLiteral("description")).toString())
                                         .contains(query, Qt::CaseInsensitive))
                continue;
            const auto presentation = catalogPresentation(entry, state);
            if (updates && !presentation.updateAvailable)
                continue;
            auto title = entry.value(QStringLiteral("title")).toString() + QLatin1Char('\n') +
                         entry.value(QStringLiteral("author")).toString();
            if (entry.value(QStringLiteral("authenticated_official")).toBool())
                title += QStringLiteral(" · Oficial Nodivu");
            auto* item = new QTreeWidgetItem(
                list_,
                {title, entry.value(QStringLiteral("version")).toString(),
                 presentation.action + QStringLiteral(" · ") +
                     resourceTierText(entry.value(QStringLiteral("resource_tier")).toString()) +
                     QStringLiteral(" · %1 MiB")
                         .arg(entry.value(QStringLiteral("download_bytes")).toDouble() /
                                  (1024 * 1024),
                              0, 'f', 1)});
            item->setData(0, Qt::UserRole, id);
            if (state.logos.contains(id))
                item->setIcon(0, QIcon(QPixmap::fromImage(state.logos.value(id))));
            item->setData(0, Qt::UserRole + 3, entry.value(QStringLiteral("version")).toString());
            item->setToolTip(0, presentation.detail);
            if (id == selected)
                item->setSelected(true);
        }
    }
    message_->setText(
        !state.error.isEmpty()     ? friendlyError(state.error)
        : controller_.busy()       ? QStringLiteral("Operação em andamento…")
        : !state.message.isEmpty() ? state.message
        : state.online
            ? QStringLiteral("Catálogo autenticado")
            : QStringLiteral("Consulta local/offline · atualize para instalar pela loja"));
    errorDetails_->setVisible(!state.error.isEmpty());
    message_->setStyleSheet(
        QStringLiteral("color: %1;")
            .arg(state.error.isEmpty() ? Theme::muted().name() : Theme::warningText().name()));
    message_->setProperty("role", state.error.isEmpty() ? QStringLiteral("info")
                                                        : QStringLiteral("warning"));
    if (!state.warnings.isEmpty())
        message_->setText(message_->text() + QLatin1Char('\n') +
                          state.warnings.join(QLatin1Char('\n')));
    int enabled = 0, pending = 0;
    for (const auto& package : state.installed) {
        enabled += package.enabled && !package.removed && package.ok() ? 1 : 0;
        pending += state.packageStatus.value(package.key).pending ? 1 : 0;
    }
    count_->setText(local ? QStringLiteral("%1/8 tipos habilitados · %2 aguardando reinício")
                                .arg(enabled)
                                .arg(pending)
                          : QStringLiteral("%1 plugins").arg(list_->topLevelItemCount()));
    updateSelection();
    list_->resizeColumnToContents(0);
    renderProgress();
}
void StoreWindow::updateSelection() {
    const auto& state = controller_.state();
    const bool local = tabs_->currentIndex() == 0;
    const auto selected = list_->selectedItems();
    const auto key =
        selected.isEmpty() ? QString() : selected.first()->data(0, Qt::UserRole).toString();
    const auto blocked = controller_.busy()   ? QStringLiteral("Aguarde a operação atual.")
                         : selected.isEmpty() ? QStringLiteral("Selecione um plugin.")
                                              : QString();
    for (auto* button : {action_, details_, enable_, remove_, rollback_, cancelUpdate_})
        setActionAvailability(button, button->text(), blocked);
    selectionInfo_->clear();
    if (local && selected.isEmpty())
        action_->setText(QStringLiteral("Selecione um plugin"));
    if (!local) {
        for (const auto& value : state.catalog) {
            const auto entry = value.toObject();
            if (entry.value(QStringLiteral("id")).toString() != key)
                continue;
            const auto presentation = catalogPresentation(entry, state);
            action_->setText(presentation.action);
            setActionAvailability(action_, presentation.action,
                                  blocked.isEmpty() ? presentation.reason : blocked);
            selectionInfo_->setText(presentation.detail);
        }
        synchronizeSecondaryActions();
        return;
    }
    for (const auto& package : state.installed) {
        if (package.key != key)
            continue;
        const auto status = state.packageStatus.value(key);
        enable_->setText(package.enabled ? QStringLiteral("Desativar") : QStringLiteral("Ativar"));
        remove_->setText(package.removed ? QStringLiteral("Cancelar remoção")
                                         : QStringLiteral("Remover…"));
        int active = 0;
        for (const auto& p : state.installed)
            active += p.ok() && p.enabled && !p.removed ? 1 : 0;
        setActionAvailability(enable_, enable_->text(),
                              !blocked.isEmpty() ? blocked
                              : package.removed
                                  ? QStringLiteral("Cancele a remoção antes de alterar a ativação.")
                              : !package.enabled && !package.ok()
                                  ? QStringLiteral("Corrija o manifesto antes de ativar.")
                              : !package.enabled && active >= 8
                                  ? QStringLiteral("Até oito tipos podem ficar habilitados.")
                                  : QString());
        setActionAvailability(
            remove_, remove_->text(),
            !blocked.isEmpty() ? blocked
            : PackageService::isProtected(key)
                ? QStringLiteral("Este pacote acompanha o app. Você pode desativá-lo.")
            : !status.pendingVersion.isEmpty()
                ? QStringLiteral("Cancele a atualização antes de remover.")
                : QString());
        setActionAvailability(
            rollback_, rollback_->text(),
            !blocked.isEmpty() ? blocked
            : !status.pendingVersion.isEmpty()
                ? QStringLiteral("Cancele a troca pendente antes de retornar versão.")
            : !status.hasPrevious ? QStringLiteral("Não há versões anteriores arquivadas.")
                                  : QString());
        setActionAvailability(cancelUpdate_, cancelUpdate_->text(),
                              !blocked.isEmpty() ? blocked
                              : status.pendingVersion.isEmpty()
                                  ? QStringLiteral("Não há atualização pendente.")
                                  : QString());
        selectionInfo_->setText(selected.first()->text(2));
        action_->setText(enable_->text());
        setActionAvailability(action_, action_->text(),
                              enable_->isEnabled() ? QString() : enable_->statusTip());
    }
    synchronizeSecondaryActions();
}
void StoreWindow::synchronizeSecondaryActions() {
    for (const auto& pair : secondary_) {
        pair.first->setText(pair.second->text());
        pair.first->setEnabled(pair.second->isEnabled());
        pair.first->setToolTip(pair.second->toolTip());
    }
    more_->setEnabled(!list_->selectedItems().isEmpty() && !controller_.busy());
}
void StoreWindow::renderProgress() {
    progress_->setVisible(controller_.busy());
    cancel_->setVisible(controller_.busy());
    cancel_->setEnabled(!controller_.committing());
    const auto operation = controller_.progress();
    if (!operation)
        return;
    const auto total = operation->expected.load();
    const auto received = operation->received.load();
    progress_->setRange(0, total > 0 ? 100 : 0);
    if (total > 0)
        progress_->setValue(static_cast<int>(std::min<qint64>(100, received * 100 / total)));
    if (controller_.committing())
        progress_->setFormat(QStringLiteral("Instalando · etapa final"));
    else
        progress_->setFormat(QStringLiteral("%1 / %2 KiB").arg(received / 1024).arg(total / 1024));
}
void StoreWindow::selectedAction() {
    if (!action_->isEnabled() || list_->selectedItems().isEmpty())
        return;
    auto* item = list_->selectedItems().first();
    if (tabs_->currentIndex() == 0) {
        enable_->click();
    } else if (QMessageBox::question(
                   this, QStringLiteral("Confiar no publicador"),
                   QStringLiteral(
                       "Instalar %1, versão %2? Código nativo executa com os direitos do backend.")
                       .arg(item->text(0), item->data(0, Qt::UserRole + 3).toString())) ==
               QMessageBox::Yes) {
        controller_.install(item->data(0, Qt::UserRole).toString(),
                            item->data(0, Qt::UserRole + 3).toString());
    }
}
} // namespace nodivu
