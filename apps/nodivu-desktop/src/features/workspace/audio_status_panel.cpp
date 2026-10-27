#include "features/workspace/audio_status_panel.hpp"
#include "ui/controls/action_availability.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
namespace nodivu {
AudioStatusPanel::AudioStatusPanel(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("audioPanel"));
    setAccessibleName(QStringLiteral("Estado do áudio"));
    setStyleSheet(QStringLiteral(
        "QFrame#audioPanel { background: #29271f; border: 0; border-bottom: 1px solid #67563a; }"
        "QFrame#audioPanel[ready=\"true\"] { background: #14251f; border-color: #30544b; }"
        "QLabel#audioTitle { color: #f3d99a; font-size: 11px; font-weight: 600; background: "
        "transparent; }"
        "QFrame[ready=\"true\"] QLabel#audioTitle { color: #70d9bd; }"
        "QLabel#audioDetail { color: #d1c8b5; font-size: 10px; background: transparent; }"
        "QPushButton { background: #70d9bd; color: #123a30; border: 1px solid #70d9bd; "
        "border-radius: 7px; "
        "padding: 7px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:enabled:hover { background: #83ddc5; border-color: #83ddc5; }"
        "QPushButton:disabled { color: #777363; background: #35392e; border-color: #474c3d; }"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(8);
    auto* copy = new QVBoxLayout;
    copy->setSpacing(4);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("audioTitle"));
    title_->setTextFormat(Qt::PlainText);
    title_->setWordWrap(true);
    detail_ = new QLabel(this);
    detail_->setObjectName(QStringLiteral("audioDetail"));
    detail_->setTextFormat(Qt::PlainText);
    detail_->setWordWrap(true);
    copy->addWidget(title_);
    copy->addWidget(detail_);
    layout->addLayout(copy, 1);
    action_ = new QPushButton(this);
    action_->setObjectName(QStringLiteral("audioAction"));
    action_->setCursor(Qt::ArrowCursor);
    layout->addWidget(action_);
    connect(action_, &QPushButton::clicked, this, &AudioStatusPanel::actionRequested);
    synchronize({QStringLiteral("Conectando…"), QStringLiteral("Preparando o motor de áudio.")});
}
void AudioStatusPanel::synchronize(const AudioState& state, const QString& blockedReason) {
    if (property("ready").toBool() != state.ready || !property("ready").isValid()) {
        setProperty("ready", state.ready);
        // Repolish descendants too, since title color depends on the ancestor state.
        setStyleSheet(styleSheet());
    }
    title_->setText(state.title);
    detail_->setText(state.message);
    detail_->setVisible(!state.ready);
    const auto hint =
        QStringLiteral("<qt>") + state.message.toHtmlEscaped() + QStringLiteral("</qt>");
    title_->setToolTip(hint);
    detail_->setToolTip(hint);
    action_->setVisible(state.action != AudioAction::None);
    action_->setText(state.label);
    setActionAvailability(action_, state.label, blockedReason);
    const int vertical = state.ready ? 5 : 8;
    layout()->setContentsMargins(14, vertical, 14, vertical);
    setMinimumHeight(state.ready ? 27 : 0);
}
} // namespace nodivu
