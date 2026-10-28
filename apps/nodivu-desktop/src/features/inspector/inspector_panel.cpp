#include "features/inspector/inspector_panel.hpp"
#include "contracts/playback_state.hpp"
#include "contracts/virtual_cable.hpp"
#include "features/graph/capture_source_widget.hpp"
#include "ui/controls/control_presentation.hpp"
#include "ui/controls/device_selector.hpp"
#include "ui/controls/inline_check_box.hpp"
#include "ui/controls/player_details.hpp"
#include "ui/controls/worker_controls.hpp"
#include "ui/theme/studio_icons.hpp"
#include "ui/theme/theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace nodivu {
namespace {
class MomentaryButton final : public QPushButton {
  public:
    MomentaryButton(SessionController& session, QString node, quint32 parameter,
                    const QString& label, QWidget* parent)
        : QPushButton(label, parent), session_(session), node_(std::move(node)),
          parameter_(parameter) {
        timer_.setInterval(100);
        connect(this, &QPushButton::pressed, this, [this] {
            held_ = true;
            pulse(true);
            timer_.start();
        });
        connect(this, &QPushButton::released, this, [this] { release(); });
        connect(&timer_, &QTimer::timeout, this, [this] { pulse(true); });
        qApp->installEventFilter(this);
    }
    ~MomentaryButton() override {
        release();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ApplicationDeactivate ||
            event->type() == QEvent::WindowDeactivate ||
            (watched == this &&
             (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide)))
            release();
        return QPushButton::eventFilter(watched, event);
    }

  private:
    void pulse(bool pressed) {
        // Gesture leases use the independent bounded backend queue, not graph edits.
        session_.gesture(node_, parameter_, pressed);
    }
    void release() {
        if (!held_)
            return;
        held_ = false;
        timer_.stop();
        setDown(false);
        pulse(false);
    }
    SessionController& session_;
    QString node_;
    quint32 parameter_;
    QTimer timer_;
    bool held_ = false;
};
} // namespace

InspectorPanel::InspectorPanel(SessionController& session, QWidget* parent)
    : QFrame(parent), session_(session) {
    setObjectName(QStringLiteral("blockInspector"));
    setStyleSheet(QStringLiteral(
        "QFrame#blockInspector { background: #202a35; border: 1px solid #516373; border-radius: "
        "10px; }"
        "QFrame#inspectorHandle { background: #26313e; border: 0; }"
        "QFrame#inspectorHandle QPushButton { background: transparent; border: 0; "
        "padding: 2px 3px; color: #c5d2df; font-size: 17px; }"
        "QFrame#inspectorHandle QPushButton[headerAction=\"true\"] { "
        "border: 1px solid transparent; border-radius: 4px; padding: 1px 2px; }"
        "QFrame#inspectorHandle QPushButton[headerAction=\"true\"]:hover { "
        "background: #354153; border-color: #65748a; }"
        "QLabel { font-size: 11px; color: #c2cddb; }"
        "QGroupBox { font-size: 11px; color: #aab9cd; border-color: #384754; }"
        "QComboBox { background: #121924; border: 1px solid #344257; border-radius: 7px; "
        "padding: 7px; color: #dbe3ef; font-size: 11px; }"
        "QComboBox:hover, QDoubleSpinBox:hover { border-color: #65748a; }"
        "QComboBox:focus, QDoubleSpinBox:focus { border-color: #83ddc5; }"
        "QComboBox QAbstractItemView { background: #121924; color: #dbe3ef; "
        "selection-background-color: #243e39; }"
        "QDoubleSpinBox { color: #dbe3ef; background: #121924; border: 1px solid #344257; "
        "border-radius: 5px; font-size: 11px; padding: 7px; }"
        "QTabWidget::pane { border: 0; background: #202a35; }"
        "QTabBar::tab { color: #dbe3ef; background: #262e3b; border: 1px solid #384353; "
        "border-radius: 5px; margin-right: 5px; padding: 6px 9px; font-size: 11px; }"
        "QTabBar::tab:selected { color: #70d9bd; background: #243e39; border-color: #70d9bd; }"
        "QTabBar::tab:hover { border-color: #65748a; }"
        "QPushButton[playerAction=\"true\"] { font-size: 11px; padding: 8px; "
        "color: #dbe3ef; background: #262e3b; border: 1px solid #384353; border-radius: 7px; }"
        "QPushButton[playerAction=\"true\"]:enabled:hover { background: #354153; border-color: "
        "#65748a; }"
        "QPushButton[playerAction=\"true\"]:disabled { color: #626b78; background: #202733; "
        "border-color: #2b3543; }"));
    setFixedWidth(Theme::InspectorWidth);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    handle_ = new QFrame(this);
    handle_->setObjectName(QStringLiteral("inspectorHandle"));
    handle_->setCursor(Qt::SizeAllCursor);
    handle_->installEventFilter(this);
    auto* header = new QHBoxLayout(handle_);
    header->setContentsMargins(10, 11, 10, 11);
    header->setSpacing(7);
    auto* grip = new QLabel(handle_);
    grip->setPixmap(studioIcon(StudioIcon::Grip).pixmap(16, 20));
    grip->setObjectName(QStringLiteral("inspectorMoveHint"));
    grip->setStyleSheet(QStringLiteral("color: #7a899c; font-size: 15px;"));
    grip->setCursor(Qt::ArrowCursor);
    grip->setAttribute(Qt::WA_NoMousePropagation);
    grip->setToolTip(QStringLiteral("Arraste o cabeçalho para mover o painel."));
    grip->setAccessibleName(QStringLiteral("Como mover o painel"));
    header->addWidget(grip);
    auto* heading = new QVBoxLayout;
    heading->setSpacing(4);
    title_ = new QLabel(QStringLiteral("Configurações"), handle_);
    title_->setTextFormat(Qt::PlainText);
    title_->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600; color: #e4e8ee;"));
    title_->setAttribute(Qt::WA_TransparentForMouseEvents);
    heading->addWidget(title_);
    subtitle_ = new QLabel(handle_);
    subtitle_->setTextFormat(Qt::PlainText);
    subtitle_->setStyleSheet(QStringLiteral("font-size: 11px; color: #95a1b2;"));
    subtitle_->setAttribute(Qt::WA_TransparentForMouseEvents);
    heading->addWidget(subtitle_);
    header->addLayout(heading, 1);
    auto* home = new QPushButton(handle_);
    home->setIcon(studioIcon(StudioIcon::Target));
    auto* collapse = new QPushButton(handle_);
    collapse->setIcon(studioIcon(StudioIcon::Minus));
    auto* close = new QPushButton(handle_);
    close->setIcon(studioIcon(StudioIcon::Close));
    home->setObjectName(QStringLiteral("inspectorHome"));
    collapse->setObjectName(QStringLiteral("inspectorCollapse"));
    close->setObjectName(QStringLiteral("inspectorClose"));
    collapse->setProperty("headerAction", true);
    close->setProperty("headerAction", true);
    for (auto* button : {home, collapse, close}) {
        button->setFixedSize(Theme::ControlTarget, 32);
        button->setCursor(Qt::ArrowCursor);
        header->addWidget(button);
    }
    home->setAccessibleName(QStringLiteral("Mover inspetor ao canto"));
    close->setAccessibleName(QStringLiteral("Fechar inspetor"));
    collapse->setAccessibleName(QStringLiteral("Recolher ou expandir inspetor"));
    for (auto* button : {home, collapse, close})
        button->setToolTip(button->accessibleName());
    layout->addWidget(handle_);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    auto panelPalette = palette();
    panelPalette.setColor(QPalette::Window, QColor(32, 42, 53));
    scroll_->setPalette(panelPalette);
    layout->addWidget(scroll_, 1);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    connect(home, &QPushButton::clicked, this, &InspectorPanel::dockToCorner);
    connect(collapse, &QPushButton::clicked, this, [this] {
        collapsed_ = !collapsed_;
        scroll_->setVisible(!collapsed_);
        constrainToParent();
    });
    connect(&session_, &SessionController::changed, this, &InspectorPanel::synchronize);
    connect(&session_, &SessionController::devicesChanged, this, &InspectorPanel::synchronize);
    if (parent)
        parent->installEventFilter(this);
    hide();
}
void InspectorPanel::constrainToParent() {
    if (!parentWidget() || adjustingGeometry_)
        return;
    const QScopedValueRollback<bool> guard(adjustingGeometry_, true);
    const auto bounds = parentWidget()->size();
    setFixedWidth(std::min(Theme::InspectorWidth, std::max(100, bounds.width() - 16)));
    const int maximum = std::max(100, bounds.height() - 16);
    const int bodyWidth = std::max(1, width() - 2);
    if (content_)
        if (auto* tabs = content_->findChild<QTabWidget*>(); tabs && tabs->currentWidget()) {
            auto* pageLayout = tabs->currentWidget()->layout();
            const int wrapped = pageLayout->totalHeightForWidth(std::max(1, bodyWidth - 28));
            const int pageHeight = wrapped >= 0 ? wrapped : pageLayout->sizeHint().height();
            // QTabWidget normally reserves the tallest page. Match the active page instead.
            tabs->setFixedHeight(tabs->tabBar()->sizeHint().height() + pageHeight + 8);
        }
    const int wrappedHeight =
        content_ && content_->layout() ? content_->layout()->totalHeightForWidth(bodyWidth) : -1;
    const int contentHeight =
        content_ ? (wrappedHeight >= 0 ? wrappedHeight : content_->sizeHint().height()) : 0;
    const int naturalHeight = handle_->sizeHint().height() + contentHeight + 10;
    setFixedHeight(collapsed_ ? handle_->sizeHint().height() + 2
                              : std::clamp(naturalHeight, 100, maximum));
    move(std::clamp(x(), 8, std::max(8, bounds.width() - width() - 8)),
         std::clamp(y(), 8, std::max(8, bounds.height() - height() - 8)));
}
void InspectorPanel::dockToCorner() {
    if (parentWidget())
        move(std::max(8, parentWidget()->width() - width() - 16), 64);
    constrainToParent();
}
void InspectorPanel::setSavedPath(const QString& path) {
    if (savedPath_ == path)
        return;
    savedPath_ = path;
    synchronize();
}
bool InspectorPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == content_ && event->type() == QEvent::LayoutRequest)
        constrainToParent();
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        constrainToParent();
    if (watched == handle_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragging_ = true;
                dragOffset_ = mouse->globalPosition().toPoint() - mapToGlobal(QPoint(0, 0));
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && dragging_ && parentWidget()) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const auto position =
                parentWidget()->mapFromGlobal(mouse->globalPosition().toPoint() - dragOffset_);
            move(std::clamp(position.x(), 8, std::max(8, parentWidget()->width() - width() - 8)),
                 std::clamp(position.y(), 8, std::max(8, parentWidget()->height() - height() - 8)));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            dragging_ = false;
        }
    }
    return QFrame::eventFilter(watched, event);
}
void InspectorPanel::inspect(const QString& id) {
    if (session_.node(id).isEmpty())
        return;
    if (selectedId_ != id) {
        selectedId_ = id;
        savedPath_.clear();
        rebuild();
    }
    constrainToParent();
    if (!isVisible())
        dockToCorner();
    show();
    raise();
    synchronize();
}
void InspectorPanel::focusField(const QString& key) {
    collapsed_ = false;
    scroll_->show();
    constrainToParent();
    if (auto* field = fields_.value(key).data()) {
        scroll_->ensureWidgetVisible(field);
        field->setFocus(Qt::OtherFocusReason);
    }
}
void InspectorPanel::rebuild() {
    fields_.clear();
    values_.clear();
    worker_.clear();
    playerDetails_.clear();
    playButton_.clear();
    reloadButton_.clear();
    seek_.clear();
    // Destroy controls synchronously so a held gesture is released before replacement.
    delete scroll_->takeWidget();
    content_ = new QWidget(scroll_);
    content_->installEventFilter(this);
    content_->setPalette(scroll_->palette());
    content_->setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(content_);
    layout->setContentsMargins(14, 14, 14, 14);
    scroll_->setWidget(content_);
    const auto descriptor = session_.catalog().describe(session_.node(selectedId_));
    title_->setText(descriptor.title);
    subtitle_->setText(descriptor.subtitle);
    QStringList pageNames;
    for (const auto& field : descriptor.controls) {
        const auto page = field.page.isEmpty() ? QStringLiteral("Ajustes") : field.page;
        if (!pageNames.contains(page))
            pageNames.append(page);
    }
    QTabWidget* tabs = nullptr;
    if (pageNames.size() > 1) {
        tabs = new QTabWidget(content_);
        tabs->setObjectName(QStringLiteral("inspectorTabs"));
        layout->addWidget(tabs);
    }
    QHash<QString, QVBoxLayout*> pages;
    QHash<QString, QGridLayout*> groups;
    QHash<QString, int> groupSlots;
    for (const auto& field : descriptor.controls) {
        const auto pageName = field.page.isEmpty() ? QStringLiteral("Ajustes") : field.page;
        QVBoxLayout* target = layout;
        if (tabs) {
            if (!pages.contains(pageName)) {
                auto* page = new QWidget(tabs);
                auto* pageLayout = new QVBoxLayout(page);
                pageLayout->setContentsMargins(0, 0, 0, 0);
                pageLayout->setAlignment(Qt::AlignTop);
                pages.insert(pageName, pageLayout);
                tabs->addTab(page, pageName);
            }
            target = pages.value(pageName);
        }
        const auto groupName =
            field.group.isEmpty()
                ? (field.advanced ? QStringLiteral("Avançado") : QStringLiteral("Controles"))
                : field.group;
        const auto key = pageName + QChar(0x1f) + groupName +
                         (field.advanced ? QStringLiteral(":advanced") : QString());
        if (!groups.contains(key)) {
            auto* group = new QGroupBox(groupName, content_);
            group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
            auto* groupLayout = new QVBoxLayout(group);
            auto* container = new QWidget(group);
            auto* fields = new QGridLayout(container);
            fields->setContentsMargins(0, 0, 0, 0);
            fields->setHorizontalSpacing(12);
            fields->setVerticalSpacing(12);
            fields->setColumnStretch(0, 1);
            fields->setColumnStretch(1, 1);
            groupLayout->addWidget(container);
            if (field.advanced) {
                if (groupName != QStringLiteral("Avançado"))
                    group->setTitle(groupName + QStringLiteral(" · Avançado"));
                group->setCheckable(true);
                group->setChecked(false);
                container->hide();
                connect(group, &QGroupBox::toggled, container, &QWidget::setVisible);
            }
            target->addWidget(group);
            groups.insert(key, fields);
        }
        auto* fieldWidget = new QWidget(content_);
        auto* fieldLayout = new QVBoxLayout(fieldWidget);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(5);
        mountControl(fieldLayout, field);
        const bool pair =
            descriptor.layout == QStringLiteral("grid") && field.kind != ControlKind::Slider &&
            field.kind != ControlKind::Select && field.kind != ControlKind::Device &&
            !field.advanced &&
            fieldWidget->minimumSizeHint().width() <= (Theme::InspectorWidth - 76) / 2;
        const int span = pair ? 1 : 2;
        int slot = groupSlots.value(key);
        if (span == 2 && slot % 2 != 0)
            ++slot;
        groups.value(key)->addWidget(fieldWidget, slot / 2, slot % 2, 1, span);
        groupSlots.insert(key, slot + span);
    }
    if (tabs)
        connect(tabs, &QTabWidget::currentChanged, this, &InspectorPanel::constrainToParent);
    if (descriptor.filePlayer)
        mountPlayer(layout, descriptor);
    if (descriptor.captureSource)
        mountCapture(layout);
    if (descriptor.worker) {
        worker_ = new WorkerControls(false, content_);
        const auto id = selectedId_;
        connect(worker_, &WorkerControls::restartRequested, this, [this, id] {
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("restart")}});
        });
        connect(worker_, &WorkerControls::geometryChanged, this,
                &InspectorPanel::constrainToParent);
        layout->addWidget(worker_);
    }
    auto* description = new QLabel(descriptor.description, content_);
    description->setWordWrap(true);
    description->setTextFormat(Qt::PlainText);
    description->setObjectName(QStringLiteral("blockNote"));
    description->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: #8b95a7; border-top: 1px solid #29303c; padding-top: 12px;"));
    layout->addWidget(description);
    layout->addStretch();
}
void InspectorPanel::mountControl(QVBoxLayout* layout, const ControlDescriptor& control) {
    const auto id = selectedId_;
    const auto descriptor = session_.catalog().describe(session_.node(selectedId_));
    const bool inlineToggle = control.kind == ControlKind::Toggle;
    QLabel* label = nullptr;
    if (!inlineToggle) {
        label = new QLabel(control.label, content_);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        if (control.kind == ControlKind::Slider) {
            auto* line = new QHBoxLayout;
            line->addWidget(label, 1);
            auto* value = new QLabel(content_);
            value->setObjectName(control.key + QStringLiteral(":value"));
            value->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;")
                                     .arg(descriptor.kind == QStringLiteral("plugin")
                                              ? descriptor.accent.name()
                                              : QStringLiteral("#70d9bd")));
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            values_.insert(control.key, value);
            line->addWidget(value);
            layout->addLayout(line);
        } else
            layout->addWidget(label);
    }
    QWidget* widget = nullptr;
    if (control.kind == ControlKind::Device || control.kind == ControlKind::Select) {
        auto* combo = new DeviceSelector(content_);
        widget = combo;
        combo->setProperty("flow", control.flow);
        if (control.kind == ControlKind::Select)
            for (const auto& option : control.options)
                combo->addItem(option.toObject().value(QStringLiteral("label")).toString(),
                               option.toObject().value(QStringLiteral("value")).toVariant());
        connect(combo, &QComboBox::activated, this, [this, combo, id, control](int) {
            session_.updateField(id, control.key,
                                 control.kind == ControlKind::Device
                                     ? (combo->currentData().toString().isEmpty()
                                            ? QJsonValue(QJsonValue::Null)
                                            : QJsonValue(combo->currentData().toString()))
                                     : QJsonValue(combo->currentData().toDouble()));
        });
    } else if (control.kind == ControlKind::Toggle) {
        auto* check = new InlineCheckBox(control.label, content_);
        widget = check;
        connect(check, &QCheckBox::toggled, this, [this, id, control](bool enabled) {
            session_.updateField(id, control.key,
                                 control.key.startsWith(QStringLiteral("parameter:"))
                                     ? QJsonValue(enabled ? 1 : 0)
                                     : QJsonValue(enabled));
        });
    } else if (control.kind == ControlKind::Momentary) {
        widget = new MomentaryButton(session_, id, control.key.mid(10).toUInt(), control.label,
                                     content_);
    } else if (control.kind == ControlKind::Slider) {
        auto* slider = new QSlider(Qt::Horizontal, content_);
        slider->setRange(0, ControlSliderSteps);
        widget = slider;
        const auto accent = descriptor.kind == QStringLiteral("plugin") ? descriptor.accent.name()
                                                                        : QStringLiteral("#70d9bd");
        slider->setStyleSheet(
            QStringLiteral(
                "QSlider::sub-page:horizontal, QSlider::handle:horizontal { background: %1; }")
                .arg(accent));
        const auto valueLabel = values_.value(control.key);
        connect(slider, &QSlider::valueChanged, this,
                [this, id, control, valueLabel](int position) {
                    const double value = sliderControlValue(control, position);
                    if (valueLabel)
                        valueLabel->setText(controlValueText(control, value));
                    session_.updateField(id, control.key, value);
                });
    } else {
        auto* number = new QDoubleSpinBox(content_);
        widget = number;
        number->setDecimals(control.step >= 1 ? 0 : control.step >= 0.1 ? 1 : 4);
        number->setRange(control.minimum, control.maximum);
        number->setSingleStep(control.step);
        number->setSuffix(control.unit.isEmpty() ? QString() : QLatin1Char(' ') + control.unit);
        number->setKeyboardTracking(false);
        connect(number, &QDoubleSpinBox::valueChanged, this, [this, id, control](double value) {
            session_.updateField(id, control.key, value);
        });
    }
    widget->setAccessibleName(control.label);
    widget->setCursor(Qt::ArrowCursor);
    widget->setToolTip(control.help);
    widget->setEnabled(!control.readonly);
    widget->setProperty("readonly", control.readonly);
    widget->setProperty("field", control.key);
    fields_.insert(control.key, widget);
    if (label)
        label->setBuddy(widget);
    layout->addWidget(widget);
    if (!control.help.isEmpty()) {
        auto* help = new QLabel(control.help, content_);
        help->setWordWrap(true);
        help->setTextFormat(Qt::PlainText);
        help->setStyleSheet(QStringLiteral("color: #8b95a7; font-size: 11px;"));
        layout->addWidget(help);
    }
}
void InspectorPanel::mountPlayer(QVBoxLayout* layout, const NodeDescriptor& descriptor) {
    const auto id = selectedId_;
    auto* choose = new QPushButton(QStringLiteral("Escolher áudio…"), content_);
    choose->setProperty("playerAction", true);
    layout->addWidget(choose);
    auto* reload = new QPushButton(QStringLiteral("Carregar áudio salvo"), content_);
    reload->setProperty("playerAction", true);
    reloadButton_ = reload;
    layout->addWidget(reload);
    connect(reload, &QPushButton::clicked, this, [this, id] { emit reloadRequested(id); });
    connect(choose, &QPushButton::clicked, this, [this, id] {
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("Escolher áudio"), {},
                                                       QStringLiteral("Áudio (*.mp3 *.wav *.m4a)"));
        if (!file.isEmpty())
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("load")},
                              {QStringLiteral("path"), file}});
    });
    auto* play = new QPushButton(QStringLiteral("Reproduzir"), content_);
    play->setObjectName(QStringLiteral("playerPlay"));
    play->setProperty("playerAction", true);
    play->setEnabled(false);
    for (auto* button : {choose, reload, play})
        button->setCursor(Qt::ArrowCursor);
    playButton_ = play;
    layout->addWidget(play);
    connect(play, &QPushButton::clicked, this, [this, id, transport = descriptor.fileTransport] {
        const auto presentation =
            playbackState(session_.snapshot(), session_.node(id), session_.devices(),
                          session_.online(), session_.busy(), savedPath_);
        if (!presentation.canPlay && !(transport && presentation.playing && session_.online()))
            return;
        const auto state = session_.snapshot()
                               .value(QStringLiteral("plugin_states"))
                               .toObject()
                               .value(id)
                               .toObject();
        session_.command(QStringLiteral("plugin.command"),
                         {{QStringLiteral("node_id"), id},
                          {QStringLiteral("action"),
                           transport && state.value(QStringLiteral("status")).toInt() == 3
                               ? QStringLiteral("pause")
                               : QStringLiteral("play")}});
    });
    if (descriptor.fileTransport) {
        auto* seek = new QSlider(Qt::Horizontal, content_);
        seek->setEnabled(false);
        seek_ = seek;
        seek->setAccessibleName(QStringLiteral("Posição da reprodução"));
        layout->addWidget(seek);
        auto* timer = new QTimer(seek);
        timer->setSingleShot(true);
        timer->setInterval(150);
        connect(seek, &QSlider::valueChanged, timer, [timer] { timer->start(); });
        connect(seek, &QSlider::sliderReleased, timer, [timer] { timer->start(0); });
        connect(timer, &QTimer::timeout, this, [this, id, seek] {
            if (selectedId_ != id || seek->isSliderDown() || !session_.online() ||
                session_.busy() ||
                session_.snapshot().value(QStringLiteral("engine_state")).toString() !=
                    QStringLiteral("running"))
                return;
            session_.command(QStringLiteral("plugin.command"),
                             {{QStringLiteral("node_id"), id},
                              {QStringLiteral("action"), QStringLiteral("seek")},
                              {QStringLiteral("position_ms"), seek->value()}});
        });
    }
    playerDetails_ = new PlayerDetails(false, descriptor.fileTransport, content_);
    connect(playerDetails_, &PlayerDetails::geometryChanged, this,
            &InspectorPanel::constrainToParent);
    layout->addWidget(playerDetails_);
    if (seek_)
        connect(seek_, &QSlider::valueChanged, this, [this](int position) {
            if (playerDetails_ && seek_)
                playerDetails_->previewPosition(position, seek_->maximum());
        });
}
void InspectorPanel::mountCapture(QVBoxLayout* layout) {
    auto* capture = new CaptureSourceWidget(session_, selectedId_, false, content_);
    connect(capture, &CaptureSourceWidget::geometryChanged, this,
            &InspectorPanel::constrainToParent);
    layout->addWidget(capture);
}
void InspectorPanel::synchronize() {
    if (selectedId_.isEmpty())
        return;
    const auto node = session_.node(selectedId_);
    if (node.isEmpty()) {
        hide();
        selectedId_.clear();
        delete scroll_->takeWidget();
        content_ = nullptr;
        fields_.clear();
        return;
    }
    const auto block = node.value(QStringLiteral("block")).toObject();
    const auto descriptor = session_.catalog().describe(node);
    if (descriptor.kind == QStringLiteral("output")) {
        const auto presentation = outputPresentation(
            block.value(QStringLiteral("endpoint_id")).toString(), session_.devices());
        title_->setText(presentation.title);
        subtitle_->setText(presentation.subtitle);
        if (auto* note = content_->findChild<QLabel*>(QStringLiteral("blockNote")))
            note->setText(presentation.note);
    }
    for (const auto& control : descriptor.controls) {
        auto* widget = fields_.value(control.key).data();
        if (!widget)
            continue;
        widget->setEnabled(session_.online() && !control.readonly);
        const auto value = fieldValue(block, control.key);
        QSignalBlocker blocker(widget);
        if (auto* combo = qobject_cast<DeviceSelector*>(widget)) {
            if (control.kind == ControlKind::Device) {
                combo->synchronize(session_.devices(), control.flow, value.toString());
            } else {
                combo->setCurrentIndex(combo->findData(value.toVariant()));
            }
        } else if (auto* check = qobject_cast<QCheckBox*>(widget)) {
            check->setChecked(value.toBool() || value.toDouble() != 0);
        } else if (auto* slider = qobject_cast<QSlider*>(widget)) {
            if (!slider->isSliderDown())
                slider->setValue(sliderControlPosition(control, value.toDouble()));
        } else if (auto* number = qobject_cast<QDoubleSpinBox*>(widget)) {
            if (!number->hasFocus())
                number->setValue(value.toDouble());
        }
        if (const auto label = values_.value(control.key)) {
            const auto* slider = qobject_cast<QSlider*>(widget);
            if (!slider || !slider->isSliderDown())
                label->setText(controlValueText(control, value.toDouble()));
        }
    }
    const auto runtime = session_.snapshot()
                             .value(QStringLiteral("plugin_states"))
                             .toObject()
                             .value(selectedId_)
                             .toObject();
    if (worker_)
        worker_->synchronize(runtime, session_.online() && !session_.busy());
    if (playButton_) {
        const auto presentation = playbackState(session_.snapshot(), node, session_.devices(),
                                                session_.online(), session_.busy(), savedPath_);
        playButton_->setText(presentation.playing && descriptor.fileTransport
                                 ? QStringLiteral("Pausar")
                                 : QStringLiteral("Reproduzir"));
        playButton_->setEnabled(presentation.canPlay ||
                                (presentation.playing && descriptor.fileTransport &&
                                 session_.online() && !session_.busy()));
        playButton_->setToolTip(presentation.reason);
        if (reloadButton_) {
            reloadButton_->setVisible(presentation.needsLoad);
            reloadButton_->setEnabled(session_.online() && !session_.busy());
        }
        if (playerDetails_)
            playerDetails_->synchronize(presentation, !seek_ || !seek_->isSliderDown());
        if (seek_ && !seek_->isSliderDown() && !seek_->findChild<QTimer*>()->isActive()) {
            QSignalBlocker blocker(seek_);
            seek_->setRange(0, presentation.durationMs);
            seek_->setValue(presentation.positionMs);
        }
        if (seek_)
            seek_->setEnabled(
                session_.online() && !session_.busy() && presentation.durationMs > 0 &&
                session_.snapshot().value(QStringLiteral("engine_state")).toString() ==
                    QStringLiteral("running"));
    }
}
} // namespace nodivu
