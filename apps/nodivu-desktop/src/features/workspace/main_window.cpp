#include "features/workspace/main_window.hpp"
#include "contracts/playback_state.hpp"
#include "features/graph/empty_graph_widget.hpp"
#include "features/graph/graph_scene.hpp"
#include "features/graph/graph_view.hpp"
#include "features/graph/routing_panel.hpp"
#include "features/inspector/inspector_panel.hpp"
#include "features/library/library_panel.hpp"
#include "features/plugins/package_service.hpp"
#include "features/projects/project_controller.hpp"
#include "features/store/store_controller.hpp"
#include "features/store/store_window.hpp"
#include "features/updates/update_window.hpp"
#include "features/workspace/audio_status_panel.hpp"
#include "features/workspace/system_panel.hpp"
#include "platform/profile_backup.hpp"
#include "ui/controls/action_availability.hpp"
#include "ui/controls/command_palette.hpp"
#include "ui/controls/feedback_banner.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include "ui/theme/theme.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDeadlineTimer>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <cmath>

namespace nodivu {
MainWindow::MainWindow(const AppPaths& paths, QWidget* parent)
    : QMainWindow(parent), paths_(paths) {
    setWindowTitle(QStringLiteral("Nodivu — Estúdio"));
    resize(1440, 900);
    setMinimumSize(960, 650);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QWidget(central);
    header->setObjectName(QStringLiteral("studioHeader"));
    header->setFixedHeight(56);
    header->setStyleSheet(QStringLiteral("background: %1;").arg(Theme::headerBackground().name()));
    auto* tools = new QHBoxLayout(header);
    tools->setContentsMargins(18, 8, 18, 8);
    tools->setSpacing(12);
    auto* brandGroup = new QWidget(header);
    auto* brandLayout = new QHBoxLayout(brandGroup);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(8);
    auto* logo = new QLabel(QStringLiteral("n"), brandGroup);
    logo->setFixedSize(30, 30);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral("color: #70d9bd; background: #25433e; border-radius: 9px; "
                                       "font-size: 27px;"));
    brandLayout->addWidget(logo);
    auto* brand = new QLabel(QStringLiteral("NODIVU"), brandGroup);
    brand->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 700; letter-spacing: 3px;"));
    brandLayout->addWidget(brand);
    tools->addWidget(brandGroup);
    auto* separator = new QFrame(header);
    separator->setObjectName(QStringLiteral("projectHeaderSeparator"));
    separator->setFixedSize(1, 31);
    separator->setStyleSheet(QStringLiteral("background: #2b333f;"));
    tools->addWidget(separator, 0, Qt::AlignVCenter);
    auto* projectHeader = new QWidget(header);
    auto* projectLayout = new QVBoxLayout(projectHeader);
    projectLayout->setContentsMargins(2, 0, 0, 0);
    projectLayout->setSpacing(5);
    projectLayout->setAlignment(Qt::AlignVCenter);
    projectTitle_ = new QLabel(QStringLiteral("Projeto sem título"), projectHeader);
    projectTitle_->setObjectName(QStringLiteral("projectTitle"));
    projectTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    projectTitle_->setTextFormat(Qt::PlainText);
    projectTitle_->setStyleSheet(
        QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 500;"));
    projectLayout->addWidget(projectTitle_);
    auto* subtitle = new QLabel(QStringLiteral("Áudio contínuo · edição ao vivo"), projectHeader);
    subtitle->setObjectName(QStringLiteral("projectSubtitle"));
    subtitle->setStyleSheet(QStringLiteral("color: #95a1b2; font-size: 11px; font-weight: 400;"));
    projectState_ = subtitle;
    projectLayout->addWidget(subtitle);
    tools->addWidget(projectHeader, 1);
    // Keep the safety control directly reachable while administrative actions live in menus.
    auto* mute = new QPushButton(QStringLiteral("Silenciar tudo"), header);
    mute->setObjectName(QStringLiteral("muteAll"));
    mute->setCheckable(true);
    tools->addWidget(mute);
    layout->addWidget(header);

    auto* menus = new QMenuBar(central);
    menus->setObjectName(QStringLiteral("studioMenuBar"));
    menus->setAccessibleName(QStringLiteral("Menus do Estúdio"));
    menus->setNativeMenuBar(false);
    menus->setStyleSheet(QStringLiteral(
        "QMenuBar { background: #151c24; color: #aab6c7; border-bottom: 1px solid #2b333f; "
        "padding: 3px 12px; font-size: 12px; spacing: 4px; }"
        "QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 4px; }"
        "QMenuBar::item:selected { background: #26313e; color: #e4e8ee; }"
        "QMenuBar::item:pressed { background: #25433e; color: #70d9bd; }"));
    const auto menu = [menus](const QString& title, const char* name, QMenu* parent = nullptr) {
        auto* result = parent ? parent->addMenu(title) : menus->addMenu(title);
        result->setObjectName(QLatin1String(name));
        result->setToolTipsVisible(true);
        return result;
    };
    const auto action = [this](QMenu* parent, const QString& title, const char* name,
                               const QKeySequence& shortcut = {}) {
        auto* result = parent->addAction(title);
        result->setObjectName(QLatin1String(name));
        result->setShortcut(shortcut);
        // Register on the window too: shortcuts remain active while menus are closed.
        addAction(result);
        return result;
    };
    auto* fileMenu = menu(QStringLiteral("&Arquivo"), "fileMenu");
    auto* open =
        action(fileMenu, QStringLiteral("&Abrir projeto…"), "projectOpen", QKeySequence::Open);
    auto* save =
        action(fileMenu, QStringLiteral("&Salvar projeto"), "projectSave", QKeySequence::Save);
    open_ = open;
    save_ = save;
    saveAs_ = action(fileMenu, QStringLiteral("Salvar projeto &como…"), "projectSaveAs",
                     QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    recents_ = menu(QStringLiteral("Projetos &recentes"), "projectRecents", fileMenu);
    fileMenu->addSeparator();
    auto* folder = action(fileMenu, QStringLiteral("Abrir &pasta de projetos"), "projectFolder");
    setActionAvailability(folder, QStringLiteral("Abrir a pasta padrão de projetos"));
    auto* projectPanel = action(fileMenu, QStringLiteral("Recuperação e informações do projeto"),
                                "projectInformation");
    fileMenu->addSeparator();
    auto* quit = action(fileMenu, QStringLiteral("Sai&r"), "quitApplication",
                        QKeySequence(QStringLiteral("Alt+F4")));
    connect(quit, &QAction::triggered, this, [this] { close(); });

    auto* editMenu = menu(QStringLiteral("&Editar"), "editMenu");
    undo_ = action(editMenu, QStringLiteral("&Desfazer"), "editUndo", QKeySequence::Undo);
    redo_ = action(editMenu, QStringLiteral("&Refazer"), "editRedo",
                   QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    undo_->setIcon(studioIcon(StudioIcon::Undo));
    redo_->setIcon(studioIcon(StudioIcon::Redo));
    auto* viewMenu = menu(QStringLiteral("&Exibir"), "viewMenu");
    settings_ = action(viewMenu, QStringLiteral("Painel &Sistema"), "settingsToggle",
                       QKeySequence(QStringLiteral("Ctrl+,")));
    settings_->setCheckable(true);
    auto* searchBlocks = action(viewMenu, QStringLiteral("Buscar &blocos…"), "searchBlocks",
                                QKeySequence(QStringLiteral("Ctrl+F")));
    auto* palette = action(viewMenu, QStringLiteral("Buscar &comando…"), "commandPalette",
                           QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    connect(palette, &QAction::triggered, this, [this, menus] { showCommandPalette(this, menus); });
    auto* inspect =
        action(viewMenu, QStringLiteral("&Inspetor do bloco selecionado"), "inspectSelected");
    inspect_ = inspect;
    viewMenu->addSeparator();
    auto* organize = action(viewMenu, QStringLiteral("&Organizar blocos"), "organizeGraph");
    organize_ = organize;

    auto* audioMenu = menu(QStringLiteral("Á&udio"), "audioMenu");
    listen_ = action(audioMenu, QStringLiteral("&Escuta local…"), "listenShortcut");
    virtualOutput_ = action(audioMenu, QStringLiteral("Configurar &microfone virtual"),
                            "configureVirtualOutput");
    auto* devicesMenu = menu(QStringLiteral("&Dispositivos"), "devicesMenu", audioMenu);
    refreshDevices_ =
        action(devicesMenu, QStringLiteral("&Atualizar lista de dispositivos"), "refreshDevices");
    retryDevices_ = action(devicesMenu, QStringLiteral("&Reabrir dispositivos e preparar áudio"),
                           "retryDevices");
    audioMenu->addSeparator();
    muteAction_ = action(audioMenu, QStringLiteral("Silenciar tudo"), "muteAllAction");
    muteAction_->setCheckable(true);
    connect(muteAction_, &QAction::triggered, mute, &QPushButton::click);
    connect(mute, &QPushButton::toggled, muteAction_, &QAction::setChecked);

    auto* pluginsMenu = menu(QStringLiteral("&Plugins"), "pluginsMenu");
    managePlugins_ = action(pluginsMenu, QStringLiteral("&Gerenciar plugins…"), "managePlugins");
    connect(managePlugins_, &QAction::triggered, this, &MainWindow::storeRequested);

    auto* helpMenu = menu(QStringLiteral("A&juda"), "helpMenu");
    auto* applicationPanel =
        action(helpMenu, QStringLiteral("Atualizações e diagnóstico…"), "applicationInformation");
    auto* shortcuts = action(helpMenu, QStringLiteral("&Atalhos e navegação"), "keyboardHelp");
    connect(shortcuts, &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, QStringLiteral("Atalhos e navegação"),
            QStringLiteral(
                "Ctrl+O · Abrir projeto\nCtrl+S · Salvar projeto\nCtrl+Shift+S · Salvar como\n"
                "Ctrl+Z · Desfazer\nCtrl+Shift+Z · Refazer\nCtrl+F · Buscar blocos\nCtrl+Shift+P · "
                "Buscar comando\n"
                "Ctrl+, · Abrir ou fechar Sistema\nCtrl+Shift+R · Roteamento por teclado\n\n"
                "Alt · Acessar os menus; setas navegam e Esc fecha.\n"
                "Espaço+arraste · Mover pelo projeto\nCtrl+roda · Zoom\n"
                "Shift+roda · Mover para os lados\n"
                "Botão direito em um fio · Opções da conexão"));
    });
    connect(viewMenu, &QMenu::aboutToShow, this, &MainWindow::updateActions);
    layout->addWidget(menus);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setHandleWidth(4);
    splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: #242d37; }"
                                           "QSplitter::handle:hover { background: #70d9bd; }"));
    library_ = new LibraryPanel(splitter);
    auto* workspace = new QWidget(splitter);
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);
    recoveryBanner_ = new QWidget(workspace);
    auto* recoveryLayout = new QHBoxLayout(recoveryBanner_);
    recoveryLayout->addWidget(
        new QLabel(QStringLiteral("Projeto não salvo encontrado"), recoveryBanner_), 1);
    auto* restore = new QPushButton(QStringLiteral("Restaurar cópia"), recoveryBanner_);
    auto* discard = new QPushButton(QStringLiteral("Descartar cópia…"), recoveryBanner_);
    recoveryLayout->addWidget(restore);
    recoveryLayout->addWidget(discard);
    recoveryBanner_->hide();
    workspaceLayout->addWidget(recoveryBanner_);
    audioStatus_ = new AudioStatusPanel(workspace);
    workspaceLayout->addWidget(audioStatus_);
    connect(audioStatus_, &AudioStatusPanel::actionRequested, this, &MainWindow::handleAudioAction);
    auto* toolbarWidget = new QWidget(workspace);
    auto* toolbar = new QGridLayout(toolbarWidget);
    toolbar->setContentsMargins(12, 5, 12, 5);
    auto* area = new QWidget(workspace);
    auto* areaLayout = new QHBoxLayout(area);
    areaLayout->setContentsMargins(0, 0, 0, 0);
    areaLayout->setSpacing(7);
    auto* dot = new QFrame(area);
    dot->setObjectName(QStringLiteral("projectAreaDot"));
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(QStringLiteral("background: #70d9bd; border-radius: 3px;"));
    areaLayout->addWidget(dot);
    areaLayout->addWidget(new QLabel(QStringLiteral("Área de projeto"), area));
    areaLayout->addStretch();
    toolbar->addWidget(area, 0, 0);
    auto* history = new QWidget(toolbarWidget);
    auto* historyLayout = new QHBoxLayout(history);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(2);
    const auto historyButton = [history, historyLayout](QAction* action, const QString& name,
                                                        const QString& label) {
        auto* button = new QToolButton(history);
        button->setObjectName(name);
        button->setAccessibleName(label);
        button->setFixedSize(32, 32);
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; border: 1px solid transparent; "
            "border-radius: 5px; padding: 0; }"
            "QToolButton:hover { background: #26313e; border-color: #3d4b5a; }"
            "QToolButton:pressed { background: #25433e; }"
            "QToolButton:focus { border: 2px solid #70d9bd; }"
            "QToolButton:disabled { background: transparent; border-color: transparent; }"));
        setActionAvailability(button, label);
        // Share the menu action so busy/history state and shortcuts stay consistent.
        button->setDefaultAction(action);
        connect(action, &QAction::changed, button,
                [button, action] { button->setAccessibleDescription(action->statusTip()); });
        historyLayout->addWidget(button);
    };
    historyButton(undo_, QStringLiteral("canvasUndo"), QStringLiteral("Desfazer"));
    historyButton(redo_, QStringLiteral("canvasRedo"), QStringLiteral("Refazer"));
    toolbar->addWidget(history, 0, 1, Qt::AlignCenter);
    auto* viewControls = new QWidget(toolbarWidget);
    auto* viewControlsLayout = new QHBoxLayout(viewControls);
    viewControlsLayout->setContentsMargins(0, 0, 0, 0);
    auto* fit = new QPushButton(QStringLiteral("Enquadrar"), workspace);
    auto* minus = new QPushButton(QStringLiteral("−"), workspace);
    auto* reset = new QPushButton(QStringLiteral("100%"), workspace);
    auto* plus = new QPushButton(QStringLiteral("+"), workspace);
    minus->setIcon(studioIcon(StudioIcon::Minus));
    minus->setText({});
    plus->setIcon(studioIcon(StudioIcon::Plus));
    plus->setText({});
    minus->setMinimumSize(32, 32);
    plus->setMinimumSize(32, 32);
    zoomIn_ = plus;
    zoomOut_ = minus;
    zoomReset_ = reset;
    fit_ = fit;
    minus->setAccessibleName(QStringLiteral("Diminuir zoom"));
    plus->setAccessibleName(QStringLiteral("Aumentar zoom"));
    reset->setAccessibleName(QStringLiteral("Restaurar zoom para 100%"));
    reset->setObjectName(QStringLiteral("zoomReset"));
    for (auto* button : {fit, minus, reset, plus})
        viewControlsLayout->addWidget(button);
    toolbar->addWidget(viewControls, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    // Equal side columns keep history centered independently of the zoom controls' width.
    const int sideWidth = std::max(area->sizeHint().width(), viewControls->sizeHint().width());
    for (const int column : {0, 2}) {
        toolbar->setColumnMinimumWidth(column, sideWidth);
        toolbar->setColumnStretch(column, 1);
    }
    workspaceLayout->addWidget(toolbarWidget);
    scene_ = new GraphScene(session_, this);
    view_ = new GraphView(scene_, workspace);
    connect(scene_, &GraphScene::addAtRequested, view_, &GraphView::blockDropped);
    emptyGraph_ = new EmptyGraphWidget(view_->viewport());
    workspaceLayout->addWidget(view_, 1);
    error_ = new FeedbackBanner(workspace);
    error_->hide();
    workspaceLayout->addWidget(error_);
    auto* footer = new QFrame(workspace);
    footer->setObjectName(QStringLiteral("projectFooter"));
    footer->setMinimumHeight(26);
    footer->setStyleSheet(QStringLiteral(
        "QFrame#projectFooter { background: #151c24; border-top: 1px solid #2b333f; }"
        "QLabel { background: transparent; color: #8b9bb3; font-size: 11px; border: 0; }"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 5, 14, 5);
    footerLayout->setSpacing(12);
    status_ =
        new QLabel(QStringLiteral("Arraste o fundo para navegar · Ctrl + roda: zoom · Shift + "
                                  "roda: mover para os lados · Botão direito no fio: opções"),
                   footer);
    status_->setObjectName(QStringLiteral("connectionHint"));
    status_->setWordWrap(true);
    status_->setToolTip(QStringLiteral("Botão direito no fio: opções"));
    revision_ = new QLabel(QStringLiteral("Revisão 0"), footer);
    revision_->setObjectName(QStringLiteral("projectRevision"));
    footerLayout->addWidget(status_, 1);
    footerLayout->addWidget(revision_, 0, Qt::AlignRight | Qt::AlignVCenter);
    workspaceLayout->addWidget(footer);
    splitter->addWidget(library_);
    splitter->addWidget(workspace);
    splitter->setCollapsible(0, false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({Theme::LibraryWidth, 1200});
    layout->addWidget(splitter, 1);
    setCentralWidget(central);
    inspector_ = new InspectorPanel(session_, workspace);
    view_->setOverlayWidget(inspector_);
    auto* routingDock = new QDockWidget(QStringLiteral("Roteamento por teclado"), this);
    routingDock_ = routingDock;
    routingDock->setObjectName(QStringLiteral("routingDock"));
    routingDock->setStyleSheet(
        studioPanelStyle() +
        QStringLiteral("QDockWidget { background: #19222c; border: 1px solid #384353; }"));
    auto* routingHeading = new QWidget(routingDock);
    routingHeading->setStyleSheet(QStringLiteral("background: #26313e;"));
    auto* routingHeadingLayout = new QHBoxLayout(routingHeading);
    routingHeadingLayout->setContentsMargins(14, 10, 10, 10);
    auto* routingTitle = new QLabel(QStringLiteral("Roteamento"), routingHeading);
    routingTitle->setProperty("role", QStringLiteral("heading"));
    routingHeadingLayout->addWidget(routingTitle, 1);
    auto* routingClose = new QPushButton(routingHeading);
    routingClose->setIcon(studioIcon(StudioIcon::Close));
    routingClose->setProperty("role", QStringLiteral("close"));
    routingClose->setFixedSize(28, 28);
    routingClose->setToolTip(QStringLiteral("Fechar roteamento"));
    routingClose->setAccessibleName(routingClose->toolTip());
    routingHeadingLayout->addWidget(routingClose);
    routingDock->setTitleBarWidget(routingHeading);
    connect(routingClose, &QPushButton::clicked, routingDock, &QWidget::hide);
    auto* routing = new RoutingPanel(session_, routingDock);
    routingDock->setWidget(routing);
    addDockWidget(Qt::RightDockWidgetArea, routingDock);
    routingDock->hide();
    auto* routingAction = routingDock->toggleViewAction();
    routing_ = routingAction;
    routingAction->setObjectName(QStringLiteral("routingToggle"));
    routingAction->setText(QStringLiteral("&Roteamento por teclado"));
    routingAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    viewMenu->insertAction(inspect, routingAction);
    addAction(routingAction);
    connect(routing, &RoutingPanel::inspectRequested, this, [this](const QString& id) {
        system_->hide();
        scene_->selectNode(id);
        inspector_->inspect(id);
        inspector_->setSavedPath(projects_->resourcePath(id));
        if (!scene_->selectedItems().isEmpty())
            view_->ensureVisible(scene_->selectedItems().first());
        for (auto* field : inspector_->findChildren<QWidget*>()) {
            if (field->property("field").isValid() && field->isEnabled() && field->isVisible() &&
                field->focusPolicy() != Qt::NoFocus) {
                field->setFocus(Qt::ShortcutFocusReason);
                break;
            }
        }
    });
    projects_ = new ProjectController(session_, *scene_, *view_, paths_, this);
    system_ = new SystemPanel(session_, *projects_, paths_, central);
    view_->addOverlayWidget(system_);
    emptyGraph_->watchOverlay(system_);
    connect(system_, &SystemPanel::shown, inspector_, &QWidget::hide);
    connect(searchBlocks, &QAction::triggered, library_, &LibraryPanel::focusSearch);
    connect(projectPanel, &QAction::triggered, system_, &SystemPanel::openProject);
    connect(applicationPanel, &QAction::triggered, system_, &SystemPanel::openApplication);
    connect(saveAs_, &QAction::triggered, projects_, &ProjectController::saveAs);
    connect(undo_, &QAction::triggered, projects_, &ProjectController::undo);
    connect(redo_, &QAction::triggered, projects_, &ProjectController::redo);
    connect(system_, &SystemPanel::storeRequested, this, &MainWindow::storeRequested);
    connect(system_, &SystemPanel::discardRecoveryRequested, this, &MainWindow::discardRecovery);
    connect(system_, &SystemPanel::error, this, [this](const QString& message) {
        error_->setText(message);
        error_->show();
    });
    connect(system_, &SystemPanel::installRequested, this,
            [this](const QString& file, const QString& hash) {
                if (projects_->busy() || !session_.settled())
                    return;
                installer_ = file;
                installerHash_ = hash;
                if (!close() && !closing_) {
                    installer_.clear();
                    installerHash_.clear();
                }
            });
    connect(emptyGraph_, &EmptyGraphWidget::startRequested, this, [this] {
        if (!projects_->busy() && session_.settled()) {
            starterPending_ = true;
            session_.addInitialNodes();
        }
    });
    connect(emptyGraph_, &EmptyGraphWidget::sourceRequested, this,
            [this](const QString& kind, const QString& plugin) {
                if (projects_->busy())
                    return;
                for (const auto& descriptor : session_.catalog().entries()) {
                    if (descriptor.kind == kind && descriptor.pluginId == plugin) {
                        starterPending_ = true;
                        session_.addInitialNodes(descriptor);
                        return;
                    }
                }
                error_->setText(QStringLiteral(
                    "A fonte escolhida não está disponível. Confira os plugins instalados."));
                error_->show();
            });
    connect(this, &MainWindow::openRequested, this, [this] {
        if (open_->isEnabled())
            projects_->open();
    });
    connect(this, &MainWindow::saveRequested, this, [this] {
        if (save_->isEnabled())
            projects_->save();
    });
    connect(projects_, &ProjectController::error, this, [this](const QString& message) {
        error_->setText(message);
        error_->show();
    });
    connect(projects_, &ProjectController::stateChanged, this, [this] {
        const bool enabled = !projects_->busy();
        view_->setEnabled(enabled);
        library_->setEnabled(enabled);
        library_->setVirtualOutputEnabled(enabled && session_.online() && !virtualOutputPending_);
        inspector_->setEnabled(enabled);
        emptyGraph_->setReady(enabled && session_.settled());
        inspector_->setSavedPath(projects_->resourcePath(inspector_->selectedId()));
        scene_->setSavedResources(projects_->savedResources());
        projectTitle_->setText((projects_->path().isEmpty()
                                    ? QStringLiteral("Projeto sem título")
                                    : QFileInfo(projects_->path()).fileName()) +
                               (projects_->dirty() ? QStringLiteral(" •") : QString()));
        projectTitle_->setToolTip(projects_->path().isEmpty() ? QStringLiteral("Projeto sem título")
                                                              : projects_->path());
        projectState_->setText(
            projects_->dirty()            ? QStringLiteral("Alterações não salvas")
            : projects_->path().isEmpty() ? QStringLiteral("Projeto local · ainda não salvo")
            : projects_->savedAt().isValid()
                ? QStringLiteral("Salvo às %1")
                      .arg(projects_->savedAt().toLocalTime().toString(QStringLiteral("HH:mm")))
                : QStringLiteral("Projeto salvo"));
        projectState_->setStyleSheet(projects_->dirty()
                                         ? QStringLiteral("color: #f0d49b; font-size: 11px;")
                                         : QStringLiteral("color: #aab6c7; font-size: 11px;"));
        recoveryBanner_->setVisible(projects_->recoveryPending());
        updateRecents();
        updateActions();
        updateZoomControls();
    });
    connect(recents_, &QMenu::aboutToShow, this, &MainWindow::updateRecents);
    updateRecents();
    connect(restore, &QPushButton::clicked, projects_, &ProjectController::restoreRecovery);
    connect(discard, &QPushButton::clicked, this, &MainWindow::discardRecovery);
    recoveryBanner_->setVisible(projects_->recoveryPending());

    connect(library_, &LibraryPanel::widthRequested, this, [splitter](int width) {
        splitter->setSizes({width, splitter->width() - splitter->handleWidth() - width});
    });
    connect(library_, &LibraryPanel::virtualOutputRequested, this,
            &MainWindow::configureVirtualOutput);
    connect(library_, &LibraryPanel::addRequested, &session_, &SessionController::addNode);
    connect(refreshDevices_, &QAction::triggered, &session_, &SessionController::refreshDevices);
    connect(retryDevices_, &QAction::triggered, this, [this] { projects_->prepareAudio(); });
    connect(virtualOutput_, &QAction::triggered, this, &MainWindow::configureVirtualOutput);
    connect(&session_, &SessionController::catalogChanged, this, [this] {
        library_->setCatalog(session_.catalog());
        emptyGraph_->setCatalog(session_.catalog());
    });
    library_->setCatalog(session_.catalog());
    emptyGraph_->setCatalog(session_.catalog());
    connect(&session_, &SessionController::changed, this, &MainWindow::updateStatus);
    connect(&session_, &SessionController::devicesChanged, this, &MainWindow::updateStatus);
    connect(&session_, &SessionController::activityChanged, this, [this] {
        updateActions();
        updateZoomControls();
        arrangeStarter();
    });
    connect(&session_, &SessionController::error, this, [this](const QString& message) {
        starterPending_ = false;
        error_->setText(message);
        error_->show();
    });
    connect(scene_, &GraphScene::inspectRequested, this, [this](const QString& id) {
        system_->hide();
        inspector_->inspect(id);
        inspector_->setSavedPath(projects_->resourcePath(id));
    });
    connect(inspector_, &InspectorPanel::reloadRequested, this, [this](const QString& id) {
        const auto file = projects_->resourcePath(id);
        if (!file.isEmpty() && session_.online() && !session_.busy())
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("load")},
                              {QStringLiteral("path"), file}});
    });
    connect(scene_, &GraphScene::nodeActionRequested, this, &MainWindow::nodeAction);
    connect(&session_, &SessionController::nodeAdded, this, [this](const QString& id) {
        scene_->selectNode(id);
        // Wait for inline controls, the inspector and explicit drop coordinates to settle.
        QTimer::singleShot(0, this, [this, id] {
            const bool dropped = droppedNodes_.remove(id) != 0;
            if (!closing_)
                view_->revealNode(scene_->nodeItem(id), !dropped);
        });
    });
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        updateActions();
        for (auto* item : scene_->selectedItems()) {
            const auto* node = dynamic_cast<NodeItem*>(item);
            if (node && !node->needsInspector() && node->nodeId() != inspector_->selectedId())
                inspector_->hide();
        }
    });
    connect(view_, &GraphView::blockDropped, this,
            [this](const QString& kind, const QString& plugin, const QPointF& point) {
                if (!session_.settled())
                    return;
                auto connection = std::make_shared<QMetaObject::Connection>();
                *connection = connect(&session_, &SessionController::nodeAdded, scene_,
                                      [this, point, connection](const QString& id) {
                                          droppedNodes_.insert(id);
                                          scene_->placeNode(id, point);
                                          disconnect(*connection);
                                      });
                // A rejected insertion must not move a later, unrelated node.
                QTimer::singleShot(6000, scene_,
                                   [connection] { QObject::disconnect(*connection); });
                session_.addNode(kind, plugin);
            });
    connect(inspect, &QAction::triggered, this, [this] {
        system_->hide();
        for (auto* item : scene_->selectedItems())
            if (const auto* node = dynamic_cast<NodeItem*>(item)) {
                inspector_->inspect(node->nodeId());
                inspector_->setSavedPath(projects_->resourcePath(node->nodeId()));
                return;
            }
    });
    connect(organize, &QAction::triggered, scene_, &GraphScene::organize);
    connect(fit, &QPushButton::clicked, view_, &GraphView::fitNodes);
    connect(plus, &QPushButton::clicked, this, [this] { view_->zoomBy(1.2); });
    connect(minus, &QPushButton::clicked, this, [this] { view_->zoomBy(1 / 1.2); });
    connect(reset, &QPushButton::clicked, this,
            [this] { view_->zoomBy(1 / view_->transform().m11()); });
    connect(view_, &GraphView::zoomChanged, this, &MainWindow::updateZoomControls);
    updateZoomControls();
    connect(folder, &QAction::triggered, this, [this] {
        if (!QDir().mkpath(paths_.projects) ||
            !QDesktopServices::openUrl(QUrl::fromLocalFile(paths_.projects))) {
            error_->setText(QStringLiteral("Não foi possível abrir a pasta de projetos."));
            error_->show();
        }
    });
    connect(open, &QAction::triggered, this, &MainWindow::openRequested);
    connect(save, &QAction::triggered, this, &MainWindow::saveRequested);
    connect(this, &MainWindow::storeRequested, this, [this] {
        if (store_)
            store_->refreshInstalled();
        if (auto* existing = findChild<StoreWindow*>()) {
            existing->showNormal();
            existing->raise();
            existing->activateWindow();
            return;
        }
        if (store_) {
            auto* window = new StoreWindow(*store_, this);
            window->show();
        }
    });
    connect(settings_, &QAction::triggered, this, &MainWindow::showSettings);
    connect(listen_, &QAction::triggered, system_, &SystemPanel::openMonitor);
    connect(mute, &QPushButton::toggled, this, [this](bool enabled) {
        if (!mute_->isEnabled())
            return;
        session_.command(QStringLiteral("audio.mute"), {{QStringLiteral("muted"), enabled}});
        updateActions();
    });
    mute_ = mute;
    connect(&session_, &SessionController::changed, this, [this] {
        QSignalBlocker blocker(mute_);
        mute_->setChecked(session_.snapshot().value(QStringLiteral("muted")).toBool());
        mute_->setText(mute_->isChecked() ? QStringLiteral("Restaurar som")
                                          : QStringLiteral("Silenciar tudo"));
        muteAction_->setChecked(mute_->isChecked());
        muteAction_->setText(mute_->text());
    });
    updateActions();
    connect(&backend_, &BackendClient::stopped, this, [this] {
        shutdownComplete_ = true;
        if (closing_)
            close();
        else
            updateStatus();
    });
    if (!QDir().mkpath(paths.profile)) {
        error_->setText(QStringLiteral("Não foi possível criar o perfil."));
        error_->show();
    } else if (!QFileInfo::exists(paths.backend)) {
        error_->setText(QStringLiteral("Backend não encontrado: ") + paths.backend);
        error_->show();
    } else {
        auto* startup = new QFutureWatcher<PackageResult>(this);
        connect(startup, &QFutureWatcher<PackageResult>::finished, this, [this, startup, paths] {
            const auto result = startup->result();
            startup->deleteLater();
            if (!result.ok()) {
                error_->setText(result.error);
                error_->show();
                return;
            }
            if (!result.message.isEmpty()) {
                error_->setText(result.message);
                error_->show();
            }
            if (closing_)
                return;
            store_ = new StoreController(paths, this);
            backend_.start(paths.backend, paths.backendEnvironment());
        });
        startup->setFuture(QtConcurrent::run([paths] {
            const auto backupError = prepareQtProfileBackup(paths.profile);
            if (!backupError.isEmpty())
                return PackageResult{backupError, {}};
            PackageService packages(paths.plugins);
            auto result = packages.prepareBundled(paths.bundledPlugins);
            if (!result.ok())
                return result;
            result = packages.migrateLegacy(paths.bundledPlugins);
            if (!result.ok())
                return result;
            auto pending = packages.applyPending();
            if (pending.ok() && !result.message.isEmpty())
                pending.message += QLatin1Char('\n') + result.message;
            return pending;
        }));
    }
}

MainWindow::~MainWindow() {
    // Child controls may release gesture leases during destruction. Destroy them
    // before the session/backend members they reference, not in QObject's base dtor.
    const auto storeWindows = findChildren<StoreWindow*>();
    for (auto* window : storeWindows)
        delete window;
    const auto updateWindows = findChildren<UpdateWindow*>();
    for (auto* window : updateWindows)
        delete window;
    const auto docks = findChildren<QDockWidget*>();
    for (auto* dock : docks)
        delete dock;
    delete projects_;
    projects_ = nullptr;
    delete takeCentralWidget();
    delete scene_;
    scene_ = nullptr;
    delete store_;
    store_ = nullptr;
}
void MainWindow::configureVirtualOutput() {
    if (virtualOutputPending_ || !session_.online() || projects_->busy() || closing_)
        return;
    virtualOutputPending_ = true;
    updateStatus();
    auto* retry = new QTimer(this);
    retry->setInterval(40);
    const auto finish = [this, retry] {
        retry->stop();
        retry->deleteLater();
        virtualOutputPending_ = false;
        updateStatus();
    };
    connect(retry, &QTimer::timeout, this,
            [this, finish, deadline = QDeadlineTimer(6000), projectPath = projects_->path(),
             requested = false]() mutable {
                if (closing_ || !session_.online()) {
                    finish();
                    return;
                }
                if (deadline.hasExpired()) {
                    error_->setText(QStringLiteral("Não foi possível preparar a saída a tempo. "
                                                   "Tente novamente."));
                    error_->show();
                    finish();
                    return;
                }
                if (projects_->busy())
                    return;
                if (projects_->path() != projectPath) {
                    finish();
                    return;
                }
                // Flush queued field edits before using the current graph revision.
                if (!session_.settled())
                    return;
                QString output;
                for (const auto& value : session_.snapshot()
                                             .value(QStringLiteral("graph"))
                                             .toObject()
                                             .value(QStringLiteral("nodes"))
                                             .toArray()) {
                    const auto node = value.toObject();
                    if (node.value(QStringLiteral("block"))
                            .toObject()
                            .value(QStringLiteral("kind"))
                            .toString() == QStringLiteral("output")) {
                        output = node.value(QStringLiteral("id")).toString();
                        break;
                    }
                }
                if (output.isEmpty() && !requested) {
                    requested = true;
                    session_.addNode(QStringLiteral("output"));
                    return;
                }
                finish();
                // A failed insertion already reports its cause through SessionController.
                if (output.isEmpty())
                    return;
                inspector_->hide();
                scene_->selectNode(output);
                view_->revealNode(scene_->nodeItem(output));
            });
    retry->start();
}

void MainWindow::arrangeStarter() {
    if (!starterPending_ || !session_.settled() || closing_)
        return;
    const auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    const auto nodes = graph.value(QStringLiteral("nodes")).toArray();
    if (nodes.size() != 2 || graph.value(QStringLiteral("edges")).toArray().size() != 1)
        return;
    QString source, output;
    for (const auto& value : nodes) {
        const auto node = value.toObject();
        const auto id = node.value(QStringLiteral("id")).toString();
        if (node.value(QStringLiteral("block"))
                .toObject()
                .value(QStringLiteral("kind"))
                .toString() == QStringLiteral("output"))
            output = id;
        else
            source = id;
    }
    starterPending_ = false;
    // Run after inline controls and the regular insertion/reveal callbacks settle.
    QTimer::singleShot(0, this, [this, source, output] {
        auto* from = scene_->nodeItem(source);
        auto* to = scene_->nodeItem(output);
        if (closing_ || !from || !to)
            return;
        const auto available = view_->availableViewportRect();
        const auto center = view_->mapToScene(available.center());
        constexpr qreal gap = 64;
        const qreal height = std::max(from->boundingRect().height(), to->boundingRect().height());
        const QPointF origin(center.x() - (2.0 * Theme::NodeWidth + gap) / 2,
                             center.y() - height / 2);
        scene_->placeNode(source, origin);
        scene_->placeNode(output, origin + QPointF(Theme::NodeWidth + gap, 0));
        if (!available.contains(view_->mapFromScene(from->sceneBoundingRect()).boundingRect()) ||
            !available.contains(view_->mapFromScene(to->sceneBoundingRect()).boundingRect())) {
            view_->fitNodes();
            if (view_->transform().m11() > 1)
                view_->zoomBy(1 / view_->transform().m11());
        }
    });
}

void MainWindow::updateRecents() {
    const auto files = projects_->recents();
    if (recents_->property("files").toStringList() != files || recents_->isEmpty()) {
        recents_->clear();
        for (const auto& file : files) {
            auto label = QFileInfo(file).fileName();
            label.replace(QLatin1Char('&'), QStringLiteral("&&"));
            auto* recent = recents_->addAction(label);
            recent->setData(file);
            recent->setToolTip(file.toHtmlEscaped());
            // Same filenames are distinguished by their containing folder.
            for (const auto& other : files)
                if (other != file && QFileInfo(other).fileName() == QFileInfo(file).fileName()) {
                    auto folder = QFileInfo(file).absolutePath();
                    folder.replace(QLatin1Char('&'), QStringLiteral("&&"));
                    recent->setText(label + QStringLiteral(" — ") + folder);
                    break;
                }
            connect(recent, &QAction::triggered, this, [this, file] {
                if (open_->isEnabled())
                    projects_->open(file);
            });
        }
        if (files.isEmpty()) {
            auto* empty = recents_->addAction(QStringLiteral("Nenhum projeto recente"));
            empty->setEnabled(false);
        }
        recents_->setProperty("files", files);
    }
}

void MainWindow::updateZoomControls() {
    const bool hasNodes = !session_.snapshot()
                               .value(QStringLiteral("graph"))
                               .toObject()
                               .value(QStringLiteral("nodes"))
                               .toArray()
                               .isEmpty();
    const double scale = view_->transform().m11();
    const auto blocked =
        closing_                         ? QStringLiteral("O aplicativo está sendo encerrado.")
        : projects_ && projects_->busy() ? QStringLiteral("Aguarde a operação atual do projeto.")
        : !hasNodes ? QStringLiteral("Adicione um bloco para ajustar a visualização.")
                    : QString();
    view_->setZoomEnabled(blocked.isEmpty());
    zoomReset_->setText(QString::number(qRound(scale * 100)) + QLatin1Char('%'));
    setActionAvailability(zoomIn_, QStringLiteral("Aumentar zoom"),
                          !blocked.isEmpty() ? blocked
                          : scale >= 2.0 - .000001
                              ? QStringLiteral("O zoom já está no máximo de 200%.")
                              : QString());
    setActionAvailability(zoomOut_, QStringLiteral("Diminuir zoom"),
                          !blocked.isEmpty() ? blocked
                          : scale <= .1 + .000001
                              ? QStringLiteral("O zoom já está no mínimo de 10%.")
                              : QString());
    setActionAvailability(zoomReset_, QStringLiteral("Restaurar zoom para 100%"),
                          !blocked.isEmpty() ? blocked
                          : std::abs(scale - 1) < .000001
                              ? QStringLiteral("O zoom já está em 100%.")
                              : QString());
    setActionAvailability(fit_, QStringLiteral("Enquadrar todos os blocos sem movê-los"), blocked);
}

void MainWindow::updateActions() {
    if (!projects_ || !mute_)
        return;
    const auto reason = closing_             ? QStringLiteral("O aplicativo está sendo encerrado.")
                        : !session_.online() ? QStringLiteral("O motor de áudio está desconectado.")
                        : projects_->busy() || !session_.settled()
                            ? QStringLiteral("Aguarde a operação atual do projeto.")
                            : QString();
    const bool hasNodes = !session_.snapshot()
                               .value(QStringLiteral("graph"))
                               .toObject()
                               .value(QStringLiteral("nodes"))
                               .toArray()
                               .isEmpty();
    const auto graphReason = !reason.isEmpty() ? reason
                             : !hasNodes       ? QStringLiteral("Adicione um bloco ao projeto.")
                                               : QString();
    bool selected = false;
    for (auto* item : scene_->selectedItems())
        selected |= dynamic_cast<NodeItem*>(item) != nullptr;
    setActionAvailability(open_, QStringLiteral("Abrir projeto e preparar o áudio · Ctrl+O"),
                          reason);
    setActionAvailability(save_, QStringLiteral("Salvar projeto · Ctrl+S"), reason);
    setActionAvailability(
        saveAs_, QStringLiteral("Salvar uma cópia em outro arquivo · Ctrl+Shift+S"), reason);
    setActionAvailability(undo_, QStringLiteral("Desfazer a última edição · Ctrl+Z"),
                          !reason.isEmpty()      ? reason
                          : projects_->canUndo() ? QString()
                                                 : QStringLiteral("Não há edição para desfazer."));
    setActionAvailability(redo_, QStringLiteral("Refazer a edição desfeita · Ctrl+Shift+Z"),
                          !reason.isEmpty()      ? reason
                          : projects_->canRedo() ? QString()
                                                 : QStringLiteral("Não há edição para refazer."));
    setActionAvailability(recents_->menuAction(), QStringLiteral("Abrir um projeto recente"),
                          !reason.isEmpty() ? reason
                          : projects_->recents().isEmpty()
                              ? QStringLiteral("Nenhum projeto recente disponível.")
                              : QString());
    setActionAvailability(inspect_, QStringLiteral("Mostrar configurações do bloco selecionado"),
                          !graphReason.isEmpty() ? graphReason
                          : !selected            ? QStringLiteral("Selecione um bloco no grafo.")
                                                 : QString());
    setActionAvailability(organize_, QStringLiteral("Organizar a posição dos blocos"), graphReason);
    setActionAvailability(routing_, QStringLiteral("Blocos e conexões por teclado · Ctrl+Shift+R"),
                          graphReason);
    if (auto* panel = routingDock_->findChild<RoutingPanel*>())
        panel->setBlockedReason(reason);
    const auto closingReason =
        closing_ ? QStringLiteral("O aplicativo está sendo encerrado.") : QString();
    setActionAvailability(settings_, QStringLiteral("Abrir ou fechar o painel Sistema"),
                          closingReason);
    setActionAvailability(listen_, QStringLiteral("Ir à Escuta local no painel Sistema"),
                          closingReason);
    settings_->setChecked(system_->isVisible());
    setActionAvailability(managePlugins_, QStringLiteral("Abrir a loja e gerenciar plugins"),
                          !closingReason.isEmpty() ? closingReason
                          : !store_ ? QStringLiteral("Aguarde a preparação dos plugins.")
                                    : QString());
    setActionAvailability(refreshDevices_, QStringLiteral("Detectar os dispositivos disponíveis"),
                          !reason.isEmpty() ? reason
                          : session_.devicesRefreshing()
                              ? QStringLiteral("A detecção está em andamento.")
                              : QString());
    setActionAvailability(
        retryDevices_, QStringLiteral("Reabrir dispositivos e preparar arquivos salvos"),
        !closingReason.isEmpty() ? closingReason
        : projects_->busy()      ? QStringLiteral("Aguarde a operação atual do projeto.")
        : !session_.online()     ? QStringLiteral("O motor de áudio está desconectado.")
                                 : QString());
    setActionAvailability(virtualOutput_,
                          QStringLiteral("Criar ou revelar a saída para configurar o destino"),
                          !reason.isEmpty()       ? reason
                          : virtualOutputPending_ ? QStringLiteral("Aguarde a preparação da saída.")
                                                  : QString());
    const bool running = session_.snapshot().value(QStringLiteral("engine_state")).toString() ==
                         QStringLiteral("running");
    const bool muted = session_.snapshot().value(QStringLiteral("muted")).toBool();
    setActionAvailability(mute_,
                          muted ? QStringLiteral("Restaurar o som do projeto")
                                : QStringLiteral("Silenciar todo o áudio do projeto"),
                          !reason.isEmpty() ? reason
                          : !running && !muted
                              ? QStringLiteral("Não há áudio ativo para silenciar.")
                              : QString());
    muteAction_->setEnabled(mute_->isEnabled());
    muteAction_->setToolTip(mute_->toolTip());
    const auto presentation = audioState(session_.snapshot(), session_.devices(), session_.online(),
                                         session_.deviceIssue());
    const auto audioReason =
        presentation.action == AudioAction::Reconnect
            ? (projects_->busy() ? QStringLiteral("Aguarde a operação atual do projeto.")
               : !shutdownComplete_
                   ? QStringLiteral("Aguarde a inicialização ou o encerramento do motor.")
                   : QString())
        : presentation.action == AudioAction::Refresh && session_.devicesRefreshing()
            ? QStringLiteral("A detecção de dispositivos está em andamento.")
            : reason;
    audioStatus_->synchronize(presentation, audioReason);
}

void MainWindow::handleAudioAction() {
    const auto state = audioState(session_.snapshot(), session_.devices(), session_.online(),
                                  session_.deviceIssue());
    if (projects_->busy() || closing_)
        return;
    if (state.action == AudioAction::Reconnect) {
        if (shutdownComplete_) {
            shutdownComplete_ = false;
            backend_.start(paths_.backend, paths_.backendEnvironment());
            updateActions();
        }
        return;
    }
    if (!session_.settled())
        return;
    switch (state.action) {
    case AudioAction::Output:
        configureVirtualOutput();
        break;
    case AudioAction::Retry:
        projects_->prepareAudio();
        break;
    case AudioAction::Refresh:
        session_.refreshDevices();
        break;
    case AudioAction::Unmute:
        session_.command(QStringLiteral("audio.mute"), {{QStringLiteral("muted"), false}});
        break;
    default:
        break;
    }
}

void MainWindow::updateStatus() {
    updateZoomControls();
    updateActions();
    library_->setVirtualOutputEnabled(session_.online() && !projects_->busy() &&
                                      !virtualOutputPending_);
    const auto snapshot = session_.snapshot();
    emptyGraph_->setVisible(snapshot.value(QStringLiteral("graph"))
                                .toObject()
                                .value(QStringLiteral("nodes"))
                                .toArray()
                                .isEmpty());
    emptyGraph_->setReady(session_.settled() && !projects_->busy());
    revision_->setText(
        QStringLiteral("Revisão %1").arg(snapshot.value(QStringLiteral("revision")).toInteger()));
    const auto error = snapshot.value(QStringLiteral("last_error"))
                           .toObject()
                           .value(QStringLiteral("message"))
                           .toString();
    if (!error.isEmpty()) {
        if (error != lastSnapshotError_) {
            error_->setText(error);
            error_->show();
        }
    } else if (!lastSnapshotError_.isEmpty() && error_->text() == lastSnapshotError_) {
        error_->clear();
        error_->hide();
    }
    lastSnapshotError_ = error;
}

void MainWindow::nodeAction(const QString& id, const QString& action, const QPoint& position) {
    if (action == QStringLiteral("reload")) {
        const auto path = projects_->resourcePath(id);
        if (!path.isEmpty() && session_.online() && !session_.busy())
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("load")},
                              {QStringLiteral("path"), path}});
    } else if (action == QStringLiteral("restart")) {
        if (session_.online() && !session_.busy())
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("restart")}});
    } else if (action == QStringLiteral("file")) {
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("Escolher áudio"), {},
                                                       QStringLiteral("Áudio (*.mp3 *.wav *.m4a)"));
        if (!file.isEmpty())
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("load")},
                              {QStringLiteral("path"), file}});
    } else if (action == QStringLiteral("play")) {
        const auto presentation =
            playbackState(session_.snapshot(), session_.node(id), session_.devices(),
                          session_.online(), session_.busy(), projects_->resourcePath(id));
        const bool transport = session_.catalog().describe(session_.node(id)).fileTransport;
        if (!presentation.canPlay &&
            !(presentation.playing && transport && session_.online() && !session_.busy())) {
            inspector_->inspect(id);
            inspector_->setSavedPath(projects_->resourcePath(id));
            return;
        }
        const auto runtime = session_.snapshot()
                                 .value(QStringLiteral("plugin_states"))
                                 .toObject()
                                 .value(id)
                                 .toObject();
        session_.command(
            QStringLiteral("plugin.command"),
            {{QStringLiteral("node_id"), id},
             {QStringLiteral("action"), runtime.value(QStringLiteral("status")).toInt() == 3
                                            ? QStringLiteral("pause")
                                            : QStringLiteral("play")}});
    } else if (action.startsWith(QStringLiteral("seek:"))) {
        bool valid = false;
        const int milliseconds = action.mid(5).toInt(&valid);
        if (valid && milliseconds >= 0 && session_.online() && !session_.busy() &&
            session_.snapshot().value(QStringLiteral("engine_state")).toString() ==
                QStringLiteral("running"))
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("seek")},
                              {QStringLiteral("position_ms"), milliseconds}});
    } else if (action == QStringLiteral("device")) {
        const auto descriptor = session_.catalog().describe(session_.node(id));
        QMenu menu(this);
        auto* none = menu.addAction(QStringLiteral("Nenhum · liberar dispositivo"));
        QHash<QAction*, QString> devices;
        devices.insert(none, {});
        for (const auto& value : session_.devices()) {
            const auto device = value.toObject();
            if (device.value(QStringLiteral("flow")).toString() ==
                    (descriptor.inputs == 0 ? QStringLiteral("capture")
                                            : QStringLiteral("render")) &&
                device.value(QStringLiteral("state")).toString() == QStringLiteral("active"))
                devices.insert(menu.addAction(device.value(QStringLiteral("name")).toString()),
                               device.value(QStringLiteral("endpoint_id")).toString());
        }
        if (auto* selected = menu.exec(position)) {
            const auto endpoint = devices.value(selected);
            session_.updateField(id, QStringLiteral("endpoint_id"),
                                 endpoint.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                    : QJsonValue(endpoint));
        }
    }
}

void MainWindow::showSettings() {
    system_->setVisible(!system_->isVisible());
    if (system_->isVisible()) {
        system_->raise();
        system_->setFocus();
    }
}
void MainWindow::discardRecovery() {
    if (projects_->busy() || !projects_->recoveryPending())
        return;
    if (QMessageBox::question(this, QStringLiteral("Descartar recuperação"),
                              QStringLiteral("Apagar a cópia de recuperação?"),
                              QMessageBox::Discard | QMessageBox::Cancel,
                              QMessageBox::Cancel) == QMessageBox::Discard)
        projects_->discardRecovery();
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (!closing_ && session_.online() && !session_.settled()) {
        event->ignore();
        if (!closePending_) {
            closePending_ = true;
            // Flush the last coalesced adjustment before inspecting unsaved state.
            QTimer::singleShot(100, this, [this] {
                closePending_ = false;
                close();
            });
        }
        return;
    }
    if (!closing_ && projects_ && (projects_->busy() || !projects_->confirmDiscard())) {
        event->ignore();
        return;
    }
    if (!closing_ && projects_ && !projects_->finishClose()) {
        event->ignore();
        return;
    }
    closing_ = true;
    session_.beginShutdown();
    if (shutdownComplete_) {
        event->accept();
        return;
    }
    backend_.shutdown();
    event->ignore();
}
void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange)
        session_.setMinimized(isMinimized());
    QMainWindow::changeEvent(event);
}
} // namespace nodivu
