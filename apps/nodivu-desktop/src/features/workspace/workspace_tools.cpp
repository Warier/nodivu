#include "features/workspace/workspace_tools.hpp"
#include "features/graph/empty_graph_widget.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/graph/mini_map.hpp"
#include "features/library/library_panel.hpp"
#include "features/projects/project_controller.hpp"
#include "security/checked_json.hpp"
#include "ui/controls/action_availability.hpp"
#include "ui/controls/search_text.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include "ui/theme/theme.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QVBoxLayout>

namespace nodivu {
WorkspaceTools::WorkspaceTools(QMainWindow& window, GraphScene& scene, GraphView& view,
                               LibraryPanel& library, ProjectController& projects, AppPaths paths)
    : QObject(&window), window_(window), scene_(scene), view_(view), library_(library),
      projects_(projects), paths_(std::move(paths)),
      settings_(QDir(paths_.profile).filePath(QStringLiteral("workspace.ini")),
                QSettings::IniFormat) {
    auto* edit = window.findChild<QMenu*>(QStringLiteral("editMenu"));
    auto* display = window.findChild<QMenu*>(QStringLiteral("viewMenu"));
    auto* file = window.findChild<QMenu*>(QStringLiteral("fileMenu"));
    const auto action = [&window](QMenu* menu, const QString& text, const char* name,
                                  const QKeySequence& shortcut = {}) {
        auto* result = menu->addAction(text);
        result->setObjectName(QLatin1String(name));
        result->setShortcut(shortcut);
        window.addAction(result);
        return result;
    };
    edit->addSeparator();
    rename_ = action(edit, QStringLiteral("Renomear bloco…"), "renameNode");
    duplicate_ = action(edit, QStringLiteral("Duplicar seleção"), "duplicateSelection");
    remove_ = action(edit, QStringLiteral("Remover seleção"), "removeSelection");
    auto* selection = action(edit, QStringLiteral("Selecionar todos os blocos"), "selectAllNodes");
    auto* align = edit->addMenu(QStringLiteral("Organizar seleção"));
    align->setToolTipsVisible(true);
    align->menuAction()->setToolTip(QStringLiteral(
        "Selecione dois ou mais blocos com Ctrl+clique no cabeçalho ou Shift+arraste no fundo."));
    alignHorizontal_ = action(align, QStringLiteral("Em linha (horizontal)"), "alignHorizontal");
    alignVertical_ = action(align, QStringLiteral("Em coluna (vertical)"), "alignVertical");
    connect(rename_, &QAction::triggered, this, [this] {
        const auto ids = scene_.selectedNodeIds();
        if (ids.size() == 1)
            rename(ids.first());
    });
    connect(&scene_, &GraphScene::renameRequested, this, &WorkspaceTools::rename);
    connect(duplicate_, &QAction::triggered, this, &WorkspaceTools::duplicateSelection);
    connect(&scene_, &GraphScene::duplicateRequested, this, &WorkspaceTools::duplicateSelection);
    connect(remove_, &QAction::triggered, &scene_, &GraphScene::removeSelection);
    connect(selection, &QAction::triggered, &scene_, &GraphScene::selectAllNodes);
    connect(alignHorizontal_, &QAction::triggered, this, [this] { scene_.alignSelection(true); });
    connect(alignVertical_, &QAction::triggered, this, [this] { scene_.alignSelection(false); });
    display->addSeparator();
    auto* find = action(display, QStringLiteral("Localizar bloco no projeto…"), "findProjectNode",
                        QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    connect(find, &QAction::triggered, this, &WorkspaceTools::findNode);
    frameSelection_ = action(display, QStringLiteral("Enquadrar seleção"), "frameSelection");
    connect(frameSelection_, &QAction::triggered, &view_, &GraphView::fitSelection);
    compact_ = action(display, QStringLiteral("Blocos compactos"), "compactNodes");
    compact_->setCheckable(true);
    compact_->setToolTip(QStringLiteral("Recolhe controles e expande o bloco selecionado."));
    connect(compact_, &QAction::toggled, &scene_, &GraphScene::setCompact);
    miniMapToggle_ = action(display, QStringLiteral("Minimapa"), "miniMapToggle");
    miniMapToggle_->setCheckable(true);
    miniMap_ = new MiniMap(view_, scene_);
    if (auto* welcome = window.findChild<EmptyGraphWidget*>())
        welcome->watchOverlay(miniMap_);
    connect(miniMapToggle_, &QAction::toggled, miniMap_, &QWidget::setVisible);
    connect(miniMap_, &MiniMap::closeRequested, this,
            [this] { miniMapToggle_->setChecked(false); });
    view_.addOverlayWidget(miniMap_);
    auto* appearance = display->addMenu(QStringLiteral("Aparência e legibilidade"));
    contrast_ = action(appearance, QStringLiteral("Alto contraste"), "highContrast");
    contrast_->setCheckable(true);
    connect(contrast_, &QAction::toggled, this, [this](bool enabled) {
        Theme::apply(*qApp, enabled);
        scene_.invalidate();
        view_.viewport()->update();
    });
    auto* scale = appearance->addMenu(QStringLiteral("Escala da interface (ao reabrir)"));
    auto* scaleGroup = new QActionGroup(this);
    const int currentScale = settings_.value(QStringLiteral("scale"), 100).toInt();
    for (const int factor : {100, 125, 150, 175, 200}) {
        auto* option = action(scale, QString::number(factor) + QLatin1Char('%'), "interfaceScale");
        option->setObjectName(QStringLiteral("interfaceScale%1").arg(factor));
        option->setCheckable(true);
        option->setChecked(factor == currentScale);
        scaleGroup->addAction(option);
        connect(option, &QAction::triggered, this, [this, factor] {
            settings_.setValue(QStringLiteral("scale"), factor);
            savePreferences();
            if (settings_.status() == QSettings::NoError)
                emit message(QStringLiteral("Escala salva. Feche e reabra o Nodivu para aplicar."));
        });
    }
    file->addSeparator();
    auto* templates = file->addMenu(QStringLiteral("Modelos de cadeias"));
    saveTemplate_ =
        action(templates, QStringLiteral("Salvar seleção como modelo…"), "saveChainTemplate");
    insertTemplate_ = action(templates, QStringLiteral("Inserir modelo…"), "insertChainTemplate");
    const auto directory = QDir(paths_.profile).filePath(QStringLiteral("templates"));
    connect(saveTemplate_, &QAction::triggered, this, [this, directory] {
        const auto file = QFileDialog::getSaveFileName(
            &window_, QStringLiteral("Salvar cadeia · configurações e conexões internas"),
            QDir(directory).filePath(QStringLiteral("Minha cadeia.nodivu-chain.json")),
            QStringLiteral("Modelo Nodivu (*.nodivu-chain.json)"));
        if (!file.isEmpty())
            saveTemplate(file);
    });
    connect(insertTemplate_, &QAction::triggered, this, [this, directory] {
        const auto file = QFileDialog::getOpenFileName(
            &window_,
            QStringLiteral(
                "Inserir cadeia · selecione novamente dispositivos, aplicativos e arquivos"),
            directory, QStringLiteral("Modelo Nodivu (*.nodivu-chain.json)"));
        if (!file.isEmpty())
            insertTemplate(file);
    });
    connect(&scene_, &GraphScene::fragmentInserted, &view_, &GraphView::fitSelection);
    connect(&scene_, &QGraphicsScene::selectionChanged, this, &WorkspaceTools::updateActions);
    auto* session = window.findChild<SessionController*>();
    connect(session, &SessionController::activityChanged, this, &WorkspaceTools::updateActions);
    connect(&projects_, &ProjectController::stateChanged, this, &WorkspaceTools::updateActions);
    saveTimer_.setInterval(400);
    saveTimer_.setSingleShot(true);
    connect(&saveTimer_, &QTimer::timeout, this, &WorkspaceTools::savePreferences);
    splitter_ = window.findChild<QSplitter*>(QStringLiteral("workspaceSplitter"));
    if (splitter_)
        connect(splitter_, &QSplitter::splitterMoved, this, [this] { saveTimer_.start(); });
    window.installEventFilter(this);
    connect(&library_, &LibraryPanel::preferencesChanged, this, [this] {
        if (!restoring_)
            saveTimer_.start();
    });
    for (auto* option : {compact_, miniMapToggle_, contrast_})
        connect(option, &QAction::toggled, this, [this] {
            if (!restoring_)
                saveTimer_.start();
        });
    compact_->setChecked(settings_.value(QStringLiteral("compact"), false).toBool());
    miniMapToggle_->setChecked(settings_.value(QStringLiteral("minimap"), false).toBool());
    contrast_->setChecked(settings_.value(QStringLiteral("contrast"), false).toBool());
    if (Theme::highContrast() != contrast_->isChecked())
        Theme::apply(*qApp, contrast_->isChecked());
    const auto state =
        QJsonDocument::fromJson(settings_.value(QStringLiteral("library")).toByteArray()).object();
    if (!state.isEmpty())
        library_.restoreWorkspaceState(state);
    const auto geometry = settings_.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty() && window.restoreGeometry(geometry))
        window.setProperty("workspaceRestored", true);
    restoring_ = false;
    updateActions();
}
WorkspaceTools::~WorkspaceTools() {
    saveTimer_.stop();
    savePreferences();
}
bool WorkspaceTools::eventFilter(QObject* watched, QEvent* event) {
    if (!restoring_ && (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
                        event->type() == QEvent::WindowStateChange))
        saveTimer_.start();
    return QObject::eventFilter(watched, event);
}
void WorkspaceTools::savePreferences() {
    if (restoring_)
        return;
    settings_.setValue(QStringLiteral("geometry"), window_.saveGeometry());
    settings_.setValue(QStringLiteral("library"),
                       QJsonDocument(library_.workspaceState()).toJson(QJsonDocument::Compact));
    settings_.setValue(QStringLiteral("compact"), compact_->isChecked());
    settings_.setValue(QStringLiteral("minimap"), miniMapToggle_->isChecked());
    settings_.setValue(QStringLiteral("contrast"), contrast_->isChecked());
    settings_.sync();
    if (settings_.status() != QSettings::NoError && !settingsErrorShown_) {
        settingsErrorShown_ = true;
        emit message(QStringLiteral("Não foi possível salvar as preferências da interface. "
                                    "Verifique o acesso à pasta de perfil."));
    }
}
void WorkspaceTools::updateActions() {
    const auto ids = scene_.selectedNodeIds();
    auto* session = window_.findChild<SessionController*>();
    const auto blocked = projects_.busy() || !session->settled()
                             ? QStringLiteral("Aguarde a operação atual ou reconecte o motor.")
                             : QString();
    const auto selectionReason = !blocked.isEmpty() ? blocked
                                 : ids.isEmpty()    ? QStringLiteral("Selecione um ou mais blocos.")
                                                    : QString();
    setActionAvailability(rename_, QStringLiteral("Renomear bloco · F2 no canvas"),
                          !blocked.isEmpty() ? blocked
                          : ids.size() != 1  ? QStringLiteral("Selecione apenas um bloco.")
                                             : QString());
    setActionAvailability(duplicate_, QStringLiteral("Duplicar seleção · Ctrl+D no canvas"),
                          selectionReason);
    setActionAvailability(saveTemplate_,
                          QStringLiteral("Salvar parâmetros e conexões internas; dispositivos, "
                                         "aplicativos e arquivos serão escolhidos novamente."),
                          selectionReason);
    setActionAvailability(
        insertTemplate_,
        QStringLiteral("Inserir uma cadeia salva, com novos identificadores e fontes sem seleção."),
        blocked);
    setActionAvailability(remove_, QStringLiteral("Remover seleção · Delete no canvas"),
                          !blocked.isEmpty() ? blocked
                          : scene_.selectedItems().isEmpty()
                              ? QStringLiteral("Selecione blocos ou conexões.")
                              : QString());
    for (auto* action : {alignHorizontal_, alignVertical_})
        setActionAvailability(
            action,
            action == alignHorizontal_
                ? QStringLiteral("Organizar lado a lado, com espaço entre os blocos.")
                : QStringLiteral("Organizar um abaixo do outro, com espaço entre os blocos."),
            !blocked.isEmpty() ? blocked
            : ids.size() < 2   ? QStringLiteral("Selecione dois ou mais blocos com Ctrl+clique no "
                                                  "cabeçalho ou Shift+arraste no fundo.")
                               : QString());
    setActionAvailability(frameSelection_,
                          QStringLiteral("Centralizar e enquadrar os blocos selecionados"),
                          selectionReason);
}
void WorkspaceTools::rename(const QString& id) {
    if (projects_.busy())
        return;
    QInputDialog dialog(&window_);
    dialog.setObjectName(QStringLiteral("renameNodeDialog"));
    dialog.setWindowTitle(QStringLiteral("Renomear bloco"));
    dialog.setLabelText(QStringLiteral(
        "Nome no projeto (até 80 caracteres). Deixe vazio para usar o nome original."));
    dialog.setTextValue(scene_.nodeTitle(id));
    dialog.setOkButtonText(QStringLiteral("Renomear"));
    if (auto* field = dialog.findChild<QLineEdit*>())
        field->setMaxLength(80);
    if (dialog.exec() == QDialog::Accepted)
        scene_.renameNode(id, dialog.textValue());
}
QPointF WorkspaceTools::insertionPoint() const {
    return view_.mapToScene(view_.availableViewportRect().center());
}
void WorkspaceTools::duplicateSelection() {
    if (projects_.busy() || scene_.selectedNodeIds().isEmpty())
        return;
    QRectF bounds;
    for (const auto& id : scene_.selectedNodeIds())
        bounds = bounds.united(scene_.nodeItem(id)->sceneBoundingRect());
    scene_.insertFragment(scene_.selectedFragment(), bounds.topLeft() + QPointF(48, 48));
}
void WorkspaceTools::saveTemplate(const QString& file) {
    if (projects_.busy() || scene_.selectedNodeIds().isEmpty())
        return;
    const auto result = ProjectRepository::write(file, scene_.selectedFragment());
    if (!result.ok())
        emit message(result.error);
}
void WorkspaceTools::insertTemplate(const QString& file) {
    if (projects_.busy())
        return;
    const auto result = ProjectRepository::read(file);
    if (!result.ok()) {
        emit message(result.error);
        return;
    }
    const auto json = checkedJson(result.contents, ProjectRepository::MaxProjectBytes);
    if (!json.ok()) {
        emit message(QStringLiteral("Não foi possível ler o modelo: ") + json.error);
        return;
    }
    scene_.insertFragment(json.object, insertionPoint());
}
void WorkspaceTools::findNode() {
    QDialog dialog(&window_);
    dialog.setObjectName(QStringLiteral("findProjectNodeDialog"));
    dialog.setWindowTitle(QStringLiteral("Localizar no projeto"));
    dialog.setStyleSheet(studioPanelStyle());
    dialog.resize(520, 390);
    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("projectNodeSearch"));
    search->setPlaceholderText(QStringLiteral("Nome do bloco ou tipo…"));
    search->setAccessibleName(QStringLiteral("Buscar blocos no projeto"));
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("projectNodeResults"));
    list->setAccessibleName(QStringLiteral("Blocos encontrados no projeto"));
    auto* empty = new QLabel(QStringLiteral("Nenhum bloco encontrado."), &dialog);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    layout->addWidget(empty);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Localizar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    layout->addWidget(buttons);
    const auto fill = [this, list, empty, search, buttons] {
        auto* session = window_.findChild<SessionController*>();
        const auto selected =
            list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        list->clear();
        for (const auto& value : session->snapshot()
                                     .value(QStringLiteral("graph"))
                                     .toObject()
                                     .value(QStringLiteral("nodes"))
                                     .toArray()) {
            const auto node = value.toObject();
            const auto id = node.value(QStringLiteral("id")).toString();
            const auto descriptor = session->catalog().describe(node);
            const auto title = scene_.nodeTitle(id);
            if (!matchesSearchText(title + QLatin1Char(' ') + descriptor.title, search->text()))
                continue;
            auto* item =
                new QListWidgetItem(title + QStringLiteral(" · ") + descriptor.title, list);
            item->setData(Qt::UserRole, id);
            item->setToolTip(descriptor.description);
            if (id == selected)
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count())
            list->setCurrentRow(0);
        empty->setVisible(list->count() == 0);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(list->count() > 0);
    };
    connect(search, &QLineEdit::textChanged, &dialog, fill);
    connect(window_.findChild<SessionController*>(), &SessionController::documentEdited, &dialog,
            fill);
    connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(search, &QLineEdit::returnPressed, &dialog, [&dialog, list] {
        if (list->currentItem())
            dialog.accept();
    });
    fill();
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted && list->currentItem()) {
        const auto id = list->currentItem()->data(Qt::UserRole).toString();
        scene_.selectNode(id);
        if (auto* node = scene_.nodeItem(id))
            view_.focusPosition(node->sceneBoundingRect().center());
        view_.setFocus(Qt::ShortcutFocusReason);
    }
}
} // namespace nodivu
