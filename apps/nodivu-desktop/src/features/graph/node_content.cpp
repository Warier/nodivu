#include "features/graph/node_content.hpp"
#include "ui/controls/control_presentation.hpp"
#include "ui/controls/device_selector.hpp"
#include "ui/controls/inline_check_box.hpp"
#include "ui/controls/player_details.hpp"
#include "ui/controls/worker_controls.hpp"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace nodivu {
NodeContent::NodeContent(const QString& nodeId, const NodeDescriptor& descriptor)
    : descriptor_(descriptor) {
    setObjectName(QStringLiteral("nodeContent"));
    setProperty("node_id", nodeId);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(
        QStringLiteral(
            "QWidget#nodeContent { background: transparent; }"
            "QLabel { color: #95a1b2; background: transparent; font-size: 9px; }"
            "QLabel#nodeLevel { font-size: 8px; }"
            "QComboBox, QDoubleSpinBox { color: #dbe3ef; background: #121924; "
            "border: 1px solid #344257; border-radius: 7px; padding: 7px; font-size: 10px; }"
            "QComboBox:hover, QDoubleSpinBox:hover { border-color: #65748a; }"
            "QComboBox:focus, QDoubleSpinBox:focus { border-color: #83ddc5; }"
            "QComboBox:disabled { color: #758193; }"
            "QComboBox QAbstractItemView { background: #121924; color: #dbe3ef; "
            "selection-background-color: #243e39; selection-color: #dbe3ef; font-size: 10px; }"
            "QCheckBox { color: #97a6bb; font-size: 9px; spacing: 6px; }"
            "QPushButton { font-size: 10px; padding: 6px; border: 1px solid #384353; "
            "border-radius: 7px; color: #dbe3ef; background: #262e3b; }"
            "QPushButton:enabled:hover { background: #354153; border-color: #65748a; }"
            "QPushButton:disabled { color: #626b78; background: #202733; border-color: #2b3543; }"
            "QPushButton:focus { border-color: #83ddc5; }"
            "QPushButton:pressed { background: #243e39; }"
            "QSlider::sub-page:horizontal { background: %2; }"
            "QSlider::handle:horizontal { background: %2; }"
            "QProgressBar { background: #101722; border: 0; border-radius: 3px; }"
            "QProgressBar::chunk { background: %1; border-radius: 3px; }")
            .arg(descriptor.kind == QStringLiteral("output") ? QStringLiteral("#b296fa")
                                                             : QStringLiteral("#70d9bd"),
                 descriptor.kind == QStringLiteral("plugin") ? descriptor.accent.name()
                                                             : QStringLiteral("#70d9bd")));
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    if (descriptor.missing) {
        auto* label = new QLabel(
            QStringLiteral("Reinstale ou ative este plugin. Dados e fios preservados."), this);
        label->setWordWrap(true);
        layout_->addWidget(label);
    } else {
        for (const auto& control : descriptor.controls)
            if (control.quick && !control.advanced)
                mountControl(control);
    }
    const auto button = [this](const QString& text, const QString& action) {
        auto* result = new QPushButton(text, this);
        result->setObjectName(action);
        result->setCursor(Qt::ArrowCursor);
        layout_->addWidget(result);
        layout_->addSpacing(4);
        connect(result, &QPushButton::clicked, this,
                [this, action] { emit actionRequested(action); });
        return result;
    };
    if (descriptor.filePlayer) {
        choose_ = button(QStringLiteral("Escolher áudio"), QStringLiteral("file"));
        choose_->installEventFilter(this);
        reload_ = button(QStringLiteral("Carregar áudio salvo"), QStringLiteral("reload"));
        play_ = button(QStringLiteral("▶ Reproduzir"), QStringLiteral("play"));
        play_->setEnabled(false);
        playerDetails_ = new PlayerDetails(true, descriptor.fileTransport, this);
        connect(playerDetails_, &PlayerDetails::geometryChanged, this,
                &NodeContent::resizeToContents);
        if (descriptor.fileTransport) {
            seek_ = new QSlider(Qt::Horizontal, this);
            seek_->setAccessibleName(QStringLiteral("Posição no áudio"));
            seek_->setObjectName(QStringLiteral("playerSeek"));
            seek_->setEnabled(false);
            layout_->addSpacing(18);
            layout_->addWidget(seek_);
            // The native slider previews while dragging and commits on release;
            // keyboard navigation commits immediately, without a queued seek.
            connect(seek_, &QSlider::valueChanged, this, [this](int value) {
                playerDetails_->previewPosition(value, playback_.durationMs);
                if (!seek_->isSliderDown())
                    emit actionRequested(QStringLiteral("seek:%1").arg(value));
            });
            connect(seek_, &QSlider::sliderReleased, this, [this] {
                emit actionRequested(QStringLiteral("seek:%1").arg(seek_->value()));
            });
        }
        layout_->addWidget(playerDetails_);
    }
    if (descriptor.worker) {
        worker_ = new WorkerControls(true, this);
        layout_->addWidget(worker_);
        connect(worker_, &WorkerControls::geometryChanged, this, &NodeContent::resizeToContents);
        connect(worker_, &WorkerControls::restartRequested, this,
                [this] { emit actionRequested(QStringLiteral("restart")); });
    }
    layout_->addSpacing(7);
    meter_ = new QProgressBar(this);
    meter_->setRange(0, 1000);
    meter_->setTextVisible(false);
    meter_->setFixedHeight(6);
    meter_->setAccessibleName(QStringLiteral("Nível de ") + descriptor.title);
    layout_->addWidget(meter_);
    layout_->addSpacing(4);
    level_ = new QLabel(QStringLiteral("Silêncio"), this);
    level_->setObjectName(QStringLiteral("nodeLevel"));
    level_->setFixedHeight(10);
    layout_->addWidget(level_);
    setFixedWidth(194);
    resizeToContents();
    // Focus in transport controls and nested spinbox editors also selects the
    // owning card, so scene shortcuts cannot affect a previously selected node.
    for (auto* widget : findChildren<QWidget*>())
        widget->installEventFilter(this);
}
void NodeContent::mountControl(const ControlDescriptor& control) {
    Field field{control};
    auto* group = new QWidget(this);
    auto* stack = new QVBoxLayout(group);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(5);
    if (control.kind == ControlKind::Toggle) {
        auto* check =
            new InlineCheckBox(control.key == QStringLiteral("bypass")
                                   ? (descriptor_.inputs == 0    ? QStringLiteral("Silenciar fonte")
                                      : descriptor_.outputs == 0 ? QStringLiteral("Suspender")
                                                                 : QStringLiteral("Bypass"))
                                   : control.label,
                               group);
        field.control = check;
        stack->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this, control](bool checked) {
            emit fieldEdited(control.key, control.key.startsWith(QStringLiteral("parameter:"))
                                              ? QJsonValue(checked ? 1 : 0)
                                              : QJsonValue(checked));
        });
    } else {
        auto* line = new QHBoxLayout;
        line->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(control.label, group);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        line->addWidget(label, 1);
        if (control.kind == ControlKind::Slider || control.kind == ControlKind::Number) {
            field.value = new QLabel(group);
            field.value->setObjectName(control.key + QStringLiteral(":value"));
            field.value->setStyleSheet(QStringLiteral("color: %1; font-size: 9px;")
                                           .arg(descriptor_.kind == QStringLiteral("plugin")
                                                    ? descriptor_.accent.name()
                                                    : QStringLiteral("#70d9bd")));
            line->addWidget(field.value);
        }
        stack->addLayout(line);
        if (control.kind == ControlKind::Device || control.kind == ControlKind::Select) {
            auto* combo = new DeviceSelector(group);
            combo->setFixedHeight(30);
            if (control.kind == ControlKind::Select)
                for (const auto& option : control.options)
                    combo->addItem(option.toObject().value(QStringLiteral("label")).toString(),
                                   option.toObject().value(QStringLiteral("value")).toVariant());
            field.control = combo;
            connect(combo, &QComboBox::activated, this, [this, control, combo](int) {
                const auto id = combo->currentData().toString();
                emit fieldEdited(control.key, control.kind == ControlKind::Device
                                                  ? (id.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                  : QJsonValue(id))
                                                  : QJsonValue(combo->currentData().toDouble()));
            });
        } else if (control.kind == ControlKind::Slider) {
            auto* slider = new QSlider(Qt::Horizontal, group);
            slider->setRange(0, ControlSliderSteps);
            field.control = slider;
            connect(slider, &QSlider::valueChanged, this,
                    [this, control, valueLabel = field.value](int position) {
                        const double value = sliderControlValue(control, position);
                        valueLabel->setText(controlValueText(control, value));
                        emit fieldEdited(control.key, value);
                    });
        } else if (control.kind == ControlKind::Momentary) {
            auto* press = new QPushButton(control.label, group);
            field.control = press;
            press->installEventFilter(this);
            connect(press, &QPushButton::pressed, this,
                    [this, control] { emit gestureRequested(control.key.mid(10).toUInt(), true); });
            connect(press, &QPushButton::released, this, [this, control] {
                emit gestureRequested(control.key.mid(10).toUInt(), false);
            });
        } else {
            auto* number = new QDoubleSpinBox(group);
            number->setDecimals(control.step >= 1 ? 0 : control.step >= 0.1 ? 1 : 4);
            number->setRange(control.minimum, control.maximum);
            number->setSingleStep(control.step);
            number->setKeyboardTracking(false);
            field.control = number;
            connect(number, &QDoubleSpinBox::valueChanged, this,
                    [this, control, valueLabel = field.value](double value) {
                        if (valueLabel)
                            valueLabel->setText(controlValueText(control, value));
                        emit fieldEdited(control.key, value);
                    });
        }
        label->setBuddy(field.control);
        stack->addWidget(field.control);
    }
    field.control->setProperty("field", control.key);
    field.control->setProperty("node_id", property("node_id"));
    field.control->setObjectName(descriptor_.kind == QStringLiteral("output") &&
                                         control.kind == ControlKind::Device
                                     ? QStringLiteral("outputDevice")
                                     : control.key);
    field.control->setAccessibleName(control.label);
    field.control->setToolTip(control.help.isEmpty() ? control.label : control.help);
    field.control->setCursor(Qt::ArrowCursor);
    field.control->installEventFilter(this);
    if (fields_.isEmpty())
        setFocusProxy(field.control);
    fields_.append(field);
    layout_->addWidget(group);
    layout_->addSpacing(9);
}
void NodeContent::synchronize(const QJsonObject& block, const QJsonArray& devices, bool enabled,
                              double peak, const QJsonObject& runtime) {
    for (const auto& field : fields_) {
        auto* widget = field.control;
        const auto& control = field.descriptor;
        widget->setEnabled(enabled && !control.readonly);
        const auto value = fieldValue(block, control.key);
        const QSignalBlocker blocker(widget);
        if (auto* combo = qobject_cast<DeviceSelector*>(widget)) {
            if (control.kind == ControlKind::Device)
                combo->synchronize(devices, control.flow, value.toString());
            else
                combo->setCurrentIndex(combo->findData(value.toVariant()));
        } else if (auto* check = qobject_cast<QCheckBox*>(widget)) {
            check->setChecked(value.toBool() || value.toDouble() != 0);
        } else if (auto* slider = qobject_cast<QSlider*>(widget)) {
            if (!slider->isSliderDown()) {
                slider->setValue(sliderControlPosition(control, value.toDouble()));
            }
        } else if (auto* number = qobject_cast<QDoubleSpinBox*>(widget)) {
            if (!number->hasFocus())
                number->setValue(value.toDouble());
        }
        if (field.value) {
            const auto* slider = qobject_cast<QSlider*>(widget);
            const auto* number = qobject_cast<QDoubleSpinBox*>(widget);
            // A proxy can keep scene focus while the inspector edits this node.
            // Only an active drag/text edit owns a local value preview.
            if ((!slider || !slider->isSliderDown()) && (!number || !number->hasFocus()))
                field.value->setText(controlValueText(control, value.toDouble()));
        }
    }
    const double db = std::isfinite(peak) && peak > 0 ? 20 * std::log10(peak) : -INFINITY;
    meter_->setValue(std::isfinite(db) ? qRound(std::clamp((db + 60) / 60, 0.0, 1.0) * 1000) : 0);
    level_->setText(std::isfinite(db) ? QStringLiteral("%1 dBFS").arg(db, 0, 'f', 1)
                                      : QStringLiteral("Silêncio"));
    if (worker_)
        worker_->synchronize(runtime, enabled);
    resizeToContents();
}
void NodeContent::setPlayback(const PlaybackState& state, bool enabled) {
    playback_ = state;
    if (!play_)
        return;
    choose_->setEnabled(enabled);
    reload_->setVisible(state.needsLoad);
    reload_->setEnabled(enabled);
    play_->setEnabled(enabled && (state.canPlay || (state.playing && descriptor_.fileTransport)));
    play_->setText(state.playing && descriptor_.fileTransport ? QStringLiteral("Ⅱ Pausar")
                                                              : QStringLiteral("▶ Reproduzir"));
    play_->setToolTip(state.reason);
    playerDetails_->synchronize(state, !seek_ || !seek_->isSliderDown());
    if (seek_) {
        seek_->setEnabled(enabled && state.durationMs > 0 && (state.canPlay || state.playing));
        if (!seek_->isSliderDown()) {
            const QSignalBlocker blocker(seek_);
            seek_->setRange(0, state.durationMs);
            seek_->setValue(state.positionMs);
            playerDetails_->previewPosition(state.positionMs, state.durationMs);
        }
    }
    resizeToContents();
}
void NodeContent::focusPrimaryControl() {
    if (focusProxy())
        focusProxy()->setFocus(Qt::OtherFocusReason);
    else if (choose_)
        choose_->setFocus(Qt::OtherFocusReason);
}
void NodeContent::addCaptureWidget(QWidget* widget) {
    layout_->insertWidget(std::max(0, layout_->count() - 4), widget);
    widget->installEventFilter(this);
    for (auto* child : widget->findChildren<QWidget*>())
        child->installEventFilter(this);
    setFocusProxy(widget);
    resizeToContents();
}
void NodeContent::resizeToContents() {
    layout_->invalidate();
    const int height = layout_->totalHeightForWidth(width()) >= 0
                           ? layout_->totalHeightForWidth(width())
                           : layout_->sizeHint().height();
    if (height != this->height()) {
        setFixedHeight(std::max(20, height));
        emit geometryChanged();
    }
}
bool NodeContent::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn)
        emit interactionStarted();
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
        const auto key = watched->property("field").toString();
        if (auto* button = qobject_cast<QPushButton*>(watched);
            button && button->isDown() && key.startsWith(QStringLiteral("parameter:"))) {
            button->setDown(false);
            emit gestureRequested(key.mid(10).toUInt(), false);
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace nodivu
