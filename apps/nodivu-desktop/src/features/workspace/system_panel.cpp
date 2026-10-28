#include "features/workspace/system_panel.hpp"
#include "contracts/monitor_state.hpp"
#include "contracts/virtual_cable.hpp"
#include "features/projects/project_controller.hpp"
#include "features/updates/update_panel.hpp"
#include "ui/controls/action_availability.hpp"
#include "ui/controls/device_selector.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/studio_panel_style.hpp"
#include "ui/theme/theme.hpp"
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace nodivu {
namespace {
QLabel* text(const QString& value, QWidget* parent, const QString& role = {}) {
    auto* label = new QLabel(value, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    if (!role.isEmpty())
        label->setProperty("role", role);
    return label;
}
void divider(QVBoxLayout* layout, QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setProperty("role", QStringLiteral("divider"));
    line->setFixedHeight(1);
    layout->addSpacing(8);
    layout->addWidget(line);
    layout->addSpacing(8);
}
void meter(QProgressBar* bar, QLabel* label, double peak) {
    const double db = std::isfinite(peak) && peak > 0 ? 20 * std::log10(peak) : -INFINITY;
    bar->setValue(std::isfinite(db) ? qRound(std::clamp((db + 60) / 60, 0.0, 1.0) * 1000) : 0);
    label->setText(std::isfinite(db) ? QStringLiteral("%1 dBFS").arg(db, 0, 'f', 1)
                                     : QStringLiteral("Silêncio"));
}
} // namespace
SystemPanel::SystemPanel(SessionController& session, ProjectController& projects, AppPaths paths,
                         QWidget* parent)
    : QFrame(parent), session_(session), projects_(projects), paths_(std::move(paths)) {
    setObjectName(QStringLiteral("systemPanel"));
    setAccessibleName(QStringLiteral("Sistema, escuta e diagnóstico"));
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(
        studioPanelStyle() +
        QStringLiteral(
            "QFrame#systemPanel { background: #19222c; border-left: 1px solid #2b333f; }"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* headingWidget = new QWidget(this);
    auto* heading = new QHBoxLayout(headingWidget);
    heading->setContentsMargins(20, 12, 12, 8);
    heading->addWidget(text(QStringLiteral("Sistema"), headingWidget, QStringLiteral("heading")),
                       1);
    auto* close = new QPushButton(headingWidget);
    close->setIcon(studioIcon(StudioIcon::Close));
    close->setObjectName(QStringLiteral("systemClose"));
    close->setProperty("role", QStringLiteral("close"));
    close->setFixedSize(32, 32);
    close->setAccessibleName(QStringLiteral("Fechar sistema"));
    close->setToolTip(close->accessibleName());
    heading->addWidget(close);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    outer->addWidget(headingWidget);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("systemSections"));
    tabs_->setAccessibleName(QStringLiteral("Seções do Sistema"));
    for (const auto& title :
         {QStringLiteral("Áudio"), QStringLiteral("Projeto"), QStringLiteral("Aplicativo")})
        tabs_->addTab(title);
    tabs_->setExpanding(true);
    outer->addWidget(tabs_);
    pages_ = new QStackedWidget(this);
    outer->addWidget(pages_, 1);
    const auto makePage = [this](const QString& name) {
        auto* page = new QScrollArea(pages_);
        page->setObjectName(name);
        page->setWidgetResizable(true);
        page->setFrameShape(QFrame::NoFrame);
        page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        pages_->addWidget(page);
        return page;
    };
    scroll_ = makePage(QStringLiteral("systemScroll"));
    auto* projectScroll = makePage(QStringLiteral("projectSystemScroll"));
    auto* appScroll = makePage(QStringLiteral("appSystemScroll"));
    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
    auto* content = new QWidget(scroll_);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(Theme::PanelPadding, 16, Theme::PanelPadding, Theme::PanelPadding);
    layout->setSpacing(Theme::ControlSpacing);
    auto* cableToggle = new QToolButton(content);
    cableToggle->setText(QStringLiteral("Cabo virtual · configuração opcional"));
    cableToggle->setObjectName(QStringLiteral("cableSetup"));
    cableToggle->setCheckable(true);
    cableToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    cableToggle->setArrowType(Qt::RightArrow);
    cableToggle->setStyleSheet(QStringLiteral(
        "QToolButton { color: #b296fa; font-size: 12px; font-weight: 600; background: #1d2530; "
        "border: 1px solid #384353; border-radius: 7px; padding: 10px; text-align: left; }"
        "QToolButton:hover { border-color: #65748a; }"));
    layout->addWidget(cableToggle);
    auto* cable = new QWidget(content);
    auto* cableLayout = new QVBoxLayout(cable);
    cableLayout->setContentsMargins(0, 0, 0, 0);
    cableStatus_ = text({}, cable);
    cableStatus_->setObjectName(QStringLiteral("cableStatus"));
    cableLayout->addWidget(cableStatus_);
    cableLayout->addWidget(text(QStringLiteral("1. Entrada: seu microfone real ou Player MP3.\n"
                                               "2. Saída do Nodivu: CABLE Input.\n"
                                               "3. Microfone do outro app: CABLE Output."),
                                cable));
    cableRoute_ = text({}, cable);
    cableRoute_->setObjectName(QStringLiteral("cableRoute"));
    cableLayout->addWidget(cableRoute_);
    cableLayout->addWidget(
        text(QStringLiteral("Conecte os blocos por fios. Para voz + MP3, use o Mixer. "
                            "Não devolva CABLE Output ao próprio cabo."),
             cable));
    auto* installer = new QPushButton(QStringLiteral("Instalar VB-CABLE…"), cable);
    installer->setObjectName(QStringLiteral("cableInstaller"));
    setActionAvailability(
        installer, QStringLiteral("Abrir o instalador oficial do VB-CABLE"),
        QFileInfo::exists(paths_.cableInstaller)
            ? QString()
            : QStringLiteral("O instalador opcional do VB-CABLE não acompanha esta edição."));
    cableLayout->addWidget(installer);
    connect(installer, &QPushButton::clicked, this, [this] {
        if (!QFileInfo::exists(paths_.cableInstaller)) {
            emit error(QStringLiteral("Instalador do cabo indisponível."));
            return;
        }
        QMessageBox prompt(
            QMessageBox::Question, QStringLiteral("VB-CABLE"),
            QStringLiteral(
                "Abrir o instalador oficial do VB-CABLE?\n\n"
                "VB-Audio Software · donationware. Pode pedir administrador e reinício do Windows. "
                "Se o cabo já funciona, não reinstale."),
            QMessageBox::NoButton, this);
        auto* cancel = prompt.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
        auto* open = prompt.addButton(QStringLiteral("Abrir instalador"), QMessageBox::AcceptRole);
        prompt.setDefaultButton(cancel);
        prompt.setEscapeButton(cancel);
        prompt.exec();
        if (prompt.clickedButton() == open &&
            !QDesktopServices::openUrl(QUrl::fromLocalFile(paths_.cableInstaller)))
            emit error(QStringLiteral("Não foi possível abrir o instalador do VB-CABLE."));
    });
    cableLayout->addWidget(text(
        QStringLiteral(
            "Instalação e teste\nVB-CABLE é um produto VB-Audio, distribuído como donationware. "
            "Use o pacote oficial de vb-audio.com/Cable, reinicie o Windows e atualize os "
            "dispositivos. "
            "Nenhum padrão do Windows precisa mudar. Para ouvir o cabo, escolha outra saída física "
            "na Escuta local. Medidor de envio não comprova recepção; teste no app de destino. "
            "Se renomeou o cabo, identifique-o manualmente."),
        cable));
    cable->hide();
    layout->addWidget(cable);
    connect(cableToggle, &QToolButton::toggled, cable, &QWidget::setVisible);
    connect(cableToggle, &QToolButton::toggled, cableToggle, [cableToggle](bool open) {
        cableToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    divider(layout, content);
    monitorSection_ = new QWidget(content);
    monitorSection_->setObjectName(QStringLiteral("monitorSection"));
    monitorSection_->setFocusPolicy(Qt::StrongFocus);
    auto* monitorLayout = new QVBoxLayout(monitorSection_);
    monitorLayout->setContentsMargins(0, 0, 0, 0);
    monitorLayout->setSpacing(10);
    monitorLayout->addWidget(
        text(QStringLiteral("ESCUTA LOCAL"), monitorSection_, QStringLiteral("eyebrow")));
    monitorLayout->addWidget(
        text(QStringLiteral("Ouvir minha saída"), monitorSection_, QStringLiteral("heading")));
    auto* deviceLabel = text(QStringLiteral("Fones para conferir o áudio final"), monitorSection_);
    monitorLayout->addWidget(deviceLabel);
    device_ = new DeviceSelector(monitorSection_);
    device_->setObjectName(QStringLiteral("monitorDevice"));
    device_->setAccessibleName(QStringLiteral("Saída da escuta local"));
    deviceLabel->setBuddy(device_);
    monitorLayout->addWidget(device_);
    monitor_ = new QPushButton(QStringLiteral("Ligar escuta"), monitorSection_);
    monitor_->setObjectName(QStringLiteral("monitorToggle"));
    monitorLayout->addWidget(monitor_);
    monitorStatus_ = text({}, monitorSection_);
    monitorStatus_->setObjectName(QStringLiteral("monitorStatus"));
    monitorLayout->addWidget(monitorStatus_);
    monitorHint_ = text({}, monitorSection_);
    monitorHint_->setObjectName(QStringLiteral("monitorHint"));
    monitorLayout->addWidget(monitorHint_);
    monitorLayout->addWidget(text(
        QStringLiteral(
            "Copia o som enviado à saída principal. Use fones para evitar retorno pelo microfone. "
            "Não muda o volume enviado aos outros apps."),
        monitorSection_));
    layout->addWidget(monitorSection_);
    connect(device_, &QComboBox::currentIndexChanged, this, &SystemPanel::synchronizeMonitor);
    connect(monitor_, &QPushButton::clicked, this, [this] {
        const auto state = monitorState(session_.snapshot(), session_.devices(), session_.online(),
                                        projects_.busy() || !session_.settled(),
                                        device_->currentData().toString());
        if (!state.toggleReason.isEmpty())
            return;
        session_.command(QStringLiteral("audio.monitor"),
                         {{QStringLiteral("endpoint_id"),
                           state.active ? QJsonValue(QJsonValue::Null)
                                        : QJsonValue(device_->currentData().toString())}});
        synchronizeMonitor();
    });
    divider(layout, content);
    layout->addWidget(text(QStringLiteral("MONITORAMENTO"), content, QStringLiteral("eyebrow")));
    layout->addWidget(text(QStringLiteral("Sinal ao vivo"), content, QStringLiteral("heading")));
    signal_ = text({}, content);
    signal_->setObjectName(QStringLiteral("signalInfo"));
    layout->addWidget(signal_);
    const auto mountMeter = [&](const QString& name, QLabel*& value, QProgressBar*& bar) {
        auto* row = new QHBoxLayout;
        row->addWidget(text(name, content), 1);
        value = text(QStringLiteral("Silêncio"), content);
        value->setStyleSheet(QStringLiteral("color: #c2d3de; font-size: 11px;"));
        row->addWidget(value);
        layout->addLayout(row);
        bar = new QProgressBar(content);
        bar->setAccessibleName(name);
        bar->setRange(0, 1000);
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        layout->addWidget(bar);
    };
    mountMeter(QStringLiteral("Captura / gerador"), inputDb_, inputMeter_);
    mountMeter(QStringLiteral("Enviado à saída"), outputDb_, outputMeter_);
    divider(layout, content);
    layout->addStretch();
    scroll_->setWidget(content);
    content = new QWidget(projectScroll);
    layout = new QVBoxLayout(content);
    layout->setContentsMargins(Theme::PanelPadding, 16, Theme::PanelPadding, Theme::PanelPadding);
    layout->setSpacing(Theme::ControlSpacing);
    layout->addWidget(
        text(QStringLiteral("CONEXÕES DO PROJETO"), content, QStringLiteral("eyebrow")));
    connections_ = new QVBoxLayout;
    connections_->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(connections_);
    divider(layout, content);
    layout->addWidget(
        text(QStringLiteral("RECUPERAÇÃO DO PROJETO"), content, QStringLiteral("eyebrow")));
    recovery_ = text({}, content);
    recovery_->setObjectName(QStringLiteral("systemRecoveryStatus"));
    layout->addWidget(recovery_);
    restore_ = new QPushButton(QStringLiteral("Restaurar cópia"), content);
    restore_->setObjectName(QStringLiteral("systemRecoveryRestore"));
    discard_ = new QPushButton(QStringLiteral("Descartar cópia…"), content);
    discard_->setObjectName(QStringLiteral("systemRecoveryDiscard"));
    layout->addWidget(restore_);
    layout->addWidget(discard_);
    connect(restore_, &QPushButton::clicked, &projects_, &ProjectController::restoreRecovery);
    connect(discard_, &QPushButton::clicked, this, &SystemPanel::discardRecoveryRequested);
    divider(layout, content);
    layout->addStretch();
    projectScroll->setWidget(content);
    content = new QWidget(appScroll);
    layout = new QVBoxLayout(content);
    layout->setContentsMargins(Theme::PanelPadding, 16, Theme::PanelPadding, Theme::PanelPadding);
    layout->setSpacing(Theme::ControlSpacing);
    plugins_ = new QPushButton(QStringLiteral("Gerenciar plugins"), content);
    plugins_->setIcon(studioIcon(StudioIcon::Plugin));
    plugins_->setObjectName(QStringLiteral("systemPlugins"));
    layout->addWidget(plugins_);
    connect(plugins_, &QPushButton::clicked, this, &SystemPanel::storeRequested);
    divider(layout, content);
    layout->addWidget(text(QStringLiteral("ATUALIZAÇÕES"), content, QStringLiteral("eyebrow")));
    updates_ = new UpdatePanel(paths_.profile, paths_.updatesSource, content);
    layout->addWidget(updates_);
    connect(updates_, &UpdatePanel::installRequested, this, &SystemPanel::installRequested);
    divider(layout, content);
    auto* logs = new QPushButton(QStringLiteral("Abrir logs de diagnóstico"), content);
    logs->setObjectName(QStringLiteral("systemDiagnostics"));
    layout->addWidget(logs);
    connect(logs, &QPushButton::clicked, this, [this] {
        const auto directory = QDir(paths_.profile).filePath(QStringLiteral("logs"));
        if (!QDir().mkpath(directory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
            emit error(QStringLiteral("Não foi possível abrir a pasta de diagnóstico."));
    });
    layout->addWidget(text(QStringLiteral("DIAGNÓSTICO"), content, QStringLiteral("eyebrow")));
    metrics_ = text({}, content);
    metrics_->setObjectName(QStringLiteral("systemMetrics"));
    layout->addWidget(metrics_);
    layout->addWidget(text(QStringLiteral("Apagar um fio silencia o caminho. Retirar o dispositivo "
                                          "ou fechar o app libera o áudio.\n\n"
                                          "Salve para preservar blocos, fios e visão. Um fio por "
                                          "entrada; saídas podem alimentar vários destinos."),
                           content));
    layout->addStretch();
    appScroll->setWidget(content);
    parent->installEventFilter(this);
    connect(&session_, &SessionController::changed, this, &SystemPanel::synchronize);
    connect(&session_, &SessionController::activityChanged, this, &SystemPanel::synchronize);
    connect(&session_, &SessionController::devicesChanged, this, &SystemPanel::synchronize);
    connect(&projects_, &ProjectController::stateChanged, this, &SystemPanel::synchronize);
    for (auto* button : findChildren<QAbstractButton*>())
        button->setCursor(Qt::ArrowCursor);
    hide();
}
void SystemPanel::fitToParent() {
    if (parentWidget()) {
        const auto size = parentWidget()->size();
        const int width = std::min(Theme::SidePanelWidth, qRound(size.width() * .9));
        const auto* menu = parentWidget()->findChild<QMenuBar*>();
        const int top = menu ? menu->geometry().bottom() + 1 : 88;
        setGeometry(size.width() - width, top, width, std::max(100, size.height() - top));
    }
}
bool SystemPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        fitToParent();
    return QFrame::eventFilter(watched, event);
}
void SystemPanel::showEvent(QShowEvent* event) {
    fitToParent();
    raise();
    synchronize();
    emit shown();
    QFrame::showEvent(event);
}
void SystemPanel::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}
void SystemPanel::openMonitor() {
    tabs_->setCurrentIndex(0);
    show();
    raise();
    QTimer::singleShot(0, this, [this] {
        scroll_->ensureWidgetVisible(monitorSection_, 0, 12);
        if (device_->isEnabled())
            device_->setFocus(Qt::ShortcutFocusReason);
        else
            monitorSection_->setFocus(Qt::ShortcutFocusReason);
    });
}
void SystemPanel::openProject() {
    tabs_->setCurrentIndex(1);
    show();
    raise();
    setFocus();
}
void SystemPanel::openApplication() {
    tabs_->setCurrentIndex(2);
    show();
    raise();
    setFocus();
}

void SystemPanel::synchronizeMonitor() {
    auto state =
        monitorState(session_.snapshot(), session_.devices(), session_.online(),
                     projects_.busy() || !session_.settled(), device_->currentData().toString());
    if (state.devices != monitorDevices_ || state.selectedId != monitorId_ ||
        device_->count() == 0) {
        QSignalBlocker blocker(device_);
        const auto previous = device_->currentData().toString();
        device_->clear();
        device_->addItem(QStringLiteral("Selecione seus fones…"), QString());
        for (const auto& value : state.devices) {
            const auto device = value.toObject();
            device_->addItem((device.value(QStringLiteral("is_default")).toBool()
                                  ? QStringLiteral("★ Padrão · ")
                                  : QString()) +
                                 device.value(QStringLiteral("name")).toString(),
                             device.value(QStringLiteral("endpoint_id")).toString());
        }
        if (state.active && device_->findData(state.selectedId) < 0)
            device_->addItem(QStringLiteral("Dispositivo selecionado indisponível"),
                             state.selectedId);
        const int index = device_->findData(state.active ? state.selectedId : previous);
        device_->setCurrentIndex(index < 0 ? 0 : index);
        monitorDevices_ = state.devices;
        monitorId_ = state.selectedId;
        state = monitorState(session_.snapshot(), session_.devices(), session_.online(),
                             projects_.busy() || !session_.settled(),
                             device_->currentData().toString());
    }
    monitor_->setText(state.active ? QStringLiteral("Desligar escuta")
                                   : QStringLiteral("Ligar escuta"));
    setActionAvailability(device_, QStringLiteral("Escolher os fones da escuta local"),
                          state.selectionReason);
    setActionAvailability(monitor_, monitor_->text(), state.toggleReason);
    monitorStatus_->setText(state.message);
    monitorStatus_->setStyleSheet(state.failed ? QStringLiteral("color: #f3b2bb;") : QString());
    monitorHint_->setText(state.toggleReason);
    monitorHint_->setVisible(!state.toggleReason.isEmpty());
}
void SystemPanel::synchronizeConnections() {
    const auto graph = session_.snapshot().value(QStringLiteral("graph")).toObject();
    if (graph != graph_ || connections_->count() == 0) {
        graph_ = graph;
        while (auto* item = connections_->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        const auto edges = graph.value(QStringLiteral("edges")).toArray();
        if (edges.isEmpty())
            connections_->addWidget(text(QStringLiteral("Nenhum fio."), this));
        for (const auto& value : edges) {
            const auto edge = value.toObject();
            const auto name = [&](const QString& key) {
                return session_.catalog().describe(session_.node(edge.value(key).toString())).title;
            };
            const auto caption = QStringLiteral("%1 → %2 / %3 ×")
                                     .arg(name(QStringLiteral("from")), name(QStringLiteral("to")))
                                     .arg(edge.value(QStringLiteral("to_port")).toInt() + 1);
            auto* button = new QPushButton(caption, this);
            button->setProperty("connection", edge);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            connections_->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, edge] {
                if (session_.settled() && !projects_.busy())
                    session_.disconnectEdge(edge);
            });
        }
    }
    const auto reason = !session_.online() ? QStringLiteral("O motor de áudio está desconectado.")
                        : projects_.busy() || !session_.settled()
                            ? QStringLiteral("Aguarde a operação atual do projeto.")
                            : QString();
    for (int index = 0; index < connections_->count(); ++index)
        if (auto* button = qobject_cast<QPushButton*>(connections_->itemAt(index)->widget()))
            setActionAvailability(button, QStringLiteral("Apagar conexão: ") + button->text(),
                                  reason);
}
void SystemPanel::synchronizeCable() {
    bool render = false, capture = false, found = false;
    for (const auto& value : session_.devices()) {
        const auto device = value.toObject();
        const auto name = device.value(QStringLiteral("name")).toString();
        const auto flow = device.value(QStringLiteral("flow")).toString();
        const bool named = (flow == QStringLiteral("render") &&
                            name == QStringLiteral("CABLE Input (VB-Audio Virtual Cable)")) ||
                           (flow == QStringLiteral("capture") &&
                            name == QStringLiteral("CABLE Output (VB-Audio Virtual Cable)"));
        found |= named;
        const bool active =
            device.value(QStringLiteral("state")).toString() == QStringLiteral("active");
        render |= named && active && flow == QStringLiteral("render");
        capture |= named && active && flow == QStringLiteral("capture");
    }
    // Names are advisory only; they never choose an endpoint or prove driver identity.
    cableStatus_->setText(
        render && capture ? QStringLiteral("Par com os nomes padrão disponível. Disponibilidade "
                                           "não comprova passagem de áudio.")
        : found           ? QStringLiteral(
                      "Par incompleto ou inativo. Verifique os dispositivos e atualize a lista.")
                : QStringLiteral("Par padrão do VB-CABLE não encontrado. Cabos renomeados exigem "
                                 "identificação manual."));
    cableStatus_->setStyleSheet(render && capture ? QStringLiteral("color: #70d9bd;")
                                                  : QStringLiteral("color: #e7bc69;"));
    cableRoute_->setText(cableRouteMessage(session_.snapshot(), session_.devices()));
}
void SystemPanel::synchronize() {
    if (!isVisible())
        return;
    synchronizeMonitor();
    synchronizeConnections();
    synchronizeCable();
    const auto snapshot = session_.snapshot();
    const bool running =
        session_.online() &&
        snapshot.value(QStringLiteral("engine_state")).toString() == QStringLiteral("running");
    const QStringList signalNames{
        QStringLiteral("Sem caminho completo · silêncio"), QStringLiteral("Microfone → saída"),
        QStringLiteral("Tom de teste → saída"), QStringLiteral("Plugin → saída"),
        QStringLiteral("Mistura de sinais → saída")};
    signal_->setText(snapshot.value(QStringLiteral("muted")).toBool()
                         ? QStringLiteral("Tudo silenciado.")
                         : signalNames.value(snapshot.value(QStringLiteral("signal")).toInt(),
                                             signalNames.first()));
    const auto metrics = snapshot.value(QStringLiteral("metrics")).toObject();
    meter(inputMeter_, inputDb_,
          running ? metrics.value(QStringLiteral("input_peak")).toDouble() : 0);
    meter(outputMeter_, outputDb_,
          running ? metrics.value(QStringLiteral("output_peak")).toDouble() : 0);
    metrics_->setText(
        running ? QStringLiteral(
                      "Abertura %1 · Frames: %2\nFalta de captura: %3\nSaída: %4 · clipping: %5")
                      .arg(snapshot.value(QStringLiteral("generation")).toInteger())
                      .arg(metrics.value(QStringLiteral("rendered_frames")).toInteger())
                      .arg(metrics.value(QStringLiteral("underruns")).toInteger())
                      .arg(metrics.value(QStringLiteral("render_starvations")).toInteger())
                      .arg(metrics.value(QStringLiteral("clipped_samples")).toInteger())
                : QStringLiteral("Dispositivos ainda não abertos."));
    const auto reason = !session_.online() ? QStringLiteral("O motor de áudio está desconectado.")
                        : projects_.busy() || !session_.settled()
                            ? QStringLiteral("Aguarde a operação atual do projeto.")
                            : QString();
    recovery_->setText(projects_.recoveryStatus());
    restore_->setVisible(projects_.recoveryPending());
    discard_->setVisible(projects_.recoveryPending());
    setActionAvailability(restore_, QStringLiteral("Restaurar cópia de recuperação"), reason);
    setActionAvailability(discard_, QStringLiteral("Descartar cópia de recuperação"),
                          projects_.busy() ? QStringLiteral("Aguarde a operação atual do projeto.")
                                           : QString());
    setActionAvailability(plugins_, QStringLiteral("Abrir o gerenciador de plugins"), reason);
    updates_->setInstallBlockedReason(reason);
}
} // namespace nodivu
